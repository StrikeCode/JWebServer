#include "lst_timer.h"
#include "../http/http_conn.h"

sort_timer_lst::~sort_timer_lst()
{
    util_timer *tmp = head;
    while (tmp)
    {
        head = tmp->next;
        delete tmp;
        tmp = head;
    }
}

// 将定时器timer添加到链表中
void sort_timer_lst::add_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    if (!head) // 空链表
    {
        head = tail = timer;
        return;
    }
    // 若目标定时器超时时间小于当前链表中所有定时器的超时时间
    // 则把该定时器插入到头部，作为链表头节点
    // 否则就要插入合适的位置以保证升序
    if (timer->expire < head->expire)
    {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return;
    }
    add_timer(timer, head);
}
// 当某个定时任务发生变化时，调整对应的定时器的超时时间
// 这个函数只考虑被调整的定时器的【超时时间的延长情况】，即该定时器要往链表尾部移动
void sort_timer_lst::adjust_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    util_timer *tmp = timer->next;
    // 被调整定时器在链表尾部，或该定时器超时时间仍小于下一个定时器的超时时间，则不用调整
    if (!tmp || (timer->expire < tmp->expire))
    {
        return;
    }
    // 若目标定时器是链表头节点，则将该定时器取出重新插入链表
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        timer->next = NULL;
        add_timer(timer, head);
    }
    // 若目标定时器不是链表头节点，则将该定时器从链表中取出，然后插入原来所在位置之后的部分链表中
    else
    {
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
    }
}

void sort_timer_lst::del_timer(util_timer *timer)
{
    if (!timer)
    {
        return;
    }
    // 链表只剩待删除定时器
    if ((timer == head) && (timer == tail))
    {
        delete timer;
        head = NULL;
        tail = NULL;
        return;
    }
    if (timer == head)
    {
        head = head->next;
        head->prev = NULL;
        delete timer;
        return;
    }
    if(timer == tail) 
    {
        tail = tail->prev;
        tail->next = NULL;
        delete timer;
        return;
    }

    // 目标定时器位于链表中间
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
}

// SIGALARM信号每次触发就在其信号处理函数中执行一次tick函数
// 来处理链表上到期的任务。
void sort_timer_lst::tick()
{
    if(!head)
    {
        return ;
    }
    //printf("timer tick\n");
    time_t cur = time(NULL);
    util_timer *tmp = head;
    // 从头开始依次处理每个定时器，直到遇到一个尚未到期的定时器
    while(tmp)
    {
        // 最快到时的定时器的时间都比现在的时间大
        if(cur < tmp->expire)
        {
            break;
        }
        // 执行定时任务
        tmp->cb_func(tmp->user_data);
        head = tmp->next;
        if(head)
        {
            head->prev = NULL;
        }
        delete tmp;
        tmp = head;
    }
}


void sort_timer_lst::add_timer(util_timer *timer, util_timer *lst_head)
{
    util_timer *prev = lst_head;
    util_timer *tmp = prev->next; // 可能插入的位置
    while(tmp) 
    {
        if(timer->expire < tmp->expire)
        {
            prev->next = timer;
            timer->next = tmp;
            tmp->prev = timer;
            timer->prev = prev;
            break;
        }
        prev = tmp;
        tmp = tmp->next;
    }
    if(!tmp) // 链表尾部插入
    {
        prev->next = timer;
        timer->prev = prev;
        timer->next = NULL;
        tail = timer;
    }
}

void Utils::init(int timeslot)
{
    m_TIMESLOT = timeslot;
}

int Utils::setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void Utils::addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;
    
    if(1 == TRIGMode) // ET
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else // LT(EPOLL默认LT)
        event.events = EPOLLIN | EPOLLRDHUP;
    // 使一个socket连接任何时刻都只被一个线程所处理
    if(one_shot)
        event.events |= EPOLLONESHOT;

    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void Utils::sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(u_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

void Utils::addsig(int sig, void(handler)(int), bool restart)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if(restart)
        sa.sa_flags |= SA_RESTART;
    sigfillset(&sa.sa_mask); // 设置所有信号
    // 为信号注册处理函数
    assert(sigaction(sig, &sa, NULL) != -1);
}

void Utils::timer_handler()
{
    // 定时处理任务，检查有没有到时的定时器，执行其对应任务
    m_timer_lst.tick();

    // 重新定时
    alarm(m_TIMESLOT); // 到时会发出SIGALARM信号
}

void Utils::show_error(int connfd, const char *info)
{
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

// 静态成员类外初始化
int *Utils::u_pipefd = 0;
int Utils::u_epollfd = 0;

class Utils;
// 定时器回调函数，删除非活动连接socket上的注册事件，并关闭之
void cb_func(client_data *user_data)
{
    epoll_ctl(Utils::u_epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
    //printf("close fd %d\n", user_data->sockfd);
}