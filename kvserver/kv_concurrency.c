#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "kv_concurrency.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

// 全局死锁检测器实例
kv_deadlock_detector_t g_deadlock_detector = {0};

// 哈希函数，用于将键映射到锁分段
static int hash_key(const char *key, int size) {
    unsigned long hash = 0;
    int c;
    
    while ((c = *key++)) {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }
    
    return hash % size;
}

// 获取当前时间
static void get_current_time(struct timespec *ts) {
    clock_gettime(CLOCK_REALTIME, ts);
}

// 计算超时时间
static void calculate_timeout(struct timespec *timeout, int timeout_ms) {
    get_current_time(timeout);
    timeout->tv_sec += timeout_ms / 1000;
    timeout->tv_nsec += (timeout_ms % 1000) * 1000000;
    if (timeout->tv_nsec >= 1000000000) {
        timeout->tv_sec++;
        timeout->tv_nsec -= 1000000000;
    }
}

// 检查是否超时
static bool is_timeout(const struct timespec *timeout) {
    struct timespec now;
    get_current_time(&now);
    
    if (now.tv_sec > timeout->tv_sec) {
        return true;
    } else if (now.tv_sec == timeout->tv_sec && now.tv_nsec > timeout->tv_nsec) {
        return true;
    }
    return false;
}

// 比较两个键的大小（用于锁排序）
static int compare_keys(const void *a, const void *b) {
    const char **key_a = (const char **)a;
    const char **key_b = (const char **)b;
    return strcmp(*key_a, *key_b);
}

// ==================== 死锁检测器实现 ====================

// 初始化死锁检测器
int kv_deadlock_detector_init(int max_wait_time_ms) {
    if (g_deadlock_detector.enabled) {
        return KV_LOCK_SUCCESS; // 已经初始化
    }
    
    memset(&g_deadlock_detector, 0, sizeof(kv_deadlock_detector_t));
    
    if (pthread_mutex_init(&g_deadlock_detector.graph_mutex, NULL) != 0) {
        return KV_LOCK_ERROR;
    }
    
    g_deadlock_detector.enabled = true;
    g_deadlock_detector.max_wait_time_ms = max_wait_time_ms;
    g_deadlock_detector.lock_graph = NULL;
    
    return KV_LOCK_SUCCESS;
}

// 销毁死锁检测器
void kv_deadlock_detector_destroy(void) {
    if (!g_deadlock_detector.enabled) {
        return;
    }
    
    pthread_mutex_lock(&g_deadlock_detector.graph_mutex);
    
    // 清理锁图
    kv_thread_lock_info_t *current = g_deadlock_detector.lock_graph;
    while (current) {
        kv_thread_lock_info_t *next = current->next;
        free(current);
        current = next;
    }
    
    g_deadlock_detector.lock_graph = NULL;
    g_deadlock_detector.enabled = false;
    
    pthread_mutex_unlock(&g_deadlock_detector.graph_mutex);
    pthread_mutex_destroy(&g_deadlock_detector.graph_mutex);
}

// 添加锁请求到死锁检测图
int kv_deadlock_detector_add_lock_request(void *lock_ptr, kv_lock_type_t type) {
    if (!g_deadlock_detector.enabled) {
        return KV_LOCK_SUCCESS;
    }
    
    pthread_mutex_lock(&g_deadlock_detector.graph_mutex);
    
    // 创建新的锁信息节点
    kv_thread_lock_info_t *info = malloc(sizeof(kv_thread_lock_info_t));
    if (!info) {
        pthread_mutex_unlock(&g_deadlock_detector.graph_mutex);
        return KV_LOCK_ERROR;
    }
    
    info->thread_id = pthread_self();
    info->lock_ptr = lock_ptr;
    info->lock_type = type;
    info->status = LOCK_STATUS_WAITING;
    get_current_time(&info->acquire_time);
    calculate_timeout(&info->timeout, g_deadlock_detector.max_wait_time_ms);
    
    // 添加到链表头部
    info->next = g_deadlock_detector.lock_graph;
    g_deadlock_detector.lock_graph = info;
    
    pthread_mutex_unlock(&g_deadlock_detector.graph_mutex);
    return KV_LOCK_SUCCESS;
}

// 从死锁检测图中移除锁请求
int kv_deadlock_detector_remove_lock_request(void *lock_ptr) {
    if (!g_deadlock_detector.enabled) {
        return KV_LOCK_SUCCESS;
    }
    
    pthread_mutex_lock(&g_deadlock_detector.graph_mutex);
    
    pthread_t current_thread = pthread_self();
    kv_thread_lock_info_t **current = &g_deadlock_detector.lock_graph;
    
    while (*current) {
        if ((*current)->thread_id == current_thread && (*current)->lock_ptr == lock_ptr) {
            kv_thread_lock_info_t *to_remove = *current;
            *current = (*current)->next;
            free(to_remove);
            pthread_mutex_unlock(&g_deadlock_detector.graph_mutex);
            return KV_LOCK_SUCCESS;
        }
        current = &((*current)->next);
    }
    
    pthread_mutex_unlock(&g_deadlock_detector.graph_mutex);
    return KV_LOCK_ERROR;
}

// 检查死锁（简化版本 - 检查循环等待）
int kv_deadlock_detector_check_deadlock(void) {
    if (!g_deadlock_detector.enabled) {
        return KV_LOCK_SUCCESS;
    }
    
    pthread_mutex_lock(&g_deadlock_detector.graph_mutex);
    
    // 简化的死锁检测：检查是否有线程等待时间过长
    kv_thread_lock_info_t *current = g_deadlock_detector.lock_graph;
    while (current) {
        if (current->status == LOCK_STATUS_WAITING && is_timeout(&current->timeout)) {
            pthread_mutex_unlock(&g_deadlock_detector.graph_mutex);
            return KV_LOCK_DEADLOCK;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&g_deadlock_detector.graph_mutex);
    return KV_LOCK_SUCCESS;
}

// 检查指定锁是否超时
bool kv_deadlock_detector_is_timeout(void *lock_ptr) {
    if (!g_deadlock_detector.enabled) {
        return false;
    }
    
    pthread_mutex_lock(&g_deadlock_detector.graph_mutex);
    
    pthread_t current_thread = pthread_self();
    kv_thread_lock_info_t *current = g_deadlock_detector.lock_graph;
    
    while (current) {
        if (current->thread_id == current_thread && current->lock_ptr == lock_ptr) {
            bool timeout = is_timeout(&current->timeout);
            pthread_mutex_unlock(&g_deadlock_detector.graph_mutex);
            return timeout;
        }
        current = current->next;
    }
    
    pthread_mutex_unlock(&g_deadlock_detector.graph_mutex);
    return false;
}

// ==================== 读写锁实现 ====================

// 初始化读写锁
int kv_rwlock_init(kv_rwlock_t *rwlock) {
    if (!rwlock) return KV_LOCK_INVALID_PARAM;
    
    int ret = pthread_rwlock_init(&rwlock->lock, NULL);
    if (ret == 0) {
        rwlock->initialized = 1;
        rwlock->timeout_enabled = false;
        memset(&rwlock->timeout, 0, sizeof(struct timespec));
    }
    return ret == 0 ? KV_LOCK_SUCCESS : KV_LOCK_ERROR;
}

// 初始化带超时的读写锁
int kv_rwlock_init_with_timeout(kv_rwlock_t *rwlock, int timeout_ms) {
    if (!rwlock || timeout_ms < 0) return KV_LOCK_INVALID_PARAM;
    
    int ret = kv_rwlock_init(rwlock);
    if (ret == KV_LOCK_SUCCESS) {
        rwlock->timeout_enabled = true;
        calculate_timeout(&rwlock->timeout, timeout_ms);
    }
    return ret;
}

// 销毁读写锁
void kv_rwlock_destroy(kv_rwlock_t *rwlock) {
    if (!rwlock || !rwlock->initialized) return;
    
    pthread_rwlock_destroy(&rwlock->lock);
    rwlock->initialized = 0;
    rwlock->timeout_enabled = false;
}

// 获取读锁（共享锁）
int kv_read_lock(kv_rwlock_t *rwlock) {
    if (!rwlock || !rwlock->initialized) return KV_LOCK_INVALID_PARAM;
    
    // 添加到死锁检测图
    kv_deadlock_detector_add_lock_request(rwlock, LOCK_TYPE_READ);
    
    int ret = pthread_rwlock_rdlock(&rwlock->lock);
    
    // 从死锁检测图中移除
    kv_deadlock_detector_remove_lock_request(rwlock);
    
    return ret == 0 ? KV_LOCK_SUCCESS : KV_LOCK_ERROR;
}

// 获取读锁（带超时）
int kv_read_lock_timeout(kv_rwlock_t *rwlock, int timeout_ms) {
    if (!rwlock || !rwlock->initialized || timeout_ms < 0) return KV_LOCK_INVALID_PARAM;
    
    struct timespec timeout;
    calculate_timeout(&timeout, timeout_ms);
    
    // 添加到死锁检测图
    kv_deadlock_detector_add_lock_request(rwlock, LOCK_TYPE_READ);
    
    int ret = pthread_rwlock_timedrdlock(&rwlock->lock, &timeout);
    
    // 从死锁检测图中移除
    kv_deadlock_detector_remove_lock_request(rwlock);
    
    if (ret == 0) {
        return KV_LOCK_SUCCESS;
    } else if (ret == ETIMEDOUT) {
        return KV_LOCK_TIMEOUT;
    } else {
        return KV_LOCK_ERROR;
    }
}

// 释放读锁
int kv_read_unlock(kv_rwlock_t *rwlock) {
    if (!rwlock || !rwlock->initialized) return KV_LOCK_INVALID_PARAM;
    
    int ret = pthread_rwlock_unlock(&rwlock->lock);
    return ret == 0 ? KV_LOCK_SUCCESS : KV_LOCK_ERROR;
}

// 获取写锁（排他锁）
int kv_write_lock(kv_rwlock_t *rwlock) {
    if (!rwlock || !rwlock->initialized) return KV_LOCK_INVALID_PARAM;
    
    // 添加到死锁检测图
    kv_deadlock_detector_add_lock_request(rwlock, LOCK_TYPE_WRITE);
    
    int ret = pthread_rwlock_wrlock(&rwlock->lock);
    
    // 从死锁检测图中移除
    kv_deadlock_detector_remove_lock_request(rwlock);
    
    return ret == 0 ? KV_LOCK_SUCCESS : KV_LOCK_ERROR;
}

// 获取写锁（带超时）
int kv_write_lock_timeout(kv_rwlock_t *rwlock, int timeout_ms) {
    if (!rwlock || !rwlock->initialized || timeout_ms < 0) return KV_LOCK_INVALID_PARAM;
    
    struct timespec timeout;
    calculate_timeout(&timeout, timeout_ms);
    
    // 添加到死锁检测图
    kv_deadlock_detector_add_lock_request(rwlock, LOCK_TYPE_WRITE);
    
    int ret = pthread_rwlock_timedwrlock(&rwlock->lock, &timeout);
    
    // 从死锁检测图中移除
    kv_deadlock_detector_remove_lock_request(rwlock);
    
    if (ret == 0) {
        return KV_LOCK_SUCCESS;
    } else if (ret == ETIMEDOUT) {
        return KV_LOCK_TIMEOUT;
    } else {
        return KV_LOCK_ERROR;
    }
}

// 释放写锁
int kv_write_unlock(kv_rwlock_t *rwlock) {
    if (!rwlock || !rwlock->initialized) return KV_LOCK_INVALID_PARAM;
    
    int ret = pthread_rwlock_unlock(&rwlock->lock);
    return ret == 0 ? KV_LOCK_SUCCESS : KV_LOCK_ERROR;
}

// 尝试获取读锁
int kv_try_read_lock(kv_rwlock_t *rwlock) {
    if (!rwlock || !rwlock->initialized) return KV_LOCK_INVALID_PARAM;
    
    int ret = pthread_rwlock_tryrdlock(&rwlock->lock);
    if (ret == 0) {
        return KV_LOCK_SUCCESS;
    } else if (ret == EBUSY) {
        return KV_LOCK_TIMEOUT;
    } else {
        return KV_LOCK_ERROR;
    }
}

// 尝试获取写锁
int kv_try_write_lock(kv_rwlock_t *rwlock) {
    if (!rwlock || !rwlock->initialized) return KV_LOCK_INVALID_PARAM;
    
    int ret = pthread_rwlock_trywrlock(&rwlock->lock);
    if (ret == 0) {
        return KV_LOCK_SUCCESS;
    } else if (ret == EBUSY) {
        return KV_LOCK_TIMEOUT;
    } else {
        return KV_LOCK_ERROR;
    }
}

// ==================== 自旋锁实现 ====================

// 初始化自旋锁
int kv_spinlock_init(kv_spinlock_t *spinlock) {
    if (!spinlock) return KV_LOCK_INVALID_PARAM;
    
    int ret = pthread_spin_init(&spinlock->lock, PTHREAD_PROCESS_PRIVATE);
    if (ret == 0) {
        spinlock->initialized = 1;
    }
    return ret == 0 ? KV_LOCK_SUCCESS : KV_LOCK_ERROR;
}

// 销毁自旋锁
void kv_spinlock_destroy(kv_spinlock_t *spinlock) {
    if (!spinlock || !spinlock->initialized) return;
    
    pthread_spin_destroy(&spinlock->lock);
    spinlock->initialized = 0;
}

// 获取自旋锁
int kv_spin_lock(kv_spinlock_t *spinlock) {
    if (!spinlock || !spinlock->initialized) return KV_LOCK_INVALID_PARAM;
    
    int ret = pthread_spin_lock(&spinlock->lock);
    return ret == 0 ? KV_LOCK_SUCCESS : KV_LOCK_ERROR;
}

// 释放自旋锁
int kv_spin_unlock(kv_spinlock_t *spinlock) {
    if (!spinlock || !spinlock->initialized) return KV_LOCK_INVALID_PARAM;
    
    int ret = pthread_spin_unlock(&spinlock->lock);
    return ret == 0 ? KV_LOCK_SUCCESS : KV_LOCK_ERROR;
}

// 尝试获取自旋锁
int kv_try_spin_lock(kv_spinlock_t *spinlock) {
    if (!spinlock || !spinlock->initialized) return KV_LOCK_INVALID_PARAM;
    
    int ret = pthread_spin_trylock(&spinlock->lock);
    if (ret == 0) {
        return KV_LOCK_SUCCESS;
    } else if (ret == EBUSY) {
        return KV_LOCK_TIMEOUT;
    } else {
        return KV_LOCK_ERROR;
    }
}

// ==================== 分段锁实现 ====================

// 初始化分段锁
int kv_segment_locks_init(kv_segment_locks_t *seglocks, int count) {
    return kv_segment_locks_init_with_timeout(seglocks, count, 5000); // 默认5秒超时
}

// 初始化带超时的分段锁
int kv_segment_locks_init_with_timeout(kv_segment_locks_t *seglocks, int count, int timeout_ms) {
    if (!seglocks || count <= 0 || timeout_ms < 0) return KV_LOCK_INVALID_PARAM;
    
    seglocks->locks = (kv_rwlock_t *)calloc(count, sizeof(kv_rwlock_t));
    if (!seglocks->locks) return KV_LOCK_ERROR;
    
    seglocks->lock_count = count;
    seglocks->default_timeout_ms = timeout_ms;
    
    int i;
    for (i = 0; i < count; i++) {
        if (kv_rwlock_init_with_timeout(&seglocks->locks[i], timeout_ms) != KV_LOCK_SUCCESS) {
            // 初始化失败，释放已初始化的锁
            while (--i >= 0) {
                kv_rwlock_destroy(&seglocks->locks[i]);
            }
            free(seglocks->locks);
            seglocks->locks = NULL;
            seglocks->lock_count = 0;
            return KV_LOCK_ERROR;
        }
    }
    
    return KV_LOCK_SUCCESS;
}

// 销毁分段锁
void kv_segment_locks_destroy(kv_segment_locks_t *seglocks) {
    if (!seglocks || !seglocks->locks) return;
    
    int i;
    for (i = 0; i < seglocks->lock_count; i++) {
        kv_rwlock_destroy(&seglocks->locks[i]);
    }
    
    free(seglocks->locks);
    seglocks->locks = NULL;
    seglocks->lock_count = 0;
    seglocks->default_timeout_ms = 0;
}

// 获取指定键的读锁
int kv_segment_read_lock(kv_segment_locks_t *seglocks, const char *key) {
    return kv_segment_read_lock_timeout(seglocks, key, seglocks->default_timeout_ms);
}

// 获取指定键的读锁（带超时）
int kv_segment_read_lock_timeout(kv_segment_locks_t *seglocks, const char *key, int timeout_ms) {
    if (!seglocks || !seglocks->locks || !key || seglocks->lock_count <= 0) return KV_LOCK_INVALID_PARAM;
    
    int idx = hash_key(key, seglocks->lock_count);
    return kv_read_lock_timeout(&seglocks->locks[idx], timeout_ms);
}

// 释放指定键的读锁
int kv_segment_read_unlock(kv_segment_locks_t *seglocks, const char *key) {
    if (!seglocks || !seglocks->locks || !key || seglocks->lock_count <= 0) return KV_LOCK_INVALID_PARAM;
    
    int idx = hash_key(key, seglocks->lock_count);
    return kv_read_unlock(&seglocks->locks[idx]);
}

// 获取指定键的写锁
int kv_segment_write_lock(kv_segment_locks_t *seglocks, const char *key) {
    return kv_segment_write_lock_timeout(seglocks, key, seglocks->default_timeout_ms);
}

// 获取指定键的写锁（带超时）
int kv_segment_write_lock_timeout(kv_segment_locks_t *seglocks, const char *key, int timeout_ms) {
    if (!seglocks || !seglocks->locks || !key || seglocks->lock_count <= 0) return KV_LOCK_INVALID_PARAM;
    
    int idx = hash_key(key, seglocks->lock_count);
    return kv_write_lock_timeout(&seglocks->locks[idx], timeout_ms);
}

// 释放指定键的写锁
int kv_segment_write_unlock(kv_segment_locks_t *seglocks, const char *key) {
    if (!seglocks || !seglocks->locks || !key || seglocks->lock_count <= 0) return KV_LOCK_INVALID_PARAM;
    
    int idx = hash_key(key, seglocks->lock_count);
    return kv_write_unlock(&seglocks->locks[idx]);
}

// 获取所有段的写锁（用于全局操作）
int kv_segment_write_lock_all(kv_segment_locks_t *seglocks) {
    return kv_segment_write_lock_all_timeout(seglocks, seglocks->default_timeout_ms);
}

// 获取所有段的写锁（带超时）
int kv_segment_write_lock_all_timeout(kv_segment_locks_t *seglocks, int timeout_ms) {
    if (!seglocks || !seglocks->locks || seglocks->lock_count <= 0) return KV_LOCK_INVALID_PARAM;
    
    int i;
    for (i = 0; i < seglocks->lock_count; i++) {
        int ret = kv_write_lock_timeout(&seglocks->locks[i], timeout_ms);
        if (ret != KV_LOCK_SUCCESS) {
            // 锁获取失败，释放已获取的锁
            while (--i >= 0) {
                kv_write_unlock(&seglocks->locks[i]);
            }
            return ret;
        }
    }
    
    return KV_LOCK_SUCCESS;
}

// 释放所有段的写锁
int kv_segment_write_unlock_all(kv_segment_locks_t *seglocks) {
    if (!seglocks || !seglocks->locks || seglocks->lock_count <= 0) return KV_LOCK_INVALID_PARAM;
    
    int i;
    for (i = 0; i < seglocks->lock_count; i++) {
        kv_write_unlock(&seglocks->locks[i]);
    }
    
    return KV_LOCK_SUCCESS;
}

// 多键锁定（按键排序避免死锁）
int kv_segment_multi_lock(kv_segment_locks_t *seglocks, const char **keys, int key_count, kv_lock_type_t lock_type) {
    if (!seglocks || !keys || key_count <= 0) return KV_LOCK_INVALID_PARAM;
    
    // 创建键的副本并排序
    const char **sorted_keys = malloc(key_count * sizeof(char *));
    if (!sorted_keys) return KV_LOCK_ERROR;
    
    memcpy(sorted_keys, keys, key_count * sizeof(char *));
    qsort(sorted_keys, key_count, sizeof(char *), compare_keys);
    
    // 按排序后的顺序获取锁
    int i;
    for (i = 0; i < key_count; i++) {
        int ret;
        if (lock_type == LOCK_TYPE_READ) {
            ret = kv_segment_read_lock(seglocks, sorted_keys[i]);
        } else {
            ret = kv_segment_write_lock(seglocks, sorted_keys[i]);
        }
        
        if (ret != KV_LOCK_SUCCESS) {
            // 锁获取失败，释放已获取的锁
            while (--i >= 0) {
                if (lock_type == LOCK_TYPE_READ) {
                    kv_segment_read_unlock(seglocks, sorted_keys[i]);
                } else {
                    kv_segment_write_unlock(seglocks, sorted_keys[i]);
                }
            }
            free(sorted_keys);
            return ret;
        }
    }
    
    free(sorted_keys);
    return KV_LOCK_SUCCESS;
}

// 多键解锁
int kv_segment_multi_unlock(kv_segment_locks_t *seglocks, const char **keys, int key_count, kv_lock_type_t lock_type) {
    if (!seglocks || !keys || key_count <= 0) return KV_LOCK_INVALID_PARAM;
    
    int i;
    for (i = 0; i < key_count; i++) {
        if (lock_type == LOCK_TYPE_READ) {
            kv_segment_read_unlock(seglocks, keys[i]);
        } else {
            kv_segment_write_unlock(seglocks, keys[i]);
        }
    }
    
    return KV_LOCK_SUCCESS;
} 