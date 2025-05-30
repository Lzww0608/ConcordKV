/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV utility functions - timer.c
 */
/**
 * @file timer.c
 * @brief 高精度定时器实现，使用epoll + 最小堆实现
 */

#include "timer.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <time.h>
#include <stdio.h>
#include <pthread.h>

// 最小堆节点结构体
typedef struct heap_node {
    uint64_t expire_time;         // 过期时间（毫秒时间戳）
    uint64_t period;              // 周期时间（毫秒），对于一次性定时器为0
    concord_timer_id_t timer_id;  // 定时器ID
    concord_timer_callback_t callback;  // 回调函数
    void *arg;                    // 回调函数参数
    int in_calling;               // 是否正在调用回调函数
    int canceled;                 // 是否已取消
} heap_node_t;

// 最小堆结构体
typedef struct {
    heap_node_t **nodes;          // 节点数组
    int capacity;                 // 容量
    int size;                     // 当前大小
} min_heap_t;

// 定时器管理器结构体
struct concord_timer_manager_t {
    min_heap_t heap;              // 最小堆
    int epoll_fd;                 // epoll文件描述符
    int timer_fd;                 // timerfd文件描述符
    pthread_mutex_t mutex;        // 互斥锁
    uint64_t next_timer_id;       // 下一个定时器ID
};

// 初始化最小堆
static int min_heap_init(min_heap_t *heap, int capacity) {
    heap->nodes = (heap_node_t **)calloc(capacity, sizeof(heap_node_t *));
    if (!heap->nodes) {
        return -1;
    }
    heap->capacity = capacity;
    heap->size = 0;
    return 0;
}

// 清理最小堆
static void min_heap_cleanup(min_heap_t *heap) {
    if (heap->nodes) {
        for (int i = 0; i < heap->size; i++) {
            free(heap->nodes[i]);
        }
        free(heap->nodes);
        heap->nodes = NULL;
    }
    heap->capacity = 0;
    heap->size = 0;
}

// 扩展最小堆容量
static int min_heap_reserve(min_heap_t *heap, int new_capacity) {
    if (new_capacity <= heap->capacity) {
        return 0;
    }
    
    heap_node_t **new_nodes = (heap_node_t **)realloc(
        heap->nodes, new_capacity * sizeof(heap_node_t *)
    );
    
    if (!new_nodes) {
        return -1;
    }
    
    heap->nodes = new_nodes;
    heap->capacity = new_capacity;
    return 0;
}

// 向上调整堆
static void min_heap_shift_up(min_heap_t *heap, int index) {
    heap_node_t *node = heap->nodes[index];
    int parent = (index - 1) / 2;
    
    while (index > 0 && heap->nodes[parent]->expire_time > node->expire_time) {
        heap->nodes[index] = heap->nodes[parent];
        index = parent;
        parent = (index - 1) / 2;
    }
    
    heap->nodes[index] = node;
}

// 向下调整堆
static void min_heap_shift_down(min_heap_t *heap, int index) {
    heap_node_t *node = heap->nodes[index];
    int min_child;
    
    while (index * 2 + 1 < heap->size) {
        min_child = index * 2 + 1;
        
        if (min_child + 1 < heap->size && 
            heap->nodes[min_child]->expire_time > heap->nodes[min_child + 1]->expire_time) {
            min_child++;
        }
        
        if (node->expire_time <= heap->nodes[min_child]->expire_time) {
            break;
        }
        
        heap->nodes[index] = heap->nodes[min_child];
        index = min_child;
    }
    
    heap->nodes[index] = node;
}

// 插入节点到最小堆
static int min_heap_push(min_heap_t *heap, heap_node_t *node) {
    if (heap->size >= heap->capacity) {
        int new_capacity = heap->capacity * 2;
        if (new_capacity == 0) {
            new_capacity = 8;  // 初始容量
        }
        
        if (min_heap_reserve(heap, new_capacity) != 0) {
            return -1;
        }
    }
    
    heap->nodes[heap->size] = node;
    min_heap_shift_up(heap, heap->size);
    heap->size++;
    
    return 0;
}

// 从最小堆中弹出顶部节点
static heap_node_t *min_heap_pop(min_heap_t *heap) {
    if (heap->size == 0) {
        return NULL;
    }
    
    heap_node_t *node = heap->nodes[0];
    
    heap->size--;
    
    if (heap->size > 0) {
        heap->nodes[0] = heap->nodes[heap->size];
        min_heap_shift_down(heap, 0);
    }
    
    return node;
}

// 查看最小堆顶部节点
static heap_node_t *min_heap_top(min_heap_t *heap) {
    if (heap->size == 0) {
        return NULL;
    }
    
    return heap->nodes[0];
}

// 按定时器ID查找节点的索引
static int min_heap_find(min_heap_t *heap, concord_timer_id_t timer_id) {
    for (int i = 0; i < heap->size; i++) {
        if (heap->nodes[i]->timer_id == timer_id) {
            return i;
        }
    }
    return -1;
}

// 移除指定索引的节点
static heap_node_t *min_heap_remove(min_heap_t *heap, int index) {
    if (index < 0 || index >= heap->size) {
        return NULL;
    }
    
    heap_node_t *removed = heap->nodes[index];
    heap->size--;
    
    if (index == heap->size) {
        return removed;
    }
    
    heap->nodes[index] = heap->nodes[heap->size];
    
    // 根据移动节点的值确定向上还是向下调整
    if (index > 0 && heap->nodes[index]->expire_time < heap->nodes[(index - 1) / 2]->expire_time) {
        min_heap_shift_up(heap, index);
    } else {
        min_heap_shift_down(heap, index);
    }
    
    return removed;
}

// 重新调整timerfd
static int update_timerfd(concord_timer_manager_t *manager) {
    heap_node_t *top = min_heap_top(&manager->heap);
    if (!top) {
        // 没有定时器，停止timerfd
        struct itimerspec its = {0};
        return timerfd_settime(manager->timer_fd, 0, &its, NULL);
    }
    
    uint64_t now = concord_time_now_ms();
    uint64_t expire_ms = (top->expire_time > now) ? (top->expire_time - now) : 0;
    
    struct itimerspec its = {0};
    its.it_value.tv_sec = expire_ms / 1000;
    its.it_value.tv_nsec = (expire_ms % 1000) * 1000000;
    
    return timerfd_settime(manager->timer_fd, 0, &its, NULL);
}

// 获取当前高精度时间戳(纳秒)
uint64_t concord_time_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

// 获取当前高精度时间戳(微秒)
uint64_t concord_time_now_us(void) {
    return concord_time_now_ns() / 1000ULL;
}

// 获取当前高精度时间戳(毫秒)
uint64_t concord_time_now_ms(void) {
    return concord_time_now_ns() / 1000000ULL;
}

// 休眠指定时间(毫秒)
void concord_time_sleep_ms(uint64_t ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

// 休眠指定时间(微秒)
void concord_time_sleep_us(uint64_t us) {
    struct timespec ts;
    ts.tv_sec = us / 1000000;
    ts.tv_nsec = (us % 1000000) * 1000;
    nanosleep(&ts, NULL);
}

// 格式化时间戳为字符串
char *concord_time_format(uint64_t timestamp_ms, const char *format, char *buf, size_t size) {
    time_t seconds = timestamp_ms / 1000;
    struct tm tm_info;
    
    if (!localtime_r(&seconds, &tm_info)) {
        return NULL;
    }
    
    if (strftime(buf, size, format, &tm_info) == 0) {
        return NULL;
    }
    
    return buf;
}

// 解析时间字符串为时间戳
uint64_t concord_time_parse(const char *time_str, const char *format) {
    struct tm tm_info = {0};
    
    if (!strptime(time_str, format, &tm_info)) {
        return 0;
    }
    
    time_t seconds = mktime(&tm_info);
    if (seconds == -1) {
        return 0;
    }
    
    return (uint64_t)seconds * 1000;
}

// 计算两个时间点之间的差值(毫秒)
uint64_t concord_time_diff_ms(uint64_t start, uint64_t end) {
    return (end >= start) ? (end - start) : 0;
}

// 创建定时器管理器
concord_timer_manager_t *concord_timer_manager_create(void) {
    concord_timer_manager_t *manager = (concord_timer_manager_t *)calloc(1, sizeof(concord_timer_manager_t));
    if (!manager) {
        return NULL;
    }
    
    // 初始化最小堆
    if (min_heap_init(&manager->heap, 16) != 0) {
        free(manager);
        return NULL;
    }
    
    // 创建epoll实例
    manager->epoll_fd = epoll_create1(0);
    if (manager->epoll_fd == -1) {
        min_heap_cleanup(&manager->heap);
        free(manager);
        return NULL;
    }
    
    // 创建timerfd
    manager->timer_fd = timerfd_create(CLOCK_REALTIME, 0);
    if (manager->timer_fd == -1) {
        close(manager->epoll_fd);
        min_heap_cleanup(&manager->heap);
        free(manager);
        return NULL;
    }
    
    // 将timerfd添加到epoll中
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = manager->timer_fd;
    if (epoll_ctl(manager->epoll_fd, EPOLL_CTL_ADD, manager->timer_fd, &ev) == -1) {
        close(manager->timer_fd);
        close(manager->epoll_fd);
        min_heap_cleanup(&manager->heap);
        free(manager);
        return NULL;
    }
    
    // 初始化互斥锁
    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {
        close(manager->timer_fd);
        close(manager->epoll_fd);
        min_heap_cleanup(&manager->heap);
        free(manager);
        return NULL;
    }
    
    manager->next_timer_id = 1;  // 从1开始分配定时器ID
    
    return manager;
}

// 销毁定时器管理器
void concord_timer_manager_destroy(concord_timer_manager_t *manager) {
    if (!manager) {
        return;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    close(manager->timer_fd);
    close(manager->epoll_fd);
    min_heap_cleanup(&manager->heap);
    
    pthread_mutex_unlock(&manager->mutex);
    pthread_mutex_destroy(&manager->mutex);
    
    free(manager);
}

// 添加定时器
concord_timer_id_t concord_timer_add(
    concord_timer_manager_t *manager,
    concord_timer_type_t type,
    uint64_t timeout_ms,
    concord_timer_callback_t callback,
    void *arg
) {
    if (!manager || !callback || timeout_ms == 0) {
        return 0;
    }
    
    heap_node_t *node = (heap_node_t *)calloc(1, sizeof(heap_node_t));
    if (!node) {
        return 0;
    }
    
    uint64_t now = concord_time_now_ms();
    
    node->expire_time = now + timeout_ms;
    node->period = (type == CONCORD_TIMER_PERIODIC) ? timeout_ms : 0;
    node->callback = callback;
    node->arg = arg;
    node->in_calling = 0;
    node->canceled = 0;
    
    pthread_mutex_lock(&manager->mutex);
    
    node->timer_id = manager->next_timer_id++;
    if (manager->next_timer_id == 0) {  // 溢出检查
        manager->next_timer_id = 1;
    }
    
    if (min_heap_push(&manager->heap, node) != 0) {
        pthread_mutex_unlock(&manager->mutex);
        free(node);
        return 0;
    }
    
    // 更新timerfd
    update_timerfd(manager);
    
    pthread_mutex_unlock(&manager->mutex);
    
    return node->timer_id;
}

// 取消定时器
int concord_timer_cancel(concord_timer_manager_t *manager, concord_timer_id_t timer_id) {
    if (!manager || timer_id == 0) {
        return -1;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    int index = min_heap_find(&manager->heap, timer_id);
    if (index == -1) {
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }
    
    heap_node_t *node = manager->heap.nodes[index];
    
    if (node->in_calling) {
        // 如果定时器正在被调用，标记为已取消
        node->canceled = 1;
    } else {
        // 否则，直接从堆中移除
        node = min_heap_remove(&manager->heap, index);
        free(node);
        
        // 更新timerfd
        update_timerfd(manager);
    }
    
    pthread_mutex_unlock(&manager->mutex);
    
    return 0;
}

// 修改定时器超时时间
int concord_timer_update(
    concord_timer_manager_t *manager,
    concord_timer_id_t timer_id, 
    uint64_t new_timeout_ms
) {
    if (!manager || timer_id == 0 || new_timeout_ms == 0) {
        return -1;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    int index = min_heap_find(&manager->heap, timer_id);
    if (index == -1) {
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }
    
    heap_node_t *node = min_heap_remove(&manager->heap, index);
    
    uint64_t now = concord_time_now_ms();
    node->expire_time = now + new_timeout_ms;
    
    if (node->period > 0) {
        node->period = new_timeout_ms;
    }
    
    if (min_heap_push(&manager->heap, node) != 0) {
        pthread_mutex_unlock(&manager->mutex);
        free(node);
        return -1;
    }
    
    // 更新timerfd
    update_timerfd(manager);
    
    pthread_mutex_unlock(&manager->mutex);
    
    return 0;
}

// 定时器管理器事件循环，处理所有到期的定时器
int concord_timer_process(concord_timer_manager_t *manager, int timeout_ms) {
    if (!manager) {
        return -1;
    }
    
    struct epoll_event events[1];
    int nfds = epoll_wait(manager->epoll_fd, events, 1, timeout_ms);
    
    if (nfds <= 0) {
        return 0;  // 超时或出错
    }
    
    // 读取timerfd数据（必须读取，否则epoll会一直触发）
    uint64_t exp;
    if (read(manager->timer_fd, &exp, sizeof(uint64_t)) != sizeof(uint64_t)) {
        return 0;
    }
    
    int processed = 0;
    uint64_t now = concord_time_now_ms();
    
    pthread_mutex_lock(&manager->mutex);
    
    // 处理所有到期的定时器
    while (1) {
        heap_node_t *top = min_heap_top(&manager->heap);
        if (!top || top->expire_time > now) {
            break;  // 没有更多到期的定时器
        }
        
        // 从堆中移除定时器
        top = min_heap_pop(&manager->heap);
        
        if (!top->canceled) {
            // 标记为正在调用
            top->in_calling = 1;
            
            // 解锁互斥锁，允许在回调函数中操作定时器管理器
            pthread_mutex_unlock(&manager->mutex);
            
            // 调用回调函数
            top->callback(top->arg, now);
            
            // 重新加锁
            pthread_mutex_lock(&manager->mutex);
            
            processed++;
        }
        
        // 处理周期性定时器
        if (!top->canceled && top->period > 0) {
            // 计算下一次触发时间
            top->expire_time = now + top->period;
            top->in_calling = 0;
            
            // 重新插入到堆中
            if (min_heap_push(&manager->heap, top) != 0) {
                free(top);
            }
        } else {
            // 一次性定时器或已取消的定时器，直接释放
            free(top);
        }
    }
    
    // 更新timerfd
    update_timerfd(manager);
    
    pthread_mutex_unlock(&manager->mutex);
    
    return processed;
} 