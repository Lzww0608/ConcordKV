/*
 * @Author: Lzww0608  
 * @Date: 2025-6-9 19:26:55
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-12 16:44:38
 * @Description: ConcordKV 高性能缓存层实现 - 第一部分
 */

#define _POSIX_C_SOURCE 200809L
#include "kv_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <pthread.h>

// === 内部工具函数声明 ===

// 时间相关函数
static uint64_t kv_cache_get_current_time_us(void);
static bool kv_cache_is_expired(const kv_cache_entry_t *entry, uint64_t current_time);

// 哈希相关函数
static uint64_t kv_cache_hash_key(const char *key, size_t key_len, uint32_t seed);
static size_t kv_cache_hash_to_bucket(uint64_t hash, size_t bucket_count);

// 条目管理函数
static kv_cache_entry_t* kv_cache_entry_create(const char *key, size_t key_len,
                                                const char *value, size_t value_len,
                                                uint64_t ttl, uint64_t current_time);
static void kv_cache_entry_destroy(kv_cache_t *cache, kv_cache_entry_t *entry);

// 哈希表操作
static kv_cache_hashtable_t* kv_cache_hashtable_create(size_t bucket_count, uint32_t seed);
static void kv_cache_hashtable_destroy(kv_cache_t *cache, kv_cache_hashtable_t *table);
static kv_cache_entry_t* kv_cache_hashtable_find(kv_cache_hashtable_t *table,
                                                  const char *key, size_t key_len);
static int kv_cache_hashtable_insert(kv_cache_hashtable_t *table, kv_cache_entry_t *entry);
static int kv_cache_hashtable_remove(kv_cache_hashtable_t *table, const char *key, size_t key_len);

// LRU链表操作
static kv_cache_lru_list_t* kv_cache_lru_list_create(void);
static void kv_cache_lru_list_destroy(kv_cache_lru_list_t *list);
static void kv_cache_lru_list_move_to_head(kv_cache_lru_list_t *list, kv_cache_entry_t *entry);
static void kv_cache_lru_list_add_to_head(kv_cache_lru_list_t *list, kv_cache_entry_t *entry);
static kv_cache_entry_t* kv_cache_lru_list_remove_tail(kv_cache_lru_list_t *list);
static void kv_cache_lru_list_remove_entry(kv_cache_lru_list_t *list, kv_cache_entry_t *entry);

// 内存管理函数
static void* kv_cache_malloc(kv_cache_t *cache, size_t size);
static void kv_cache_free(kv_cache_t *cache, void *ptr);

// 清理和淘汰函数
static void* kv_cache_cleanup_thread(void *arg);
static int kv_cache_evict_lru_entries(kv_cache_t *cache, size_t count);
static int kv_cache_cleanup_expired_entries(kv_cache_t *cache);

// 统计函数
static void kv_cache_update_stats_get(kv_cache_t *cache, bool hit, uint64_t duration);
static void kv_cache_update_stats_set(kv_cache_t *cache, uint64_t duration);

// LFU频率管理函数
static kv_cache_frequency_manager_t* kv_cache_frequency_manager_create(size_t bucket_count);
static void kv_cache_frequency_manager_destroy(kv_cache_t *cache, kv_cache_frequency_manager_t *freq_mgr);
static void kv_cache_frequency_manager_increment(kv_cache_frequency_manager_t *freq_mgr, kv_cache_entry_t *entry);
static kv_cache_entry_t* kv_cache_frequency_manager_find_lfu_entry(kv_cache_t *cache);
static void kv_cache_frequency_manager_decay(kv_cache_frequency_manager_t *freq_mgr);

// CLOCK算法管理函数
static kv_cache_clock_manager_t* kv_cache_clock_manager_create(size_t capacity);
static void kv_cache_clock_manager_destroy(kv_cache_t *cache, kv_cache_clock_manager_t *clock_mgr);
static int kv_cache_clock_manager_add(kv_cache_clock_manager_t *clock_mgr, kv_cache_entry_t *entry);
static kv_cache_entry_t* kv_cache_clock_manager_evict(kv_cache_clock_manager_t *clock_mgr);
static void kv_cache_clock_manager_access(kv_cache_clock_manager_t *clock_mgr, kv_cache_entry_t *entry);

// ARC算法管理函数
static kv_cache_arc_manager_t* kv_cache_arc_manager_create(size_t capacity);
static void kv_cache_arc_manager_destroy(kv_cache_t *cache, kv_cache_arc_manager_t *arc_mgr);
static int kv_cache_arc_manager_access(kv_cache_arc_manager_t *arc_mgr, kv_cache_entry_t *entry);
static kv_cache_entry_t* kv_cache_arc_manager_evict(kv_cache_arc_manager_t *arc_mgr);

// === 时间工具函数实现 ===

static uint64_t kv_cache_get_current_time_us(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        return 0;
    }
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

static bool kv_cache_is_expired(const kv_cache_entry_t *entry, uint64_t current_time) {
    if (entry->ttl == 0) {
        return false; // 永不过期
    }
    return (current_time > entry->create_time + entry->ttl);
}

// === 哈希函数实现 ===

// MurmurHash3 32位版本
static uint64_t kv_cache_hash_key(const char *key, size_t key_len, uint32_t seed) {
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;
    const uint32_t r1 = 15;
    const uint32_t r2 = 13;
    const uint32_t m = 5;
    const uint32_t n = 0xe6546b64;

    uint32_t hash = seed;

    const int nblocks = key_len / 4;
    const uint32_t *blocks = (const uint32_t *)key;

    for (int i = 0; i < nblocks; i++) {
        uint32_t k = blocks[i];
        k *= c1;
        k = (k << r1) | (k >> (32 - r1));
        k *= c2;

        hash ^= k;
        hash = ((hash << r2) | (hash >> (32 - r2))) * m + n;
    }

    const uint8_t *tail = (const uint8_t *)(key + nblocks * 4);
    uint32_t k1 = 0;

    switch (key_len & 3) {
    case 3: k1 ^= tail[2] << 16;
    case 2: k1 ^= tail[1] << 8;
    case 1: k1 ^= tail[0];
        k1 *= c1;
        k1 = (k1 << r1) | (k1 >> (32 - r1));
        k1 *= c2;
        hash ^= k1;
    }

    hash ^= key_len;
    hash ^= (hash >> 16);
    hash *= 0x85ebca6b;
    hash ^= (hash >> 13);
    hash *= 0xc2b2ae35;
    hash ^= (hash >> 16);

    return hash;
}

static size_t kv_cache_hash_to_bucket(uint64_t hash, size_t bucket_count) {
    return hash % bucket_count;
}

// === 内存管理函数实现 ===

static void* kv_cache_malloc(kv_cache_t *cache, size_t size) {
    if (cache && cache->malloc_func) {
        return cache->malloc_func(size);
    }
    return malloc(size);
}

static void kv_cache_free(kv_cache_t *cache, void *ptr) {
    if (cache && cache->free_func) {
        cache->free_func(ptr);
        return;
    }
    free(ptr);
}

// === 缓存条目管理函数实现 ===

static kv_cache_entry_t* kv_cache_entry_create(const char *key, size_t key_len,
                                                const char *value, size_t value_len,
                                                uint64_t ttl, uint64_t current_time) {
    if (!key || key_len == 0 || !value || value_len == 0) {
        return NULL;
    }

    if (key_len > KV_CACHE_MAX_KEY_LENGTH || value_len > KV_CACHE_MAX_VALUE_LENGTH) {
        return NULL;
    }

    kv_cache_entry_t *entry = malloc(sizeof(kv_cache_entry_t));
    if (!entry) {
        return NULL;
    }

    // 分配并复制键
    entry->key = malloc(key_len + 1);
    if (!entry->key) {
        free(entry);
        return NULL;
    }
    memcpy(entry->key, key, key_len);
    entry->key[key_len] = '\0';
    entry->key_len = key_len;

    // 分配并复制值
    entry->value = malloc(value_len + 1);
    if (!entry->value) {
        free(entry->key);
        free(entry);
        return NULL;
    }
    memcpy(entry->value, value, value_len);
    entry->value[value_len] = '\0';
    entry->value_len = value_len;

    // 初始化元数据
    entry->hash = kv_cache_hash_key(key, key_len, KV_CACHE_DEFAULT_HASH_SEED);
    entry->state = KV_CACHE_ENTRY_VALID;

    // 初始化时间信息
    entry->create_time = current_time;
    entry->access_time = current_time;
    entry->update_time = current_time;
    entry->ttl = ttl;

    // 初始化指针
    entry->prev = NULL;
    entry->next = NULL;
    entry->hash_next = NULL;

    // 初始化LFU信息
    entry->frequency = 1;
    entry->frequency_time = current_time;

    // 初始化引用计数和锁
    entry->ref_count = 1;
    if (pthread_mutex_init(&entry->entry_lock, NULL) != 0) {
        free(entry->value);
        free(entry->key);
        free(entry);
        return NULL;
    }

    return entry;
}

static void kv_cache_entry_destroy(kv_cache_t *cache, kv_cache_entry_t *entry) {
    if (!entry) {
        return;
    }

    // 销毁锁
    pthread_mutex_destroy(&entry->entry_lock);

    // 释放内存
    if (entry->key) {
        kv_cache_free(cache, entry->key);
    }
    if (entry->value) {
        kv_cache_free(cache, entry->value);
    }
    kv_cache_free(cache, entry);
}

// === 哈希表操作实现 ===

static kv_cache_hashtable_t* kv_cache_hashtable_create(size_t bucket_count, uint32_t seed) {
    if (bucket_count < KV_CACHE_MIN_HASH_SIZE) {
        bucket_count = KV_CACHE_MIN_HASH_SIZE;
    }

    kv_cache_hashtable_t *table = malloc(sizeof(kv_cache_hashtable_t));
    if (!table) {
        return NULL;
    }

    table->buckets = calloc(bucket_count, sizeof(kv_cache_entry_t*));
    if (!table->buckets) {
        free(table);
        return NULL;
    }

    table->bucket_count = bucket_count;
    table->entry_count = 0;
    table->hash_seed = seed;

    if (pthread_rwlock_init(&table->table_lock, NULL) != 0) {
        free(table->buckets);
        free(table);
        return NULL;
    }

    return table;
}

static void kv_cache_hashtable_destroy(kv_cache_t *cache, kv_cache_hashtable_t *table) {
    if (!table) {
        return;
    }

    // 销毁所有条目
    for (size_t i = 0; i < table->bucket_count; i++) {
        kv_cache_entry_t *entry = table->buckets[i];
        while (entry) {
            kv_cache_entry_t *next = entry->hash_next;
            kv_cache_entry_destroy(cache, entry);
            entry = next;
        }
    }

    // 销毁锁
    pthread_rwlock_destroy(&table->table_lock);

    // 释放内存
    free(table->buckets);
    free(table);
}

static kv_cache_entry_t* kv_cache_hashtable_find(kv_cache_hashtable_t *table,
                                                  const char *key, size_t key_len) {
    if (!table || !key || key_len == 0) {
        return NULL;
    }

    uint64_t hash = kv_cache_hash_key(key, key_len, table->hash_seed);
    size_t bucket = kv_cache_hash_to_bucket(hash, table->bucket_count);

    pthread_rwlock_rdlock(&table->table_lock);

    kv_cache_entry_t *entry = table->buckets[bucket];
    while (entry) {
        if (entry->key_len == key_len && 
            entry->hash == hash &&
            memcmp(entry->key, key, key_len) == 0) {
            pthread_rwlock_unlock(&table->table_lock);
            return entry;
        }
        entry = entry->hash_next;
    }

    pthread_rwlock_unlock(&table->table_lock);
    return NULL;
}

static int kv_cache_hashtable_insert(kv_cache_hashtable_t *table, kv_cache_entry_t *entry) {
    if (!table || !entry) {
        return KV_ERR_INVALID_PARAM;
    }

    size_t bucket = kv_cache_hash_to_bucket(entry->hash, table->bucket_count);

    pthread_rwlock_wrlock(&table->table_lock);

    // 检查是否已存在
    kv_cache_entry_t *existing = table->buckets[bucket];
    while (existing) {
        if (existing->key_len == entry->key_len &&
            existing->hash == entry->hash &&
            memcmp(existing->key, entry->key, entry->key_len) == 0) {
            pthread_rwlock_unlock(&table->table_lock);
            return KV_ERR_KEY_EXISTS;
        }
        existing = existing->hash_next;
    }

    // 插入到桶的头部
    entry->hash_next = table->buckets[bucket];
    table->buckets[bucket] = entry;
    table->entry_count++;

    pthread_rwlock_unlock(&table->table_lock);
    return KV_SUCCESS;
}

static int kv_cache_hashtable_remove(kv_cache_hashtable_t *table, const char *key, size_t key_len) {
    if (!table || !key || key_len == 0) {
        return KV_ERR_INVALID_PARAM;
    }

    uint64_t hash = kv_cache_hash_key(key, key_len, table->hash_seed);
    size_t bucket = kv_cache_hash_to_bucket(hash, table->bucket_count);

    pthread_rwlock_wrlock(&table->table_lock);

    kv_cache_entry_t **current = &table->buckets[bucket];
    while (*current) {
        kv_cache_entry_t *entry = *current;
        if (entry->key_len == key_len &&
            entry->hash == hash &&
            memcmp(entry->key, key, key_len) == 0) {
            
            *current = entry->hash_next;
            entry->hash_next = NULL;
            table->entry_count--;
            
            pthread_rwlock_unlock(&table->table_lock);
            return KV_SUCCESS;
        }
        current = &entry->hash_next;
    }

    pthread_rwlock_unlock(&table->table_lock);
    return KV_ERR_KEY_NOT_FOUND;
}

// === LRU链表操作实现 ===

static kv_cache_lru_list_t* kv_cache_lru_list_create(void) {
    kv_cache_lru_list_t *list = malloc(sizeof(kv_cache_lru_list_t));
    if (!list) {
        return NULL;
    }

    list->head = NULL;
    list->tail = NULL;
    list->size = 0;

    if (pthread_mutex_init(&list->list_lock, NULL) != 0) {
        free(list);
        return NULL;
    }

    return list;
}

static void kv_cache_lru_list_destroy(kv_cache_lru_list_t *list) {
    if (!list) {
        return;
    }

    pthread_mutex_destroy(&list->list_lock);
    free(list);
}

static void kv_cache_lru_list_move_to_head(kv_cache_lru_list_t *list, kv_cache_entry_t *entry) {
    if (!list || !entry) {
        return;
    }

    pthread_mutex_lock(&list->list_lock);

    // 如果已经在头部，不需要移动
    if (list->head == entry) {
        pthread_mutex_unlock(&list->list_lock);
        return;
    }

    // 从当前位置移除
    if (entry->prev) {
        entry->prev->next = entry->next;
    }
    if (entry->next) {
        entry->next->prev = entry->prev;
    } else {
        // 如果是尾节点，更新尾指针
        list->tail = entry->prev;
    }

    // 插入到头部
    entry->prev = NULL;
    entry->next = list->head;
    if (list->head) {
        list->head->prev = entry;
    }
    list->head = entry;

    // 如果链表原来为空，设置尾指针
    if (!list->tail) {
        list->tail = entry;
    }

    pthread_mutex_unlock(&list->list_lock);
}

static void kv_cache_lru_list_add_to_head(kv_cache_lru_list_t *list, kv_cache_entry_t *entry) {
    if (!list || !entry) {
        return;
    }

    pthread_mutex_lock(&list->list_lock);

    entry->prev = NULL;
    entry->next = list->head;

    if (list->head) {
        list->head->prev = entry;
    } else {
        // 链表为空，设置尾指针
        list->tail = entry;
    }

    list->head = entry;
    list->size++;

    pthread_mutex_unlock(&list->list_lock);
}

static kv_cache_entry_t* kv_cache_lru_list_remove_tail(kv_cache_lru_list_t *list) {
    if (!list) {
        return NULL;
    }

    pthread_mutex_lock(&list->list_lock);

    kv_cache_entry_t *entry = list->tail;
    if (!entry) {
        pthread_mutex_unlock(&list->list_lock);
        return NULL;
    }

    // 移除尾节点
    list->tail = entry->prev;
    if (list->tail) {
        list->tail->next = NULL;
    } else {
        // 链表变空
        list->head = NULL;
    }

    entry->prev = NULL;
    entry->next = NULL;
    list->size--;

    pthread_mutex_unlock(&list->list_lock);
    return entry;
}

static void kv_cache_lru_list_remove_entry(kv_cache_lru_list_t *list, kv_cache_entry_t *entry) {
    if (!list || !entry) {
        return;
    }

    pthread_mutex_lock(&list->list_lock);

    // 更新前驱节点
    if (entry->prev) {
        entry->prev->next = entry->next;
    } else {
        // 如果是头节点
        list->head = entry->next;
    }

    // 更新后继节点
    if (entry->next) {
        entry->next->prev = entry->prev;
    } else {
        // 如果是尾节点
        list->tail = entry->prev;
    }

    entry->prev = NULL;
    entry->next = NULL;
    list->size--;

    pthread_mutex_unlock(&list->list_lock);
}

// === 清理和淘汰函数实现 ===

// 通用淘汰函数，支持所有策略
static int kv_cache_evict_entries_by_policy(kv_cache_t *cache, size_t count) {
    if (!cache || count == 0) {
        return KV_SUCCESS;
    }

    size_t evicted = 0;
    
    while (evicted < count) {
        kv_cache_entry_t *victim = NULL;
        
        // 根据策略选择淘汰的条目
        switch (cache->config.policy) {
            case KV_CACHE_POLICY_LRU:
                victim = kv_cache_lru_list_remove_tail(cache->lru_list);
                break;
                
            case KV_CACHE_POLICY_LFU:
                victim = kv_cache_frequency_manager_find_lfu_entry(cache);
                if (victim) {
                    // 从LRU链表中移除
                    kv_cache_lru_list_remove_entry(cache->lru_list, victim);
                }
                break;
                
            case KV_CACHE_POLICY_CLOCK:
                victim = kv_cache_clock_manager_evict(cache->clock_mgr);
                if (victim) {
                    // 从LRU链表中移除
                    kv_cache_lru_list_remove_entry(cache->lru_list, victim);
                }
                break;
                
            case KV_CACHE_POLICY_ARC:
                victim = kv_cache_arc_manager_evict(cache->arc_mgr);
                // ARC算法内部已经处理了链表操作
                break;
                
            case KV_CACHE_POLICY_FIFO:
                victim = kv_cache_lru_list_remove_tail(cache->lru_list);
                break;
                
            case KV_CACHE_POLICY_RANDOM:
                // 随机选择一个条目
                if (cache->hashtable->entry_count > 0) {
                    size_t random_bucket = rand() % cache->hashtable->bucket_count;
                    for (size_t i = 0; i < cache->hashtable->bucket_count; i++) {
                        size_t bucket_idx = (random_bucket + i) % cache->hashtable->bucket_count;
                        if (cache->hashtable->buckets[bucket_idx]) {
                            victim = cache->hashtable->buckets[bucket_idx];
                            kv_cache_lru_list_remove_entry(cache->lru_list, victim);
                            break;
                        }
                    }
                }
                break;
                
            default:
                return KV_ERR_INVALID_PARAM;
        }
        
        if (!victim) {
            break; // 没有更多条目可以淘汰
        }

        // 从哈希表中删除
        kv_cache_hashtable_remove(cache->hashtable, victim->key, victim->key_len);
        
        // 销毁条目
        kv_cache_entry_destroy(cache, victim);
        
        cache->stats.current_entries--;
        cache->stats.evictions++;
        evicted++;
    }

    return KV_SUCCESS;
}

// 保持向后兼容的LRU淘汰函数
static int kv_cache_evict_lru_entries(kv_cache_t *cache, size_t count) {
    return kv_cache_evict_entries_by_policy(cache, count);
}

static int kv_cache_cleanup_expired_entries(kv_cache_t *cache) {
    if (!cache) {
        return KV_ERR_INVALID_PARAM;
    }

    uint64_t current_time = cache->get_time_func();
    size_t expired_count = 0;
    
    // 遍历哈希表的所有桶
    for (size_t i = 0; i < cache->hashtable->bucket_count; i++) {
        kv_cache_entry_t *entry = cache->hashtable->buckets[i];
        kv_cache_entry_t *prev = NULL;
        
        while (entry) {
            kv_cache_entry_t *next = entry->hash_next;
            
            if (kv_cache_is_expired(entry, current_time)) {
                // 删除过期条目
                if (prev) {
                    prev->hash_next = next;
                } else {
                    cache->hashtable->buckets[i] = next;
                }
                
                // 从LRU链表中删除
                kv_cache_lru_list_remove_entry(cache->lru_list, entry);
                
                // 销毁条目
                kv_cache_entry_destroy(cache, entry);
                
                cache->stats.current_entries--;
                cache->stats.expirations++;
                expired_count++;
                
                entry = next;
            } else {
                prev = entry;
                entry = next;
            }
        }
    }

    return (int)expired_count;
}

static void* kv_cache_cleanup_thread(void *arg) {
    kv_cache_t *cache = (kv_cache_t*)arg;
    if (!cache) {
        return NULL;
    }

    struct timespec timeout;
    uint64_t cleanup_interval_ns = cache->config.cleanup_interval * 1000; // 转换为纳秒

    while (cache->cleanup_running) {
        // 等待清理间隔
        pthread_mutex_lock(&cache->cleanup_mutex);
        
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_nsec += cleanup_interval_ns;
        if (timeout.tv_nsec >= 1000000000) {
            timeout.tv_sec += timeout.tv_nsec / 1000000000;
            timeout.tv_nsec %= 1000000000;
        }

        int wait_result = pthread_cond_timedwait(&cache->cleanup_cond, &cache->cleanup_mutex, &timeout);
        pthread_mutex_unlock(&cache->cleanup_mutex);

        if (!cache->cleanup_running) {
            break;
        }

        // 执行清理操作
        if (wait_result == ETIMEDOUT || wait_result == 0) {
            // 获取写锁进行清理
            if (pthread_rwlock_wrlock(&cache->cache_lock) == 0) {
                cache->state = KV_CACHE_STATE_CLEANING;
                
                // 清理过期条目
                kv_cache_cleanup_expired_entries(cache);
                
                // 更新最后清理时间
                cache->stats.last_cleanup_time = cache->get_time_func();
                
                cache->state = KV_CACHE_STATE_RUNNING;
                pthread_rwlock_unlock(&cache->cache_lock);
            }
        }
    }

    return NULL;
}

// === 统计函数实现 ===

static void kv_cache_update_stats_get(kv_cache_t *cache, bool hit, uint64_t duration) {
    if (!cache || !cache->config.enable_stats) {
        return;
    }

    cache->stats.total_requests++;
    cache->stats.get_count++;

    if (hit) {
        cache->stats.cache_hits++;
    } else {
        cache->stats.cache_misses++;
    }

    // 更新时间统计
    if (duration > 0) {
        if (cache->stats.get_count == 1) {
            cache->stats.avg_get_time = duration;
        } else {
            cache->stats.avg_get_time = 
                (cache->stats.avg_get_time * (cache->stats.get_count - 1) + duration) / cache->stats.get_count;
        }

        if (duration > cache->stats.max_get_time) {
            cache->stats.max_get_time = duration;
        }
    }
}

static void kv_cache_update_stats_set(kv_cache_t *cache, uint64_t duration) {
    if (!cache || !cache->config.enable_stats) {
        return;
    }

    cache->stats.set_count++;

    // 更新时间统计
    if (duration > 0) {
        if (cache->stats.set_count == 1) {
            cache->stats.avg_set_time = duration;
        } else {
            cache->stats.avg_set_time = 
                (cache->stats.avg_set_time * (cache->stats.set_count - 1) + duration) / cache->stats.set_count;
        }

        if (duration > cache->stats.max_set_time) {
            cache->stats.max_set_time = duration;
        }
    }
}

// === 配置和工具函数实现 ===

kv_cache_config_t* kv_cache_config_create_default(kv_cache_policy_t policy) {
    kv_cache_config_t *config = malloc(sizeof(kv_cache_config_t));
    if (!config) {
        return NULL;
    }

    // 基础配置
    config->policy = policy;
    config->max_entries = KV_CACHE_DEFAULT_MAX_ENTRIES;
    config->max_memory = KV_CACHE_DEFAULT_MAX_MEMORY;
    config->hash_table_size = KV_CACHE_DEFAULT_HASH_SIZE;

    // 超时配置
    config->default_ttl = KV_CACHE_DEFAULT_TTL;
    config->cleanup_interval = KV_CACHE_DEFAULT_CLEANUP_INTERVAL;
    config->max_idle_time = KV_CACHE_DEFAULT_TTL * 2;

    // 性能配置
    config->enable_stats = true;
    config->enable_compression = false;
    config->thread_safe = true;
    config->hash_seed = KV_CACHE_DEFAULT_HASH_SEED;

    // 淘汰配置
    config->eviction_factor = KV_CACHE_DEFAULT_EVICTION_FACTOR;
    config->min_eviction_count = 1;
    config->max_eviction_count = 1000;

    // LFU特有配置
    config->frequency_decay_time = 3600; // 1小时
    config->frequency_decay_factor = 0.8f;

    return config;
}

void kv_cache_config_destroy(kv_cache_config_t *config) {
    if (config) {
        free(config);
    }
}

bool kv_cache_config_validate(const kv_cache_config_t *config) {
    if (!config) {
        return false;
    }

    // 检查策略
    if (config->policy >= KV_CACHE_POLICY_MAX) {
        return false;
    }

    // 检查哈希表大小
    if (config->hash_table_size < KV_CACHE_MIN_HASH_SIZE || 
        config->hash_table_size > KV_CACHE_MAX_HASH_SIZE) {
        return false;
    }

    // 检查淘汰配置
    if (config->eviction_factor < 0.01f || config->eviction_factor > 0.5f) {
        return false;
    }

    if (config->min_eviction_count > config->max_eviction_count) {
        return false;
    }

    // 检查LFU配置
    if (config->frequency_decay_factor < 0.1f || config->frequency_decay_factor > 1.0f) {
        return false;
    }

    return true;
}

double kv_cache_hit_ratio(const kv_cache_stats_t *stats) {
    if (!stats || stats->total_requests == 0) {
        return 0.0;
    }
    return (double)stats->cache_hits / (double)stats->total_requests;
}

const char* kv_cache_policy_name(kv_cache_policy_t policy) {
    switch (policy) {
        case KV_CACHE_POLICY_LRU:    return "LRU";
        case KV_CACHE_POLICY_LFU:    return "LFU";
        case KV_CACHE_POLICY_FIFO:   return "FIFO";
        case KV_CACHE_POLICY_RANDOM: return "RANDOM";
        case KV_CACHE_POLICY_CLOCK:  return "CLOCK";
        case KV_CACHE_POLICY_ARC:    return "ARC";
        default:                     return "UNKNOWN";
    }
}

const char* kv_cache_state_name(kv_cache_state_t state) {
    switch (state) {
        case KV_CACHE_STATE_INIT:     return "INIT";
        case KV_CACHE_STATE_RUNNING:  return "RUNNING";
        case KV_CACHE_STATE_FULL:     return "FULL";
        case KV_CACHE_STATE_CLEANING: return "CLEANING";
        case KV_CACHE_STATE_SHUTDOWN: return "SHUTDOWN";
        default:                      return "UNKNOWN";
    }
}

// === 缓存创建函数 ===

kv_cache_t* kv_cache_create(const kv_cache_config_t *config) {
    if (!config || !kv_cache_config_validate(config)) {
        return NULL;
    }

    // 分配缓存结构
    kv_cache_t *cache = malloc(sizeof(kv_cache_t));
    if (!cache) {
        return NULL;
    }

    // 复制配置
    memcpy(&cache->config, config, sizeof(kv_cache_config_t));
    cache->state = KV_CACHE_STATE_INIT;

    // 创建哈希表
    cache->hashtable = kv_cache_hashtable_create(config->hash_table_size, config->hash_seed);
    if (!cache->hashtable) {
        free(cache);
        return NULL;
    }

    // 创建LRU链表
    cache->lru_list = kv_cache_lru_list_create();
    if (!cache->lru_list) {
        kv_cache_hashtable_destroy(cache, cache->hashtable);
        free(cache);
        return NULL;
    }

    // 创建频率管理器（所有策略都需要，用于统计）
    cache->freq_mgr = kv_cache_frequency_manager_create(1024);
    if (!cache->freq_mgr) {
        kv_cache_lru_list_destroy(cache->lru_list);
        kv_cache_hashtable_destroy(cache, cache->hashtable);
        free(cache);
        return NULL;
    }

    // 根据策略创建相应的管理器
    cache->clock_mgr = NULL;
    cache->arc_mgr = NULL;
    
    if (config->policy == KV_CACHE_POLICY_CLOCK) {
        cache->clock_mgr = kv_cache_clock_manager_create(config->max_entries);
        if (!cache->clock_mgr) {
            kv_cache_frequency_manager_destroy(cache, cache->freq_mgr);
            kv_cache_lru_list_destroy(cache->lru_list);
            kv_cache_hashtable_destroy(cache, cache->hashtable);
            free(cache);
            return NULL;
        }
    } else if (config->policy == KV_CACHE_POLICY_ARC) {
        cache->arc_mgr = kv_cache_arc_manager_create(config->max_entries);
        if (!cache->arc_mgr) {
            kv_cache_frequency_manager_destroy(cache, cache->freq_mgr);
            kv_cache_lru_list_destroy(cache->lru_list);
            kv_cache_hashtable_destroy(cache, cache->hashtable);
            free(cache);
            return NULL;
        }
    }

    // 初始化统计信息
    memset(&cache->stats, 0, sizeof(kv_cache_stats_t));
    cache->stats.start_time = kv_cache_get_current_time_us();
    cache->stats.max_entries = config->max_entries;
    cache->stats.max_memory = config->max_memory;

    // 初始化锁
    if (pthread_rwlock_init(&cache->cache_lock, NULL) != 0) {
        if (cache->arc_mgr) kv_cache_arc_manager_destroy(cache, cache->arc_mgr);
        if (cache->clock_mgr) kv_cache_clock_manager_destroy(cache, cache->clock_mgr);
        kv_cache_frequency_manager_destroy(cache, cache->freq_mgr);
        kv_cache_lru_list_destroy(cache->lru_list);
        kv_cache_hashtable_destroy(cache, cache->hashtable);
        free(cache);
        return NULL;
    }

    if (pthread_mutex_init(&cache->cleanup_mutex, NULL) != 0) {
        pthread_rwlock_destroy(&cache->cache_lock);
        if (cache->arc_mgr) kv_cache_arc_manager_destroy(cache, cache->arc_mgr);
        if (cache->clock_mgr) kv_cache_clock_manager_destroy(cache, cache->clock_mgr);
        kv_cache_frequency_manager_destroy(cache, cache->freq_mgr);
        kv_cache_lru_list_destroy(cache->lru_list);
        kv_cache_hashtable_destroy(cache, cache->hashtable);
        free(cache);
        return NULL;
    }

    if (pthread_cond_init(&cache->cleanup_cond, NULL) != 0) {
        pthread_mutex_destroy(&cache->cleanup_mutex);
        pthread_rwlock_destroy(&cache->cache_lock);
        if (cache->arc_mgr) kv_cache_arc_manager_destroy(cache, cache->arc_mgr);
        if (cache->clock_mgr) kv_cache_clock_manager_destroy(cache, cache->clock_mgr);
        kv_cache_frequency_manager_destroy(cache, cache->freq_mgr);
        kv_cache_lru_list_destroy(cache->lru_list);
        kv_cache_hashtable_destroy(cache, cache->hashtable);
        free(cache);
        return NULL;
    }

    // 设置内存管理函数
    cache->malloc_func = NULL;
    cache->free_func = NULL;

    // 设置时间函数
    cache->get_time_func = kv_cache_get_current_time_us;

    // 设置调试信息
    snprintf(cache->name, sizeof(cache->name), "cache_%p", (void*)cache);
    cache->magic = KV_CACHE_MAGIC;

    // 启动清理线程
    cache->cleanup_running = true;
    if (pthread_create(&cache->cleanup_thread, NULL, kv_cache_cleanup_thread, cache) != 0) {
        cache->cleanup_running = false;
        pthread_cond_destroy(&cache->cleanup_cond);
        pthread_mutex_destroy(&cache->cleanup_mutex);
        pthread_rwlock_destroy(&cache->cache_lock);
        if (cache->arc_mgr) kv_cache_arc_manager_destroy(cache, cache->arc_mgr);
        if (cache->clock_mgr) kv_cache_clock_manager_destroy(cache, cache->clock_mgr);
        kv_cache_frequency_manager_destroy(cache, cache->freq_mgr);
        kv_cache_lru_list_destroy(cache->lru_list);
        kv_cache_hashtable_destroy(cache, cache->hashtable);
        free(cache);
        return NULL;
    }

    cache->state = KV_CACHE_STATE_RUNNING;
    return cache;
}

// === 主要API函数实现 ===

// === 缓存销毁函数 ===

void kv_cache_destroy(kv_cache_t *cache) {
    if (!cache || cache->magic != KV_CACHE_MAGIC) {
        return;
    }

    // 停止清理线程
    if (cache->cleanup_running) {
        cache->cleanup_running = false;
        pthread_cond_signal(&cache->cleanup_cond);
        pthread_join(cache->cleanup_thread, NULL);
    }

    // 设置状态为关闭
    cache->state = KV_CACHE_STATE_SHUTDOWN;

    // 销毁数据结构
    if (cache->hashtable) {
        kv_cache_hashtable_destroy(cache, cache->hashtable);
    }
    if (cache->lru_list) {
        kv_cache_lru_list_destroy(cache->lru_list);
    }
    if (cache->freq_mgr) {
        kv_cache_frequency_manager_destroy(cache, cache->freq_mgr);
    }
    if (cache->clock_mgr) {
        kv_cache_clock_manager_destroy(cache, cache->clock_mgr);
    }
    if (cache->arc_mgr) {
        kv_cache_arc_manager_destroy(cache, cache->arc_mgr);
    }

    // 销毁锁
    pthread_cond_destroy(&cache->cleanup_cond);
    pthread_mutex_destroy(&cache->cleanup_mutex);
    pthread_rwlock_destroy(&cache->cache_lock);

    // 清除魔数并释放内存
    cache->magic = 0;
    free(cache);
}

// === 核心API函数实现 ===

int kv_cache_get(kv_cache_t *cache, const char *key, size_t key_len, 
                 char **value_out, size_t *value_len_out) {
    if (!cache || cache->magic != KV_CACHE_MAGIC || cache->state != KV_CACHE_STATE_RUNNING) {
        return KV_ERR_INVALID_PARAM;
    }
    
    if (!key || key_len == 0 || !value_out || !value_len_out) {
        return KV_ERR_INVALID_PARAM;
    }

    if (key_len > KV_CACHE_MAX_KEY_LENGTH) {
        return KV_ERR_KEY_TOO_LONG;
    }

    uint64_t start_time = cache->get_time_func();
    int result = KV_SUCCESS;
    
    // 获取读锁
    if (pthread_rwlock_rdlock(&cache->cache_lock) != 0) {
        return KV_ERR_LOCK_FAILED;
    }

    do {
        // 查找条目
        kv_cache_entry_t *entry = kv_cache_hashtable_find(cache->hashtable, key, key_len);
        if (!entry) {
            result = KV_ERR_KEY_NOT_FOUND;
            break;
        }

        // 检查是否过期
        uint64_t current_time = cache->get_time_func();
        if (kv_cache_is_expired(entry, current_time)) {
            result = KV_ERR_KEY_NOT_FOUND;
            break;
        }

        // 分配内存并复制值
        *value_out = kv_cache_malloc(cache, entry->value_len + 1);
        if (!*value_out) {
            result = KV_ERR_MEMORY_ALLOC;
            break;
        }

        memcpy(*value_out, entry->value, entry->value_len);
        (*value_out)[entry->value_len] = '\0';
        *value_len_out = entry->value_len;

        // 更新访问时间
        entry->access_time = current_time;

        // 根据策略更新相应的数据结构
        switch (cache->config.policy) {
            case KV_CACHE_POLICY_LRU:
                kv_cache_lru_list_move_to_head(cache->lru_list, entry);
                break;
                
            case KV_CACHE_POLICY_LFU:
                kv_cache_frequency_manager_increment(cache->freq_mgr, entry);
                break;
                
            case KV_CACHE_POLICY_CLOCK:
                kv_cache_clock_manager_access(cache->clock_mgr, entry);
                break;
                
            case KV_CACHE_POLICY_ARC:
                kv_cache_arc_manager_access(cache->arc_mgr, entry);
                break;
                
            case KV_CACHE_POLICY_FIFO:
            case KV_CACHE_POLICY_RANDOM:
            default:
                // FIFO和RANDOM策略不需要更新访问信息
                break;
        }

    } while (0);

    pthread_rwlock_unlock(&cache->cache_lock);

    // 更新统计信息
    uint64_t duration = cache->get_time_func() - start_time;
    kv_cache_update_stats_get(cache, (result == KV_SUCCESS), duration);

    return result;
}

int kv_cache_set(kv_cache_t *cache, const char *key, size_t key_len,
                 const char *value, size_t value_len, uint64_t ttl) {
    if (!cache || cache->magic != KV_CACHE_MAGIC) {
        return KV_ERR_INVALID_PARAM;
    }
    
    if (!key || key_len == 0 || !value || value_len == 0) {
        return KV_ERR_INVALID_PARAM;
    }

    if (key_len > KV_CACHE_MAX_KEY_LENGTH || value_len > KV_CACHE_MAX_VALUE_LENGTH) {
        return KV_ERR_KEY_TOO_LONG;
    }

    uint64_t start_time = cache->get_time_func();
    uint64_t current_time = start_time;
    int result = KV_SUCCESS;

    // 获取写锁
    if (pthread_rwlock_wrlock(&cache->cache_lock) != 0) {
        return KV_ERR_LOCK_FAILED;
    }

    do {
        // 检查是否需要淘汰
        if (cache->config.max_entries > 0 && 
            cache->stats.current_entries >= cache->config.max_entries) {
            
            size_t evict_count = (size_t)(cache->config.max_entries * cache->config.eviction_factor);
            if (evict_count < cache->config.min_eviction_count) {
                evict_count = cache->config.min_eviction_count;
            }
            
            int evict_result = kv_cache_evict_lru_entries(cache, evict_count);
            if (evict_result != KV_SUCCESS) {
                result = evict_result;
                break;
            }
        }

        // 检查现有条目
        kv_cache_entry_t *existing = kv_cache_hashtable_find(cache->hashtable, key, key_len);
        if (existing) {
            // 更新现有条目
            char *new_value = kv_cache_malloc(cache, value_len + 1);
            if (!new_value) {
                result = KV_ERR_MEMORY_ALLOC;
                break;
            }

            // 释放旧值并设置新值
            kv_cache_free(cache, existing->value);
            existing->value = new_value;
            memcpy(existing->value, value, value_len);
            existing->value[value_len] = '\0';
            existing->value_len = value_len;
            
            // 更新时间信息
            existing->update_time = current_time;
            existing->access_time = current_time;
            existing->ttl = ttl;
            existing->state = KV_CACHE_ENTRY_VALID;

            // 根据策略更新相应的数据结构
            switch (cache->config.policy) {
                case KV_CACHE_POLICY_LRU:
                    kv_cache_lru_list_move_to_head(cache->lru_list, existing);
                    break;
                    
                case KV_CACHE_POLICY_LFU:
                    kv_cache_frequency_manager_increment(cache->freq_mgr, existing);
                    break;
                    
                case KV_CACHE_POLICY_CLOCK:
                    kv_cache_clock_manager_access(cache->clock_mgr, existing);
                    break;
                    
                case KV_CACHE_POLICY_ARC:
                    kv_cache_arc_manager_access(cache->arc_mgr, existing);
                    break;
                    
                case KV_CACHE_POLICY_FIFO:
                case KV_CACHE_POLICY_RANDOM:
                default:
                    // FIFO和RANDOM策略不需要更新访问信息
                    break;
            }

            cache->stats.update_count++;
        } else {
            // 创建新条目
            kv_cache_entry_t *new_entry = kv_cache_entry_create(key, key_len, value, value_len, ttl, current_time);
            if (!new_entry) {
                result = KV_ERR_MEMORY_ALLOC;
                break;
            }

            // 插入哈希表
            result = kv_cache_hashtable_insert(cache->hashtable, new_entry);
            if (result != KV_SUCCESS) {
                kv_cache_entry_destroy(cache, new_entry);
                break;
            }

            // 添加到LRU链表头部（所有策略都需要）
            kv_cache_lru_list_add_to_head(cache->lru_list, new_entry);
            
            // 根据策略添加到相应的管理器
            switch (cache->config.policy) {
                case KV_CACHE_POLICY_CLOCK:
                    kv_cache_clock_manager_add(cache->clock_mgr, new_entry);
                    break;
                    
                case KV_CACHE_POLICY_ARC:
                    kv_cache_arc_manager_access(cache->arc_mgr, new_entry);
                    break;
                    
                case KV_CACHE_POLICY_LFU:
                    // 初始化频率为1
                    new_entry->frequency = 1;
                    new_entry->frequency_time = current_time;
                    break;
                    
                case KV_CACHE_POLICY_LRU:
                case KV_CACHE_POLICY_FIFO:
                case KV_CACHE_POLICY_RANDOM:
                default:
                    // 这些策略只需要LRU链表
                    break;
            }
            
            cache->stats.current_entries++;
        }

    } while (0);

    pthread_rwlock_unlock(&cache->cache_lock);

    // 更新统计信息
    uint64_t duration = cache->get_time_func() - start_time;
    kv_cache_update_stats_set(cache, duration);
    if (result == KV_SUCCESS) {
        cache->stats.set_count++;
    }

    return result;
}

int kv_cache_delete(kv_cache_t *cache, const char *key, size_t key_len) {
    if (!cache || cache->magic != KV_CACHE_MAGIC || cache->state != KV_CACHE_STATE_RUNNING) {
        return KV_ERR_INVALID_PARAM;
    }
    
    if (!key || key_len == 0) {
        return KV_ERR_INVALID_PARAM;
    }

    if (key_len > KV_CACHE_MAX_KEY_LENGTH) {
        return KV_ERR_KEY_TOO_LONG;
    }

    // 获取写锁
    if (pthread_rwlock_wrlock(&cache->cache_lock) != 0) {
        return KV_ERR_LOCK_FAILED;
    }

    int result = KV_SUCCESS;

    // 查找并删除条目
    kv_cache_entry_t *entry = kv_cache_hashtable_find(cache->hashtable, key, key_len);
    if (!entry) {
        result = KV_ERR_KEY_NOT_FOUND;
    } else {
        // 从哈希表删除
        kv_cache_hashtable_remove(cache->hashtable, key, key_len);
        
        // 从LRU链表删除
        kv_cache_lru_list_remove_entry(cache->lru_list, entry);
        
        // 销毁条目
        kv_cache_entry_destroy(cache, entry);
        
        cache->stats.current_entries--;
        cache->stats.delete_count++;
    }

    pthread_rwlock_unlock(&cache->cache_lock);

    return result;
}

bool kv_cache_exists(kv_cache_t *cache, const char *key, size_t key_len) {
    if (!cache || cache->magic != KV_CACHE_MAGIC || cache->state != KV_CACHE_STATE_RUNNING) {
        return false;
    }
    
    if (!key || key_len == 0 || key_len > KV_CACHE_MAX_KEY_LENGTH) {
        return false;
    }

    // 获取读锁
    if (pthread_rwlock_rdlock(&cache->cache_lock) != 0) {
        return false;
    }

    bool exists = false;
    kv_cache_entry_t *entry = kv_cache_hashtable_find(cache->hashtable, key, key_len);
    if (entry) {
        uint64_t current_time = cache->get_time_func();
        exists = !kv_cache_is_expired(entry, current_time);
    }

    pthread_rwlock_unlock(&cache->cache_lock);

    return exists;
}

int kv_cache_clear(kv_cache_t *cache) {
    if (!cache || cache->magic != KV_CACHE_MAGIC) {
        return KV_ERR_INVALID_PARAM;
    }

    // 获取写锁
    if (pthread_rwlock_wrlock(&cache->cache_lock) != 0) {
        return KV_ERR_LOCK_FAILED;
    }

    // 清空哈希表和LRU链表
    kv_cache_hashtable_destroy(cache, cache->hashtable);
    kv_cache_lru_list_destroy(cache->lru_list);

    // 重新创建数据结构
    cache->hashtable = kv_cache_hashtable_create(cache->config.hash_table_size, cache->config.hash_seed);
    cache->lru_list = kv_cache_lru_list_create();

    if (!cache->hashtable || !cache->lru_list) {
        pthread_rwlock_unlock(&cache->cache_lock);
        return KV_ERR_MEMORY_ALLOC;
    }

    // 重置统计信息
    uint64_t start_time = cache->stats.start_time;
    size_t max_entries = cache->stats.max_entries;
    size_t max_memory = cache->stats.max_memory;
    
    memset(&cache->stats, 0, sizeof(kv_cache_stats_t));
    cache->stats.start_time = start_time;
    cache->stats.max_entries = max_entries;
    cache->stats.max_memory = max_memory;

    pthread_rwlock_unlock(&cache->cache_lock);

    return KV_SUCCESS;
}

// === 统计和工具函数实现 ===

int kv_cache_get_stats(kv_cache_t *cache, kv_cache_stats_t *stats_out) {
    if (!cache || cache->magic != KV_CACHE_MAGIC || !stats_out) {
        return KV_ERR_INVALID_PARAM;
    }

    if (pthread_rwlock_rdlock(&cache->cache_lock) != 0) {
        return KV_ERR_LOCK_FAILED;
    }

    memcpy(stats_out, &cache->stats, sizeof(kv_cache_stats_t));

    pthread_rwlock_unlock(&cache->cache_lock);

    return KV_SUCCESS;
}

int kv_cache_reset_stats(kv_cache_t *cache) {
    if (!cache || cache->magic != KV_CACHE_MAGIC) {
        return KV_ERR_INVALID_PARAM;
    }

    if (pthread_rwlock_wrlock(&cache->cache_lock) != 0) {
        return KV_ERR_LOCK_FAILED;
    }

    // 保留基本配置信息
    size_t current_entries = cache->stats.current_entries;
    size_t max_entries = cache->stats.max_entries;
    size_t current_memory = cache->stats.current_memory;
    size_t max_memory = cache->stats.max_memory;

    // 重置统计信息
    memset(&cache->stats, 0, sizeof(kv_cache_stats_t));
    cache->stats.start_time = cache->get_time_func();
    cache->stats.current_entries = current_entries;
    cache->stats.max_entries = max_entries;
    cache->stats.current_memory = current_memory;
    cache->stats.max_memory = max_memory;

    pthread_rwlock_unlock(&cache->cache_lock);

    return KV_SUCCESS;
}

// === 高级功能函数实现 ===

int kv_cache_cleanup_expired(kv_cache_t *cache) {
    if (!cache || cache->magic != KV_CACHE_MAGIC) {
        return KV_ERR_INVALID_PARAM;
    }

    if (pthread_rwlock_wrlock(&cache->cache_lock) != 0) {
        return KV_ERR_LOCK_FAILED;
    }

    int expired_count = kv_cache_cleanup_expired_entries(cache);

    pthread_rwlock_unlock(&cache->cache_lock);

    return expired_count;
}

int kv_cache_evict_entries(kv_cache_t *cache, size_t count) {
    if (!cache || cache->magic != KV_CACHE_MAGIC || count == 0) {
        return KV_ERR_INVALID_PARAM;
    }

    if (pthread_rwlock_wrlock(&cache->cache_lock) != 0) {
        return KV_ERR_LOCK_FAILED;
    }

    int result = kv_cache_evict_lru_entries(cache, count);

    pthread_rwlock_unlock(&cache->cache_lock);

    return result;
}

bool kv_cache_validate_integrity(kv_cache_t *cache) {
    if (!cache || cache->magic != KV_CACHE_MAGIC) {
        return false;
    }

    if (pthread_rwlock_rdlock(&cache->cache_lock) != 0) {
        return false;
    }

    bool is_valid = true;

    // 检查哈希表完整性
    if (!cache->hashtable || !cache->hashtable->buckets) {
        is_valid = false;
        goto cleanup;
    }

    // 检查LRU链表完整性
    if (!cache->lru_list) {
        is_valid = false;
        goto cleanup;
    }

    // 验证统计信息一致性
    size_t hashtable_entries = cache->hashtable->entry_count;
    size_t lru_entries = cache->lru_list->size;
    size_t stats_entries = cache->stats.current_entries;

    if (hashtable_entries != lru_entries || hashtable_entries != stats_entries) {
        is_valid = false;
        goto cleanup;
    }

    // 验证LRU链表结构
    if (cache->lru_list->size > 0) {
        if (!cache->lru_list->head || !cache->lru_list->tail) {
            is_valid = false;
            goto cleanup;
        }

        // 检查链表头部
        if (cache->lru_list->head->prev != NULL) {
            is_valid = false;
            goto cleanup;
        }

        // 检查链表尾部
        if (cache->lru_list->tail->next != NULL) {
            is_valid = false;
            goto cleanup;
        }
    }

cleanup:
    pthread_rwlock_unlock(&cache->cache_lock);
    return is_valid;
}

int kv_cache_get_memory_usage(kv_cache_t *cache, 
                              size_t *total_out, size_t *data_out, size_t *meta_out) {
    if (!cache || cache->magic != KV_CACHE_MAGIC || 
        !total_out || !data_out || !meta_out) {
        return KV_ERR_INVALID_PARAM;
    }

    if (pthread_rwlock_rdlock(&cache->cache_lock) != 0) {
        return KV_ERR_LOCK_FAILED;
    }

    size_t data_memory = 0;
    size_t meta_memory = 0;

    // 计算数据内存使用
    for (size_t i = 0; i < cache->hashtable->bucket_count; i++) {
        kv_cache_entry_t *entry = cache->hashtable->buckets[i];
        while (entry) {
            data_memory += entry->key_len + entry->value_len;
            meta_memory += sizeof(kv_cache_entry_t);
            entry = entry->hash_next;
        }
    }

    // 计算结构体内存使用
    meta_memory += sizeof(kv_cache_t);
    meta_memory += sizeof(kv_cache_hashtable_t);
    meta_memory += cache->hashtable->bucket_count * sizeof(kv_cache_entry_t*);
    meta_memory += sizeof(kv_cache_lru_list_t);

    *data_out = data_memory;
    *meta_out = meta_memory;
    *total_out = data_memory + meta_memory;

    // 更新统计信息
    cache->stats.current_memory = *total_out;

    pthread_rwlock_unlock(&cache->cache_lock);

    return KV_SUCCESS;
}

// === LFU频率管理器实现 ===

static kv_cache_frequency_manager_t* kv_cache_frequency_manager_create(size_t bucket_count) {
    kv_cache_frequency_manager_t *freq_mgr = malloc(sizeof(kv_cache_frequency_manager_t));
    if (!freq_mgr) {
        return NULL;
    }

    freq_mgr->frequency_buckets = calloc(bucket_count, sizeof(uint64_t));
    if (!freq_mgr->frequency_buckets) {
        free(freq_mgr);
        return NULL;
    }

    freq_mgr->bucket_count = bucket_count;
    freq_mgr->min_frequency = 1;
    freq_mgr->max_frequency = 1;
    freq_mgr->total_accesses = 0;

    if (pthread_mutex_init(&freq_mgr->freq_lock, NULL) != 0) {
        free(freq_mgr->frequency_buckets);
        free(freq_mgr);
        return NULL;
    }

    return freq_mgr;
}

static void kv_cache_frequency_manager_destroy(kv_cache_t *cache, kv_cache_frequency_manager_t *freq_mgr) {
    if (!freq_mgr) {
        return;
    }

    pthread_mutex_destroy(&freq_mgr->freq_lock);
    kv_cache_free(cache, freq_mgr->frequency_buckets);
    kv_cache_free(cache, freq_mgr);
}

static void kv_cache_frequency_manager_increment(kv_cache_frequency_manager_t *freq_mgr, kv_cache_entry_t *entry) {
    if (!freq_mgr || !entry) {
        return;
    }

    pthread_mutex_lock(&freq_mgr->freq_lock);
    
    uint64_t current_time = kv_cache_get_current_time_us();
    
    // 频率衰减：如果距离上次更新超过衰减时间，则进行衰减
    if (current_time > entry->frequency_time + 3600000000ULL) { // 1小时衰减
        entry->frequency = (entry->frequency + 1) / 2; // 衰减50%
        if (entry->frequency == 0) {
            entry->frequency = 1;
        }
    }
    
    // 增加访问频率
    entry->frequency++;
    entry->frequency_time = current_time;
    freq_mgr->total_accesses++;
    
    // 更新最大频率
    if (entry->frequency > freq_mgr->max_frequency) {
        freq_mgr->max_frequency = entry->frequency;
    }
    
    pthread_mutex_unlock(&freq_mgr->freq_lock);
}

static kv_cache_entry_t* kv_cache_frequency_manager_find_lfu_entry(kv_cache_t *cache) {
    if (!cache || !cache->hashtable) {
        return NULL;
    }

    kv_cache_entry_t *lfu_entry = NULL;
    uint64_t min_frequency = UINT64_MAX;

    // 遍历哈希表找到频率最低的条目
    for (size_t i = 0; i < cache->hashtable->bucket_count; i++) {
        kv_cache_entry_t *entry = cache->hashtable->buckets[i];
        while (entry) {
            if (entry->frequency < min_frequency) {
                min_frequency = entry->frequency;
                lfu_entry = entry;
            }
            entry = entry->hash_next;
        }
    }

    return lfu_entry;
}

static void kv_cache_frequency_manager_decay(kv_cache_frequency_manager_t *freq_mgr) {
    if (!freq_mgr) {
        return;
    }

    pthread_mutex_lock(&freq_mgr->freq_lock);
    
    // 简单的频率衰减：所有频率除以2
    for (size_t i = 0; i < freq_mgr->bucket_count; i++) {
        freq_mgr->frequency_buckets[i] /= 2;
    }
    
    // 更新最小和最大频率
    freq_mgr->min_frequency = (freq_mgr->min_frequency > 1) ? freq_mgr->min_frequency / 2 : 1;
    freq_mgr->max_frequency = (freq_mgr->max_frequency > 1) ? freq_mgr->max_frequency / 2 : 1;
    
    pthread_mutex_unlock(&freq_mgr->freq_lock);
}

// === CLOCK算法管理器实现 ===

static kv_cache_clock_manager_t* kv_cache_clock_manager_create(size_t capacity) {
    kv_cache_clock_manager_t *clock_mgr = malloc(sizeof(kv_cache_clock_manager_t));
    if (!clock_mgr) {
        return NULL;
    }

    clock_mgr->entries = calloc(capacity, sizeof(kv_cache_entry_t*));
    if (!clock_mgr->entries) {
        free(clock_mgr);
        return NULL;
    }

    clock_mgr->capacity = capacity;
    clock_mgr->current_size = 0;
    clock_mgr->clock_hand = 0;

    if (pthread_mutex_init(&clock_mgr->clock_lock, NULL) != 0) {
        free(clock_mgr->entries);
        free(clock_mgr);
        return NULL;
    }

    return clock_mgr;
}

static void kv_cache_clock_manager_destroy(kv_cache_t *cache, kv_cache_clock_manager_t *clock_mgr) {
    if (!clock_mgr) {
        return;
    }

    pthread_mutex_destroy(&clock_mgr->clock_lock);
    kv_cache_free(cache, clock_mgr->entries);
    kv_cache_free(cache, clock_mgr);
}

static int kv_cache_clock_manager_add(kv_cache_clock_manager_t *clock_mgr, kv_cache_entry_t *entry) {
    if (!clock_mgr || !entry) {
        return -1;
    }

    pthread_mutex_lock(&clock_mgr->clock_lock);

    if (clock_mgr->current_size < clock_mgr->capacity) {
        // 找到第一个空位置
        for (size_t i = 0; i < clock_mgr->capacity; i++) {
            if (clock_mgr->entries[i] == NULL) {
                clock_mgr->entries[i] = entry;
                entry->reference_bit = 1; // 新加入的条目设置引用位
                clock_mgr->current_size++;
                break;
            }
        }
    }

    pthread_mutex_unlock(&clock_mgr->clock_lock);
    return 0;
}

static kv_cache_entry_t* kv_cache_clock_manager_evict(kv_cache_clock_manager_t *clock_mgr) {
    if (!clock_mgr || clock_mgr->current_size == 0) {
        return NULL;
    }

    pthread_mutex_lock(&clock_mgr->clock_lock);

    kv_cache_entry_t *victim = NULL;
    size_t start_hand = clock_mgr->clock_hand;

    // CLOCK算法：寻找引用位为0的条目
    do {
        kv_cache_entry_t *entry = clock_mgr->entries[clock_mgr->clock_hand];
        
        if (entry != NULL) {
            if (entry->reference_bit == 0) {
                // 找到受害者
                victim = entry;
                clock_mgr->entries[clock_mgr->clock_hand] = NULL;
                clock_mgr->current_size--;
                break;
            } else {
                // 清除引用位，给第二次机会
                entry->reference_bit = 0;
            }
        }
        
        // 移动时钟指针
        clock_mgr->clock_hand = (clock_mgr->clock_hand + 1) % clock_mgr->capacity;
        
    } while (clock_mgr->clock_hand != start_hand);

    pthread_mutex_unlock(&clock_mgr->clock_lock);
    return victim;
}

static void kv_cache_clock_manager_access(kv_cache_clock_manager_t *clock_mgr, kv_cache_entry_t *entry) {
    if (!clock_mgr || !entry) {
        return;
    }

    pthread_mutex_lock(&clock_mgr->clock_lock);
    
    // 设置引用位，表示最近被访问
    entry->reference_bit = 1;
    
    pthread_mutex_unlock(&clock_mgr->clock_lock);
}

// === ARC算法管理器实现 ===

static kv_cache_arc_manager_t* kv_cache_arc_manager_create(size_t capacity) {
    kv_cache_arc_manager_t *arc_mgr = malloc(sizeof(kv_cache_arc_manager_t));
    if (!arc_mgr) {
        return NULL;
    }

    // 创建四个LRU链表
    arc_mgr->t1_list = kv_cache_lru_list_create();
    arc_mgr->t2_list = kv_cache_lru_list_create();
    arc_mgr->b1_list = kv_cache_lru_list_create();
    arc_mgr->b2_list = kv_cache_lru_list_create();

    if (!arc_mgr->t1_list || !arc_mgr->t2_list || 
        !arc_mgr->b1_list || !arc_mgr->b2_list) {
        kv_cache_lru_list_destroy(arc_mgr->t1_list);
        kv_cache_lru_list_destroy(arc_mgr->t2_list);
        kv_cache_lru_list_destroy(arc_mgr->b1_list);
        kv_cache_lru_list_destroy(arc_mgr->b2_list);
        free(arc_mgr);
        return NULL;
    }

    arc_mgr->c = capacity;
    arc_mgr->p = 0; // 初始自适应参数

    if (pthread_mutex_init(&arc_mgr->arc_lock, NULL) != 0) {
        kv_cache_lru_list_destroy(arc_mgr->t1_list);
        kv_cache_lru_list_destroy(arc_mgr->t2_list);
        kv_cache_lru_list_destroy(arc_mgr->b1_list);
        kv_cache_lru_list_destroy(arc_mgr->b2_list);
        free(arc_mgr);
        return NULL;
    }

    return arc_mgr;
}

static void kv_cache_arc_manager_destroy(kv_cache_t *cache, kv_cache_arc_manager_t *arc_mgr) {
    if (!arc_mgr) {
        return;
    }

    pthread_mutex_destroy(&arc_mgr->arc_lock);
    kv_cache_lru_list_destroy(arc_mgr->t1_list);
    kv_cache_lru_list_destroy(arc_mgr->t2_list);
    kv_cache_lru_list_destroy(arc_mgr->b1_list);
    kv_cache_lru_list_destroy(arc_mgr->b2_list);
    kv_cache_free(cache, arc_mgr);
}

static int kv_cache_arc_manager_access(kv_cache_arc_manager_t *arc_mgr, kv_cache_entry_t *entry) {
    if (!arc_mgr || !entry) {
        return -1;
    }

    pthread_mutex_lock(&arc_mgr->arc_lock);

    // ARC算法的核心逻辑
    switch (entry->arc_list_type) {
        case 0: // T1 -> T2 (从最近访问提升到频繁访问)
            kv_cache_lru_list_remove_entry(arc_mgr->t1_list, entry);
            kv_cache_lru_list_add_to_head(arc_mgr->t2_list, entry);
            entry->arc_list_type = 1;
            break;
            
        case 1: // T2 -> T2头部 (在频繁访问列表中移到最前)
            kv_cache_lru_list_move_to_head(arc_mgr->t2_list, entry);
            break;
            
        case 2: // B1 -> T2 (从历史记录恢复到频繁访问)
            kv_cache_lru_list_remove_entry(arc_mgr->b1_list, entry);
            kv_cache_lru_list_add_to_head(arc_mgr->t2_list, entry);
            entry->arc_list_type = 1;
            // 调整自适应参数p
            arc_mgr->p = (arc_mgr->p + 1 < arc_mgr->c) ? arc_mgr->p + 1 : arc_mgr->c;
            break;
            
        case 3: // B2 -> T2 (从历史记录恢复到频繁访问)
            kv_cache_lru_list_remove_entry(arc_mgr->b2_list, entry);
            kv_cache_lru_list_add_to_head(arc_mgr->t2_list, entry);
            entry->arc_list_type = 1;
            // 调整自适应参数p
            arc_mgr->p = (arc_mgr->p > 0) ? arc_mgr->p - 1 : 0;
            break;
            
        default: // 新条目加入T1
            kv_cache_lru_list_add_to_head(arc_mgr->t1_list, entry);
            entry->arc_list_type = 0;
            break;
    }

    pthread_mutex_unlock(&arc_mgr->arc_lock);
    return 0;
}

static kv_cache_entry_t* kv_cache_arc_manager_evict(kv_cache_arc_manager_t *arc_mgr) {
    if (!arc_mgr) {
        return NULL;
    }

    pthread_mutex_lock(&arc_mgr->arc_lock);

    kv_cache_entry_t *victim = NULL;
    
    // ARC淘汰策略：根据p参数决定从T1还是T2淘汰
    if (arc_mgr->t1_list->size > arc_mgr->p) {
        // 从T1淘汰
        victim = kv_cache_lru_list_remove_tail(arc_mgr->t1_list);
        if (victim) {
            // 注意：这里不将victim移动到B1，因为victim将被销毁
            // B1/B2列表应该只保存键的历史记录，不保存实际数据
            victim->arc_list_type = 255; // 标记为已淘汰
        }
    } else {
        // 从T2淘汰
        victim = kv_cache_lru_list_remove_tail(arc_mgr->t2_list);
        if (victim) {
            // 注意：这里不将victim移动到B2，因为victim将被销毁
            victim->arc_list_type = 255; // 标记为已淘汰
        }
    }

    pthread_mutex_unlock(&arc_mgr->arc_lock);
    return victim;
}

void kv_cache_print_stats(kv_cache_t *cache, FILE *fp) {
    if (!cache || !fp) {
        return;
    }

    kv_cache_stats_t stats;
    if (kv_cache_get_stats(cache, &stats) != KV_SUCCESS) {
        fprintf(fp, "Failed to get cache statistics\n");
        return;
    }

    fprintf(fp, "\n=== ConcordKV Cache Statistics ===\n");
    fprintf(fp, "Cache Name: %s\n", cache->name);
    fprintf(fp, "Policy: %s\n", kv_cache_policy_name(cache->config.policy));
    fprintf(fp, "State: %s\n", kv_cache_state_name(cache->state));
    
    fprintf(fp, "\n--- Basic Stats ---\n");
    fprintf(fp, "Total Requests: %llu\n", (unsigned long long)stats.total_requests);
    fprintf(fp, "Cache Hits: %llu\n", (unsigned long long)stats.cache_hits);
    fprintf(fp, "Cache Misses: %llu\n", (unsigned long long)stats.cache_misses);
    fprintf(fp, "Hit Ratio: %.2f%%\n", kv_cache_hit_ratio(&stats) * 100.0);
    
    fprintf(fp, "\n--- Operations ---\n");
    fprintf(fp, "GET Operations: %llu\n", (unsigned long long)stats.get_count);
    fprintf(fp, "SET Operations: %llu\n", (unsigned long long)stats.set_count);
    fprintf(fp, "DELETE Operations: %llu\n", (unsigned long long)stats.delete_count);
    fprintf(fp, "UPDATE Operations: %llu\n", (unsigned long long)stats.update_count);
    
    fprintf(fp, "\n--- Performance ---\n");
    fprintf(fp, "Avg GET Time: %llu ns\n", (unsigned long long)stats.avg_get_time);
    fprintf(fp, "Avg SET Time: %llu ns\n", (unsigned long long)stats.avg_set_time);
    fprintf(fp, "Max GET Time: %llu ns\n", (unsigned long long)stats.max_get_time);
    fprintf(fp, "Max SET Time: %llu ns\n", (unsigned long long)stats.max_set_time);
    
    fprintf(fp, "\n--- Memory Usage ---\n");
    fprintf(fp, "Current Entries: %zu\n", stats.current_entries);
    fprintf(fp, "Max Entries: %zu\n", stats.max_entries);
    fprintf(fp, "Current Memory: %zu bytes\n", stats.current_memory);
    fprintf(fp, "Max Memory: %zu bytes\n", stats.max_memory);
    
    fprintf(fp, "\n--- Maintenance ---\n");
    fprintf(fp, "Evictions: %llu\n", (unsigned long long)stats.evictions);
    fprintf(fp, "Expirations: %llu\n", (unsigned long long)stats.expirations);
    
    // 运行时间
    uint64_t current_time = kv_cache_get_current_time_us();
    uint64_t runtime = current_time - stats.start_time;
    fprintf(fp, "Runtime: %.2f seconds\n", (double)runtime / 1000000.0);
    
    if (stats.last_cleanup_time > 0) {
        uint64_t last_cleanup = current_time - stats.last_cleanup_time;
        fprintf(fp, "Last Cleanup: %.2f seconds ago\n", (double)last_cleanup / 1000000.0);
    }
    
    fprintf(fp, "================================\n\n");
} 