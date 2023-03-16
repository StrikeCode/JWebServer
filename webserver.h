#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "./threadpool/threadpool.h"
#include "./http/http_conn.h"

const int MAX_FD = 65536;           // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数
const int TIMESLOT = 5;             // 最小超时单位

class WebServer
{
public:
    WebServer();
    ~WebServer();

    void init(int port, string user, string passWord, string databaseName, 
              int log_write, int opt_linger, int trigmode, int sql_num,
              int thread_num, int close_log, int actor_model);

    void thread_pool();
    void sql_pool();
    void log_write();
    void trig_mode(); // 设置触发方式
    void eventListen();
    void eventLoop();
    // 根据连接fd创建定时器
    void timer(int connfd, struct sockaddr_in client_address);
    void adjust_timer(util_timer *timer);
    void deal_timer(util_timer *timer, int sockfd);
    bool dealclientdata(); // accept
    bool dealwithsignal(bool &timeout, bool &stop_server);
    void dealwithread(int sockfd);
    void dealwithwrite(int sockfd);      

public:
    // basic
    int m_port;
    char *m_root; // ??? 文件根路径？
    int m_log_write; // 日志写入方式（异步/同步）
    int m_close_log;
    int m_actormodel; // 并发模型，reactor/proactor

    int m_pipefd[2];
    int m_epollfd;
    http_conn *users; // 连接对象数组

    // database
    connection_pool *m_connPool;
    string m_user;
    string m_passWord;
    string m_databaseName;
    int m_sql_num;  //  连接池内连接数

    // threadpool
    // 用完成工作的类来实例化线程池
    threadpool<http_conn>  *m_pool;
    int m_thread_num;

    // epoll_event
    epoll_event events[MAX_EVENT_NUMBER];

    int m_listenfd;
    int m_OPT_LINGER; // 是否优雅关闭
    int m_TRIGMode; // 组合触发模式
    int m_LISTENTrigmode;
    int m_CONNTrigmode;

    // timer
    client_data *users_timer;
    Utils utils;
};
#endif