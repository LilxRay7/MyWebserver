# MyWebserver

todo：Webbench压力测试并发数量

(latest)
version 0.4: (2022/07/08) 添加数据库连接池，新增解析POST请求，通过访问服务器数据库实现web端用户注册和登录功能，能够请求服务器上的图片和视频文件

version 0.3: (2022/07/02) 添加异步日志系统，记录服务器运行状态

version 0.2: (2022/06/24) 添加了定时器处理非活动连接

version 0.1: (2022/06/18) 一个基本的Webserver：线程池 + epoll LT + Reactor并发模式 + 状态机解析HTTP报文（仅支持GET）
