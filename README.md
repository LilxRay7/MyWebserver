# MyWebserver

## Introduction
Linux下C++实现的高性能Web服务器，经webbench压力测试可以实现近万QPS

## Technical points

* 使用**Epoll(ET)**、**非阻塞socket**与**线程池**实现**模拟Proactor**事件处理的**半同步/半反应堆**并发模型
* 使用**状态机**解析HTTP请求报文，支持解析**GET**和**POST**请求
* 利用**RAII**机制实现了数据库连接池，减少连接开销，同时实现了用户**注册**和**登录**功能，可以请求**图片和视频文件**
* 利用**单例模式**与**阻塞队列**实现的**异步日志**系统，记录服务器运行状态
* 基于**升序双向链表**实现的**定时器**，关闭超时的非活动连接

## Specific

* 模拟Proactor事件处理的半同步/半反应堆并发模型
    * 异步线程由主线程来充当，它通过socket()、bind()、listen()并使用ET模式监听listenfd的读请求，主线程通过epoll_wait()监听所有socket上的事件，因为是ET模式，accept()要一直循环到就绪连接为空
    * 如果有新的连接到来，主线程就accept()并完成初始化工作（往epoll内核注册读事件EPOLLIN、边缘触发ET、设置非阻塞O_NONBLOCK和定时器timer等）
    * 如果主线程监听到socket上可读事件，就由主线程完成数据的读取read()
    * 读取socket传来的数据后，主线程将任务append()到任务队列中，通过信号量post()通知工作线程
    * 工作线程都wait()睡眠在队列上，工作线程竞争得到该request，这种机制使得只有空闲的工作线程才有机会处理新任务
    * 工作线程从队列中取得任务对象后，无需执行读写操作，可直接process()之，处理客户逻辑是同步线程，先process_read()后process_write()，完毕后为该socket注册可写事件EPOLLOUT
    * 如果主线程监听到socket上可写事件，就由主线程完成数据的发送write()，并根据长短连接keep-alive选择是否关闭该socket
    * 使用线程池，减少线程的创建与关闭的开销
    * 各线程对任务队列进行操作时通过互斥锁mutex进行同步

* 状态机解析HTTP报文
    * 通过一个主状态机和从状态机实现HTTP报文的边读取边解析，主状态机在内部调用从状态机
    * 从状态机在buffer中解析出一个行后返回line_ok，将内容交给主状态机，如果行没有读取完则返回line_open表示需要继续读取，如果请求存在问题则返回line_bad
    * 主状态机使用checkstate记录当前状态，实现从requestline到header再到conten的状态转移，最终进行do_request()
    * 执行request时会根据文件状态（是否存在、可读）以及cgi标志位返回客户端对应的结果

* 数据库池
    * 实现了数据库连接池，减少数据库连接建立与关闭的开销
    * 将数据库连接的获取与释放通过RAII机制封装，避免手动释放
    * 使用局部静态变量懒汉模式实现的单例模式，保证数据库池的唯一
    * 使用信号量sem同步连接的获取，每次取出连接，wait()信号量原子-1，释放连接，post()信号量原子+1
    * 对连接池操作时多线程通过互斥锁mutex完成同步

* 异步日志系统
    * 使用局部静态变量懒汉模式实现的单例模式，保证日志系统的唯一，实现按天分类，超行分类功能
    * 使用异步写入方式，将生产者-消费者模型封装为阻塞队列block_queue，工作线程(生产者)将要写的内容push()进队列
    * 工作线程通过条件变量cond通知写线程，写线程(消费者)pop()出日志，写入日志文件
    * 使用互斥锁mutex保证写线程和工作线程在操作阻塞队列时同步

* 定时器
    * 使用自定义的双向升序链表作为定时器容器
    * 利用alarm()周期性触发SIGALRM信号，主循环通过统一事件源利用管道接收信号处理函数的通知，执行timer_handler()，其中执行一次tick()并重置alarm()
    * tick()从头节点开始处理超时任务，调用超时任务的回调函数cb_func()，删除非连接活动在socket上的注册事件并close()连接
    * 主循环在监听到socket上的读写事件后也会adjust_timer()调整对应的定时器

## Todo

* 小根堆定时器
* RAII机制锁
* 智能指针
* 缓冲区自动增长
* 双缓冲技术Log日志
* ...

## Environment
* Server:
    * Ubuntu 22.04 LTS on remote VMware
    * mysql 8.0.29

* Browser:
    * Windows、Linux
    * Chrome
    * FireFox

## Build

* MySQL

    ```C++
    // 建立yourdb库
    create database yourdb;

    // 创建user表
    USE yourdb;
    CREATE TABLE user(
        username char(50) NULL,
        passwd char(50) NULL
    )ENGINE=InnoDB;

    // 添加数据
    INSERT INTO user(username, passwd) VALUES('name', 'password');
    ```

* 修改main.c中的数据库初始化信息

    ```C++
    // "root", "root"修改为服务器数据库的登录名和密码
    // "yourdb"修改为数据库名称
    connPool->init("localhost", "root", "root", "yourdb", 3306, 8);
    ```

* 修改http_conn.cpp中的root路径

    ```C++
	// 修改为root文件夹所在路径
    const char* doc_root="/home/xxx/MyWebserver/root";
    ```

* 修改main.cpp中的服务器ip

    ```C++
	// ip修改为服务器ip
    const char* ip = "192.168.17.129";
    ```

* 生成server

    ```C++
    make 
    ```

* 启动server

    ```C++
    ./run port
    ```

* 浏览器
    ```C++
    ip:port
    ```


## Index tree
```
.
├── CGImysql
│   ├── sql_connection_pool.cpp
│   ├── sql_connection_pool.h
│   └── test_mysql.cpp
├── http
│   ├── http_conn.cpp
│   └── http_conn.h
├── LICENSE
├── lock
│   └── locker.h
├── log
│   ├── block_queue.h
│   ├── log.cpp
│   └── log.h
├── main.cpp
├── makefile
├── README.md
├── root
├── run
├── test
│   ├── stress_test.cpp
│   ├── test
│   └── webbench-1.5
├── threadpool
│   └── threadpool.h
└── timer
    └── lst_timer.h
```

## Stress test
test enviroment：
* Ubuntu 22.04 LTS on remote VMware
* RAM：4GB

![](./root/test2.jpg)

因虚拟机内存原因webbench最多可以fork约9300个子进程进行并发访问

![](./root/test1.jpg)

## History
* (2022/07/11) 服务器经压力测试能够承受接近上万并发访问

* (2022/07/08) 添加数据库连接池，新增解析POST请求，实现web端用户注册和登录功能，能够请求服务器上的图片和视频文件

* (2022/07/02) 添加异步日志系统，记录服务器运行状态

* (2022/06/24) 添加了定时器处理非活动连接

* (2022/06/18) 一个基本的Webserver：线程池 + epoll ET + Rroactor并发模式 + 状态机解析HTTP报文（仅支持GET）

## Thanks
《Linux高性能服务器编程》，游双著

TinyWebServer@qinguoyi: https://github.com/qinguoyi/TinyWebServer