// 使用单例模式创建日志系统，对服务器运行状态、错误信息和访问数据进行记录，实现按天分类，超行分类功能，
// 使用异步写入方式，将生产者-消费者模型封装为阻塞队列
// 创建一个写线程，工作线程将要写的内容push进队列，写线程从队列中取出内容，写入日志文件。
#ifndef LOG_H
#define LOG_H

#include<stdio.h>
#include<iostream>
#include<string>
#include<stdarg.h>
#include<pthread.h>

#include"block_queue.h"

// 宏定义写日志方法
// __VA_ARGS__是一个可变参数的宏，定义时宏定义中参数列表的最后一个参数为省略号
// __VA_ARGS__宏前面加上##的作用在于，当可变参数的个数为0时，这里printf参数列表中的的##会把前面多余的','去掉
// 否则会编译出错，建议使用后面这种，使得程序更加健壮。
#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

using namespace std;

class Log {
    public:
        // 单例模式是最常用的设计模式之一，保证一个类仅有一个实例，并提供一个访问它的全局访问点
        // 单例模式有两种实现方法，分别是懒汉和饿汉模式
        // 懒汉模式，即非常懒，不用的时候不去初始化，所以在第一次被使用时才进行初始化
        // 饿汉模式，即迫不及待，在程序运行时立即初始化
        // c++11后使用局部变量懒汉不用加锁
        static Log* get_instance() {
            static Log instance;
            return &instance;
        } 

        static void* flush_log_thread(void* args) {
            Log::get_instance()->async_write_log();
        }

        // 参数：日志文件名、日志缓冲区大小、最大行数和最长日志条队列
        bool init(const char* file_name, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 8);

        void write_log(int level, const char* format, ...);

        void flush(void);

    private:
        // 将构造函数设为私有函数，以防止外界创建单例类的对象，用一个公有的静态方法get_instance()获取该实例
        Log();
        virtual ~Log();
        void* async_write_log() {
            string single_log;
            // 从阻塞队列中取出一个日志string，写入文件
            while (m_log_queue->pop(single_log)) {
                m_mutex.lock();
                // int fputs(const char *str, FILE *stream);
                fputs(single_log.c_str(), m_fp);
                m_mutex.unlock();
            }
        }
    
    private:
        // 路径名
        char dir_name[128];
        // 日志文件名
        char log_name[128];
        // 日志最大行数
        int m_split_lines;
        // 日志缓冲区大小
        int m_log_buf_size;
        // 日志行数记录
        long long m_count;
        // 按天分类，记录当前时间是哪一天
        int m_today;
        // 打开日志的文件指针
        FILE* m_fp;
        char* m_buf;
        // 阻塞队列
        block_queue<string>* m_log_queue;
        // 是否同步标志位
        bool m_is_async;
        // 互斥锁
        locker m_mutex;
};

#endif