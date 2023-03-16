#include <mysql/mysql.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <list>
#include <pthread.h>
#include <stdlib.h>
#include <string>
#include "sql_connection_pool.h"

using namespace std;

connection_pool::connection_pool()
{
    m_CurConn = 0;
    m_FreeConn = 0;
}
// 实现时不用加static
connection_pool *connection_pool::GetInstance()
{
    // C++11保证线程安全
    static connection_pool connPool;
    return &connPool;
}

void connection_pool::init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log)
{
    m_url = url;
    m_Port = Port;
    m_User = User;
    m_PassWord = PassWord;
    m_DatabaseName = DataBaseName;
    m_close_log = close_log;
    
    // 创建连接
    for(int i = 0; i < MaxConn; i++)
    {
        MYSQL *con = NULL;
        con = mysql_init(con);
        
        if(con == NULL)
        {
            LOG_ERROR("MYSQL ERROR");
            exit(1);
        }
        con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DataBaseName.c_str(), Port, NULL, 0);

        if(con == NULL)
        {
            LOG_ERROR("MYSQL ERROR");
            exit(1);
        }
        connList.push_back(con);
        ++m_FreeConn;
    }
    
    reserve = sem(m_FreeConn);
    m_MaxConn = m_FreeConn;
}

MYSQL *connection_pool::GetConnection()
{
    MYSQL *con = NULL;
    if(0 == connList.size())
    {
        return NULL;
    }

    reserve.wait(); // 将信号量-1，若小于0则等待
    
    lock.lock();
    con = connList.front();
    connList.pop_front();

    --m_FreeConn;
    ++m_CurConn;

    lock.unlock();

    return con;
}

bool connection_pool::ReleaseConnection(MYSQL *conn)
{
    if(NULL == conn)
    {
        return false;
    }

    lock.lock();

    connList.push_back(conn);
    ++m_FreeConn;
    --m_CurConn;

    lock.unlock();

    reserve.post();
    return true;
}

void connection_pool::DestroyPool()
{
    lock.lock();
    if(connList.size() > 0)
    {
        list<MYSQL *>::iterator it;
        for(it = connList.begin(); it != connList.end(); ++it)
        {
            MYSQL *con = *it;
            mysql_close(con);
        }
        m_FreeConn = 0;
        m_CurConn = 0;
        connList.clear();
    }
    lock.unlock();
}

int connection_pool::GetFreeConn()
{
    return this->m_FreeConn;
}

connection_pool::~connection_pool()
{
    DestroyPool();
}

// ？？不大理解这种实现方式
connectionRAII::connectionRAII(MYSQL **con, connection_pool *connPool)
{
    *con = connPool->GetConnection(); // 获取连接通过这个RAII类来实现
    
    conRAII = *con;
    poolRAII = connPool;
}

connectionRAII::~connectionRAII()
{
    poolRAII->ReleaseConnection(conRAII);
}