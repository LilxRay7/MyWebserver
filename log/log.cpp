#include<string.h>
#include<time.h>
#include<sys/time.h>
#include<stdarg.h>
#include<pthread.h>

#include"log.h"

using namespace std;

Log::Log() {
    m_count = 0;
    m_is_async = true;
}

Log::~Log() {
    if (m_fp != NULL) {
        fclose(m_fp);
    }
}

// 初始化工作进程并创建日志文件
bool Log::init(const char* file_name, int log_buf_size, int split_line, int max_queue_size) {
    if (max_queue_size >= 1) {
        m_log_queue = new block_queue<string> (max_queue_size);
        pthread_t tid;
        // flush_log_thread为回调函数，这里表示创建线程异步写日志
        // 同理threadpool中创建线程时有worker函数
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }

    m_log_buf_size = log_buf_size;
    m_buf = new char[m_log_buf_size];
    // 清空缓冲区，常用操作
    memset(m_buf, '\0', m_log_buf_size);
    m_split_lines = split_line;

    time_t t = time(NULL);
    struct tm* sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    //  char *strrchr(const char *str, int c) 在参数str所指向的字符串中搜索最后一次出现字符c的位置，无则返回空指针
    const char* p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    if (p == nullptr) {
        // int snprintf(char *str, size_t size, const char *format, ...)
        // 设将可变参数(...)按照format格式化成字符串，并将字符串复制到str中，size为要写入的字符的最大数目，超过size会被截断
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, 
                 my_tm.tm_mday, file_name);
    } else {
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, 
                 my_tm.tm_mday, file_name);
    }

    m_today = my_tm.tm_mday;

    // FILE *fopen(const char *filename, const char *mode) 使用给定的模式 mode 打开 filename 所指向的文件
    // "a": 追加到一个文件，写操作向文件末尾追加数据，如果文件不存在，则创建文件
    m_fp = fopen(log_full_name, "a");
    if (m_fp == nullptr) {
        return false;
    }

    return true;
}

// 根据日志分级写入日志
// Debug: 调试代码时的输出，在系统实际运行时，一般不使用
// Warn: 这种警告与调试时终端的warning类似，同样是调试代码时使用
// Info: 报告系统当前的状态，当前执行的流程或接收的信息等
// Error和Fatal: 输出系统的错误信息
void Log::write_log(int level, const char* format, ...) {
    // struct timeval
    // {
    //  __time_t tv_sec;        /* Seconds. */
    //  __suseconds_t tv_usec;  /* Microseconds. */
    // };
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};

    switch (level) {
        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[erro]:");
            break;
        default:
            strcpy(s, "[info]:");
            break;
    }

    {
        // 写日志前加锁
        locker_RAII lock_RAII(m_mutex);
        m_count++;

        // 如果是新一天或者日志数量到达最大行数就新开日志
        if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {
            char new_log[256] = {0};
            fflush(m_fp);
            fclose(m_fp);
            char tail[16] = {0};
            snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);
            if (m_today != my_tm.tm_mday) {
                snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
                m_today = my_tm.tm_mday;
                m_count = 0;
            } else {
                snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
            }
            m_fp = fopen(new_log, "a");
        }
    }

    va_list valst;
    va_start(valst, format);

    string log_str;
    // 写日志前加锁
    locker_RAII lock_RAII(m_mutex);

    // 写入的具体时间内容格式
    // snprintf()返回值为欲写入的字符串长度
    // %02d: 二位十进制整数
    // %06ld: 六位long整数
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_sec, s);

    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    if (m_is_async && !m_log_queue->full()) {
        // 将要写的内容push进队列，异步的体现之处
        m_log_queue->push(log_str);
    }
    // va_start与va_end总是成对出现
    va_end(valst);
}

void Log::flush(void) {
    // 写日志前加锁
    locker_RAII lock_RAII(m_mutex);
    // 强制刷新写入流缓冲区
    fflush(m_fp);
}