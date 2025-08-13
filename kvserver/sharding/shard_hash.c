/*
 * @Author: Lzww0608
 * @Date: 2025-6-25 12:58:13
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-25 12:58:13
 * @Description: ConcordKV 分片哈希计算模块实现 - 高性能哈希路由与缓存
 */

#include "shard_hash.h"
#include "../kv_memory.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
#include <assert.h>

// 调试宏
#ifdef DEBUG
#define SHARD_HASH_DEBUG(fmt, ...) printf("[SHARD_HASH_DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define SHARD_HASH_DEBUG(fmt, ...)
#endif

// === Murmur3哈希算法实现 ===

#define ROTL32(x, r) ((x << r) | (x >> (32 - r)))
#define ROTL64(x, r) ((x << r) | (x >> (64 - r)))

uint32_t murmur3_hash32(const void *key, int len, uint32_t seed) {
    const uint8_t *data = (const uint8_t*)key;
    const int nblocks = len / 4;
    
    uint32_t h1 = seed;
    
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;
    
    // body
    const uint32_t *blocks = (const uint32_t *)(data + nblocks * 4);
    
    for (int i = -nblocks; i; i++) {
        uint32_t k1 = blocks[i];
        
        k1 *= c1;
        k1 = ROTL32(k1, 15);
        k1 *= c2;
        
        h1 ^= k1;
        h1 = ROTL32(h1, 13);
        h1 = h1 * 5 + 0xe6546b64;
    }
    
    // tail
    const uint8_t *tail = (const uint8_t*)(data + nblocks * 4);
    uint32_t k1 = 0;
    
    switch (len & 3) {
        case 3: k1 ^= tail[2] << 16; __attribute__((fallthrough));
        case 2: k1 ^= tail[1] << 8; __attribute__((fallthrough));
        case 1: k1 ^= tail[0];
                k1 *= c1;
                k1 = ROTL32(k1, 15);
                k1 *= c2;
                h1 ^= k1;
                break;
    }
    
    // finalization
    h1 ^= len;
    h1 ^= h1 >> 16;
    h1 *= 0x85ebca6b;
    h1 ^= h1 >> 13;
    h1 *= 0xc2b2ae35;
    h1 ^= h1 >> 16;
    
    return h1;
}

uint64_t murmur3_hash64(const void *key, int len, uint32_t seed) {
    const uint8_t *data = (const uint8_t*)key;
    const int nblocks = len / 16;
    
    uint64_t h1 = seed;
    uint64_t h2 = seed;
    
    const uint64_t c1 = 0x87c37b91114253d5ULL;
    const uint64_t c2 = 0x4cf5ad432745937fULL;
    
    // body
    const uint64_t *blocks = (const uint64_t *)(data);
    
    for (int i = 0; i < nblocks; i++) {
        uint64_t k1 = blocks[i * 2 + 0];
        uint64_t k2 = blocks[i * 2 + 1];
        
        k1 *= c1; k1 = ROTL64(k1, 31); k1 *= c2; h1 ^= k1;
        h1 = ROTL64(h1, 27); h1 += h2; h1 = h1 * 5 + 0x52dce729;
        
        k2 *= c2; k2 = ROTL64(k2, 33); k2 *= c1; h2 ^= k2;
        h2 = ROTL64(h2, 31); h2 += h1; h2 = h2 * 5 + 0x38495ab5;
    }
    
    // tail
    const uint8_t *tail = (const uint8_t*)(data + nblocks * 16);
    uint64_t k1 = 0;
    uint64_t k2 = 0;
    
    switch (len & 15) {
        case 15: k2 ^= ((uint64_t)tail[14]) << 48; __attribute__((fallthrough));
        case 14: k2 ^= ((uint64_t)tail[13]) << 40; __attribute__((fallthrough));
        case 13: k2 ^= ((uint64_t)tail[12]) << 32; __attribute__((fallthrough));
        case 12: k2 ^= ((uint64_t)tail[11]) << 24; __attribute__((fallthrough));
        case 11: k2 ^= ((uint64_t)tail[10]) << 16; __attribute__((fallthrough));
        case 10: k2 ^= ((uint64_t)tail[9]) << 8; __attribute__((fallthrough));
        case  9: k2 ^= ((uint64_t)tail[8]) << 0;
                 k2 *= c2; k2 = ROTL64(k2, 33); k2 *= c1; h2 ^= k2;
                 __attribute__((fallthrough));
        
        case  8: k1 ^= ((uint64_t)tail[7]) << 56; __attribute__((fallthrough));
        case  7: k1 ^= ((uint64_t)tail[6]) << 48; __attribute__((fallthrough));
        case  6: k1 ^= ((uint64_t)tail[5]) << 40; __attribute__((fallthrough));
        case  5: k1 ^= ((uint64_t)tail[4]) << 32; __attribute__((fallthrough));
        case  4: k1 ^= ((uint64_t)tail[3]) << 24; __attribute__((fallthrough));
        case  3: k1 ^= ((uint64_t)tail[2]) << 16; __attribute__((fallthrough));
        case  2: k1 ^= ((uint64_t)tail[1]) << 8; __attribute__((fallthrough));
        case  1: k1 ^= ((uint64_t)tail[0]) << 0;
                 k1 *= c1; k1 = ROTL64(k1, 31); k1 *= c2; h1 ^= k1;
                 break;
    }
    
    // finalization
    h1 ^= len; h2 ^= len;
    
    h1 += h2;
    h2 += h1;
    
    h1 ^= h1 >> 33;
    h1 *= 0xff51afd7ed558ccdULL;
    h1 ^= h1 >> 33;
    h1 *= 0xc4ceb9fe1a85ec53ULL;
    h1 ^= h1 >> 33;
    
    h2 ^= h2 >> 33;
    h2 *= 0xff51afd7ed558ccdULL;
    h2 ^= h2 >> 33;
    h2 *= 0xc4ceb9fe1a85ec53ULL;
    h2 ^= h2 >> 33;
    
    h1 += h2;
    
    return h1;
}

// === 分片哈希管理器实现 ===

shard_hash_manager_t* shard_hash_manager_create(uint32_t shard_count, 
                                               size_t cache_size, 
                                               uint32_t cache_ttl) {
    if (shard_count == 0 || cache_size == 0) {
        return NULL;
    }
    
    shard_hash_manager_t *manager = kv_store_malloc(sizeof(shard_hash_manager_t));
    if (!manager) {
        return NULL;
    }
    
    memset(manager, 0, sizeof(shard_hash_manager_t));
    
    // 初始化缓存
    manager->cache = kv_store_malloc(sizeof(shard_hash_cache_entry_t) * cache_size);
    if (!manager->cache) {
        kv_store_free(manager);
        return NULL;
    }
    memset(manager->cache, 0, sizeof(shard_hash_cache_entry_t) * cache_size);
    
    manager->cache_size = cache_size;
    manager->cache_count = 0;
    manager->cache_ttl = cache_ttl;
    
    // 初始化分片配置
    manager->shard_count = shard_count;
    manager->virtual_nodes = 160; // 默认虚拟节点数
    
    // 初始化分片分布统计
    manager->shard_distribution = kv_store_malloc(sizeof(uint64_t) * shard_count);
    if (!manager->shard_distribution) {
        kv_store_free(manager->cache);
        kv_store_free(manager);
        return NULL;
    }
    memset(manager->shard_distribution, 0, sizeof(uint64_t) * shard_count);
    
    // 初始化锁
    if (pthread_rwlock_init(&manager->cache_lock, NULL) != 0) {
        kv_store_free(manager->shard_distribution);
        kv_store_free(manager->cache);
        kv_store_free(manager);
        return NULL;
    }
    
    if (pthread_mutex_init(&manager->stats_lock, NULL) != 0) {
        pthread_rwlock_destroy(&manager->cache_lock);
        kv_store_free(manager->shard_distribution);
        kv_store_free(manager->cache);
        kv_store_free(manager);
        return NULL;
    }
    
    SHARD_HASH_DEBUG("Created shard hash manager: shards=%u, cache_size=%zu, ttl=%u", 
                     shard_count, cache_size, cache_ttl);
    
    return manager;
}

void shard_hash_manager_destroy(shard_hash_manager_t *manager) {
    if (!manager) return;
    
    // 清理缓存中的字符串
    pthread_rwlock_wrlock(&manager->cache_lock);
    for (size_t i = 0; i < manager->cache_size; i++) {
        if (manager->cache[i].key) {
            kv_store_free(manager->cache[i].key);
        }
    }
    pthread_rwlock_unlock(&manager->cache_lock);
    
    // 销毁锁
    pthread_rwlock_destroy(&manager->cache_lock);
    pthread_mutex_destroy(&manager->stats_lock);
    
    // 释放内存
    kv_store_free(manager->cache);
    kv_store_free(manager->shard_distribution);
    kv_store_free(manager);
    
    SHARD_HASH_DEBUG("Destroyed shard hash manager");
}

uint64_t shard_hash_compute(const char *key, size_t key_len) {
    if (!key || key_len == 0) {
        return 0;
    }
    
    return murmur3_hash64(key, (int)key_len, SHARD_HASH_SEED);
}

uint32_t shard_hash_to_shard_id(uint64_t hash, uint32_t shard_count) {
    if (shard_count == 0) {
        return 0;
    }
    
    return (uint32_t)(hash % shard_count);
}

// 缓存查找辅助函数
static int shard_hash_cache_find(shard_hash_manager_t *manager, const char *key, size_t key_len) {
    time_t now = time(NULL);
    
    for (size_t i = 0; i < manager->cache_size; i++) {
        shard_hash_cache_entry_t *entry = &manager->cache[i];
        
        if (!entry->valid || !entry->key) {
            continue;
        }
        
        // 检查TTL
        if (now - entry->timestamp > manager->cache_ttl) {
            entry->valid = false;
            continue;
        }
        
        // 检查键匹配
        if (strlen(entry->key) == key_len && memcmp(entry->key, key, key_len) == 0) {
            return (int)i;
        }
    }
    
    return -1;
}

// 缓存插入辅助函数
static int shard_hash_cache_insert(shard_hash_manager_t *manager, 
                                  const char *key, size_t key_len, 
                                  uint64_t hash, uint32_t shard_id) {
    time_t now = time(NULL);
    int insert_idx = -1;
    
    // 首先查找空闲或过期的slot
    for (size_t i = 0; i < manager->cache_size; i++) {
        shard_hash_cache_entry_t *entry = &manager->cache[i];
        
        if (!entry->valid || !entry->key || 
            (now - entry->timestamp > manager->cache_ttl)) {
            insert_idx = (int)i;
            break;
        }
    }
    
    // 如果没有空闲slot，使用LRU策略
    if (insert_idx == -1) {
        time_t oldest_time = now;
        for (size_t i = 0; i < manager->cache_size; i++) {
            if (manager->cache[i].timestamp < oldest_time) {
                oldest_time = manager->cache[i].timestamp;
                insert_idx = (int)i;
            }
        }
    }
    
    if (insert_idx == -1) {
        return -1; // 缓存已满
    }
    
    shard_hash_cache_entry_t *entry = &manager->cache[insert_idx];
    
    // 清理旧的键
    if (entry->key) {
        kv_store_free(entry->key);
    }
    
    // 插入新条目
    entry->key = kv_store_malloc(key_len + 1);
    if (!entry->key) {
        return -1;
    }
    
    memcpy(entry->key, key, key_len);
    entry->key[key_len] = '\0';
    entry->hash = hash;
    entry->shard_id = shard_id;
    entry->timestamp = now;
    entry->valid = true;
    
    if ((size_t)insert_idx >= manager->cache_count) {
        manager->cache_count = insert_idx + 1;
    }
    
    return insert_idx;
}

uint32_t shard_hash_get_shard_id(shard_hash_manager_t *manager, 
                                const char *key, 
                                size_t key_len) {
    if (!manager || !key || key_len == 0) {
        return 0;
    }
    
    // 更新统计
    pthread_mutex_lock(&manager->stats_lock);
    manager->total_hash_requests++;
    pthread_mutex_unlock(&manager->stats_lock);
    
    // 首先尝试缓存查找
    pthread_rwlock_rdlock(&manager->cache_lock);
    int cache_idx = shard_hash_cache_find(manager, key, key_len);
    if (cache_idx >= 0) {
        uint32_t shard_id = manager->cache[cache_idx].shard_id;
        pthread_rwlock_unlock(&manager->cache_lock);
        
        // 更新缓存命中统计
        pthread_mutex_lock(&manager->stats_lock);
        manager->cache_hits++;
        pthread_mutex_unlock(&manager->stats_lock);
        
        // 更新分片分布统计
        if (shard_id < manager->shard_count) {
            pthread_mutex_lock(&manager->stats_lock);
            manager->shard_distribution[shard_id]++;
            pthread_mutex_unlock(&manager->stats_lock);
        }
        
        return shard_id;
    }
    pthread_rwlock_unlock(&manager->cache_lock);
    
    // 缓存未命中，计算哈希
    pthread_mutex_lock(&manager->stats_lock);
    manager->cache_misses++;
    pthread_mutex_unlock(&manager->stats_lock);
    
    uint64_t hash = shard_hash_compute(key, key_len);
    uint32_t shard_id = shard_hash_to_shard_id(hash, manager->shard_count);
    
    // 插入缓存
    pthread_rwlock_wrlock(&manager->cache_lock);
    shard_hash_cache_insert(manager, key, key_len, hash, shard_id);
    pthread_rwlock_unlock(&manager->cache_lock);
    
    // 更新分片分布统计
    if (shard_id < manager->shard_count) {
        pthread_mutex_lock(&manager->stats_lock);
        manager->shard_distribution[shard_id]++;
        pthread_mutex_unlock(&manager->stats_lock);
    }
    
    SHARD_HASH_DEBUG("Computed shard for key '%.*s': hash=%lu, shard=%u", 
                     (int)key_len, key, hash, shard_id);
    
    return shard_id;
}

int shard_hash_batch_get_shard_ids(shard_hash_manager_t *manager,
                                  const char **keys,
                                  const size_t *key_lens,
                                  size_t count,
                                  uint32_t *shard_ids) {
    if (!manager || !keys || !key_lens || !shard_ids || count == 0) {
        return -1;
    }
    
    for (size_t i = 0; i < count; i++) {
        shard_ids[i] = shard_hash_get_shard_id(manager, keys[i], key_lens[i]);
    }
    
    return 0;
}

void shard_hash_cache_clear(shard_hash_manager_t *manager) {
    if (!manager) return;
    
    pthread_rwlock_wrlock(&manager->cache_lock);
    
    for (size_t i = 0; i < manager->cache_size; i++) {
        shard_hash_cache_entry_t *entry = &manager->cache[i];
        if (entry->key) {
            kv_store_free(entry->key);
            entry->key = NULL;
        }
        entry->valid = false;
    }
    
    manager->cache_count = 0;
    
    pthread_rwlock_unlock(&manager->cache_lock);
    
    SHARD_HASH_DEBUG("Cleared hash cache");
}

void shard_hash_cache_set_ttl(shard_hash_manager_t *manager, uint32_t ttl) {
    if (!manager) return;
    
    pthread_rwlock_wrlock(&manager->cache_lock);
    manager->cache_ttl = ttl;
    pthread_rwlock_unlock(&manager->cache_lock);
    
    SHARD_HASH_DEBUG("Updated cache TTL to %u seconds", ttl);
}

size_t shard_hash_cache_cleanup_expired(shard_hash_manager_t *manager) {
    if (!manager) return 0;
    
    time_t now = time(NULL);
    size_t cleaned = 0;
    
    pthread_rwlock_wrlock(&manager->cache_lock);
    
    for (size_t i = 0; i < manager->cache_size; i++) {
        shard_hash_cache_entry_t *entry = &manager->cache[i];
        
        if (entry->valid && (now - entry->timestamp > manager->cache_ttl)) {
            if (entry->key) {
                kv_store_free(entry->key);
                entry->key = NULL;
            }
            entry->valid = false;
            cleaned++;
        }
    }
    
    pthread_rwlock_unlock(&manager->cache_lock);
    
    SHARD_HASH_DEBUG("Cleaned %zu expired cache entries", cleaned);
    
    return cleaned;
}

int shard_hash_get_stats(shard_hash_manager_t *manager, shard_hash_stats_t *stats) {
    if (!manager || !stats) {
        return -1;
    }
    
    pthread_mutex_lock(&manager->stats_lock);
    
    stats->total_requests = manager->total_hash_requests;
    stats->cache_hits = manager->cache_hits;
    stats->cache_misses = manager->cache_misses;
    stats->cache_hit_rate = (manager->total_hash_requests > 0) ? 
        (double)manager->cache_hits / manager->total_hash_requests : 0.0;
    stats->hash_collisions = manager->hash_collisions;
    
    // 计算活跃缓存条目数
    pthread_rwlock_rdlock(&manager->cache_lock);
    uint32_t active_entries = 0;
    time_t now = time(NULL);
    
    for (size_t i = 0; i < manager->cache_size; i++) {
        shard_hash_cache_entry_t *entry = &manager->cache[i];
        if (entry->valid && entry->key && 
            (now - entry->timestamp <= manager->cache_ttl)) {
            active_entries++;
        }
    }
    pthread_rwlock_unlock(&manager->cache_lock);
    
    stats->active_cache_entries = active_entries;
    
    // 分配分片统计数组
    stats->shard_key_counts = kv_store_malloc(sizeof(uint64_t) * manager->shard_count);
    stats->shard_load_percentages = kv_store_malloc(sizeof(double) * manager->shard_count);
    
    if (stats->shard_key_counts && stats->shard_load_percentages) {
        uint64_t total_keys = 0;
        
        // 复制分片分布数据
        memcpy(stats->shard_key_counts, manager->shard_distribution, 
               sizeof(uint64_t) * manager->shard_count);
        
        // 计算总键数
        for (uint32_t i = 0; i < manager->shard_count; i++) {
            total_keys += manager->shard_distribution[i];
        }
        
        // 计算负载百分比
        for (uint32_t i = 0; i < manager->shard_count; i++) {
            stats->shard_load_percentages[i] = (total_keys > 0) ? 
                (double)manager->shard_distribution[i] / total_keys * 100.0 : 0.0;
        }
        
        // 计算分布方差
        stats->distribution_variance = shard_hash_calculate_distribution_variance(manager);
    }
    
    pthread_mutex_unlock(&manager->stats_lock);
    
    return 0;
}

void shard_hash_reset_stats(shard_hash_manager_t *manager) {
    if (!manager) return;
    
    pthread_mutex_lock(&manager->stats_lock);
    
    manager->total_hash_requests = 0;
    manager->cache_hits = 0;
    manager->cache_misses = 0;
    manager->hash_collisions = 0;
    
    memset(manager->shard_distribution, 0, sizeof(uint64_t) * manager->shard_count);
    
    pthread_mutex_unlock(&manager->stats_lock);
    
    SHARD_HASH_DEBUG("Reset hash statistics");
}

void shard_hash_print_distribution(shard_hash_manager_t *manager) {
    if (!manager) return;
    
    pthread_mutex_lock(&manager->stats_lock);
    
    printf("=== Shard Hash Distribution ===\n");
    printf("Total requests: %lu\n", manager->total_hash_requests);
    printf("Cache hits: %lu (%.2f%%)\n", manager->cache_hits, 
           (manager->total_hash_requests > 0) ? 
           (double)manager->cache_hits / manager->total_hash_requests * 100.0 : 0.0);
    printf("Cache misses: %lu (%.2f%%)\n", manager->cache_misses,
           (manager->total_hash_requests > 0) ? 
           (double)manager->cache_misses / manager->total_hash_requests * 100.0 : 0.0);
    
    uint64_t total_keys = 0;
    for (uint32_t i = 0; i < manager->shard_count; i++) {
        total_keys += manager->shard_distribution[i];
    }
    
    printf("Total keys distributed: %lu\n", total_keys);
    printf("Shard distribution:\n");
    
    for (uint32_t i = 0; i < manager->shard_count; i++) {
        double percentage = (total_keys > 0) ? 
            (double)manager->shard_distribution[i] / total_keys * 100.0 : 0.0;
        printf("  Shard %u: %lu keys (%.2f%%)\n", 
               i, manager->shard_distribution[i], percentage);
    }
    
    printf("Distribution variance: %.6f\n", 
           shard_hash_calculate_distribution_variance(manager));
    printf("==============================\n");
    
    pthread_mutex_unlock(&manager->stats_lock);
}

int shard_hash_update_shard_count(shard_hash_manager_t *manager, 
                                 uint32_t new_shard_count) {
    if (!manager || new_shard_count == 0) {
        return -1;
    }
    
    pthread_mutex_lock(&manager->stats_lock);
    
    // 重新分配分片分布数组
    uint64_t *new_distribution = kv_store_malloc(sizeof(uint64_t) * new_shard_count);
    if (!new_distribution) {
        pthread_mutex_unlock(&manager->stats_lock);
        return -1;
    }
    
    memset(new_distribution, 0, sizeof(uint64_t) * new_shard_count);
    
    // 释放旧数组
    kv_store_free(manager->shard_distribution);
    manager->shard_distribution = new_distribution;
    manager->shard_count = new_shard_count;
    
    pthread_mutex_unlock(&manager->stats_lock);
    
    // 清空缓存，因为分片数量变化会影响路由
    shard_hash_cache_clear(manager);
    
    SHARD_HASH_DEBUG("Updated shard count to %u", new_shard_count);
    
    return 0;
}

void shard_hash_set_virtual_nodes(shard_hash_manager_t *manager, 
                                 uint32_t virtual_nodes) {
    if (!manager) return;
    
    manager->virtual_nodes = virtual_nodes;
    
    SHARD_HASH_DEBUG("Updated virtual nodes to %u", virtual_nodes);
}

bool shard_hash_validate_key(const char *key, size_t key_len) {
    if (!key || key_len == 0 || key_len > 65536) {
        return false;
    }
    
    return true;
}

double shard_hash_calculate_distribution_variance(shard_hash_manager_t *manager) {
    if (!manager || manager->shard_count == 0) {
        return 0.0;
    }
    
    uint64_t total_keys = 0;
    for (uint32_t i = 0; i < manager->shard_count; i++) {
        total_keys += manager->shard_distribution[i];
    }
    
    if (total_keys == 0) {
        return 0.0;
    }
    
    double mean = (double)total_keys / manager->shard_count;
    double variance = 0.0;
    
    for (uint32_t i = 0; i < manager->shard_count; i++) {
        double diff = (double)manager->shard_distribution[i] - mean;
        variance += diff * diff;
    }
    
    return variance / manager->shard_count;
}

uint32_t shard_hash_find_hottest_shard(shard_hash_manager_t *manager) {
    if (!manager || manager->shard_count == 0) {
        return 0;
    }
    
    pthread_mutex_lock(&manager->stats_lock);
    
    uint32_t hottest_shard = 0;
    uint64_t max_keys = manager->shard_distribution[0];
    
    for (uint32_t i = 1; i < manager->shard_count; i++) {
        if (manager->shard_distribution[i] > max_keys) {
            max_keys = manager->shard_distribution[i];
            hottest_shard = i;
        }
    }
    
    pthread_mutex_unlock(&manager->stats_lock);
    
    return hottest_shard;
}

uint32_t shard_hash_find_coldest_shard(shard_hash_manager_t *manager) {
    if (!manager || manager->shard_count == 0) {
        return 0;
    }
    
    pthread_mutex_lock(&manager->stats_lock);
    
    uint32_t coldest_shard = 0;
    uint64_t min_keys = manager->shard_distribution[0];
    
    for (uint32_t i = 1; i < manager->shard_count; i++) {
        if (manager->shard_distribution[i] < min_keys) {
            min_keys = manager->shard_distribution[i];
            coldest_shard = i;
        }
    }
    
    pthread_mutex_unlock(&manager->stats_lock);
    
    return coldest_shard;
}

void shard_hash_set_debug(shard_hash_manager_t *manager, bool debug) {
    // 调试开关的实现可以通过编译时宏或运行时标志来控制
    (void)manager; // 避免未使用参数警告
    (void)debug;   // 避免未使用参数警告
    SHARD_HASH_DEBUG("Debug mode %s", debug ? "enabled" : "disabled");
}

int shard_hash_export_cache_state(shard_hash_manager_t *manager, 
                                 const char *output_file) {
    if (!manager || !output_file) {
        return -1;
    }
    
    FILE *fp = fopen(output_file, "w");
    if (!fp) {
        return -1;
    }
    
    pthread_rwlock_rdlock(&manager->cache_lock);
    
    fprintf(fp, "# Shard Hash Cache State Export\n");
    fprintf(fp, "# Format: index,key,hash,shard_id,timestamp,valid\n");
    
    for (size_t i = 0; i < manager->cache_size; i++) {
        shard_hash_cache_entry_t *entry = &manager->cache[i];
        if (entry->key) {
            fprintf(fp, "%zu,%s,%lu,%u,%ld,%d\n",
                   i, entry->key, entry->hash, entry->shard_id, 
                   entry->timestamp, entry->valid);
        }
    }
    
    pthread_rwlock_unlock(&manager->cache_lock);
    
    fclose(fp);
    
    SHARD_HASH_DEBUG("Exported cache state to %s", output_file);
    
    return 0;
} 