// 线程同步库，包含信号量、互斥锁和条件变量
#ifndef LOCKER_H
#define LOCKER_H

#include <exception>
#include <pthread.h>
#include <semaphore.h>

// 封装信号量的类
class sem {
    public:
        // 构造函数，创建并初始化信号量
        sem() {
            if (sem_init(&m_sem, 0, 0) != 0) {
                // 构造函数没有返回值，可以通过抛出异常来报告错误
                throw std::exception();
            }
        }
        // 重载构造函数，设定信号量初始数量
        sem(int num) {
            if (sem_init(&m_sem, 0, num) != 0) {
                throw std::exception();
            }
        }
        // 析构函数，销毁信号量
        ~sem() {
            sem_destroy(&m_sem);
        }
        // 等待信号量
        bool wait() {
            return sem_wait(&m_sem) == 0;
        }
        // 增加信号量
        bool post() {
            return sem_post(&m_sem) == 0;
        }

    private:
        sem_t m_sem;
};

// 封装互斥锁的类
class locker {
    public:
        // 构造函数，创建并初始化互斥锁
        locker() {
            if (pthread_mutex_init(&m_mutex, NULL) != 0) {
                throw std::exception();
            }
        }
        // 析构函数，销毁互斥锁
        ~locker() {
            pthread_mutex_destroy(&m_mutex);
        }
        // 获取互斥锁
        bool lock() {
            return pthread_mutex_lock(&m_mutex) == 0;
        }
        // 释放互斥锁
        bool unlock() {
            return pthread_mutex_unlock(&m_mutex) == 0;
        }
        // 返回互斥锁
        pthread_mutex_t *get() {
            return &m_mutex;
        }

    private:
        pthread_mutex_t m_mutex;
};

// 封装条件变量的类
class cond {
    public:
        // 构造函数，创建并初始化条件变量
        cond() {
            if (pthread_cond_init(&m_cond, NULL) != 0) {
                throw std::exception();
            }
        }
        // 销毁条件变量
        ~cond() {
            pthread_cond_destroy(&m_cond);
        }
        // 用于等待目标条件变量。该函数调用时需要传入 mutex参数(加锁的互斥锁)
        // 函数执行时，先把调用线程放入条件变量的请求队列，然后将互斥锁mutex解锁，当函数成功返回为0时，表示重新抢到了互斥锁
        // 互斥锁会再次被锁上，也就是说函数内部会有一次解锁和加锁操作.
        bool wait(pthread_mutex_t* m_mutex) {
            int ret = 0;
        // pthread_mutex_lock(&m_mutex);
            ret = pthread_cond_wait(&m_cond, m_mutex);
        // pthread_mutex_unlock(&m_mutex);
            return ret == 0;
        }
        // bool timewait(pthread_mutex_t *m_mutex, struct timespec t);
        // 唤醒等待条件变量的线程
        bool signal() {
            return pthread_cond_signal(&m_cond) == 0;
        }
        // 以广播的方式唤醒所有等待目标条件变量的线程
        bool broadcast() {
        return pthread_cond_broadcast(&m_cond) == 0;
        }

    private:
        // pthread_mutex_t m_mutex;
        pthread_cond_t m_cond;
};

#endif