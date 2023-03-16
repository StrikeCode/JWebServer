// 2023年03月14日 19:47:56
// 功能：阻塞队列实现的实现
// Author: Jiang Yuhao
/*
    循环数组实现阻塞队列,头删尾插
    m_back = (m_back + 1) % m_max_size
    线程安全：每个操作前都要先加互斥锁，操作后，再解锁
*/

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "../lock/locker.h"
using namespace std;

template<class T>
class block_queue
{
public:
    block_queue(int max_size = 1000)
    {
        if(max_size <= 0)
        {
            exit(-1);
        }

        m_max_size = max_size;
        m_array = new T[max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    ~block_queue()
    {
        m_mutex.lock();
        if(m_array != NULL)
            delete []m_array;
        m_mutex.unlock();
    }
    // 通过修改指针来清空队列
    void clear()
    {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }

    bool full()
    {
        m_mutex.lock();
        if(m_size >= m_max_size)
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    bool empty()
    {
        m_mutex.lock();
        if(m_size == 0)
        {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    bool front(T &value)
    {
        // ？？改进：可以复用empty()函数吗
        m_mutex.lock();
        if(m_size == 0)
        {
            m_mutex.unlock();
            return false;
        }
        // m_front指向首元素
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }

    bool back(T &value)
    {
        m_mutex.lock();
        if(m_size == 0)
        {
            m_mutex.unlock();
            return false;
        }
        // m_front指向首元素
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }

    int size()
    {
        // ??可直接返回 m_size吗？
        int tmp = 0;
        m_mutex.lock();
        tmp = m_size;
        m_mutex.unlock();
        return tmp;
    }

    int max_size()
    {
        int tmp = 0;
        m_mutex.lock();
        tmp = m_max_size;
        m_mutex.unlock();
        return tmp;
    }
    // 往队列添加元素，需要将所有使用队列的线程先唤醒
    // 当有元素push进队列，相当于生产者生产了一个元素
    // 若当前没有线程等待条件变量，则唤醒无意义
    bool push(const T &item)
    {
        m_mutex.lock();
        if(m_size >= m_max_size)
        {
            m_cond.broadcast(); // ？？？惊群
            m_mutex.unlock();
            return false;
        }

        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;
        
        m_size++;

        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    // 若当前队列空，则等待条件变量
    bool pop(T &item)
    {
        m_mutex.lock();
        // ？？ 这里为什么等到生产出的资源，仍然返回false
        while(m_size <= 0)
        {
            // 再等一下生产者
            // wait成功返回 0
            // wait调用后有：
            // 将线程放入条件变量的请求队列后，内部解锁
            // 线程等待被pthread_cond_broadcast信号唤醒或者pthread_cond_signal信号唤醒，唤醒后去竞争锁
            // 再次加锁
            // push会pthread_cond_broadcast
            if(!m_cond.wait(m_mutex.get()))
            {
                m_mutex.unlock();
                return false;
            }
        }
        // ???m_front指向队首元素的前一个位置,下面两行对调试试
        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    // 增加超时处理
    bool pop(T &item, int ms_timeout)
    {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        
        gettimeofday(&now, NULL);
        m_mutex.lock();
        if(m_size <= 0)
        {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            if(!m_cond.timewait(m_mutex.get(), t))
            {
                m_mutex.unlock();
                return false;
            }
        }

        if(m_size <= 0)
        {
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }


private:
    locker m_mutex; 
    cond m_cond;

    T *m_array; // 底层用数组
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};

#endif