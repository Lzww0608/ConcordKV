/*
 * @Author: Lzww0608  
 * @Date: 2025-6-17 10:00:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-17 10:00:00
 * @Description: ConcordKV 存储引擎监控指标管理器
 */

#ifndef __KV_ENGINE_METRICS_H__
#define __KV_ENGINE_METRICS_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include "kv_engine_interface.h"
#include "../common/metrics/metrics.h"

#ifdef __cplusplus
extern "C" {
#endif

// 引擎特有指标联合体
typedef union kv_engine_specific_metrics {
    struct { // LSM-Tree特有指标
        concord_metric_t *compaction_count;     // 压缩次数计数器
        concord_metric_t *flush_count;          // 刷盘次数计数器
        concord_metric_t *levels;               // 层数仪表盘
        concord_metric_t *memtable_size;        // MemTable大小仪表盘
        concord_metric_t *sstable_count;        // SSTable文件数量仪表盘
    } lsm;
    
    struct { // B+Tree特有指标
        concord_metric_t *node_splits;          // 节点分裂次数计数器
        concord_metric_t *node_merges;          // 节点合并次数计数器
        concord_metric_t *tree_height;          // 树高度仪表盘
        concord_metric_t *internal_nodes;       // 内部节点数仪表盘
        concord_metric_t *leaf_nodes;           // 叶子节点数仪表盘
    } btree;
    
    struct { // Hash特有指标
        concord_metric_t *bucket_collisions;    // 桶冲突次数计数器
        concord_metric_t *load_factor;          // 负载因子仪表盘
        concord_metric_t *rehash_count;         // 重哈希次数计数器
        concord_metric_t *bucket_count;         // 桶数量仪表盘
    } hash;
    
    struct { // RBTree特有指标
        concord_metric_t *rotations;            // 旋转次数计数器
        concord_metric_t *rebalances;           // 重平衡次数计数器
        concord_metric_t *tree_depth;           // 树深度仪表盘
        concord_metric_t *black_height;         // 黑高度仪表盘
    } rbtree;
    
    struct { // Array特有指标
        concord_metric_t *resizes;              // 数组扩容次数计数器
        concord_metric_t *capacity;             // 数组容量仪表盘
        concord_metric_t *utilization;          // 利用率仪表盘
    } array;
} kv_engine_specific_metrics_t;

// 单个引擎的指标集合
typedef struct kv_engine_metrics_set {
    // === 基础CRUD指标 ===
    concord_metric_t *read_count;           // 读操作计数器
    concord_metric_t *write_count;          // 写操作计数器
    concord_metric_t *delete_count;         // 删除操作计数器
    concord_metric_t *update_count;         // 更新操作计数器
    
    // === 性能指标 ===
    concord_metric_t *read_latency;         // 读延迟直方图
    concord_metric_t *write_latency;        // 写延迟直方图
    concord_metric_t *delete_latency;       // 删除延迟直方图
    concord_metric_t *throughput_qps;       // QPS计量表
    
    // === 内存使用指标 ===
    concord_metric_t *memory_usage;         // 内存使用仪表盘(字节)
    concord_metric_t *cache_hit_rate;       // 缓存命中率仪表盘
    concord_metric_t *memory_growth_rate;   // 内存增长速率计量表
    
    // === 错误指标 ===
    concord_metric_t *error_count;          // 错误计数器
    concord_metric_t *timeout_count;        // 超时计数器
    concord_metric_t *not_found_count;      // 未找到计数器
    
    // === 引擎特有指标 ===
    kv_engine_specific_metrics_t engine_specific;
    
    // === 元数据 ===
    kv_engine_type_t engine_type;           // 引擎类型
    char engine_name[64];                   // 引擎名称
    bool initialized;                       // 是否已初始化
    time_t creation_time;                   // 创建时间
} kv_engine_metrics_set_t;

// HTTP服务器配置
typedef struct kv_metrics_http_config {
    bool enabled;                           // 是否启用HTTP服务
    char host[256];                         // 监听主机
    int port;                               // 监听端口
    char path[256];                         // 指标路径
    int max_connections;                    // 最大连接数
    int timeout_seconds;                    // 超时时间
} kv_metrics_http_config_t;

// 监控管理器配置
typedef struct kv_engine_metrics_config {
    bool monitoring_enabled;                // 监控总开关
    uint64_t collection_interval_ms;        // 收集间隔(毫秒)
    size_t retention_samples;               // 保留样本数
    bool enable_latency_histograms;         // 是否启用延迟直方图
    bool enable_engine_specific;            // 是否启用引擎特有指标
    bool enable_memory_tracking;            // 是否启用内存跟踪
    bool enable_alerts;                     // 是否启用告警
    
    // 延迟直方图桶配置
    double latency_buckets[16];             // 延迟桶边界(毫秒)
    int latency_bucket_count;               // 延迟桶数量
    
    // 告警阈值
    double high_latency_threshold_ms;       // 高延迟阈值
    size_t high_memory_threshold_mb;        // 高内存阈值
    double error_rate_threshold;            // 错误率阈值
    
    // HTTP服务器配置
    kv_metrics_http_config_t http_config;
} kv_engine_metrics_config_t;

// 监控管理器主结构
typedef struct kv_engine_metrics_manager {
    concord_metrics_repo_t *repo;           // metrics仓库
    kv_engine_metrics_set_t *engine_metrics[KV_ENGINE_MAX]; // 每个引擎的指标集
    kv_engine_metrics_config_t config;      // 配置信息
    pthread_rwlock_t lock;                  // 线程安全锁
    
    // 运行状态
    bool running;                           // 是否运行中
    pthread_t collection_thread;            // 收集线程
    bool collection_thread_running;         // 收集线程状态
    
    // 统计信息
    uint64_t total_metrics_count;           // 总指标数量
    uint64_t collection_count;              // 收集次数
    uint64_t last_collection_time;          // 上次收集时间
    time_t start_time;                      // 启动时间
    
    // HTTP服务器
    int http_server_fd;                     // HTTP服务器文件描述符
    bool http_server_running;               // HTTP服务器状态
} kv_engine_metrics_manager_t;

// === 管理器生命周期API ===

/**
 * 创建监控管理器
 * @return 监控管理器实例，失败返回NULL
 */
kv_engine_metrics_manager_t* kv_engine_metrics_manager_create(void);

/**
 * 销毁监控管理器
 * @param manager 监控管理器
 */
void kv_engine_metrics_manager_destroy(kv_engine_metrics_manager_t *manager);

/**
 * 启动监控管理器
 * @param manager 监控管理器
 * @return 0成功，负数失败
 */
int kv_engine_metrics_manager_start(kv_engine_metrics_manager_t *manager);

/**
 * 停止监控管理器
 * @param manager 监控管理器
 * @return 0成功，负数失败
 */
int kv_engine_metrics_manager_stop(kv_engine_metrics_manager_t *manager);

// === 配置管理API ===

/**
 * 创建默认配置
 * @return 默认配置实例
 */
kv_engine_metrics_config_t kv_engine_metrics_config_default(void);

/**
 * 从文件加载配置
 * @param manager 监控管理器
 * @param config_file 配置文件路径
 * @return 0成功，负数失败
 */
int kv_engine_metrics_load_config(kv_engine_metrics_manager_t *manager, 
                                 const char *config_file);

/**
 * 应用配置
 * @param manager 监控管理器
 * @param config 配置对象
 * @return 0成功，负数失败
 */
int kv_engine_metrics_apply_config(kv_engine_metrics_manager_t *manager, 
                                  const kv_engine_metrics_config_t *config);

// === 引擎注册管理API ===

/**
 * 注册存储引擎
 * @param manager 监控管理器
 * @param type 引擎类型
 * @param name 引擎名称
 * @return 0成功，负数失败
 */
int kv_engine_metrics_register_engine(kv_engine_metrics_manager_t *manager, 
                                     kv_engine_type_t type, 
                                     const char *name);

/**
 * 注销存储引擎
 * @param manager 监控管理器
 * @param type 引擎类型
 * @return 0成功，负数失败
 */
int kv_engine_metrics_unregister_engine(kv_engine_metrics_manager_t *manager, 
                                       kv_engine_type_t type);

/**
 * 检查引擎是否已注册
 * @param manager 监控管理器
 * @param type 引擎类型
 * @return true已注册，false未注册
 */
bool kv_engine_metrics_is_engine_registered(kv_engine_metrics_manager_t *manager, 
                                           kv_engine_type_t type);

// === 指标收集API ===

/**
 * 记录读操作
 * @param manager 监控管理器
 * @param type 引擎类型
 * @param latency_ms 延迟(毫秒)
 * @return 0成功，负数失败
 */
int kv_engine_metrics_record_read(kv_engine_metrics_manager_t *manager, 
                                 kv_engine_type_t type, 
                                 double latency_ms);

/**
 * 记录写操作
 * @param manager 监控管理器
 * @param type 引擎类型
 * @param latency_ms 延迟(毫秒)
 * @return 0成功，负数失败
 */
int kv_engine_metrics_record_write(kv_engine_metrics_manager_t *manager, 
                                  kv_engine_type_t type, 
                                  double latency_ms);

/**
 * 记录删除操作
 * @param manager 监控管理器
 * @param type 引擎类型
 * @param latency_ms 延迟(毫秒)
 * @return 0成功，负数失败
 */
int kv_engine_metrics_record_delete(kv_engine_metrics_manager_t *manager, 
                                   kv_engine_type_t type, 
                                   double latency_ms);

/**
 * 记录更新操作
 * @param manager 监控管理器
 * @param type 引擎类型
 * @param latency_ms 延迟(毫秒)
 * @return 0成功，负数失败
 */
int kv_engine_metrics_record_update(kv_engine_metrics_manager_t *manager, 
                                   kv_engine_type_t type, 
                                   double latency_ms);

/**
 * 更新内存使用量
 * @param manager 监控管理器
 * @param type 引擎类型
 * @param bytes 内存字节数
 * @return 0成功，负数失败
 */
int kv_engine_metrics_update_memory_usage(kv_engine_metrics_manager_t *manager, 
                                         kv_engine_type_t type, 
                                         size_t bytes);

/**
 * 记录错误
 * @param manager 监控管理器
 * @param type 引擎类型
 * @param error_type 错误类型
 * @return 0成功，负数失败
 */
int kv_engine_metrics_record_error(kv_engine_metrics_manager_t *manager, 
                                  kv_engine_type_t type, 
                                  const char *error_type);

// === 引擎特有指标API ===

/**
 * 记录LSM压缩操作
 * @param manager 监控管理器
 * @return 0成功，负数失败
 */
int kv_engine_metrics_record_lsm_compaction(kv_engine_metrics_manager_t *manager);

/**
 * 记录LSM刷盘操作
 * @param manager 监控管理器
 * @return 0成功，负数失败
 */
int kv_engine_metrics_record_lsm_flush(kv_engine_metrics_manager_t *manager);

/**
 * 记录B+Tree节点分裂
 * @param manager 监控管理器
 * @return 0成功，负数失败
 */
int kv_engine_metrics_record_btree_split(kv_engine_metrics_manager_t *manager);

/**
 * 记录Hash桶冲突
 * @param manager 监控管理器
 * @return 0成功，负数失败
 */
int kv_engine_metrics_record_hash_collision(kv_engine_metrics_manager_t *manager);

// === 查询导出API ===

/**
 * 获取引擎统计信息
 * @param manager 监控管理器
 * @param type 引擎类型
 * @param stats 输出统计信息
 * @return 0成功，负数失败
 */
int kv_engine_metrics_get_stats(kv_engine_metrics_manager_t *manager, 
                               kv_engine_type_t type, 
                               kv_engine_stats_t *stats);

/**
 * 导出指标数据
 * @param manager 监控管理器
 * @param format 输出格式
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @return 0成功，负数失败
 */
int kv_engine_metrics_export(kv_engine_metrics_manager_t *manager, 
                            concord_metrics_format_t format, 
                            char *buffer, 
                            size_t size);

/**
 * 获取管理器状态信息
 * @param manager 监控管理器
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @return 0成功，负数失败
 */
int kv_engine_metrics_get_manager_status(kv_engine_metrics_manager_t *manager, 
                                        char *buffer, 
                                        size_t size);

// === HTTP服务API ===

/**
 * 启动HTTP监控服务
 * @param manager 监控管理器
 * @return 0成功，负数失败
 */
int kv_engine_metrics_start_http_server(kv_engine_metrics_manager_t *manager);

/**
 * 停止HTTP监控服务
 * @param manager 监控管理器
 * @return 0成功，负数失败
 */
int kv_engine_metrics_stop_http_server(kv_engine_metrics_manager_t *manager);

// === 工具函数 ===

/**
 * 获取当前时间(微秒)
 * @return 当前时间戳
 */
uint64_t kv_engine_metrics_get_time_us(void);

/**
 * 格式化引擎指标名称
 * @param type 引擎类型
 * @param metric_name 指标名称
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @return 格式化后的字符串长度
 */
int kv_engine_metrics_format_metric_name(kv_engine_type_t type, 
                                        const char *metric_name, 
                                        char *buffer, 
                                        size_t size);

#ifdef __cplusplus
}
#endif

#endif // __KV_ENGINE_METRICS_H__ 