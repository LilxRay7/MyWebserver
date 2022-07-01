#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<list>
#include<cstdio>
#include<exception>
#include<pthread.h>

#include"../lock/locker.h"

// 线程池类，引入模板方便代码复用
// 使用一个工作队列完全解除了主线程和工作线程的耦合关系
// 主线程往工作队列中插入任务，工作线程通过竞争来取得任务并执行它
template<typename T>
class threadpool {
    public:
        // thread_number代表线程池中线程的数量，max_requests代表请求队列中最多允许的等待处理的请求的数量
        threadpool(int thread_number = 8, int max_requests = 10000);
        ~threadpool();

        // 往请求队列中添加任务
        bool append(T* request);

    private:
        // 工作线程运行的函数，它从工作队列中取出任务并执行
        static void* worker(void *arg);
        void run();

        // 线程池中的线程数
        int m_thread_number;
        // 请求队列中允许的最大请求数
        int m_max_requests;
        // 描述线程池的数组，其大小为m_thread_number;
        pthread_t* m_threads;
        // 请求队列
        std::list<T*> m_workqueue;
        // 请求队列的互斥锁
        locker m_queuelocker;
        // 是否有任务需要处理的信号量
        sem m_queuestat;
        // 是否结束线程
        bool m_stop;
};

template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests):
    m_thread_number(thread_number), m_max_requests(max_requests), m_stop(false), m_threads(NULL) {
    if ((thread_number <= 0) || (max_requests <= 0)) {
        throw std::exception();
    }

    m_threads = new pthread_t[m_thread_number];
    if (!m_threads) {
        throw std::exception();
    }

    // 创建thread_number个线程并设置为脱离线程
    for (int i = 0; i < thread_number; i++) {
        printf("creating the %dth thread\n", i);
        if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete []m_threads;
            throw std::exception();
        }
        if (pthread_detach(m_threads[i])) {
            delete []m_threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool() {
    delete []m_threads;
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T* request) {
    // 操作工作队列时一定要加锁，因为它被所有线程共享
    m_queuelocker.lock();
    if (m_workqueue.size() > m_max_requests) {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    // 操作完解锁
    m_queuelocker.unlock();
    // 信号量加一，代表工作队列有待处理的任务
    m_queuestat.post();
    return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg) {
    threadpool* pool = (threadpool*)arg;
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run() {
    while (!m_stop) {
        // 工作线程通过竞争来取得任务并执行
        // 信号量减一
        m_queuestat.wait();
        // 操作工作队列时一定要加锁，因为它被所有线程共享
        m_queuelocker.lock();
        if (m_workqueue.empty()) {
            m_queuelocker.unlock();
            continue;
        }

        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request) {
            continue;
        }
        // 处理客户请求
        request->process();
    }
}

#endif