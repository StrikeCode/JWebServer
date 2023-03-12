/*
    2023年03月12日 13:26:48
    Author: Jiang Yuhao
*/
// 半同步半反应堆线程池
// 借助工作队列解耦主线程和工作线程
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>

// 线程同步机制的包装类
#include "../lock/locker.h"
#include "../CGImysql/sql_connection_pool.h"

template<typename T>
class threadpool
{
public:
    // max_requests是请求队列中最多允许的、等待处理的请求数量
    threadpool(int actor_model, connection_pool *connPool, int thread_number = 8, int max_requests = 10000);
    ~threadpool();
    // 往请求队列中添加一条客户请求
    bool append(T *request, int state);

private:
    // 工作线程运行的函数，不断从工作队列中取任务并执行
    static void* worker(void *arg);
    void run();

private:
    int m_thread_number;        // 线程池中的线程数
    int m_max_requests;         // 请求队列中允许的最大请求数
    pthread_t *m_threads;       // 存储线程的数组（线程池）
    std::list<T*> m_workqueue;  // 请求队列
    locker m_queuelocker;       // 保护请求队列的互斥锁
    sem m_queuestat;            // 是否有任务要处理,信号量，可允许多个线程访问临界区
    connection_pool *m_connPool; // 数据库连接池
    int m_actor_model;          //模型切换 ??
};

template<typename T>
threadpool<T>::threadpool(int actor_model, connection_pool *connPool, int thread_number, int max_requests):
    m_actor_model(actor_model), m_thread_number(thread_number), m_max_requests(max_requests), m_threads(NULL), m_connPool(connPool)
{
    if((thread_number <= 0) || (max_requests <= 0))
    {
        throw std::exception();
    }
    m_threads = new pthread_t(m_thread_number);
    if(!m_threads)
    {
        throw std::exception();
    }

    // 创建thread_number个线程，将他们设置为分离线程
    for(int i = 0; i < thread_number; ++i)
    {
        printf("create the %dth thread\n", i);
        // 静态成员函数worker要调用非静态成员，采用传一个指向实例对象的this指针
        if(pthread_create(m_threads + i, NULL, worker, this) != 0)
        {
            delete []m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i]))
        {
            delete []m_threads;
            throw std::exception();
        }
    }
}
template<typename T>
threadpool<T>::~threadpool()
{
    delete []m_threads;
}

template<typename T>
bool threadpool<T>::append(T* request, int state)
{
    // 操作工作(任务)队列时要加锁
    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state; // ???
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
bool threadpool<T>::append_p(T* request)
{
    // 操作工作(任务)队列时要加锁
    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }
    request->m_state = state; // ???
    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template<typename T>
void* threadpool<T>::worker(void *arg)
{
    threadpool *pool = (threadpool*)arg;
    pool->run();
    return pool;
}
template<typename T>
void threadpool<T>::run()
{
    while(true)
    {
        // 等一个任务来(post会解除wait的阻塞)
        m_queuestat.wait();
        m_queuelocker.lock(); // 加锁
        if(m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();
        m_workqueue.pop_front();    // 获取队头任务
        m_queuelocker.unlock(); // 解锁
        if(!request)
        {
            continue;
        }
        
        if(1 == m_actor_model)
        {
            // 解析请求,并组织好响应的响应行状态行、头部以及资源正文
            if(0 == request->m_state)
            {
                // 循环读取客户数据
                if(request->read_once())
                {
                    request->improv = 1;
                    // 从线程池m_connPool中获取一条MYSQL连接
                    connectionRAII mysqlcon(&request->mysql, m_connPool);
                    request->process();
                }
                else 
                {
                    request->improv = 1; // ???
                    request->timer_flag = 1; // 定时器相关参数
                }
            }
            else // 发送响应
            {
                if(request->write())
                {
                    request->improv = 1;
                }
                else 
                {
                    request->imrpov = 1;
                    request->timer_flag = 1;
                }
            }
        }
        else 
        {
            connectionRAII mysqlcon(&request->mysql, m_connPool);
            request->process();
        }
    }
}
#endif