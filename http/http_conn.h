// 2023年03月09日 21:06:39
#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"
#include "../timer/lst_timer.h"
#include "../log/log.h"

class http_conn
{
public:
// 文件名的最大长度
static const int FILENAME_LEN = 200;
// 读缓冲区大小
static const int READ_BUFFER_SIZE = 2048;
// 写缓冲的大小
static const int WRITE_BUFFER_SIZE = 1024;
// HTTP请求方法，但仅支持GET
enum METHOD{GET = 0, POST, HEAD, PUT, DELTE,
            TRACE, OPTIONS, CONNECT, PATH};
// 解析客户请求时，主状态机所处的状态
enum CHECK_STATE
{
    CHECK_STATE_REQUESTLINE = 0,
    CHECK_STATE_HEADER,
    CHECK_STATE_CONTENT
};
// 服务器处理HTTP请求的可能结果
// 服务器处理HTTP请求的结果：
// NO_REQUEST 表示请求不完整,需要继续读取客户数据
// GET_REQUEST 表示获得了一个完整的客户请求
// BAD_REQUEST 表示客户请求有语法错误
// NO_RESOURCE 服务器没有指定资源
// FORBIDDEN_REQUEST 表示客户对资源没有足够的访问权限
// FILE_REQUEST文件请求
// INTERNAL_ERROR 表示服务器内部错误
// CLOSED_CONNECTION 表示客户端已经关闭连接
enum HTTP_CODE
{
    NO_REQUEST,
    GET_REQUEST,
    BAD_REQUEST,
    NO_RESOURCE,
    FORBIDDEN_REQUEST,
    FILE_REQUEST,
    INTERNAL_ERROR,
    CLOSED_CONNECTION
};
// 从状态机状态：读取到一个完整的行，行出错，行数据尚不完整
enum LINE_STATUS
{
    LINE_OK = 0,
    LINE_BAD,
    LINE_OPEN
};
public:
    http_conn(){}
    ~http_conn(){}

public:
    // 初始化新接受的连接
    void init(int sockfd, const sockaddr_in &addr, char *, int, int, string user, string passwd, string sqlname);
    // 关闭连接
    void close_conn(bool real_close = true);
    //处理客户请求
    void process();
    // 读取浏览器端发来的全部数据
    bool read_once();
    // 非阻塞写操作
    bool write();
    sockaddr_in *get_address()
    {
        return &m_address;
    }
    // CGI使用线程池初始化数据库表
    void initmysql_result(connection_pool *pool);
    int timer_flag;
    int improv; // ???

private:
    // 初始化连接
    void init();
    // 解析HTTP请求
    HTTP_CODE process_read();
    // 填充HTTP应答
    bool process_write(HTTP_CODE ret);

    // 下面一组函数被process_read调用以分析HTTP请求
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE parse_content(char *text);
    HTTP_CODE do_request();
    char *get_line() {return m_read_buf + m_start_line;}
    LINE_STATUS parse_line();

    // 下面这一组函数被process_write调用以填充HTTP应答
    void unmap();
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();
public:
    // 所有socket上的事件都被注册到同一个epoll内核事件表中
    // 所以将其设为静态
    static int m_epollfd;
    // 统计用户数量
    static int m_user_count;
    MYSQL *mysql;
    int m_state; // 读为0，写为1

private:
    // 该HTTP连接的socket和对方的socket地址
    int m_sockfd;
    sockaddr_in m_address;

    // 读缓冲区
    char m_read_buf[READ_BUFFER_SIZE];
    // 标识读缓冲已经读入的客户数据的最后一个字节的下一个位置
    int m_read_idx;
    // 当前正在分析的字符再读缓冲区中的位置
    int m_checked_idx;
    // 当前正在解析的行的起始位置
    int m_start_line;
    // 写缓冲区
    char m_write_buf[WRITE_BUFFER_SIZE];
    // 写缓冲区待发送字节数
    int m_write_idx;

    // 主状态机当前所处的状态
    CHECK_STATE m_check_state;
    // 请求方法
    METHOD m_method;

    // 以下为解析请求报文中对应的6个变量
    // 客户请求的目标文件的完整路径
    // 其内容为doc_root + m_url,doc_root是网站根目录
    char m_real_file[FILENAME_LEN];
    // 客户请求的目标文件的文件名
    char *m_url;
    // HTTP协议版本号，此处仅支持HTTP/1.1
    char *m_version;
    // 主机名
    char *m_host;
    // HTTP请求的消息体的长度
    int m_content_length;
    // HTTP请求是否保持连接
    bool m_linger;

    // 客户请求的目标文件，被mmap到内存中的起始位置
    char *m_file_address;
    // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读
    // 并获取文件大小等信息
    struct stat m_file_stat;
    
    // 采用writev来执行写操作，所以定义下面两个成员
    // m_iv_count 标识被写内存块的数量
    struct iovec m_iv[2];
    int m_iv_count;

    int cgi;                // 是否启用的POST
    char *m_string;         // 存储请求头数据
    int bytes_to_send;      // 剩余发送字节数
    int bytes_have_send;    // 已发送字节数
    char *doc_root;

    map<string, string> m_users;    // ???
    int m_TRIGMode;     // 是否为边缘触发
    int m_close_log;    // ???

    char sql_user[100];
    char sql_passwd[100];
    char sql_name[100]; // ??? 数据库名称吗
};


#endif