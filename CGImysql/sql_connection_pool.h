#ifndef CONNECTION_POOL
#define CONNECTION_POOL

#include<stdio.h>
#include<list>
#include<mysql/mysql.h>
#include<error.h>
#include<string.h>
#include<iostream>
#include<string>

#include"../lock/locker.h"

using namespace std;

class connection_pool {
    public:
        // 获取数据库连接
        MYSQL* get_connection();
        // 释放连接
        bool release_connection(MYSQL* conn);
        // 获取空闲连接
        int get_free_conn();
        // 销毁连接池
        void destroy_pool();

        // 局部静态变量单例模式
        static connection_pool* get_instance();
        // 初始化
        void init(string url, string user, string password, string database_name, int port, unsigned int max_conn);
        connection_pool();
        ~connection_pool();

    private:
        // 最大连接数
        unsigned int max_conn;
        // 当前已使用的连接数
        unsigned int cur_conn;
        // 当前空闲的连接数
        unsigned int free_conn;

        // 互斥锁
        locker lock;
        // 信号量
        sem reserve;
        // 连接池
        list<MYSQL*> conn_list;

        // 主机地址
        string url;
        // 数据库端口号
        string port;
        // 登录数据库用户名
        string user;
        // 登录数据库密码
        string password;
        // 使用数据库名
        string database_name;
};

// 将数据库连接的获取与释放通过RAII机制封装，避免手动释放
class connection_RAII {
    public:
        connection_RAII(MYSQL** con, connection_pool* conn_pool);
        ~connection_RAII();

    private:
        MYSQL* con_RAII;
        connection_pool* pool_RAII;
};

#endif