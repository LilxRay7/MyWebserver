// 输入mysql_config命令查看LIB依赖 --libs_r 后面的内容是编译时需要添加的选项
// 例如 g++ sql_connection_pool.cpp -L/usr/lib/x86_64-linux-gnu -lmysqlclient -lz -lzstd -lssl -lcrypto -lresolv -lm
// 实际上只加一个 -lmysqlclient 也可
#include<mysql/mysql.h>
#include<stdio.h>
#include<string>
#include<string.h>
#include<stdlib.h>
#include<list>
#include<pthread.h>
#include<iostream>

#include"sql_connection_pool.h"

using namespace std;

// 构造函数
connection_pool::connection_pool() {
    cur_conn = 0;
    free_conn = 0;
}

// 局部静态变量单例模式
connection_pool* connection_pool::get_instance() {
    static connection_pool conn_pool;
    return &conn_pool;
}

// 初始化
void connection_pool::init(string Url, string User, string Password, string Database_name, int Port, unsigned int Max_conn) {
    url = Url;
    user = User;
    password = Password;
    database_name = Database_name;
    port = Port;

    lock.lock();
    for (int i = 0; i < Max_conn; i++) {
        MYSQL* con = nullptr;
        con = mysql_init(con);

        if (con == nullptr) {
            printf("error: %s", mysql_error(con));
            exit(1);
        }

        // c_str()生成一个const char*指针，指向以空字符终止的数组
        con = mysql_real_connect(con, url.c_str(), user.c_str(), password.c_str(), database_name.c_str(), Port, nullptr, 0);

        if (con == nullptr) {
            printf("error: %s", mysql_error(con));
            exit(1);
        }

        conn_list.push_back(con);
        ++free_conn;

        printf("init success. free connection: %d\n", free_conn);
    }
    // 设置信号量数量
    reserve = sem(free_conn);
    max_conn = free_conn;
    lock.unlock();
}

// 当有请求时，返回一个可用连接，更新已使用和空闲连接数
MYSQL* connection_pool::get_connection() {
    if (conn_list.size() == 0)
        return nullptr;

    MYSQL* con = nullptr;

    // 信号量减一
    reserve.wait();
    // 操作连接池前上锁
    lock.lock();

    con = conn_list.front();
    conn_list.pop_front();
    --free_conn;
    ++cur_conn;

    // 解锁
    lock.unlock();

    printf("get connection success. free connection: %d\n", free_conn);
    return con;
}

// 释放当前使用的连接
bool connection_pool::release_connection(MYSQL* con) {
    if (con == nullptr)
        return false;

    // 操作连接池前上锁
    lock.lock();

    conn_list.push_back(con);
    ++free_conn;
    --cur_conn;

    // 解锁
    lock.unlock();
    // 信号量加一
    reserve.post();

    printf("release connection success. free connection: %d\n", free_conn);
    return true;
}

// 销毁数据库连接池
void connection_pool::destroy_pool() {
    // 操作连接池前上锁
    lock.lock();

    if (conn_list.size() <= 0) {
        // 解锁
        lock.unlock();
        return;
    }
        
    for (auto &item : conn_list) {
        mysql_close(item);
    }

    cur_conn = 0;
    free_conn = 0;
    conn_list.clear();
    // 解锁
    lock.unlock();
    printf("destroy pool success.\n");
}

// 当前空闲的连接数
int connection_pool::get_free_conn() {
    return free_conn;
}

// 析构函数
connection_pool::~connection_pool() {
    destroy_pool();
}

connection_RAII::connection_RAII(MYSQL** SQL, connection_pool* conn_pool) {
    *SQL = conn_pool->get_connection();
    con_RAII = *SQL;
    pool_RAII = conn_pool;
}

connection_RAII::~connection_RAII() {
    pool_RAII->release_connection(con_RAII);
}