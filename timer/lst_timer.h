// 升序定时器链表
#ifndef LST_TIMER
#define LST_TIMER

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#define BUFFER_SIZE 64

#include "../log/log.h"
class util_timer;

// 用户数据结构,连接资源
struct client_data
{
    sockaddr_in address;   // 客户端socket地址
    int sockfd;            // socket 文件描述符
    util_timer *timer;     // 链表
};

// 定时器类
class util_timer
{
public:
    util_timer() : prev(NULL), next(NULL) {}

public:
    time_t expire;                  // 任务的超时时间，绝对时间
    void (*cb_func)(client_data *); // 任务回调函数
    client_data *user_data;         // 回调函数处理的客户数据，由定时器执行者传递给回调函数
    util_timer *prev;
    util_timer *next;
};

// 定时器链表，升序，双向，有头尾节点
class sort_timer_lst
{
public:
    sort_timer_lst() : head(NULL), tail(NULL){};
    // 删除所有定时器
    ~sort_timer_lst();

    // 将定时器timer添加到链表中
    void add_timer(util_timer *timer);
    
    // 当某个定时任务发生变化时，调整对应的定时器的超时时间
    // 这个函数只考虑被调整的定时器的【超时时间的延长情况】，即该定时器要往链表尾部移动
    void adjust_timer(util_timer *timer);
    
    void del_timer(util_timer *timer);
    
    // SIGALARM信号每次触发就在其信号处理函数中执行一次tick函数
    // 来处理链表上到期的任务。
    void tick();
private:
    // 重载的辅助函数
    // 被add_timer和adjust_timer调用
    // 功能：将目标定时器timer添加到lst_head之后的部分链表中
    void add_timer(util_timer *timer, util_timer *lst_head);

    
private:
    util_timer *head;
    util_timer *tail;
};

// 工具类(定时器、IO设置)
class Utils
{
public:
    Utils() {}
    ~Utils() {}

    void init(int timeslot);

    int setnonblocking(int fd);
    // TRIGMode指定ET还是LT
    void addfd(int epollfd, int fd, bool one_shot, int TRIGMode);

    // 将信号事件通过管道传给主循环
    static void sig_handler(int sig);

    void addsig(int sig, void(handler)(int), bool restart = true);

    // 定时处理任务，重新定时来不断触发SIGALRM信号
    void timer_handler();

    void show_error(int connfd, const char *info);

public:
    static int * u_pipefd; // 管道数组
    sort_timer_lst m_timer_lst;
    static int u_epollfd;
    int m_TIMESLOT; // Alarm的间隔时间
};

void cb_func(client_data *user_data);
#endif