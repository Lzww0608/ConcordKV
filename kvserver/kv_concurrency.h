#ifndef __KV_CONCURRENCY_H__
#define __KV_CONCURRENCY_H__

#include <pthread.h>
#include <stdint.h>

// 读写锁封装
typedef struct {
    pthread_rwlock_t lock;
    int initialized;
} kv_rwlock_t;

// 初始化读写锁
int kv_rwlock_init(kv_rwlock_t *rwlock);

// 销毁读写锁
void kv_rwlock_destroy(kv_rwlock_t *rwlock);

// 获取读锁（共享锁）
int kv_read_lock(kv_rwlock_t *rwlock);

// 释放读锁
int kv_read_unlock(kv_rwlock_t *rwlock);

// 获取写锁（排他锁）
int kv_write_lock(kv_rwlock_t *rwlock);

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
} kv_segment_locks_t;

// 初始化分段锁
int kv_segment_locks_init(kv_segment_locks_t *seglocks, int count);

// 销毁分段锁
void kv_segment_locks_destroy(kv_segment_locks_t *seglocks);

// 获取指定键的读锁
int kv_segment_read_lock(kv_segment_locks_t *seglocks, const char *key);

// 释放指定键的读锁
int kv_segment_read_unlock(kv_segment_locks_t *seglocks, const char *key);

// 获取指定键的写锁
int kv_segment_write_lock(kv_segment_locks_t *seglocks, const char *key);

// 释放指定键的写锁
int kv_segment_write_unlock(kv_segment_locks_t *seglocks, const char *key);

// 获取所有段的写锁（用于全局操作）
int kv_segment_write_lock_all(kv_segment_locks_t *seglocks);

// 释放所有段的写锁
int kv_segment_write_unlock_all(kv_segment_locks_t *seglocks);

#endif // __KV_CONCURRENCY_H__ 