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
            m_mutex.lock();
            if (m_array != nullptr) {
                delete[] m_array;
            }
            m_mutex.unlock();
        }

        void clear() {
            m_mutex.lock();
            m_size = 0;
            m_front = -1;
            m_back = -1;
            m_mutex.unlock();
        }

        // 判断队列是否满
        bool full() {
            m_mutex.lock();
            if (m_size >= m_max_size) {
                m_mutex.unlock();
                return true;
            }
            m_mutex.unlock();
            return false;
        }

        // 判断队列是否为空
        bool empty() {
            m_mutex.lock();
            if (m_size == 0) {
                m_mutex.unlock();
                return true;
            }
            m_mutex.unlock();
            return false;
        }

        // 返回队首元素
        bool front(T& value) {
            m_mutex.lock();
            if (m_size == 0) {
                m_mutex.unlock();
                return false;  
            }
            value = m_array[m_front];
            m_mutex.unlock();
            return true;
        }

        // 返回队尾元素
        bool back(T& value) {
            m_mutex.lock();
            if (m_size == 0) {
                m_mutex.unlock();
                return false;  
            }
            value = m_array[m_back];
            m_mutex.unlock();
            return true;
        }

        int size() {
            int ret = 0;
            m_mutex.lock();
            ret = m_size;
            m_mutex.unlock();
            return ret;
        }

        int max_size() {
            int ret = 0;
            m_mutex.lock();
            ret = m_max_size;
            m_mutex.unlock();
            return ret;
        }

        // 往队列中添加元素，当有元素push进队列，相当于生产者生产了一个元素
        bool push(const T& item) {
            m_mutex.lock();
            if (m_size >= m_max_size) {
                m_cond.broadcast();
                m_mutex.unlock();
                return false;
            }
            // 循环数组
            m_back = (m_back + 1) % m_max_size;
            m_array[m_back] = item;
            m_size++;
            m_cond.broadcast();
            m_mutex.unlock();
            return true;
        }

        // pop时，如果当前队列没有元素，将会等待条件变量
        bool pop(T& item) {
            m_mutex.lock();
            while (m_size <= 0) {
                if (!m_cond.wait(m_mutex.get())) {
                    m_mutex.unlock();
                    return false;
                }
            }
            m_front = (m_front + 1) % m_max_size;
            item = m_array[m_front];
            m_size--;
            m_mutex.unlock();
            return true;
        }

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