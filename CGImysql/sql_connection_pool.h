/*
    2023年03月15日 15:07:11
    Author:Jiang Yuhao
*/
#ifndef _CONNECTION_POOL_
#define _CONNECTION_POOL_

#include <stdio.h>
#include <list>
#include <mysql/mysql.h>
#include <string.h>
#include <iostream>
#include <string>
#include "../lock/locker.h"
#include "../log/log.h"

using namespace std;

class connection_pool
{
public:
    
    MYSQL *GetConnection();

    bool ReleaseConnection(MYSQL *conn);
    int GetFreeConn(); // 获取空闲连接数
    void DestroyPool(); // 销毁所有连接

    // 懒汉单例模式
    static connection_pool *GetInstance();

    void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log);

private:
    connection_pool(); // 单例模式
    ~connection_pool();

    int m_MaxConn;  // 最大连接数
    int m_CurConn;  // 当前已使用连接数
    int m_FreeConn; // 当前空闲连接数
    locker lock;
    list<MYSQL *> connList; // 连接池底层容器
    sem reserve;    // 信号量做线程间通信
public:
    string m_url;   // 主机地址
    string m_Port;  // 数据库端口号
    string m_User;  // 登录数据库用户名
    string m_Password;  // 登录数据库密码
    string m_DatabaseName; // 使用数据库名
    int m_close_log;    // 是否记录日志
};

// 将连接对象构造和析构封装
class ConnectionRAII
{
public: 
    // 要修改的变量是MYSQL *,所以要传MYSQL **
    ConnectionRAII(MYSQL **con, connection_pool *connPool);
    ~ConnectionRAII();

private:
    MYSQL *conRAII;
    connection_pool *poolRAII;
};
#endif