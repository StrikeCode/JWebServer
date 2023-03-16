// 2023年03月14日 19:47:56
// 功能：日志类的实现
// Author: Jiang Yuhao
#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include "block_queue.h"

using namespace std;

class Log
{
public:
    // C++0X以后，要求编译器保证内部静态变量的线程安全性
    static Log *get_instance() // 懒汉式
    {
        static Log instance;
        return &instance;
    }

    // 异步刷写日志,调用私有方法async_write_log
    static void *flush_log_thread(void *args)
    {
        Log::get_instance()->async_write_log();
    }
    // file_name：日志文件名
    // close_log:是否关闭日志
    // split_lines: 每个日志文件最大行数
    // max_queue_size：阻塞队列最大长度
    bool init(const char *file_name, int close_log, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);

    // 日志分级与分文件写入
    void write_log(int level, const char *format, ...);

    void flush(void);

private:
    Log(); // 单例禁止外部创建对象
    virtual ~Log();
    void *async_write_log()
    {
        string single_log; // 一行日志
        // 从阻塞队列取一行日志写入
        while (m_log_queue->pop(single_log))
        {
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }
private:
    char dir_name[128]; // 路径名
    char log_name[128]; // 日志文件名
    int m_split_lines;  // 日志最大行数
    int m_log_buf_size; // 日志缓冲区大小
    long long m_count;  // 日志行数
    int m_today;        // 当前是哪一天
    FILE  *m_fp;        // 指向log文件内的具体位置
    char *m_buf;
    block_queue<string> *m_log_queue; // 阻塞队列
    bool m_is_async;    // 是否为异步写日志
    locker m_mutex;
    int m_close_log;    // 关闭日志,0表示打开日志
};
#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif