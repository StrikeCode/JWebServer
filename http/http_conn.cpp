// 2023年03月10日 21:46:54
// Author: Jiang Yuhao
#include "http_conn.h"

#include <mysql/mysql.h>
#include <fstream>

// 定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

locker m_lock;
map<string, string> users; // 存用户名和密码

// ???数据库连接池还未写到
void http_conn::initmysql_result(connection_pool *connPool)
{
    // 从连接池拿一条连接
    MYSQL *mysql = NULL;
    connectionRAII mysqlcon(&mysql, connPool);

    // 在user表中检索username,passwd 数据，浏览器端输入
    // mysql_query返回0表示执行成功
    if (mysql_query(mysql, "SELECT username,passwd FROM user"))
    {
        LOG_ERROR("SELECT error: %s\n", mysql_error(mysql));
    }

    // 查询得到的结果集
    MYSQL_RES *result = mysql_store_result(mysql);

    // 结果集中的列数
    int num_fields = mysql_num_fields(result);

    // 返回所有字段结构的数组
    MYSQL_FIELD *fields = mysql_fetch_fields(result);

    // 从结果集中遍历获取下一行，将对应的用户名和密码存入map中
    while (MYSQL_ROW row = mysql_fetch_row(result))
    {
        string name(row[0]); //
        string passwd(row[1]);
        users[name] = passwd;
    }
}

// 对文件描述符设置非阻塞
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

// 将内核事件表注册读事件，ET模式，选择开启EPOLLONESHOT
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode) // ET
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

// 从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

// 将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0;
int http_conn::m_epollfd = -1;

void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        printf("close %d\n", m_sockfd); // 试着改为LOGO输出
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--; // 客户数量-1
    }
}
// 初始化连接，外部调用初始化socket 地址
void http_conn::init(int sockfd, const sockaddr_in &addr, char *root, int TRIGMode,
                     int close_log, string user, string passwd, string sqlname)
{
    m_sockfd = sockfd;
    m_address = addr;

    addfd(m_epollfd, sockfd, true, m_TRIGMode);
    m_user_count++;
    // 当浏览器出现连接重置时，可能是网站根目录出错或http响应格式出错或访问文件中内容完全为空
    doc_root = root;
    m_TRIGMode = TRIGMode;
    m_close_log = close_log; // 是否关闭日志记录

    strcpy(sql_user, user.c_str());
    strcpy(sql_passwd, passwd.c_str());
    strcpy(sql_name, sqlname.c_str());
    init();
}

void http_conn::init()
{
    mysql = NULL;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    cgi = 0;
    m_state = 0;
    timer_flag = 0;
    improv = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

// 从状态机，用于解析出一行内容
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    // m_checked_idx指向m_read_buf（应用程序的读缓冲区）中当前正在分析的字节
    // m_read_idx指向m_read_buf中客户数据的尾部的下一字节
    // m_read_buf 中第0~m_checked_idx字节都已分析完毕，第m_checked_idx ~ （m_read_idx - 1）字节由下面的循环进行分析
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        // 获得当前要分析的字节
        temp = m_read_buf[m_checked_idx];
        // 若当前字节是 回车符，说明可能读到一个完整的行
        if (temp == '\r')
        {
            // 若 "\r"碰巧是目前m_read_buf中最后一个已经被读入的客户数据，则这次分析没读到一个完整的行，返回LINE_OPEN表示还要继续读取客户数据才能进行下一步分析
            if ((m_checked_idx + 1) == m_read_idx)
            {
                return LINE_OPEN;
            }
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                // 若下一个字符是换行符，则表明成功读取到一个完整的行
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n') // 上次到结尾只接收到'\r'的情况
        {                      // 可能读到完整的行
            if ((m_checked_idx > 1) && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN; // 还要继续读取客户数据
}

// 非阻塞ET工作模式下，需要一次性将数据读完
// 循环读取客户数据，直到无数据可读或对方关闭连接
bool http_conn::read_once()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }
    int bytes_read = 0;
    // LT读数据
    if (0 == m_TRIGMode)
    {
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        m_read_idx += bytes_read;

        if (bytes_read <= 0)
        {
            return false;
        }
        return true;
    }
    // ET读数据
    else
    {
        while (true)
        {
            bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
            if (bytes_read == -1)
            {
                // 不认为是错误
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    break;
                }
                return false;
            }
            else if (bytes_read == 0)
            {
                return false;
            }
            m_read_idx += bytes_read;
        }
        return true;
    }
}

// 分析请求行
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    // 检索第一个匹配' ' 或 '\t'的位置
    m_url = strpbrk(text, " \t");

    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0'; // text此时输出为GET

    char *method = text; // 请求方法（如get post）
    //
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
        cgi = 1;
    }
    else
    {
        return BAD_REQUEST;
    }
    // 继续跳过空格和\t, 到达请求资源字符串的开头
    m_url += strspn(m_url, " \t"); // 查找第一个不为 ' ' 且 '\t'的位置
    // 到资源名的结尾
    m_version = strpbrk(m_url, " \t");

    if (!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    // 到版本号的开头
    m_version += strspn(m_version, " \t");

    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    // 检查URL是否合法
    // 如http://192.168.200.152/share/a.html
    // 提取出 m_url = 192.168.200.152/share/a.html
    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (strncasecmp(m_url, "https://", 8) == 0)
    {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }

    // url为/时，显示判断界面
    if (strlen(m_url) == 1)
        strcat(m_url, "judge.html");

    // HTTP请求行处理完毕，状态转移到头部字段的分析
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

// 分析头部字段
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    // 遇到一个空行，说明得到了一个正确的HTTP请求
    if (text[0] == '\0')
    {
        // content_length 不为0表示POST请求
        // 则还需要获取m_content_length字节的消息体进行对应处理
        // 状态机转移到CHECK_STATE_CONTENT 状态
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }

        return GET_REQUEST;
    }
    // 处理Connection头部字段
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        // 跳过" " 和 \t
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    // 处理Content-Length 头部字段
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        // 跳过" " 和 \t
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0) //
    {
        text += 5;
        // 跳过空格和制表符
        text += strspn(text, " \t");
        printf("the reques host is:%s\n", text);
    }
    else
    {
        LOG_INFO("oop!unknow header: %s", text);
    }
    return NO_REQUEST; // 请求不完整
}


// 没有真正解析HTTP请求的消息体，只是判断它是否被完整读入
http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        // POST请求中最后为输入的用户名和密码
        m_string = text;
        return GET_REQUEST; // 表示获得一个完整请求
    }
    return NO_REQUEST;
}

// 主状态机,读取收到的GET请求报文
http_conn::HTTP_CODE http_conn::process_read()
{
    LINE_STATUS line_status = LINE_OK; // 记录当前行的【读取】状态
    HTTP_CODE retcode = NO_REQUEST;    // 记录当前HTTP请求的处理结果
    char *text = 0;
    // 主状态机，已读出完整的行或现场读一行
    // parse_line为从状态机具体实现
    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) || ((line_status == parse_line()) == LINE_OK))
    {
        text = get_line();
        m_start_line = m_checked_idx; // 记录下一行的 起始位置
        LOG_INFO("%s", text);         // ???
        switch (m_check_state)        // 根据主状态机工作情况决定
        {
        case CHECK_STATE_REQUESTLINE: // 分析请求行
        {
            retcode = parse_request_line(text);
            if (retcode == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            break;
        }
        case CHECK_STATE_HEADER: // 分析头部字段
        {
            retcode = parse_headers(text);
            if (retcode == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            else if (retcode == GET_REQUEST)
            {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            retcode = parse_content(text);
            if (retcode == GET_REQUEST)
            {
                return do_request();
            }
            // 解析完消息体即完成报文解析，避免再次进入循环，更新line_status
            // 留意循环条件是要line_status = LINE_OK
            line_status = LINE_OPEN;
            break;
        }
        default:
        {
            return INTERNAL_ERROR;
        }
        }
    }
    return NO_REQUEST; // 请求不完整
}

http_conn::HTTP_CODE http_conn::do_request()
{
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    // 找到最后一次出现/的位置
    const char *p = strrchr(m_url, '/');
    // 处理cgi,登录或注册的POST请求
    if (cgi == 1 && (*(p + 1) == '2' || *(p + 1) == '3'))
    {
        // 根据标志判断是登录检测还是 注册检测
        char flag = m_url[1];

        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/");
        strcat(m_url_real, m_url + 2); // POST请求前面有跳过标志位
        strncpy(m_real_file + len, m_url_real, FILENAME_LEN - len - 1);
        free(m_url_real);

        // 将用户名和密码提取出来
        // user=123&password=123
        // 打印m_string
        char name[100], password[100];
        int i;
        // 跳到user=后面读取数据
        for (i = 5; m_string[i] != '&'; ++i)
        {
            name[i - 5] = m_string[i];
        }
        name[i - 5] = '\0';

        int j = 0;
        // &password= 长度为10
        for (i = i + 10; m_string[i] != '\0'; ++i, ++j)
        {
            password[j] = m_string[i];
        }
        password[j] = '\0';

        // 注册
        if (*(p + 1) == '3')
        {
            // 若为注册，先检测数据库中是否有重名用户
            // 没有重名，增加数据
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, name);
            strcat(sql_insert, "', '");
            strcat(sql_insert, password);
            strcat(sql_insert, "')");

            if (users.find(name) == users.end())
            {
                m_lock.lock();
                int res = mysql_query(mysql, sql_insert);
                users.insert(pair<string, string>(name, password));
                m_lock.unlock();

                if (!res)
                {
                    strcpy(m_url, "log.html");
                }
                else // mysql插入数据失败
                {
                    strcpy(m_url, "/registerError.html");
                }
            }
            else
            {
                strcpy(m_url, "/registerError.html");
            }
        }
        // 登录
        else if (*(p + 1) == '2')
        {
            if (users.find(name) != users.end() && users[name] == password)
            {
                strcpy(m_url, "/welcome.html");
            }
            else
            {
                strcpy(m_url, "/logError.html");
            }
        }
    }

    // GET请求
    if (*(p + 1) == '0')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/register.html");
        // m_url = doc_root/register.html
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '1')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/log.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '5')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/picture.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '6')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/video.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else if (*(p + 1) == '7')
    {
        char *m_url_real = (char *)malloc(sizeof(char) * 200);
        strcpy(m_url_real, "/fans.html");
        strncpy(m_real_file + len, m_url_real, strlen(m_url_real));

        free(m_url_real);
    }
    else
    {
        // 访问具体给出的资源路径url
        strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    }

    if (stat(m_real_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE;
    }
    // S_IROTH表示其他组的读权限
    if (!(m_file_stat.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }
    if (S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }
    int fd = open(m_real_file, O_RDONLY);
    // 将fd指向的文件从0开始映射 m_file_stat.st_size 长度到内存中
    // 第一个参数为0，表示映射到内存中的起始位置由系统指定
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

// 堆内存映射区执行munmap操作
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

// 发送HTTP响应
bool http_conn::write()
{
    int temp = 0;
    // int bytes_have_send = 0;  // 放在http_conn::init里初始化
    // int bytes_to_send = m_write_idx;
    if (bytes_to_send == 0)
    {
        // 对m_sockfd监视可读事件
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        init();
        return true;
    }
    while (1)
    {
        // 直接把内存块的数据发出去
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp < 0)
        {
            // 若TCP缓冲没空间，等待下一轮EPOLLOUT事件。
            // 虽然此期间服务器无法立即收到同一客户的下一个请求，但可保证连接完整性
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;
        // 发送的数据已经超过第一个io向量的大小
        // 更新第二个块当前发送内容的起始位置和程度
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else // 更新第一个向量块的长度和起始地址
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        // 发送不成功，或发送完成
        if (bytes_to_send <= 0)
        {
            unmap();
            // 关注这个fd是否可读
            modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
            if (m_linger)
            {
                // 重新初始化http对象
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}
// 往写缓冲写入待发送的数据
bool http_conn::add_response(const char *format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }
    // 定义可变参数列表
    va_list arg_list;
    // 将变量arg_list初始化为传入参数
    va_start(arg_list, format);
    // 将数据format从可变参数列表写入缓冲区中，返回写入数据长度
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx,
                        format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list); // 清空可变参数列表
    LOG_INFO("response:%s", m_write_buf);
    return true;
}

// 添加响应的状态行
bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

// 添加响应的头部
bool http_conn::add_headers(int content_len)
{
    // add_content_length(content_len); // Content-Length
    // add_linger(); // Connection
    // add_blank_line(); // /r/n

    return add_content_length(content_len) && add_linger() && add_blank_line();
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_content_type()
{
    return add_response("Content-Type: %s\r\n", "text/html");
}

bool http_conn::add_linger()
{
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

// 添加响应正文
bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容(响应报文)
bool http_conn::process_write(HTTP_CODE ret)
{
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
        {
            return false;
        }
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
        {
            return false;
        }
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
        {
            return false;
        }
        break;
    }
    case NO_RESOURCE:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
        {
            return false;
        }
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            // m_iv[0] 是响应的状态行和头部
            // m_iv[1] 是客户端请求的资源
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            // 待发送字节数为，写缓冲区待发送字节数 + 请求资源的字节数
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body>success!</body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
            {
                return false;
            }
        }
    }
    default:
    {
        return false;
    }
    }
    // 访问错误发送的报文
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

// 线程池中工作线程的调用， 这是处理HTTP请求的入口函数
void http_conn::process()
{
    // 解析请求
    HTTP_CODE read_ret = process_read();
    // NO_REQUEST，表示请求不完整，需要继续接收请求数据
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN, m_TRIGMode);
        return;
    }

    // 返回响应
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT, m_TRIGMode);
}