/*
 * @Author: Lzww0608
 * @Date: 2025-6-25 17:51:06
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-25 17:51:09
 * @Description: ConcordKV 分片感知存储适配器 - 包装现有存储引擎，添加分片路由逻辑
 */

#ifndef __SHARD_AWARE_ENGINE_H__
#define __SHARD_AWARE_ENGINE_H__

#define _GNU_SOURCE
#include "../kv_engine_interface.h"
#include "shard_config.h"
#include "shard_hash.h"
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// 分片引擎状态
typedef enum {
    SHARD_ENGINE_STATE_INIT      = 0,    // 初始化状态
    SHARD_ENGINE_STATE_RUNNING   = 1,    // 运行状态
    SHARD_ENGINE_STATE_REBALANCING = 2,  // 重平衡状态
    SHARD_ENGINE_STATE_ERROR     = 3,    // 错误状态
    SHARD_ENGINE_STATE_SHUTDOWN  = 4     // 关闭状态
} shard_engine_state_t;

// 分片实例结构
typedef struct {
    uint32_t shard_id;                  // 分片ID
    kv_engine_t *engine;                // 底层存储引擎
    pthread_rwlock_t shard_lock;        // 分片锁
    uint64_t operation_count;           // 操作计数
    uint64_t key_count;                 // 键数量
    size_t memory_usage;                // 内存使用量
    bool is_active;                     // 是否活跃
} shard_instance_t;

// 分片感知存储引擎
typedef struct {
    // === 基础信息 ===
    kv_engine_t base_engine;            // 基础引擎结构（继承）
    shard_engine_state_t shard_state;   // 分片引擎状态
    
    // === 配置和管理 ===
    shard_config_manager_t *config_manager;  // 配置管理器
    shard_hash_manager_t *hash_manager;      // 哈希管理器
    
    // === 分片实例管理 ===
    shard_instance_t **shards;          // 分片实例数组
    uint32_t shard_count;               // 分片数量
    pthread_rwlock_t shards_lock;       // 分片数组锁
    
    // === 统计信息 ===
    uint64_t total_operations;          // 总操作数
    uint64_t cross_shard_operations;    // 跨分片操作数
    uint64_t failed_operations;         // 失败操作数
    uint64_t rebalance_count;           // 重平衡次数
    
    // === 性能优化 ===
    bool enable_parallel_operations;    // 是否启用并行操作
    uint32_t max_parallel_threads;      // 最大并行线程数
    
} shard_aware_engine_t;

// 跨分片操作结果
typedef struct {
    uint32_t total_shards;              // 涉及的分片总数
    uint32_t successful_shards;         // 成功的分片数
    uint32_t failed_shards;             // 失败的分片数
    kv_batch_t *results;                // 结果集合
    char error_message[256];            // 错误信息
} cross_shard_result_t;

// === 分片感知引擎API ===

/**
 * 创建分片感知存储引擎
 * @param config_manager 配置管理器
 * @return 分片感知引擎指针，失败返回NULL
 */
shard_aware_engine_t* shard_aware_engine_create(shard_config_manager_t *config_manager);

/**
 * 销毁分片感知存储引擎
 * @param shard_engine 分片感知引擎
 */
void shard_aware_engine_destroy(shard_aware_engine_t *shard_engine);

/**
 * 初始化分片感知引擎
 * @param shard_engine 分片感知引擎
 * @return 成功返回0，失败返回负数
 */
int shard_aware_engine_init(shard_aware_engine_t *shard_engine);

/**
 * 启动分片感知引擎
 * @param shard_engine 分片感知引擎
 * @return 成功返回0，失败返回负数
 */
int shard_aware_engine_start(shard_aware_engine_t *shard_engine);

/**
 * 停止分片感知引擎
 * @param shard_engine 分片感知引擎
 * @return 成功返回0，失败返回负数
 */
int shard_aware_engine_stop(shard_aware_engine_t *shard_engine);

// === 分片路由API ===

/**
 * 根据键获取分片ID
 * @param shard_engine 分片感知引擎
 * @param key 键
 * @return 分片ID
 */
uint32_t shard_aware_engine_get_shard_id(shard_aware_engine_t *shard_engine, const char *key);

/**
 * 获取指定分片的引擎实例
 * @param shard_engine 分片感知引擎
 * @param shard_id 分片ID
 * @return 分片引擎实例，失败返回NULL
 */
kv_engine_t* shard_aware_engine_get_shard_engine(shard_aware_engine_t *shard_engine, 
                                                 uint32_t shard_id);

// === 基础CRUD操作（分片感知） ===

/**
 * 设置键值对
 * @param shard_engine 分片感知引擎
 * @param key 键
 * @param value 值
 * @return 成功返回0，失败返回负数
 */
int shard_aware_engine_set(shard_aware_engine_t *shard_engine, 
                          const char *key, 
                          const char *value);

/**
 * 获取键对应的值
 * @param shard_engine 分片感知引擎
 * @param key 键
 * @return 值字符串，需要调用者释放内存，失败返回NULL
 */
char* shard_aware_engine_get(shard_aware_engine_t *shard_engine, const char *key);

/**
 * 删除键值对
 * @param shard_engine 分片感知引擎
 * @param key 键
 * @return 成功返回0，失败返回负数
 */
int shard_aware_engine_delete(shard_aware_engine_t *shard_engine, const char *key);

/**
 * 更新键值对
 * @param shard_engine 分片感知引擎
 * @param key 键
 * @param value 新值
 * @return 成功返回0，失败返回负数
 */
int shard_aware_engine_update(shard_aware_engine_t *shard_engine, 
                             const char *key, 
                             const char *value);

// === 批量操作（跨分片支持） ===

/**
 * 批量设置键值对
 * @param shard_engine 分片感知引擎
 * @param batch 批量操作数据
 * @param result 跨分片操作结果
 * @return 成功返回0，失败返回负数
 */
int shard_aware_engine_batch_set(shard_aware_engine_t *shard_engine,
                                const kv_batch_t *batch,
                                cross_shard_result_t *result);

/**
 * 批量获取键值对
 * @param shard_engine 分片感知引擎
 * @param keys 键数组
 * @param key_count 键数量
 * @param result 跨分片操作结果
 * @return 成功返回0，失败返回负数
 */
int shard_aware_engine_batch_get(shard_aware_engine_t *shard_engine,
                                const char **keys,
                                size_t key_count,
                                cross_shard_result_t *result);

/**
 * 批量删除键值对
 * @param shard_engine 分片感知引擎
 * @param keys 键数组
 * @param key_count 键数量
 * @param result 跨分片操作结果
 * @return 成功返回0，失败返回负数
 */
int shard_aware_engine_batch_delete(shard_aware_engine_t *shard_engine,
                                   const char **keys,
                                   size_t key_count,
                                   cross_shard_result_t *result);

// === 范围查询（跨分片支持） ===

/**
 * 范围扫描
 * @param shard_engine 分片感知引擎
 * @param range 扫描范围
 * @param result 跨分片操作结果
 * @return 成功返回0，失败返回负数
 */
int shard_aware_engine_range_scan(shard_aware_engine_t *shard_engine,
                                 const kv_range_t *range,
                                 cross_shard_result_t *result);

/**
 * 前缀扫描
 * @param shard_engine 分片感知引擎
 * @param prefix 前缀
 * @param result 跨分片操作结果
 * @return 成功返回0，失败返回负数
 */
int shard_aware_engine_prefix_scan(shard_aware_engine_t *shard_engine,
                                  const char *prefix,
                                  cross_shard_result_t *result);

// === 统计信息API ===

/**
 * 获取总键数量
 * @param shard_engine 分片感知引擎
 * @return 总键数量
 */
uint64_t shard_aware_engine_count(shard_aware_engine_t *shard_engine);

/**
 * 获取总内存使用量
 * @param shard_engine 分片感知引擎
 * @return 总内存使用量（字节）
 */
size_t shard_aware_engine_memory_usage(shard_aware_engine_t *shard_engine);

/**
 * 获取分片统计信息
 * @param shard_engine 分片感知引擎
 * @param shard_id 分片ID
 * @param stats 统计信息输出
 * @return 成功返回0，失败返回负数
 */
int shard_aware_engine_get_shard_stats(shard_aware_engine_t *shard_engine,
                                      uint32_t shard_id,
                                      kv_engine_stats_t *stats);

// === 分片管理API ===

/**
 * 添加新分片
 * @param shard_engine 分片感知引擎
 * @param shard_id 分片ID
 * @param engine_config 引擎配置
 * @return 成功返回0，失败返回负数
 */
int shard_aware_engine_add_shard(shard_aware_engine_t *shard_engine,
                                uint32_t shard_id,
                                const kv_engine_config_t *engine_config);

/**
 * 移除分片
 * @param shard_engine 分片感知引擎
 * @param shard_id 分片ID
 * @return 成功返回0，失败返回负数
 */
int shard_aware_engine_remove_shard(shard_aware_engine_t *shard_engine,
                                   uint32_t shard_id);

/**
 * 重平衡分片
 * @param shard_engine 分片感知引擎
 * @return 成功返回0，失败返回负数
 */
int shard_aware_engine_rebalance(shard_aware_engine_t *shard_engine);

// === 配置热更新API ===

/**
 * 更新分片配置
 * @param shard_engine 分片感知引擎
 * @param new_config 新配置
 * @return 成功返回0，失败返回负数
 */
int shard_aware_engine_update_config(shard_aware_engine_t *shard_engine,
                                    const shard_config_t *new_config);

// === 工具函数 ===

/**
 * 获取分片引擎状态名称
 * @param state 状态
 * @return 状态名称字符串
 */
const char* shard_engine_state_to_string(shard_engine_state_t state);

/**
 * 创建跨分片结果结构
 * @param shard_count 分片数量
 * @return 跨分片结果结构
 */
cross_shard_result_t* cross_shard_result_create(uint32_t shard_count);

/**
 * 销毁跨分片结果结构
 * @param result 跨分片结果结构
 */
void cross_shard_result_destroy(cross_shard_result_t *result);

#ifdef __cplusplus
}
#endif

#endif // __SHARD_AWARE_ENGINE_H__
