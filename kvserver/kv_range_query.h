/*
 * @Author: Lzww0608  
 * @Date: 2025-6-18 21:45:48
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-18 21:46:00
 * @Description: ConcordKV 统一范围查询和前缀扫描接口
 */
#ifndef __KV_RANGE_QUERY_H__
#define __KV_RANGE_QUERY_H__

#include "kv_engine_interface.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// 范围查询操作类型
typedef enum {
    KV_RANGE_OP_SCAN = 0,      // 范围扫描
    KV_RANGE_OP_PREFIX = 1,    // 前缀扫描
    KV_RANGE_OP_REGEX = 2,     // 正则表达式匹配(扩展功能)
    KV_RANGE_OP_FUZZY = 3      // 模糊匹配(扩展功能)
} kv_range_operation_t;

// 范围查询选项
typedef struct kv_range_options {
    size_t limit;              // 返回结果数量限制(0表示无限制)
    bool reverse;              // 是否反向遍历
    bool keys_only;            // 是否只返回键(不返回值)
    bool count_only;           // 是否只返回计数(不返回键值)
    size_t offset;             // 偏移量(跳过前N个结果)
    int32_t timeout_ms;        // 超时时间(毫秒)
} kv_range_options_t;

// 前缀查询配置
typedef struct kv_prefix_config {
    bool case_sensitive;       // 是否区分大小写
    size_t max_prefix_len;     // 最大前缀长度
    bool use_bloom_filter;     // 是否使用布隆过滤器优化
} kv_prefix_config_t;

// 范围查询结果
typedef struct kv_range_result {
    kv_pair_t *pairs;          // 查询结果数组
    size_t count;              // 实际返回结果数量
    size_t total_scanned;      // 总共扫描的记录数
    bool has_more;             // 是否还有更多结果
    char *next_start_key;      // 下一次查询的起始键(用于分页)
    double elapsed_ms;         // 查询耗时(毫秒)
} kv_range_result_t;

// 范围查询统计信息
typedef struct kv_range_stats {
    uint64_t total_range_queries;      // 总范围查询次数
    uint64_t total_prefix_queries;     // 总前缀查询次数
    uint64_t total_results_returned;   // 总返回结果数
    double avg_query_time_ms;          // 平均查询时间(毫秒)
    uint64_t cache_hits;               // 缓存命中次数
    uint64_t cache_misses;             // 缓存未命中次数
} kv_range_stats_t;

// 范围查询管理器
typedef struct kv_range_manager {
    kv_engine_t *engine;               // 关联的存储引擎
    kv_prefix_config_t prefix_config;  // 前缀查询配置
    kv_range_stats_t stats;            // 统计信息
    pthread_rwlock_t lock;             // 读写锁
    bool initialized;                  // 是否已初始化
} kv_range_manager_t;

// === 核心API函数 ===

/**
 * 创建范围查询管理器
 * @param engine 存储引擎
 * @return 管理器实例，失败返回NULL
 */
kv_range_manager_t* kv_range_manager_create(kv_engine_t *engine);

/**
 * 销毁范围查询管理器
 * @param manager 管理器实例
 */
void kv_range_manager_destroy(kv_range_manager_t *manager);

/**
 * 创建默认查询选项
 * @return 默认选项
 */
kv_range_options_t kv_range_options_default(void);

/**
 * 创建默认前缀配置
 * @return 默认前缀配置
 */
kv_prefix_config_t kv_prefix_config_default(void);

/**
 * 释放查询结果
 * @param result 查询结果
 */
void kv_range_result_destroy(kv_range_result_t *result);

// === 查询功能 ===

/**
 * 范围查询
 * @param manager 管理器实例
 * @param start_key 起始键
 * @param end_key 结束键
 * @param include_start 是否包含起始键
 * @param include_end 是否包含结束键
 * @param options 查询选项
 * @return 查询结果，失败返回NULL
 */
kv_range_result_t* kv_range_scan(kv_range_manager_t *manager,
                                const char *start_key, 
                                const char *end_key,
                                bool include_start, 
                                bool include_end,
                                const kv_range_options_t *options);

/**
 * 前缀查询
 * @param manager 管理器实例
 * @param prefix 前缀字符串
 * @param options 查询选项
 * @return 查询结果，失败返回NULL
 */
kv_range_result_t* kv_prefix_scan(kv_range_manager_t *manager,
                                 const char *prefix,
                                 const kv_range_options_t *options);

/**
 * 批量前缀查询
 * @param manager 管理器实例
 * @param prefixes 前缀数组
 * @param prefix_count 前缀数量
 * @param options 查询选项
 * @return 合并的查询结果，失败返回NULL
 */
kv_range_result_t* kv_multi_prefix_scan(kv_range_manager_t *manager,
                                       const char **prefixes,
                                       size_t prefix_count,
                                       const kv_range_options_t *options);

/**
 * 创建范围查询迭代器
 * @param manager 管理器实例
 * @param start_key 起始键
 * @param end_key 结束键
 * @param include_start 是否包含起始键
 * @param include_end 是否包含结束键
 * @param options 查询选项
 * @return 迭代器实例，失败返回NULL
 */
kv_iterator_t* kv_range_iterator_create(kv_range_manager_t *manager,
                                       const char *start_key,
                                       const char *end_key,
                                       bool include_start,
                                       bool include_end,
                                       const kv_range_options_t *options);

// === 工具函数 ===

/**
 * 检查键是否匹配前缀
 * @param key 键
 * @param prefix 前缀
 * @param case_sensitive 是否区分大小写
 * @return 是否匹配
 */
bool kv_key_match_prefix(const char *key, const char *prefix, bool case_sensitive);

/**
 * 比较键的大小关系
 * @param key1 键1
 * @param key2 键2
 * @return 比较结果(-1, 0, 1)
 */
int kv_key_compare(const char *key1, const char *key2);

/**
 * 生成前缀的下一个键
 * @param prefix 前缀
 * @return 下一个键，失败返回NULL(需要调用者释放)
 */
char* kv_prefix_next_key(const char *prefix);

// === 统计和配置 ===

/**
 * 获取范围查询统计信息
 * @param manager 管理器实例
 * @param stats 输出统计信息
 * @return 错误码
 */
int kv_range_get_stats(kv_range_manager_t *manager, kv_range_stats_t *stats);

/**
 * 重置统计信息
 * @param manager 管理器实例
 * @return 错误码
 */
int kv_range_reset_stats(kv_range_manager_t *manager);

/**
 * 设置前缀配置
 * @param manager 管理器实例
 * @param config 前缀配置
 * @return 错误码
 */
int kv_range_set_prefix_config(kv_range_manager_t *manager,
                              const kv_prefix_config_t *config);

// === 性能优化 ===

/**
 * 预热范围查询缓存
 * @param manager 管理器实例
 * @param prefixes 常用前缀数组
 * @param count 前缀数量
 * @return 错误码
 */
int kv_range_warmup_cache(kv_range_manager_t *manager, 
                         const char **prefixes, 
                         size_t count);

/**
 * 优化范围查询性能
 * @param manager 管理器实例
 * @return 错误码
 */
int kv_range_optimize(kv_range_manager_t *manager);

#ifdef __cplusplus
}
#endif

#endif /* __KV_RANGE_QUERY_H__ */ 