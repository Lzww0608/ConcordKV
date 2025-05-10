#include "kv_concurrency.h"
#include <string.h>
#include <stdlib.h>

// 哈希函数，用于将键映射到锁分段
static int hash_key(const char *key, int size) {
    unsigned long hash = 0;
    int c;
    
    while ((c = *key++)) {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }
    
    return hash % size;
}

// 初始化读写锁
int kv_rwlock_init(kv_rwlock_t *rwlock) {
    if (!rwlock) return -1;
    
    int ret = pthread_rwlock_init(&rwlock->lock, NULL);
    if (ret == 0) {
        rwlock->initialized = 1;
    }
    return ret;
}

// 销毁读写锁
void kv_rwlock_destroy(kv_rwlock_t *rwlock) {
    if (!rwlock || !rwlock->initialized) return;
    
    pthread_rwlock_destroy(&rwlock->lock);
    rwlock->initialized = 0;
}

// 获取读锁（共享锁）
int kv_read_lock(kv_rwlock_t *rwlock) {
    if (!rwlock || !rwlock->initialized) return -1;
    
    return pthread_rwlock_rdlock(&rwlock->lock);
}

// 释放读锁
int kv_read_unlock(kv_rwlock_t *rwlock) {
    if (!rwlock || !rwlock->initialized) return -1;
    
    return pthread_rwlock_unlock(&rwlock->lock);
}

// 获取写锁（排他锁）
int kv_write_lock(kv_rwlock_t *rwlock) {
    if (!rwlock || !rwlock->initialized) return -1;
    
    return pthread_rwlock_wrlock(&rwlock->lock);
}

// 释放写锁
int kv_write_unlock(kv_rwlock_t *rwlock) {
    if (!rwlock || !rwlock->initialized) return -1;
    
    return pthread_rwlock_unlock(&rwlock->lock);
}

// 尝试获取读锁
int kv_try_read_lock(kv_rwlock_t *rwlock) {
    if (!rwlock || !rwlock->initialized) return -1;
    
    return pthread_rwlock_tryrdlock(&rwlock->lock);
}

// 尝试获取写锁
int kv_try_write_lock(kv_rwlock_t *rwlock) {
    if (!rwlock || !rwlock->initialized) return -1;
    
    return pthread_rwlock_trywrlock(&rwlock->lock);
}

// 初始化自旋锁
int kv_spinlock_init(kv_spinlock_t *spinlock) {
    if (!spinlock) return -1;
    
    int ret = pthread_spin_init(&spinlock->lock, PTHREAD_PROCESS_PRIVATE);
    if (ret == 0) {
        spinlock->initialized = 1;
    }
    return ret;
}

// 销毁自旋锁
void kv_spinlock_destroy(kv_spinlock_t *spinlock) {
    if (!spinlock || !spinlock->initialized) return;
    
    pthread_spin_destroy(&spinlock->lock);
    spinlock->initialized = 0;
}

// 获取自旋锁
int kv_spin_lock(kv_spinlock_t *spinlock) {
    if (!spinlock || !spinlock->initialized) return -1;
    
    return pthread_spin_lock(&spinlock->lock);
}

// 释放自旋锁
int kv_spin_unlock(kv_spinlock_t *spinlock) {
    if (!spinlock || !spinlock->initialized) return -1;
    
    return pthread_spin_unlock(&spinlock->lock);
}

// 尝试获取自旋锁
int kv_try_spin_lock(kv_spinlock_t *spinlock) {
    if (!spinlock || !spinlock->initialized) return -1;
    
    return pthread_spin_trylock(&spinlock->lock);
}

// 初始化分段锁
int kv_segment_locks_init(kv_segment_locks_t *seglocks, int count) {
    if (!seglocks || count <= 0) return -1;
    
    seglocks->locks = (kv_rwlock_t *)calloc(count, sizeof(kv_rwlock_t));
    if (!seglocks->locks) return -1;
    
    seglocks->lock_count = count;
    
    int i;
    for (i = 0; i < count; i++) {
        if (kv_rwlock_init(&seglocks->locks[i]) != 0) {
            // 初始化失败，释放已初始化的锁
            while (--i >= 0) {
                kv_rwlock_destroy(&seglocks->locks[i]);
            }
            free(seglocks->locks);
            seglocks->locks = NULL;
            seglocks->lock_count = 0;
            return -1;
        }
    }
    
    return 0;
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
}

// 获取指定键的读锁
int kv_segment_read_lock(kv_segment_locks_t *seglocks, const char *key) {
    if (!seglocks || !seglocks->locks || !key || seglocks->lock_count <= 0) return -1;
    
    int idx = hash_key(key, seglocks->lock_count);
    return kv_read_lock(&seglocks->locks[idx]);
}

// 释放指定键的读锁
int kv_segment_read_unlock(kv_segment_locks_t *seglocks, const char *key) {
    if (!seglocks || !seglocks->locks || !key || seglocks->lock_count <= 0) return -1;
    
    int idx = hash_key(key, seglocks->lock_count);
    return kv_read_unlock(&seglocks->locks[idx]);
}

// 获取指定键的写锁
int kv_segment_write_lock(kv_segment_locks_t *seglocks, const char *key) {
    if (!seglocks || !seglocks->locks || !key || seglocks->lock_count <= 0) return -1;
    
    int idx = hash_key(key, seglocks->lock_count);
    return kv_write_lock(&seglocks->locks[idx]);
}

// 释放指定键的写锁
int kv_segment_write_unlock(kv_segment_locks_t *seglocks, const char *key) {
    if (!seglocks || !seglocks->locks || !key || seglocks->lock_count <= 0) return -1;
    
    int idx = hash_key(key, seglocks->lock_count);
    return kv_write_unlock(&seglocks->locks[idx]);
}

// 获取所有段的写锁（用于全局操作）
int kv_segment_write_lock_all(kv_segment_locks_t *seglocks) {
    if (!seglocks || !seglocks->locks || seglocks->lock_count <= 0) return -1;
    
    int i;
    for (i = 0; i < seglocks->lock_count; i++) {
        if (kv_write_lock(&seglocks->locks[i]) != 0) {
            // 锁获取失败，释放已获取的锁
            while (--i >= 0) {
                kv_write_unlock(&seglocks->locks[i]);
            }
            return -1;
        }
    }
    
    return 0;
}

// 释放所有段的写锁
int kv_segment_write_unlock_all(kv_segment_locks_t *seglocks) {
    if (!seglocks || !seglocks->locks || seglocks->lock_count <= 0) return -1;
    
    int i;
    for (i = 0; i < seglocks->lock_count; i++) {
        kv_write_unlock(&seglocks->locks[i]);
    }
    
    return 0;
} 