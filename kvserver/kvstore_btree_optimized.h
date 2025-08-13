/*
 * @Author: Lzww0608  
 * @Date: 2025-6-3 15:00:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-4 17:17:35
 * @Description: ConcordKV B+Tree优化版存储引擎头文件
 */
#ifndef __KVSTORE_BTREE_OPTIMIZED_H__
#define __KVSTORE_BTREE_OPTIMIZED_H__

#include "kvstore_btree.h"
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

// === 优化配置常量 ===
#define BTREE_OPT_DEFAULT_CACHE_SIZE 1024       // 默认缓存节点数
#define BTREE_OPT_MIN_CACHE_SIZE 64             // 最小缓存大小
#define BTREE_OPT_MAX_CACHE_SIZE 8192           // 最大缓存大小
#define BTREE_OPT_BATCH_SIZE_DEFAULT 100        // 默认批量操作大小
#define BTREE_OPT_BATCH_SIZE_MAX 10000          // 最大批量操作大小
#define BTREE_OPT_HOT_THRESHOLD 10              // 热点访问阈值
#define BTREE_OPT_ADAPTIVE_INTERVAL 1000        // 自适应调整间隔(毫秒)

// === 自适应节点大小配置 ===
typedef struct btree_adaptive_config {
    int min_node_size;          // 最小节点大小
    int max_node_size;          // 最大节点大小
    int default_node_size;      // 默认节点大小
    double load_threshold_high; // 高负载阈值 (触发扩容)
    double load_threshold_low;  // 低负载阈值 (触发缩容)
    int adaptation_interval;    // 适应调整间隔(操作次数)
    bool enabled;               // 是否启用自适应
} btree_adaptive_config_t;

// === 热点缓存配置 ===
typedef struct btree_cache_config {
    size_t max_cache_nodes;     // 最大缓存节点数
    size_t max_memory_mb;       // 最大内存使用(MB)
    int hot_threshold;          // 热点访问阈值
    int cache_ttl_seconds;      // 缓存TTL(秒)
    bool enabled;               // 是否启用缓存
} btree_cache_config_t;

// === 批量操作配置 ===
typedef struct btree_batch_config {
    size_t default_batch_size;  // 默认批量大小
    size_t max_batch_size;      // 最大批量大小
    bool sort_keys;             // 是否对键排序
    bool enable_transaction;    // 是否启用事务
    bool enabled;               // 是否启用批量操作
} btree_batch_config_t;

// === 缓存节点结构 ===
typedef struct btree_cache_node {
    btree_node_t *node;         // 缓存的B+Tree节点
    char *node_key;             // 节点标识键(用于定位)
    size_t key_len;             // 键长度
    uint64_t access_count;      // 访问计数
    uint64_t last_access_time;  // 最后访问时间
    struct btree_cache_node *prev; // LRU链表前驱
    struct btree_cache_node *next; // LRU链表后继
    bool is_dirty;              // 是否已修改
} btree_cache_node_t;

// === LRU缓存管理器 ===
typedef struct btree_cache_manager {
    btree_cache_node_t **hash_table;  // 哈希表
    size_t hash_table_size;            // 哈希表大小
    btree_cache_node_t *lru_head;      // LRU链表头
    btree_cache_node_t *lru_tail;      // LRU链表尾
    size_t current_nodes;              // 当前缓存节点数
    size_t current_memory;             // 当前内存使用
    btree_cache_config_t config;       // 缓存配置
    pthread_rwlock_t cache_lock;       // 缓存锁
    
    // 统计信息
    atomic_uint_fast64_t hits;         // 缓存命中次数
    atomic_uint_fast64_t misses;       // 缓存未命中次数
    atomic_uint_fast64_t evictions;    // 缓存驱逐次数
} btree_cache_manager_t;

// === 自适应节点管理器 ===
typedef struct btree_adaptive_manager {
    btree_adaptive_config_t config;    // 自适应配置
    uint64_t operation_count;          // 操作计数
    uint64_t last_adaptation_time;     // 上次调整时间
    double current_load_factor;        // 当前负载因子
    int current_node_size;             // 当前节点大小
    pthread_mutex_t adapt_lock;        // 调整锁
    
    // 负载统计
    atomic_uint_fast64_t insert_ops;   // 插入操作数
    atomic_uint_fast64_t search_ops;   // 搜索操作数
    atomic_uint_fast64_t delete_ops;   // 删除操作数
    atomic_uint_fast64_t node_splits;  // 节点分裂数
    atomic_uint_fast64_t node_merges;  // 节点合并数
} btree_adaptive_manager_t;

// === 批量操作结构 ===
typedef struct btree_batch_entry {
    char *key;                  // 键
    size_t key_len;            // 键长度
    char *value;               // 值
    size_t value_len;          // 值长度
    int operation;             // 操作类型 (0=set, 1=get, 2=delete)
    int result;                // 操作结果
} btree_batch_entry_t;

typedef struct btree_batch_operation {
    btree_batch_entry_t *entries;    // 批量条目数组
    size_t entry_count;              // 条目数量
    size_t max_entries;              // 最大条目数
    btree_batch_config_t config;     // 批量配置
    
    // 执行统计
    size_t success_count;            // 成功次数
    size_t error_count;              // 错误次数
    double execution_time_ms;        // 执行时间
} btree_batch_operation_t;

// === 优化版B+Tree结构 ===
typedef struct btree_optimized {
    btree_t *base_tree;                    // 基础B+Tree
    btree_cache_manager_t *cache_manager;  // 缓存管理器
    btree_adaptive_manager_t *adaptive_manager; // 自适应管理器
    
    // 优化配置
    bool cache_enabled;                    // 缓存是否启用
    bool adaptive_enabled;                 // 自适应是否启用
    bool batch_enabled;                    // 批量操作是否启用
    
    // 全局统计
    atomic_uint_fast64_t total_operations; // 总操作数
    atomic_uint_fast64_t cache_hits;       // 缓存命中数
    atomic_uint_fast64_t cache_misses;     // 缓存未命中数
    
    pthread_rwlock_t opt_lock;             // 优化锁
} btree_optimized_t;

// === 自适应节点大小功能 ===

/**
 * 创建自适应管理器
 * @param config 自适应配置
 * @return 自适应管理器，失败返回NULL
 */
btree_adaptive_manager_t* btree_adaptive_manager_create(const btree_adaptive_config_t *config);

/**
 * 销毁自适应管理器
 * @param manager 自适应管理器
 */
void btree_adaptive_manager_destroy(btree_adaptive_manager_t *manager);

/**
 * 获取默认自适应配置
 * @param config 配置结构体指针
 */
void btree_adaptive_get_default_config(btree_adaptive_config_t *config);

/**
 * 更新负载统计
 * @param manager 自适应管理器
 * @param operation_type 操作类型 (0=插入, 1=搜索, 2=删除, 3=分裂, 4=合并)
 */
void btree_adaptive_update_stats(btree_adaptive_manager_t *manager, int operation_type);

/**
 * 检查是否需要调整节点大小
 * @param manager 自适应管理器
 * @return true如果需要调整，false否则
 */
bool btree_adaptive_need_adjustment(btree_adaptive_manager_t *manager);

/**
 * 执行节点大小调整
 * @param tree 优化版B+Tree
 * @return 0成功，负数表示错误
 */
int btree_adaptive_adjust_node_size(btree_optimized_t *tree);

// === 热点缓存功能 ===

/**
 * 创建缓存管理器
 * @param config 缓存配置
 * @return 缓存管理器，失败返回NULL
 */
btree_cache_manager_t* btree_cache_manager_create(const btree_cache_config_t *config);

/**
 * 销毁缓存管理器
 * @param manager 缓存管理器
 */
void btree_cache_manager_destroy(btree_cache_manager_t *manager);

/**
 * 获取默认缓存配置
 * @param config 配置结构体指针
 */
void btree_cache_get_default_config(btree_cache_config_t *config);

/**
 * 从缓存中查找节点
 * @param manager 缓存管理器
 * @param key 节点键
 * @param key_len 键长度
 * @return 缓存的节点，未找到返回NULL
 */
btree_node_t* btree_cache_get_node(btree_cache_manager_t *manager, const char *key, size_t key_len);

/**
 * 将节点添加到缓存
 * @param manager 缓存管理器
 * @param key 节点键
 * @param key_len 键长度
 * @param node 要缓存的节点
 * @return 0成功，负数表示错误
 */
int btree_cache_put_node(btree_cache_manager_t *manager, const char *key, size_t key_len, btree_node_t *node);

/**
 * 标记缓存节点为脏
 * @param manager 缓存管理器
 * @param key 节点键
 * @param key_len 键长度
 * @return 0成功，负数表示错误
 */
int btree_cache_mark_dirty(btree_cache_manager_t *manager, const char *key, size_t key_len);

/**
 * 刷新脏缓存节点到磁盘
 * @param manager 缓存管理器
 * @return 刷新的节点数，负数表示错误
 */
int btree_cache_flush_dirty(btree_cache_manager_t *manager);

/**
 * 获取缓存统计信息
 * @param manager 缓存管理器
 * @param hits 命中次数指针
 * @param misses 未命中次数指针
 * @param hit_rate 命中率指针
 */
void btree_cache_get_stats(btree_cache_manager_t *manager, uint64_t *hits, uint64_t *misses, double *hit_rate);

// === 批量操作功能 ===

/**
 * 创建批量操作
 * @param config 批量配置
 * @return 批量操作结构，失败返回NULL
 */
btree_batch_operation_t* btree_batch_create(const btree_batch_config_t *config);

/**
 * 销毁批量操作
 * @param batch 批量操作结构
 */
void btree_batch_destroy(btree_batch_operation_t *batch);

/**
 * 获取默认批量配置
 * @param config 配置结构体指针
 */
void btree_batch_get_default_config(btree_batch_config_t *config);

/**
 * 添加批量条目
 * @param batch 批量操作结构
 * @param operation 操作类型 (0=set, 1=get, 2=delete)
 * @param key 键
 * @param key_len 键长度
 * @param value 值 (对于set操作)
 * @param value_len 值长度
 * @return 0成功，负数表示错误
 */
int btree_batch_add_entry(btree_batch_operation_t *batch, int operation,
                         const char *key, size_t key_len,
                         const char *value, size_t value_len);

/**
 * 执行批量操作
 * @param tree 优化版B+Tree
 * @param batch 批量操作结构
 * @return 成功处理的条目数，负数表示错误
 */
int btree_batch_execute(btree_optimized_t *tree, btree_batch_operation_t *batch);

/**
 * 批量插入
 * @param tree 优化版B+Tree
 * @param keys 键数组
 * @param key_lens 键长度数组
 * @param values 值数组
 * @param value_lens 值长度数组
 * @param count 条目数量
 * @return 成功插入的数量，负数表示错误
 */
int btree_batch_insert(btree_optimized_t *tree, 
                      const char **keys, const size_t *key_lens,
                      const char **values, const size_t *value_lens,
                      size_t count);

/**
 * 批量查询
 * @param tree 优化版B+Tree
 * @param keys 键数组
 * @param key_lens 键长度数组
 * @param results 结果数组 (调用者负责释放)
 * @param count 键数量
 * @return 找到的数量，负数表示错误
 */
int btree_batch_get(btree_optimized_t *tree,
                   const char **keys, const size_t *key_lens,
                   char **results, size_t count);

/**
 * 批量删除
 * @param tree 优化版B+Tree
 * @param keys 键数组
 * @param key_lens 键长度数组
 * @param count 键数量
 * @return 成功删除的数量，负数表示错误
 */
int btree_batch_delete(btree_optimized_t *tree,
                      const char **keys, const size_t *key_lens,
                      size_t count);

// === 优化版B+Tree主要接口 ===

/**
 * 创建优化版B+Tree
 * @param order 树的阶数
 * @param cache_config 缓存配置 (NULL使用默认配置)
 * @param adaptive_config 自适应配置 (NULL使用默认配置)
 * @param batch_config 批量配置 (NULL使用默认配置)
 * @return 优化版B+Tree，失败返回NULL
 */
btree_optimized_t* btree_optimized_create(int order,
                                         const btree_cache_config_t *cache_config,
                                         const btree_adaptive_config_t *adaptive_config,
                                         const btree_batch_config_t *batch_config);

/**
 * 销毁优化版B+Tree
 * @param tree 优化版B+Tree
 */
void btree_optimized_destroy(btree_optimized_t *tree);

/**
 * 优化版插入操作
 * @param tree 优化版B+Tree
 * @param key 键
 * @param key_len 键长度
 * @param value 值
 * @param value_len 值长度
 * @return 0成功，负数表示错误
 */
int btree_optimized_insert(btree_optimized_t *tree, const char *key, size_t key_len,
                          const char *value, size_t value_len);

/**
 * 优化版查询操作
 * @param tree 优化版B+Tree
 * @param key 键
 * @param key_len 键长度
 * @return 找到的值 (调用者负责释放)，未找到返回NULL
 */
char* btree_optimized_search(btree_optimized_t *tree, const char *key, size_t key_len);

/**
 * 优化版删除操作
 * @param tree 优化版B+Tree
 * @param key 键
 * @param key_len 键长度
 * @return 0成功，负数表示错误
 */
int btree_optimized_delete(btree_optimized_t *tree, const char *key, size_t key_len);

/**
 * 优化版更新操作
 * @param tree 优化版B+Tree
 * @param key 键
 * @param key_len 键长度
 * @param value 新值
 * @param value_len 值长度
 * @return 0成功，负数表示错误
 */
int btree_optimized_update(btree_optimized_t *tree, const char *key, size_t key_len,
                          const char *value, size_t value_len);

/**
 * 获取优化版统计信息
 * @param tree 优化版B+Tree
 * @param total_ops 总操作数指针
 * @param cache_hit_rate 缓存命中率指针
 * @param avg_node_size 平均节点大小指针
 */
void btree_optimized_get_stats(btree_optimized_t *tree, 
                              uint64_t *total_ops,
                              double *cache_hit_rate,
                              double *avg_node_size);

/**
 * 强制刷新所有优化组件
 * @param tree 优化版B+Tree
 * @return 0成功，负数表示错误
 */
int btree_optimized_flush(btree_optimized_t *tree);

/**
 * 启用/禁用优化功能
 * @param tree 优化版B+Tree
 * @param cache_enabled 是否启用缓存
 * @param adaptive_enabled 是否启用自适应
 * @param batch_enabled 是否启用批量操作
 * @return 0成功，负数表示错误
 */
int btree_optimized_configure(btree_optimized_t *tree,
                             bool cache_enabled,
                             bool adaptive_enabled,
                             bool batch_enabled);

#ifdef __cplusplus
}
#endif

#endif // __KVSTORE_BTREE_OPTIMIZED_H__ 