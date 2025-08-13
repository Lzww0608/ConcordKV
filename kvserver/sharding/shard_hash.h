/*
 * @Author: Lzww0608
 * @Date: 2025-6-25 12:58:27
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-25 12:58:27
 * @Description: ConcordKV 分片哈希计算模块 - 与Go层保持一致的哈希算法
 */

#ifndef __SHARD_HASH_H__
#define __SHARD_HASH_H__

#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// 哈希种子，与Go层保持一致
#define SHARD_HASH_SEED 0x9E3779B9

// 分片哈希缓存配置
#define SHARD_HASH_CACHE_SIZE 10000  // 默认缓存大小
#define SHARD_HASH_CACHE_TTL 300     // 缓存TTL(秒)

// 哈希缓存条目
typedef struct {
    char *key;              // 键
    uint64_t hash;          // 哈希值
    uint32_t shard_id;      // 分片ID
    time_t timestamp;       // 时间戳
    bool valid;             // 是否有效
} shard_hash_cache_entry_t;

// 分片哈希管理器
typedef struct {
    // 缓存相关
    shard_hash_cache_entry_t *cache; // 哈希缓存数组
    size_t cache_size;               // 缓存大小
    size_t cache_count;              // 当前缓存条目数
    uint32_t cache_ttl;              // 缓存TTL
    pthread_rwlock_t cache_lock;     // 缓存锁
    
    // 分片配置
    uint32_t shard_count;            // 分片数量
    uint32_t virtual_nodes;          // 虚拟节点数量
    
    // 统计信息
    uint64_t total_hash_requests;    // 总哈希请求数
    uint64_t cache_hits;             // 缓存命中数
    uint64_t cache_misses;           // 缓存未命中数
    uint64_t hash_collisions;        // 哈希冲突数
    
    // 哈希分布统计
    uint64_t *shard_distribution;    // 每个分片的键分布统计
    pthread_mutex_t stats_lock;      // 统计锁
} shard_hash_manager_t;

// 分片哈希统计信息
typedef struct {
    uint64_t total_requests;         // 总请求数
    uint64_t cache_hits;             // 缓存命中数
    uint64_t cache_misses;           // 缓存未命中数
    double cache_hit_rate;           // 缓存命中率
    uint64_t hash_collisions;        // 哈希冲突数
    uint32_t active_cache_entries;   // 活跃缓存条目数
    
    // 分片分布统计
    uint64_t *shard_key_counts;      // 每个分片的键数量
    double *shard_load_percentages;  // 每个分片的负载百分比
    double distribution_variance;     // 分布方差
} shard_hash_stats_t;

// === 核心哈希API ===

/**
 * 创建分片哈希管理器
 * @param shard_count 分片数量
 * @param cache_size 缓存大小
 * @param cache_ttl 缓存TTL(秒)
 * @return 分片哈希管理器指针，失败返回NULL
 */
shard_hash_manager_t* shard_hash_manager_create(uint32_t shard_count, 
                                               size_t cache_size, 
                                               uint32_t cache_ttl);

/**
 * 销毁分片哈希管理器
 * @param manager 分片哈希管理器
 */
void shard_hash_manager_destroy(shard_hash_manager_t *manager);

/**
 * 计算键的哈希值
 * @param key 键
 * @param key_len 键长度
 * @return 64位哈希值
 */
uint64_t shard_hash_compute(const char *key, size_t key_len);

/**
 * 根据键计算分片ID（带缓存）
 * @param manager 哈希管理器
 * @param key 键
 * @param key_len 键长度
 * @return 分片ID
 */
uint32_t shard_hash_get_shard_id(shard_hash_manager_t *manager, 
                                const char *key, 
                                size_t key_len);

/**
 * 根据哈希值计算分片ID
 * @param hash 哈希值
 * @param shard_count 分片数量
 * @return 分片ID
 */
uint32_t shard_hash_to_shard_id(uint64_t hash, uint32_t shard_count);

/**
 * 批量计算分片ID
 * @param manager 哈希管理器
 * @param keys 键数组
 * @param key_lens 键长度数组
 * @param count 键数量
 * @param shard_ids 输出分片ID数组
 * @return 成功返回0，失败返回负数
 */
int shard_hash_batch_get_shard_ids(shard_hash_manager_t *manager,
                                  const char **keys,
                                  const size_t *key_lens,
                                  size_t count,
                                  uint32_t *shard_ids);

// === 缓存管理API ===

/**
 * 清空哈希缓存
 * @param manager 哈希管理器
 */
void shard_hash_cache_clear(shard_hash_manager_t *manager);

/**
 * 设置缓存TTL
 * @param manager 哈希管理器
 * @param ttl 新的TTL值(秒)
 */
void shard_hash_cache_set_ttl(shard_hash_manager_t *manager, uint32_t ttl);

/**
 * 清理过期缓存条目
 * @param manager 哈希管理器
 * @return 清理的条目数量
 */
size_t shard_hash_cache_cleanup_expired(shard_hash_manager_t *manager);

// === 统计信息API ===

/**
 * 获取哈希统计信息
 * @param manager 哈希管理器
 * @param stats 统计信息输出结构
 * @return 成功返回0，失败返回负数
 */
int shard_hash_get_stats(shard_hash_manager_t *manager, shard_hash_stats_t *stats);

/**
 * 重置统计信息
 * @param manager 哈希管理器
 */
void shard_hash_reset_stats(shard_hash_manager_t *manager);

/**
 * 打印哈希分布统计
 * @param manager 哈希管理器
 */
void shard_hash_print_distribution(shard_hash_manager_t *manager);

// === 配置管理API ===

/**
 * 更新分片数量（运行时重配置）
 * @param manager 哈希管理器
 * @param new_shard_count 新的分片数量
 * @return 成功返回0，失败返回负数
 */
int shard_hash_update_shard_count(shard_hash_manager_t *manager, 
                                 uint32_t new_shard_count);

/**
 * 设置虚拟节点数量
 * @param manager 哈希管理器
 * @param virtual_nodes 虚拟节点数量
 */
void shard_hash_set_virtual_nodes(shard_hash_manager_t *manager, 
                                 uint32_t virtual_nodes);

// === 工具函数 ===

/**
 * 验证键是否适合哈希
 * @param key 键
 * @param key_len 键长度
 * @return 有效返回true，无效返回false
 */
bool shard_hash_validate_key(const char *key, size_t key_len);

/**
 * 计算哈希分布方差
 * @param manager 哈希管理器
 * @return 分布方差值
 */
double shard_hash_calculate_distribution_variance(shard_hash_manager_t *manager);

/**
 * 查找负载最高的分片
 * @param manager 哈希管理器
 * @return 负载最高的分片ID
 */
uint32_t shard_hash_find_hottest_shard(shard_hash_manager_t *manager);

/**
 * 查找负载最低的分片
 * @param manager 哈希管理器
 * @return 负载最低的分片ID
 */
uint32_t shard_hash_find_coldest_shard(shard_hash_manager_t *manager);

// === Murmur3哈希实现 ===

/**
 * Murmur3 32位哈希
 * @param key 键
 * @param len 长度
 * @param seed 种子
 * @return 32位哈希值
 */
uint32_t murmur3_hash32(const void *key, int len, uint32_t seed);

/**
 * Murmur3 64位哈希
 * @param key 键
 * @param len 长度
 * @param seed 种子
 * @return 64位哈希值
 */
uint64_t murmur3_hash64(const void *key, int len, uint32_t seed);

// === 调试和测试API ===

/**
 * 设置调试模式
 * @param manager 哈希管理器
 * @param debug 是否启用调试
 */
void shard_hash_set_debug(shard_hash_manager_t *manager, bool debug);

/**
 * 导出哈希缓存状态（用于调试）
 * @param manager 哈希管理器
 * @param output_file 输出文件路径
 * @return 成功返回0，失败返回负数
 */
int shard_hash_export_cache_state(shard_hash_manager_t *manager, 
                                 const char *output_file);

#ifdef __cplusplus
}
#endif

#endif // __SHARD_HASH_H__ 