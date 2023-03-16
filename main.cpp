#include "config/config.h"


int main(int argc, char *argv[])
{
    // 数据库相关信息
    string user = "root";
    string passwd = "123456";
    string databasename = "yourdb"; 

    // 命令行解析
    Config config;
    config.parse_arg(argc, argv);
    
    WebServer server;
    
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite,
                config.OPT_LINGER, config.TRIGMode, config.sql_num, config.thread_num,
                config.close_log, config.actor_model);
    // 启动日志模块
    server.log_write();

    // 启动数据库连接池模块
    server.sql_pool();

    // 启动线程池模块
    server.thread_pool();

    // 配置事件触发模式
    server.trig_mode();

    // 启动连接监听模块
    server.eventListen();

    // 启动事件循环
    server.eventLoop();

    return 0;
}