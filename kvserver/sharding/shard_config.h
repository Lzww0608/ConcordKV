/*
 * @Author: Lzww0608
 * @Date: 2025-6-25 17:51:34
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-25 17:51:34
 * @Description: ConcordKV 分片配置管理模块 - 扩展现有配置系统支持分片参数
 */

#ifndef __SHARD_CONFIG_H__
#define __SHARD_CONFIG_H__

#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include "../kv_engine_interface.h"
#include "../kv_engine_config.h"

#ifdef __cplusplus
extern "C" {
#endif

// 分片策略类型
typedef enum {
    SHARD_STRATEGY_HASH         = 0,    // 哈希分片
    SHARD_STRATEGY_RANGE        = 1,    // 范围分片
    SHARD_STRATEGY_CONSISTENT   = 2,    // 一致性哈希分片
    SHARD_STRATEGY_DIRECTORY    = 3,    // 目录分片
    SHARD_STRATEGY_HYBRID       = 4     // 混合分片策略
} shard_strategy_t;

// 分片负载均衡策略
typedef enum {
    SHARD_BALANCE_NONE          = 0,    // 无负载均衡
    SHARD_BALANCE_ROUND_ROBIN   = 1,    // 轮询
    SHARD_BALANCE_LEAST_LOADED  = 2,    // 最少负载
    SHARD_BALANCE_WEIGHTED      = 3,    // 加权负载均衡
    SHARD_BALANCE_ADAPTIVE      = 4     // 自适应负载均衡
} shard_balance_strategy_t;

// 分片配置结构
typedef struct {
    // === 基础分片配置 ===
    bool enabled;                       // 是否启用分片
    uint32_t shard_count;               // 分片数量
    shard_strategy_t strategy;          // 分片策略
    uint32_t virtual_nodes;             // 虚拟节点数量(一致性哈希)
    uint32_t replication_factor;        // 副本因子
    
    // === 哈希配置 ===
    uint32_t hash_seed;                 // 哈希种子
    char hash_algorithm[32];            // 哈希算法名称
    bool enable_hash_cache;             // 是否启用哈希缓存
    size_t hash_cache_size;             // 哈希缓存大小
    uint32_t hash_cache_ttl;            // 哈希缓存TTL(秒)
    
    // === 负载均衡配置 ===
    shard_balance_strategy_t balance_strategy; // 负载均衡策略
    double load_threshold;              // 负载阈值
    uint32_t rebalance_interval;        // 重平衡间隔(秒)
    bool enable_auto_rebalance;         // 是否启用自动重平衡
    uint32_t migration_batch_size;      // 迁移批次大小
    
    // === 性能配置 ===
    uint32_t max_concurrent_operations; // 最大并发操作数
    uint32_t operation_timeout;         // 操作超时(毫秒)
    bool enable_batch_operations;       // 是否启用批量操作
    size_t batch_size;                  // 批量操作大小
    
    // === 容错配置 ===
    uint32_t max_retries;               // 最大重试次数
    uint32_t retry_interval;            // 重试间隔(毫秒)
    bool enable_failover;               // 是否启用故障转移
    uint32_t health_check_interval;     // 健康检查间隔(秒)
    
    // === 监控配置 ===
    bool enable_metrics;                // 是否启用监控
    uint32_t metrics_collection_interval; // 监控数据收集间隔(秒)
    char metrics_output_dir[256];       // 监控数据输出目录
    bool enable_detailed_stats;         // 是否启用详细统计
    
    // === 持久化配置 ===
    bool enable_persistence;            // 是否启用持久化
    char persistence_dir[256];          // 持久化目录
    uint32_t persistence_interval;      // 持久化间隔(秒)
    bool enable_compression;            // 是否启用压缩
    
    // === 每个分片的引擎配置 ===
    kv_engine_type_t shard_engine_type; // 分片存储引擎类型
    kv_engine_config_t **shard_engine_configs; // 每个分片的引擎配置
    
} shard_config_t;

// 分片配置管理器
typedef struct {
    shard_config_t *config;             // 当前配置
    char config_file_path[512];         // 配置文件路径
    pthread_rwlock_t config_lock;       // 配置读写锁
    
    // 配置版本管理
    uint64_t config_version;            // 配置版本号
    time_t last_update;                 // 最后更新时间
    bool config_changed;                // 配置是否已修改
    
} shard_config_manager_t;

// === 核心配置管理API ===

/**
 * 创建分片配置管理器
 * @param config_file_path 配置文件路径
 * @return 配置管理器指针，失败返回NULL
 */
shard_config_manager_t* shard_config_manager_create(const char *config_file_path);

/**
 * 销毁分片配置管理器
 * @param manager 配置管理器
 */
void shard_config_manager_destroy(shard_config_manager_t *manager);

/**
 * 创建默认分片配置
 * @return 默认配置结构，失败返回NULL
 */
shard_config_t* shard_config_create_default(void);

/**
 * 销毁分片配置
 * @param config 配置结构
 */
void shard_config_destroy(shard_config_t *config);

/**
 * 获取当前配置（只读）
 * @param manager 配置管理器
 * @return 当前配置指针
 */
const shard_config_t* shard_config_get_current(shard_config_manager_t *manager);

/**
 * 更新分片配置（热更新）
 * @param manager 配置管理器
 * @param new_config 新配置
 * @return 成功返回0，失败返回负数
 */
int shard_config_update(shard_config_manager_t *manager, const shard_config_t *new_config);

/**
 * 从文件加载分片配置
 * @param manager 配置管理器
 * @param file_path 配置文件路径
 * @return 成功返回0，失败返回负数
 */
int shard_config_load_from_file(shard_config_manager_t *manager, const char *file_path);

/**
 * 验证分片配置
 * @param config 待验证的配置
 * @param error_msg 错误信息输出缓冲区
 * @param error_msg_size 错误信息缓冲区大小
 * @return 有效返回true，无效返回false
 */
bool shard_config_validate(const shard_config_t *config, 
                          char *error_msg, 
                          size_t error_msg_size);

/**
 * 获取分片策略名称
 * @param strategy 分片策略
 * @return 策略名称字符串
 */
const char* shard_strategy_to_string(shard_strategy_t strategy);

/**
 * 打印分片配置信息
 * @param config 配置结构
 */
void shard_config_print(const shard_config_t *config);

/**
 * 为指定分片创建引擎配置
 * @param shard_config 分片配置
 * @param shard_id 分片ID
 * @return 引擎配置，失败返回NULL
 */
kv_engine_config_t* shard_config_create_engine_config(const shard_config_t *shard_config, 
                                                      uint32_t shard_id);

#ifdef __cplusplus
}
#endif

#endif // __SHARD_CONFIG_H__
