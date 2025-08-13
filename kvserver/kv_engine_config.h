/*
 * @Author: Lzww0608  
 * @Date: 2025-6-16 16:38:49
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-16 16:52:07
 * @Description: ConcordKV 存储引擎配置系统
 */

#ifndef __KV_ENGINE_CONFIG_H__
#define __KV_ENGINE_CONFIG_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "kv_engine_interface.h"
#include "../common/config/config.h"

#ifdef __cplusplus
extern "C" {
#endif

// 引擎选择策略
typedef enum {
    KV_ENGINE_STRATEGY_FIXED = 0,        // 固定引擎
    KV_ENGINE_STRATEGY_ADAPTIVE = 1,     // 自适应选择
    KV_ENGINE_STRATEGY_LOAD_BALANCED = 2 // 负载均衡
} kv_engine_strategy_t;

// 存储引擎全局配置
typedef struct {
    // 基础配置
    kv_engine_strategy_t strategy;       // 引擎选择策略
    kv_engine_type_t default_engine;     // 默认引擎类型
    size_t max_engines;                  // 最大引擎数量
    bool enable_statistics;              // 是否启用统计
    bool enable_monitoring;              // 是否启用监控
    
    // 性能配置
    size_t global_memory_limit;          // 全局内存限制
    size_t global_cache_size;            // 全局缓存大小
    int worker_threads;                  // 工作线程数
    int io_threads;                      // IO线程数
    
    // 持久化配置
    char *data_directory;                // 数据目录
    bool enable_wal;                     // 是否启用WAL
    bool enable_sync;                    // 是否同步写入
    int sync_interval;                   // 同步间隔(秒)
    
    // 网络配置
    char *listen_host;                   // 监听主机
    int listen_port;                     // 监听端口
    int max_connections;                 // 最大连接数
    int connection_timeout;              // 连接超时(秒)
    
    // 各引擎特定配置
    kv_engine_config_t *array_config;   // Array引擎配置
    kv_engine_config_t *rbtree_config;  // RBTree引擎配置
    kv_engine_config_t *hash_config;    // Hash引擎配置
    kv_engine_config_t *btree_config;   // BTree引擎配置
    kv_engine_config_t *lsm_config;     // LSM引擎配置
} kv_engine_global_config_t;

// 存储引擎管理器
typedef struct {
    kv_engine_global_config_t *config;  // 全局配置
    kv_engine_t **engines;              // 引擎实例数组
    size_t engine_count;                // 当前引擎数量
    kv_engine_type_t current_engine;    // 当前使用的引擎
    concord_config_t *raw_config;       // 原始配置对象
    pthread_rwlock_t lock;              // 读写锁
    
    // 统计信息
    uint64_t total_operations;          // 总操作数
    uint64_t engine_switches;           // 引擎切换次数
    time_t start_time;                  // 启动时间
} kv_engine_manager_t;

// === 配置管理API ===

/**
 * 创建全局配置对象
 * @return 全局配置对象，失败返回NULL
 */
kv_engine_global_config_t* kv_engine_global_config_create(void);

/**
 * 销毁全局配置对象
 * @param config 全局配置对象
 */
void kv_engine_global_config_destroy(kv_engine_global_config_t *config);

/**
 * 从配置文件加载配置
 * @param filename 配置文件路径
 * @return 全局配置对象，失败返回NULL
 */
kv_engine_global_config_t* kv_engine_config_load_from_file(const char *filename);

/**
 * 保存配置到文件
 * @param config 全局配置对象
 * @param filename 配置文件路径
 * @return 0成功，负数失败
 */
int kv_engine_config_save_to_file(kv_engine_global_config_t *config, const char *filename);

/**
 * 从环境变量加载配置
 * @param config 全局配置对象
 * @param prefix 环境变量前缀，如"CONCORD_"
 * @return 0成功，负数失败
 */
int kv_engine_config_load_from_env(kv_engine_global_config_t *config, const char *prefix);

/**
 * 验证配置有效性
 * @param config 全局配置对象
 * @return 0有效，负数无效
 */
int kv_engine_config_validate(kv_engine_global_config_t *config);

/**
 * 打印配置信息
 * @param config 全局配置对象
 */
void kv_engine_config_print(kv_engine_global_config_t *config);

// === 引擎管理API ===

/**
 * 创建引擎管理器
 * @param config 全局配置对象
 * @return 引擎管理器，失败返回NULL
 */
kv_engine_manager_t* kv_engine_manager_create(kv_engine_global_config_t *config);

/**
 * 销毁引擎管理器
 * @param manager 引擎管理器
 */
void kv_engine_manager_destroy(kv_engine_manager_t *manager);

/**
 * 初始化所有配置的引擎
 * @param manager 引擎管理器
 * @return 0成功，负数失败
 */
int kv_engine_manager_init_engines(kv_engine_manager_t *manager);

/**
 * 获取当前活跃引擎
 * @param manager 引擎管理器
 * @return 当前引擎，失败返回NULL
 */
kv_engine_t* kv_engine_manager_get_current(kv_engine_manager_t *manager);

/**
 * 根据类型获取引擎
 * @param manager 引擎管理器
 * @param type 引擎类型
 * @return 引擎实例，失败返回NULL
 */
kv_engine_t* kv_engine_manager_get_engine(kv_engine_manager_t *manager, kv_engine_type_t type);

/**
 * 切换到指定引擎
 * @param manager 引擎管理器
 * @param type 目标引擎类型
 * @return 0成功，负数失败
 */
int kv_engine_manager_switch_engine(kv_engine_manager_t *manager, kv_engine_type_t type);

/**
 * 根据策略选择最优引擎
 * @param manager 引擎管理器
 * @param operation_type 操作类型(读/写/范围查询等)
 * @return 最优引擎，失败返回NULL
 */
kv_engine_t* kv_engine_manager_select_optimal(kv_engine_manager_t *manager, int operation_type);

/**
 * 获取引擎统计信息
 * @param manager 引擎管理器
 * @param stats 统计信息输出缓冲区
 * @return 0成功，负数失败
 */
int kv_engine_manager_get_stats(kv_engine_manager_t *manager, kv_engine_stats_t *stats);

/**
 * 热重载配置
 * @param manager 引擎管理器
 * @param filename 新配置文件路径
 * @return 0成功，负数失败
 */
int kv_engine_manager_reload_config(kv_engine_manager_t *manager, const char *filename);

// === 配置辅助函数 ===

/**
 * 从字符串解析引擎类型
 * @param type_str 引擎类型字符串
 * @return 引擎类型枚举值
 */
kv_engine_type_t kv_engine_type_from_string(const char *type_str);

/**
 * 将引擎类型转为字符串
 * @param type 引擎类型
 * @return 类型字符串
 */
const char* kv_engine_type_to_string_ex(kv_engine_type_t type);

/**
 * 从字符串解析策略类型
 * @param strategy_str 策略字符串
 * @return 策略类型枚举值
 */
kv_engine_strategy_t kv_engine_strategy_from_string(const char *strategy_str);

/**
 * 将策略类型转为字符串
 * @param strategy 策略类型
 * @return 策略字符串
 */
const char* kv_engine_strategy_to_string(kv_engine_strategy_t strategy);

// === 默认配置值 ===
#define KV_ENGINE_DEFAULT_MEMORY_LIMIT      (256 * 1024 * 1024)  // 256MB
#define KV_ENGINE_DEFAULT_CACHE_SIZE        (64 * 1024 * 1024)   // 64MB
#define KV_ENGINE_DEFAULT_WORKER_THREADS    4
#define KV_ENGINE_DEFAULT_IO_THREADS        2
#define KV_ENGINE_DEFAULT_DATA_DIR          "./data"
#define KV_ENGINE_DEFAULT_LISTEN_HOST       "0.0.0.0"
#define KV_ENGINE_DEFAULT_LISTEN_PORT       6379
#define KV_ENGINE_DEFAULT_MAX_CONNECTIONS   1000
#define KV_ENGINE_DEFAULT_CONNECTION_TIMEOUT 30
#define KV_ENGINE_DEFAULT_SYNC_INTERVAL     5

// === 操作类型定义 ===
#define KV_OPERATION_READ                   1
#define KV_OPERATION_WRITE                  2
#define KV_OPERATION_DELETE                 3
#define KV_OPERATION_SCAN                   4
#define KV_OPERATION_BATCH                  5

#ifdef __cplusplus
}
#endif

#endif /* __KV_ENGINE_CONFIG_H__ */ 