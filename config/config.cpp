#include "config.h"


Config::Config()
{
    PORT = 8888;
    
    LOGWrite = 0; // 日志写入方式, 默认同步

    // 触发组合方式,默认listenfd LT + connfd LT
    TRIGMode = 0; 

    LISTENTrigmode = 0; // listenfd 触发模式,默认LT

    CONNTrigmode = 0; // connfd 触发模式，默认LT

    OPT_LINGER = 0; // 默认不用优雅关闭连接，

    sql_num = 8; // 数据库连接池连接数量

    thread_num = 8; // 线程池内线程数

    close_log = 0;  // 是否关闭日志

    actor_model = 0;    // 并发模型选择,默认proactor
}

void Config::parse_arg(int argc, char *argv[])
{
    int opt;
    const char *str = "p:l:m:o:s:t:c:a:";
    // 不断的解析出选项的对应参数
    while((opt = getopt(argc, argv, str)) != -1)
    {
        switch(opt)
        {
            case 'p':
            {
                PORT = atoi(optarg);
                break;
            }
            case 'l':
            {
                LOGWrite = atoi(optarg);
                break;
            }
            case 'm':
            {
                TRIGMode = atoi(optarg);
                break;
            }
            case 'o':
            {
                OPT_LINGER = atoi(optarg);
                break;
            }
            case 's':
            {
                sql_num = atoi(optarg);
                break;
            }
            case 't':
            {
                thread_num = atoi(optarg);
                break;
            }
            case 'c':
            {
                close_log = atoi(optarg);
                break;
            }
            case 'a':
            {
                actor_model = atoi(optarg);
                break;
            }
            default:
                break;
        }
    }

}