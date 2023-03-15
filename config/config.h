/*
    配置解析模块
    2023年03月15日 19:12:52
    Author: Jiang Yuhao
*/
// 注意config目录，写makefile时要注意
#ifndef CONFIG_H
#define CONFIG_H

#include "../webserver.h"
using namespace std;

class Config
{
public:
    Config();
    ~Config(){}

    void parse_arg(int argc, char *argv[]);

    int PORT;
    
    int LOGWrite; // 日志写入方式

    int TRIGMode; // 触发组合方式

    int LISTENTrigmode; // listenfd 触发模式

    int CONNTrigmode; // connfd 触发模式

    int OPT_LINGER; // 优雅关闭连接

    int sql_num; // 数据库连接池连接数量

    int thread_num; // 线程池内线程数

    int close_log;  // 是否关闭日志

    int actor_model;    // 并发模型选择
};

#endif