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
        // c++11后使用局部变量懒汉不用加锁
        static Log* get_instance() {
            static Log instance;
            return &instance;
        } 

        static void* flush_log_thread(void* args) {
            Log::get_instance()->async_write_log();
        }

        bool init(const char* file_name, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 8);

        void write_log(int level, const char* format, ...);

        void flush(void);

    private:
        // 将构造函数设为私有函数
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