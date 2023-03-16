#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdarg.h>
#include "log.h"
#include <pthread.h>

using namespace std;

Log::Log()
{
    m_count = 0;
    m_is_async = false; // 同步方式写日志
}

Log::~Log()
{
    if(m_fp != NULL)
    {
        fclose(m_fp);
    }
}

// 函数定义处不能写默认值
bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size)
{
    // 若设置了max_queue_size 则设置为异步
    // 同步不需要阻塞队列
    if(max_queue_size >= 1)
    {
        m_is_async = true;
        m_log_queue = new block_queue<string>(max_queue_size);
        pthread_t tid;
        // 创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_close_log = close_log;
    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_lines;

    // 获取当前时间
    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    
    // 组织日志文件名
    // 找到最后一个/的位置
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    if(p == NULL) // 指定的文件完整路径名，就在当前目录下
    {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }
    else 
    {
        strcpy(log_name, p + 1);
        // 获取路径
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;
    // a:以追加方式打开（写到文件末尾）
    // 文件不存在就创建
    m_fp = fopen(log_full_name, "a");
    if(m_fp == NULL)
    {
        return false;
    }

    return true;
}

void Log::write_log(int level, const char *format, ...)
{
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};

    // 日志分级
    switch(level)
    {
        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[erro]:");
            break;
        default:
            strcpy(s, "[info]:");
            break;
    }

    // 写入一条log，增加行数
    m_mutex.lock();
    m_count++;
    // 超行或跨天则分页,组织新的文件名
    if(m_today != my_tm.tm_mday || m_count % m_split_lines == 0)
    {
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16];

        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        if(m_today != my_tm.tm_mday) // 隔天分页
        {
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0; // 每天清零一次行计数
        }
        else // 超行分页
        {
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }

    m_mutex.unlock();

    va_list valst;
    // format是最后一个传递给函数的已知的固定参数，即省略号之前的参数。
    // 通过这个参数获取到省略号的参数存到valst中
    va_start(valst, format);

    string log_str;
    m_mutex.lock();

    // 写入具体时间内容格式:年月日 时分秒.微妙 日志级别
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                    my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                    my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    // 具体日志内容
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m] = '\n'; // 要打换行
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();
    if(m_is_async && !m_log_queue->full())
    {
        m_log_queue->push(log_str);
    }
    else // 同步方式，直接写入 
    {
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }
    va_end(valst);
}

void Log::flush(void)
{
    m_mutex.lock();
    // 强制刷新写入流缓冲
    // 防止缓冲没写入到日志文件就被覆盖
    fflush(m_fp);
    m_mutex.unlock();

}