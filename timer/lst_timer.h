#ifndef LST_TIMER
#define LST_TIMER

#include<time.h>
#include"../log/log.h"

#define BUFFER_SIZE 64

class util_timer;

// 用户数据结构：客户端socket地址、socket文件描述符和定时器
struct client_data {
    sockaddr_in address;
    int sockfd;
    util_timer* timer;
};

// 定时器类
class util_timer {
    public:
        util_timer(): prev(nullptr), next(nullptr) {}
        
    public:
        // 任务的超时时间，使用绝对时间
        time_t expire;
        // 任务回调函数
        void(*cb_func)(client_data*);
        // 回调函数处理的客户数据，由定时器的执行者传递给回调函数
        client_data* user_data;
        // 指向前一个定时器
        util_timer* prev;
        // 指向后一个定时器
        util_timer* next;
};

// 定时器链表类，带头尾节点的升序双向链表
class sort_timer_lst {
    public:
        sort_timer_lst(): head(nullptr), tail(nullptr) {};

        // 链表被销毁时，删除所有定时器
        ~sort_timer_lst() {
            util_timer* tmp = head;
            while (tmp) {
                head = tmp->next;
                delete tmp;
                tmp = head;
            }
        }

        // 将目标定时器timer添加到链表中
        void add_timer(util_timer* timer) {
            if (timer == nullptr) {
                return;
            }
            if (!head) {
                head = tail = timer;
                return;
            }
            // 如果目标定时器的超时时间小于所有定时器的超时时间，就把该定时器插入链表头部，并作为新的头结点
            // 否则就需要调用重载函数add_timer(timer, head)插入到合适的位置
            if (timer->expire < head->expire) {
                timer->next = head;
                head->prev = timer;
                head = timer;
                return;
            }
            add_timer(timer, head);
        }

        // 当某个定时任务发生变化时，调整对应的定时器在链表中的位置
        // 这个函数只考虑定时器被延时的情况，即将该定时器往链表的后部移动
        void adjust_timer(util_timer* timer) {
            if (timer == nullptr) {
                return;
            }
            util_timer* tmp = timer->next;
            // 如果该定时器已经在链表尾部，或者新的超时值仍比下一个小，则不用调整
            if (tmp == nullptr || (timer->expire < tmp->expire)) {
                return;
            }
            // 如果该定时器是头节点，则将其取出并重新插入链表
            if (timer == head) {
                head = head->next;
                head->prev = nullptr;
                timer->next = nullptr;
                add_timer(timer, head);
            } else {
                // 否则就将其从链表取出并插入到之后的部分中
                timer->prev->next = timer->next;
                timer->next->prev = timer->prev;
                add_timer(timer, timer->next);
            }
        }

        // 从链表中删除目标定时器
        void del_timer(util_timer* timer) {
            if (!timer) {
                return;
            }
            // 以下表示链表中只有目标定时器
            if ((timer == head) && (timer == tail)) {
                delete timer;
                head = nullptr;
                tail = nullptr;
                return;
            }
            // 如果目标定时器是头节点
            if (timer == head) {
                head = head->next;
                head->prev = nullptr;
                delete timer;
                return;
            }
            // 如果目标定时器是尾节点
            if (timer == tail) {
                tail = tail->prev;
                tail->next = nullptr;
                delete timer;
                return;
            }
            // 目标定时器位于中间位置
            timer->prev->next = timer->next;
            timer->next->prev = timer->prev;
            delete timer;
        }

        // SIGALRM信号每次被触发就在其信号处理函数中执行一次tick函数，以处理到期任务
        // 如果使用统一事件源，处理函数是主函数
        bool tick() {
            if (head == nullptr) {
                return false;
            }
            // 获得当前系统时间
            time_t cur = time(NULL);
            util_timer* tmp = head;
            // 从头结点开始依次处理每个定时器，直到遇到一个尚未到期的定时器
            while (tmp) {
                // 定时器也使用绝对时间，可以直接比较
                if (cur < tmp->expire) {
                    break;
                }
                // 调用超时定时器的回调函数
                tmp->cb_func(tmp->user_data);
                // 执行完定时任务后，将它从链表中删除，并重置链表头结点
                head = tmp->next;
                if (head) {
                    head->prev = nullptr;
                }
                delete tmp;
                tmp = head;
            }
            return true;
        }

        private:
            //重载函数，被公有的add_timer和adjust_timer调用
            void add_timer(util_timer* timer, util_timer* lst_head) {
                util_timer* prev = lst_head;
                util_timer* tmp = prev->next;
                // 遍历lst_head之后的节点，直到找到一个超时时间大于目标定时器的节点，并将目标插入到该节点之前
                while (tmp) {
                    if (timer->expire < tmp->expire) {
                        prev->next = timer;
                        timer->next = tmp;
                        tmp->prev = timer;
                        timer->prev = prev;
                        break;
                    }
                    prev = tmp;
                    tmp = tmp->next;
                }
                // 遍历之后仍未找到插入位置，就直接插入尾部，并设置为新的尾节点
                if (tmp == nullptr) {
                    prev->next = timer;
                    timer->prev = prev;
                    timer->next = nullptr;
                    tail = timer;
                }
            }
        
        private:
            util_timer* head;
            util_timer* tail;
};

#endif
