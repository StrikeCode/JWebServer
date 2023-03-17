# JWebServer
Linux下轻量级WebServer服务器

- 计划后面改进日志模块，换成陈硕的



### 服务器启动参数

```
./server [-p port] [-l LOGWrite] [-m TRIGMode] [-o OPT_LINGER] [-s sql_num] [-t thread_num] [-c close_log] [-a actor_model]
```

- -p，自定义端口号
  - 默认8888
- -l，选择日志写入方式，默认同步写入
  - 0，同步写入
  - 1，异步写入
- -m，listenfd和connfd的模式组合，默认使用LT + LT
  - 0，表示使用LT + LT
  - 1，表示使用LT + ET
  - 2，表示使用ET + LT
  - 3，表示使用ET + ET
- -o，优雅关闭连接，默认不使用
  - 0，不使用
  - 1，使用
- -s，数据库连接数量
  - 默认为8
- -t，线程数量
  - 默认为8
- -c，关闭日志，默认打开
  - 0，打开日志
  - 1，关闭日志
- -a，选择反应堆模型，默认Proactor
  - 0，Proactor模型
  - 1，Reactor模型

### 优雅断开连接 struct linger

```c
#include <arpa/inet.h>

struct linger {
　　int l_onoff;
　　int l_linger;
};
```

#### 三种断开方式：

1. l_onoff = 0; l_linger忽略

**close()立刻返回**，底层会**将未发送完的数据发送完成后再释放资源，**即优雅退出。

2. l_onoff != 0; l_linger = 0;

close()立刻返回，但不会发送未发送完成的数据，而是通过一个REST包强制的关闭socket描述符，即强制退出。

3. l_onoff != 0; l_linger > 0; 

close()不会立刻返回，内核会延迟一段时间，这个时间就由l_linger的值来决定。如果超时时间到达之前，发送完未发送的数据(包括FIN包)并得到另一端的确认，close()会返回正确，socket描述符优雅性退出。否则，close()会直接返回错误值，未发送数据丢失，socket描述符被强制性退出。需要注意的时，如果socket描述符被设置为非堵塞型，则close()会直接返回值。



### 性能测试

#### 开启日志功能

500个客户端向服务器发出30秒请求

```sh
jyhlinux@ubuntu:~/share/test_presure/webbench-1.5$ ./webbench -c 500  -t  30   http://192.168.200.152:8888/
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://192.168.200.152:8888/
500 clients, running 30 sec.

Speed=46378 pages/min, 87024 bytes/sec.
Requests: 23189 susceed, 0 failed.

```

772 93251 QPS

#### 关闭日志功能

```sh
jyhlinux@ubuntu:~/share/test_presure/webbench-1.5$ ./webbench -c 500  -t  30   http://192.168.200.152:8888/
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://192.168.200.152:8888/
500 clients, running 30 sec.

Speed=111618 pages/min, 209253 bytes/sec.
Requests: 55809 susceed, 0 failed.
```

1860 / 95000 QPS

#### 开启日志功能，LT+ET

```sh
jyhlinux@ubuntu:~/share/test_presure/webbench-1.5$ ./webbench -c 500  -t  30   http://192.168.200.152:8888/
Webbench - Simple Web Benchmark 1.5
Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.

Benchmarking: GET http://192.168.200.152:8888/
500 clients, running 30 sec.

Speed=66286 pages/min, 124566 bytes/sec.
Requests: 33143 susceed, 0 failed.
```

95000QPS

### 问题



1. 只要连接建立后一段时间没有请求页面，就会出现如下错误

```sh
jyhlinux@ubuntu:~/share/JWebServer$ ./server -p 8888
double free or corruption (out)
Aborted
```

分析可能是内存泄露或者二次析构的问题，

因为是一段时间后出现的问题，所以我猜测是定时器这个模块的问题，有没有可能是多次close的问题，于是我gdb调试了一下找不到问题，只好通过将每个类的代码和原来代码进行对比，最后才在threadpool.h找到问题，创建线程误写为

```cpp
m_threads = new pthread_t(m_thread_number); // 创建一个线程id的内存空间
```

这会导致，后面按数组方式访问第一个后面的线程时非法访问内存地址

应改为

```cpp
m_threads = new pthread_t[m_thread_number];
```

心得体会：

遇到内存泄漏可以定位 `new` 和`free`附近的代码



2. 并发数提高，目前webbench最大测到5000
