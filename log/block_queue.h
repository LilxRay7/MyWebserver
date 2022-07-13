#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include<iostream>
#include<stdlib.h>
#include<pthread.h>
#include<sys/time.h>

#include"../lock/locker.h"

using namespace std;

template<class T>
class block_queue {
    public:
        // 构造函数
        block_queue(int max_size = 1000) {
            if (max_size <= 0) {
                exit(-1);
            }

            m_max_size = max_size;
            m_array = new T[max_size];
            m_size = 0;
            m_front = -1;
            m_back = -1;
        }

        ~block_queue() {
            // 操作阻塞队列前上锁
            locker_RAII lock_RAII(m_mutex);
            if (m_array != nullptr) {
                delete[] m_array;
            }
        }

        void clear() {
            // 操作阻塞队列前上锁
            locker_RAII lock_RAII(m_mutex);
            m_size = 0;
            m_front = -1;
            m_back = -1;
        }

        // 判断队列是否满
        bool full() {
            // 操作阻塞队列前上锁
            locker_RAII lock_RAII(m_mutex);
            if (m_size >= m_max_size) {
                return true;
            }
            return false;
        }

        // 判断队列是否为空
        bool empty() {
            // 操作阻塞队列前上锁
            locker_RAII lock_RAII(m_mutex);
            if (m_size == 0) {
                return true;
            }
            return false;
        }

        // 返回队首元素
        bool front(T& value) {
            // 操作阻塞队列前上锁
            locker_RAII lock_RAII(m_mutex);
            if (m_size == 0) {
                return false;  
            }
            value = m_array[m_front];
            return true;
        }

        // 返回队尾元素
        bool back(T& value) {
            // 操作阻塞队列前上锁
            locker_RAII lock_RAII(m_mutex);
            if (m_size == 0) {
                return false;  
            }
            value = m_array[m_back];
            return true;
        }

        int size() {
            int ret = 0;
            // 操作阻塞队列前上锁
            locker_RAII lock_RAII(m_mutex);
            ret = m_size;
            return ret;
        }

        int max_size() {
            int ret = 0;
            // 操作阻塞队列前上锁
            locker_RAII lock_RAII(m_mutex);
            ret = m_max_size;
            return ret;
        }

        // 生产者往队列中添加元素，当有元素push进队列，相当于生产者生产了一个元素
        bool push(const T& item) {
            // 操作阻塞队列前上锁
            locker_RAII lock_RAII(m_mutex);
            if (m_size >= m_max_size) {
                m_cond.broadcast();
                return false;
            }
            // 循环数组
            m_back = (m_back + 1) % m_max_size;
            m_array[m_back] = item;
            m_size++;
            m_cond.broadcast();
            return true;
        }

        // pop时，如果当前队列没有元素，消费者将会等待条件变量
        bool pop(T& item) {
            // 操作阻塞队列前上锁
            locker_RAII lock_RAII(m_mutex);
            while (m_size <= 0) {
                if (!m_cond.wait(m_mutex.get())) {
                    return false;
                }
            }
            // 循环队列，首部向后一位，取出后size减一
            m_front = (m_front + 1) % m_max_size;
            item = m_array[m_front];
            m_size--;
            return true;
        }

        // 增加超时处理
        // bool poop(T& item, int ms_timeout);

    private:
        // 互斥锁 "locker.h"中locker类
        locker m_mutex;
        // 条件变量 "locker.h"中cond类
        cond m_cond;
        // 阻塞队列
        T* m_array;
        // 队列大小
        int m_size;
        // 队列大小上限
        int m_max_size;
        // 队首
        int m_front;
        // 队尾
        int m_back;
};

#endif