## 日志模块

- 单例模式
- 生产者-消费者模型
- 阻塞队列

### 代码顺序

- `class block_queue` 阻塞队列
- `class Log` 日志类
- `bool Log::init`
- `void Log::write_log`

### 前置知识

#### pthread_cond_wait执行后的内部操作：

- 将线程放在**条件变量的请求队列**后，内部解锁
- 线程等待被pthread_cond_broadcast信号唤醒或者pthread_cond_signal信号唤醒，唤醒后去竞争锁
- 若竞争到互斥锁，内部再次加锁

#### **fputs**

```cpp
include <stdio.h>
int fputs(const char *str, FILE *stream);
```

- str，一个数组，包含了要写入的以空字符终止的字符序列。
- stream，指向FILE对象的指针，该FILE对象标识了要**被写入**字符串的流。



#### **可变参数宏__VA_ARGS__**

__VA_ARGS__是一个可变参数的宏，定义时宏定义中参数列表的最后一个参数为省略号，在实际使用时会发现有时会加##，有时又不加。

```cpp
//最简单的定义
#define my_print1(...)  printf(__VA_ARGS__)

//搭配va_list的format使用
#define my_print2(format, ...) printf(format, __VA_ARGS__)  
#define my_print3(format, ...) printf(format, ##__VA_ARGS__)
```

__VA_ARGS__宏前面加上##的作用在于，当可变参数的个数为0时，这里printf参数列表中的的##会把前面多余的","去掉，否则会编译出错，建议使用后面这种，使得程序更加健壮。



#### **fflush**

```cpp
include <stdio.h>
int fflush(FILE *stream);
```

fflush()会强迫**将缓冲区内的数据写回参数stream 指定的文件**中，如果参数stream 为NULL，fflush()会将所有打开的文件数据更新。

在使用多个输出函数连续进行多次输出到控制台时，有可能**下一个数据再上一个数据还没输出完毕，还在输出缓冲区中时，下一个printf就把另一个数据加入输出缓冲区，结果冲掉了原来的数据，出现输出错误**。

在prinf()后加上fflush(stdout); 强制马上输出到控制台，可以避免出现上述错误。

### 阻塞队列

- 底层容器使用**动态数组**实现
- 当队列为空时，从队列中获取元素的线程将会被挂起；当队列是满时，往队列里添加元素的线程将会挂起。
- 入队时`broadcast`唤醒等待元素的线程, 会惊群？
- 出队时，若队空，`wait`等待生产者线程



### 日志类

#### **日志分级与分文件**

超过最大行、按天分文件逻辑，具体的，

- 日志写入前会判断当前day是否为创建日志的时间，行数是否超过最大行限制
  - 若为创建日志时间当天，写入日志，否则按当前时间创建新log，更新创建时间和行数
  - 若行数超过最大行限制，在当前日志的末尾加count/max_lines为后缀创建新log
- 行数是每天从0开始统计

将系统信息格式化后输出，具体为：格式化时间 + 格式化内容