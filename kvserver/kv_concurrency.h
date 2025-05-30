/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV storage engine - kv_concurrency.h
 */
#ifndef __KV_CONCURRENCY_H__
#define __KV_CONCURRENCY_H__

#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <stdbool.h>

// 确保包含必要的系统头文件
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <unistd.h>
#include <errno.h>

// 锁类型枚举
typedef enum {
    LOCK_TYPE_READ = 0,
    LOCK_TYPE_WRITE = 1
} kv_lock_type_t;

// 锁状态枚举
typedef enum {
    LOCK_STATUS_FREE = 0,
    LOCK_STATUS_HELD = 1,
    LOCK_STATUS_WAITING = 2
} kv_lock_status_t;

// 线程锁信息
typedef struct kv_thread_lock_info {
    pthread_t thread_id;
    void *lock_ptr;
    kv_lock_type_t lock_type;
    kv_lock_status_t status;
    struct timespec acquire_time;
    struct timespec timeout;
    struct kv_thread_lock_info *next;
} kv_thread_lock_info_t;

// 死锁检测器
typedef struct {
    kv_thread_lock_info_t *lock_graph;
    pthread_mutex_t graph_mutex;
    bool enabled;
    int max_wait_time_ms;  // 最大等待时间（毫秒）
} kv_deadlock_detector_t;

// 全局死锁检测器实例
extern kv_deadlock_detector_t g_deadlock_detector;

// 死锁检测器初始化和销毁
int kv_deadlock_detector_init(int max_wait_time_ms);
void kv_deadlock_detector_destroy(void);

// 死锁检测相关函数
int kv_deadlock_detector_add_lock_request(void *lock_ptr, kv_lock_type_t type);
int kv_deadlock_detector_remove_lock_request(void *lock_ptr);
int kv_deadlock_detector_check_deadlock(void);
bool kv_deadlock_detector_is_timeout(void *lock_ptr);

// 读写锁封装（增强版，支持超时和死锁检测）
typedef struct {
    pthread_rwlock_t lock;
    int initialized;
    struct timespec timeout;
    bool timeout_enabled;
} kv_rwlock_t;

// 初始化读写锁
int kv_rwlock_init(kv_rwlock_t *rwlock);

// 初始化带超时的读写锁
int kv_rwlock_init_with_timeout(kv_rwlock_t *rwlock, int timeout_ms);

// 销毁读写锁
void kv_rwlock_destroy(kv_rwlock_t *rwlock);

// 获取读锁（共享锁）
int kv_read_lock(kv_rwlock_t *rwlock);

// 获取读锁（带超时）
int kv_read_lock_timeout(kv_rwlock_t *rwlock, int timeout_ms);

// 释放读锁
int kv_read_unlock(kv_rwlock_t *rwlock);

// 获取写锁（排他锁）
int kv_write_lock(kv_rwlock_t *rwlock);

// 获取写锁（带超时）
int kv_write_lock_timeout(kv_rwlock_t *rwlock, int timeout_ms);

// 释放写锁
int kv_write_unlock(kv_rwlock_t *rwlock);

// 尝试获取读锁
int kv_try_read_lock(kv_rwlock_t *rwlock);

// 尝试获取写锁
int kv_try_write_lock(kv_rwlock_t *rwlock);

// 自旋锁封装（用于轻量级锁定）
typedef struct {
    pthread_spinlock_t lock;
    int initialized;
} kv_spinlock_t;

// 初始化自旋锁
int kv_spinlock_init(kv_spinlock_t *spinlock);

// 销毁自旋锁
void kv_spinlock_destroy(kv_spinlock_t *spinlock);

// 获取自旋锁
int kv_spin_lock(kv_spinlock_t *spinlock);

// 释放自旋锁
int kv_spin_unlock(kv_spinlock_t *spinlock);

// 尝试获取自旋锁
int kv_try_spin_lock(kv_spinlock_t *spinlock);

// 细粒度锁管理器（用于哈希表中的分段锁）
typedef struct {
    kv_rwlock_t *locks;
    int lock_count;
    int default_timeout_ms;
} kv_segment_locks_t;

// 初始化分段锁
int kv_segment_locks_init(kv_segment_locks_t *seglocks, int count);

// 初始化带超时的分段锁
int kv_segment_locks_init_with_timeout(kv_segment_locks_t *seglocks, int count, int timeout_ms);

// 销毁分段锁
void kv_segment_locks_destroy(kv_segment_locks_t *seglocks);

// 获取指定键的读锁
int kv_segment_read_lock(kv_segment_locks_t *seglocks, const char *key);

// 获取指定键的读锁（带超时）
int kv_segment_read_lock_timeout(kv_segment_locks_t *seglocks, const char *key, int timeout_ms);

// 释放指定键的读锁
int kv_segment_read_unlock(kv_segment_locks_t *seglocks, const char *key);

// 获取指定键的写锁
int kv_segment_write_lock(kv_segment_locks_t *seglocks, const char *key);

// 获取指定键的写锁（带超时）
int kv_segment_write_lock_timeout(kv_segment_locks_t *seglocks, const char *key, int timeout_ms);

// 释放指定键的写锁
int kv_segment_write_unlock(kv_segment_locks_t *seglocks, const char *key);

// 获取所有段的写锁（用于全局操作）
int kv_segment_write_lock_all(kv_segment_locks_t *seglocks);

// 获取所有段的写锁（带超时）
int kv_segment_write_lock_all_timeout(kv_segment_locks_t *seglocks, int timeout_ms);

// 释放所有段的写锁
int kv_segment_write_unlock_all(kv_segment_locks_t *seglocks);

// 锁排序函数（用于避免死锁）
int kv_segment_multi_lock(kv_segment_locks_t *seglocks, const char **keys, int key_count, kv_lock_type_t lock_type);
int kv_segment_multi_unlock(kv_segment_locks_t *seglocks, const char **keys, int key_count, kv_lock_type_t lock_type);

// 错误码定义
#define KV_LOCK_SUCCESS         0
#define KV_LOCK_ERROR          -1
#define KV_LOCK_TIMEOUT        -2
#define KV_LOCK_DEADLOCK       -3
#define KV_LOCK_INVALID_PARAM  -4

#endif // __KV_CONCURRENCY_H__ 