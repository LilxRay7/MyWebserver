// version: 2.0
// 增加定时器处理无活动连接

#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<stdio.h>
#include<unistd.h>
#include<errno.h>
#include<string.h>
#include<fcntl.h>
#include<stdlib.h>
#include<cassert>
#include<sys/epoll.h>

#include"locker.h"
#include"threadpool.h"
#include"http_conn.h"
#include"lst_timer.h"

// 最大文件描述符
#define MAX_FD 65536
// 最大事件数
#define MAX_EVENT_NUMBER 10000
// 最小超时单位
#define TIMESLOT 5

// 这三个函数在http_conn.cpp中定义，改变文件描述符属性
extern int addfd(int epollfd, int fd, bool one_shot);
extern int removefd(int epollfd, int fd);
extern int setnonblocking(int fd);

// 设置管道，当信号触发时，信号处理函数通过管道通知主循环并传递信号值
static int pipefd[2];
// 计时器升序链表
static sort_timer_lst timer_lst;

static int epollfd = 0;

// 信号处理函数
void sig_handler(int sig) {
    // 为保证函数的可重入性，保留原来的errno
    // 若一个程序或子程序可以“在任意时刻被中断然后操作系统调度执行另外一段代码
    // 这段代码又调用了该子程序不会出错”，则称其为可重入（re-entrant）的
    int save_errno = errno;
    int msg = sig;
    // 将信号值写入管道以通知主循环
    send(pipefd[1], (char*)&msg, 1, 0);
    errno = save_errno;
}

// 设置信号函数
void addsig(int sig, void(handler)(int), bool restart = true) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

// 定时处理任务，重新定时以不断触发SIGALRM信号
void timer_handler() {
    timer_lst.tick();
    alarm(TIMESLOT);
}

// 定时器回调函数，删除非连接活动在socket上的注册事件并将其关闭
void cb_func(client_data* user_data) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, user_data->sockfd, 0);
    assert(user_data);
    close(user_data->sockfd);
    http_conn::m_user_count--;
    printf("close file descriper %d\n", user_data->sockfd);
}

void show_error(int connfd, const char* info) {
    printf("%s", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int main(int argc, char* argv[]) {
    if (argc <= 2) {
        printf("usage:%s ip_address port_number\n", basename(argv[0]));
        return 1;
    }

    const char* ip = argv[1];
    int port = atoi(argv[2]);

    // 创建线程池
    threadpool<http_conn>* pool = NULL;
    try {
        pool = new threadpool<http_conn>;
    } catch(...) {
        return 1;
    }

    // 预先为每个可能的客户分配一个 http_conn 对象
    http_conn* users = new http_conn[MAX_FD];
    assert(users);
    int user_count = 0;

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    // Linux 下 tcp 连接断开的时候调用 close() 函数，有优雅断开和强制断开两种方式
    // 通过设置 socket 描述符一个linger结构体属性
    // struct linger 
    //     int l_onoff;
    //     int l_linger;
    // };
    // l_onoff = 0: close()立刻返回，底层会将未发送完的数据发送完成后再释放资源，即优雅退出。
    // l_onoff != 0; l_linger = 0;close()立刻返回，但不会发送未发送完成的数据，而是通过一个REST包强制的关闭socket描述符，即强制退出。
    struct linger tmp = {1, 0};
    setsockopt(listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    // inet_pton() 将点分十进制 ip 地址转化为整数
    inet_pton(AF_INET, ip, &address.sin_addr);
    // htons(): host to net short int 将主机的无符号短整型转换为网络字节顺序
    address.sin_port = htons(port);

    ret = bind(listenfd, (struct sockaddr*) &address, sizeof(address));
    assert(ret >= 0);

    ret = listen(listenfd, 5);
    assert(ret >= 0);

    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, listenfd, false);
    // 所有socket上的事件都被注册到同一个epoll内核事件表中，所以将epoll文件描述符设置为静态的
    http_conn::m_epollfd = epollfd;

    // 使用socketpair创建管道，注册pipefd[0]上的可读事件
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipefd);
    assert(ret != -1);
    setnonblocking(pipefd[1]);
    addfd(epollfd, pipefd[0], false);

    // 设置信号处理函数
    addsig(SIGALRM, sig_handler, false);
    addsig(SIGTERM, sig_handler, false);
    // 忽略 SIGPIPE 信号
    // 默认情况下，往一个读端关闭的管道或socket连接中写数据将引发SIGPIPE信号
    // 程序接收到SIGPIPE信号的默认行为是结束进程，所以不希望因为错误的写操作而导致程序退出
    addsig(SIGPIPE, SIG_IGN);
    bool stop_server = false;

    // 预先为所有可能的用户分配定时器相关信息，以socket的fd为索引
    client_data* users_timer = new client_data[MAX_FD];
    bool timeout = false;
    // 定时
    alarm(TIMESLOT);

    while (!stop_server) {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR)) {
            printf("epoll failure\n");
            break;
        }

        for (int i  = 0; i < number; i++) {
            int sockfd = events[i].data.fd;
            // 如果就绪的文件描述符是listenfd，处理新到的客户连接
            if (sockfd == listenfd) {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr*)&client_address, &client_addrlength);
                if (connfd < 0) {
                    printf("errno is: %d\n", errno);
                    continue;
                }
                if (http_conn::m_user_count >= MAX_FD) {
                    show_error(connfd, "Internal server busy");
                    continue;
                }
                users[connfd].init(connfd, client_address);
                // 初始化client_data数据
                users_timer[connfd].address = client_address;
                users_timer[connfd].sockfd = connfd;
                // 创建定时器
                util_timer* timer = new util_timer;
                // 绑定用户数据
                timer->user_data = &users_timer[connfd];
                // 设置回调函数
                timer->cb_func = cb_func;
                // 设置超时时间
                time_t cur = time(NULL);
                timer->expire = cur + 3 * TIMESLOT;
                // 绑定定时器
                users_timer[connfd].timer = timer;
                // 将定时器添加到链表中
                timer_lst.add_timer(timer);
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // 当管道的读端关闭时，写端文件描述符上的POLLHUP事件将被触发
                // 当socket连接被对方关闭时，socket上的POLLRDHUP事件将被触发
                // 服务器端关闭连接，移除对应的定时器
                util_timer* timer = users_timer[sockfd].timer;
                timer->cb_func(&users_timer[sockfd]);
                if (timer) {
                    timer_lst.del_timer(timer);
                }
                // v2.0引入定时器后，关闭连接操作由定时器的回调函数执行
                // users[sockfd].close_conn();
            } else if ((sockfd == pipefd[0]) && (events[i].events & EPOLLIN)) {
                // 如果就绪的文件描述符时pipefd[0]则处理信号
                int sig;
                char signals[1024];
                ret = recv(pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1) {
                    continue;
                } else if (ret == 0) {
                    continue;
                } else {
                    // 每个信号值占一个字节
                    for (int i = 0; i < ret; i++) {
                        switch (signals[i]) {
                            // 定时时间到，timeout设为true
                            // 通常定时任务的优先级不高，可以留到后面完成
                            case SIGALRM: {
                                timeout = true;
                                break;
                            }
                            case SIGTERM: {
                                stop_server = true;
                            }
                        }
                    }
                }
            } else if (events[i].events & EPOLLIN) {
                // 处理客户连接上接收到的数据
                util_timer* timer = users_timer[sockfd].timer;
                if (users[sockfd].read()) {
                    // 检测到读事件，将该事件放入任务队列中
                    pool->append(users + sockfd);

                    // 有数据传输时定时器相关操作
                    if (timer) {
                        time_t cur = time(NULL);
                        // 将定时器往后延迟3个单位
                        timer->expire = cur + 3 * TIMESLOT;
                        // 更新定时器后调整链表
                        timer_lst.adjust_timer(timer);
                        printf("adjust timer of %d\n", sockfd);
                    }
                } else {
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer) {
                        timer_lst.del_timer(timer);
                    }
                    // v2.0引入定时器后，关闭连接操作由定时器的回调函数执行
                    // users[sockfd].close_conn();
                }
            } else if (events[i].events & EPOLLOUT) {
                // 检测到可写事件
                util_timer* timer = users_timer[sockfd].timer;
                if (users[sockfd].write()) {
                    // 有数据传输时定时器相关操作
                    if (timer) {
                        time_t cur = time(NULL);
                        timer->expire = cur + 3 * TIMESLOT;
                        timer_lst.adjust_timer(timer);
                    }
                } else {
                    timer->cb_func(&users_timer[sockfd]);
                    if (timer) {
                        timer_lst.adjust_timer(timer);
                        printf("adjust timer of %d\n", sockfd);
                    }
                }
            } else {
                printf("program shoul never come here...\n");
            }
            if (timeout) {
                // 处理定时任务
                timer_handler();
                timeout = false;
            }
        }
    }
    
    // 关闭epoll文件描述符
    close(epollfd);
    // 关闭监听socket
    close(listenfd);
    // 关闭管道
    close(pipefd[1]);
    close(pipefd[0]);
    // 删除用户数据
    delete[] users;
    // 删除用户定时器
    delete[] users_timer;
    // 销毁线程池
    delete pool;
    return 0;
}