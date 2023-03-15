#include "config/config.h"

using namespace std;

int main(int argc, char *argv[])
{
    // 数据库相关信息
    // string user = "root";
    // string passwd = "123456";
    // string databasename = "yourdb"; 

    // 命令行解析
    Config config;
    config.parse_arg(argc, argv);
    

    return 0;
}