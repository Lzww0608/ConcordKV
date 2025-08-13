/*
 * @Author: Lzww0608  
 * @Date: 2025-6-6 16:19:35
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-6 17:13:10
 * @Description: ConcordKV LSM-Tree 优化布隆过滤器实现
 */
#ifndef __LSM_BLOOM_FILTER_OPTIMIZED_H__
#define __LSM_BLOOM_FILTER_OPTIMIZED_H__

#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

// === 布隆过滤器优化常量 ===
#define BLOOM_CACHE_LINE_SIZE       64              // 缓存行大小
#define BLOOM_CACHE_LINE_BITS       (BLOOM_CACHE_LINE_SIZE * 8)
#define BLOOM_REGISTER_SIZE         64              // 寄存器大小(bits)
#define BLOOM_MIN_EXPECTED_ENTRIES  100             // 最小预期条目数
#define BLOOM_MAX_EXPECTED_ENTRIES  (100 * 1000 * 1000)  // 最大预期条目数
#define BLOOM_MIN_FALSE_POSITIVE    1               // 最小误判率(万分之一)
#define BLOOM_MAX_FALSE_POSITIVE    5000            // 最大误判率(万分之一)
#define BLOOM_DEFAULT_FALSE_POSITIVE 100           // 默认误判率(1%)

// === 布隆过滤器类型 ===
typedef enum {
    BLOOM_TYPE_STANDARD = 0,    // 标准布隆过滤器
    BLOOM_TYPE_BLOCKED,         // 块布隆过滤器(缓存行优化)
    BLOOM_TYPE_REGISTER_BLOCKED,// 寄存器块布隆过滤器
    BLOOM_TYPE_SCALABLE,        // 可扩展布隆过滤器
    BLOOM_TYPE_COUNTING,        // 计数布隆过滤器
    BLOOM_TYPE_AUTO             // 自动选择最优类型
} bloom_filter_type_t;

// === 哈希函数类型 ===
typedef enum {
    BLOOM_HASH_MURMUR3 = 0,     // MurmurHash3
    BLOOM_HASH_FNV1A,           // FNV-1a
    BLOOM_HASH_CRC32,           // CRC32
    BLOOM_HASH_XXHASH,          // xxHash
    BLOOM_HASH_CITY,            // CityHash
    BLOOM_HASH_HYBRID           // 混合哈希(自动选择)
} bloom_hash_type_t;

// === 优化策略 ===
typedef enum {
    BLOOM_OPT_NONE = 0,         // 无优化
    BLOOM_OPT_MEMORY = 1,       // 内存优化
    BLOOM_OPT_SPEED = 2,        // 速度优化
    BLOOM_OPT_BALANCED = 4,     // 平衡优化
    BLOOM_OPT_CACHE_FRIENDLY = 8,// 缓存友好
    BLOOM_OPT_SIMD = 16,        // SIMD优化
    BLOOM_OPT_AUTO = 32         // 自动优化
} bloom_optimization_flags_t;

// === 布隆过滤器配置 ===
typedef struct bloom_filter_config_s {
    uint64_t expected_entries;      // 预期条目数
    uint32_t false_positive_rate;   // 误判率(万分之一)
    bloom_filter_type_t type;       // 过滤器类型
    bloom_hash_type_t hash_type;    // 哈希函数类型
    uint32_t optimization_flags;    // 优化标志
    uint32_t max_hash_functions;    // 最大哈希函数数量
    uint32_t block_size;            // 块大小(bits)
    bool enable_statistics;         // 启用统计
    bool enable_auto_resize;        // 启用自动调整大小
    double load_factor_threshold;   // 负载因子阈值
} bloom_filter_config_t;

// === 布隆过滤器统计信息 ===
typedef struct bloom_filter_stats_s {
    uint64_t total_insertions;      // 总插入次数
    uint64_t total_queries;         // 总查询次数
    uint64_t total_hits;            // 命中次数
    uint64_t total_misses;          // 未命中次数
    uint64_t false_positives;       // 误判次数
    uint64_t true_negatives;        // 真阴性次数
    
    double actual_fpr;              // 实际误判率
    double fill_ratio;              // 填充率
    double bits_per_element;        // 每元素位数
    uint64_t memory_usage;          // 内存使用量
    
    struct timeval last_update;     // 最后更新时间
    pthread_rwlock_t stats_lock;    // 统计读写锁
} bloom_filter_stats_t;

// === 块布隆过滤器 ===
typedef struct bloom_block_s {
    uint8_t *bits;                  // 位数组
    uint32_t size_bits;             // 大小(bits)
    uint32_t hash_count;            // 哈希函数数量
    uint32_t entry_count;           // 条目数量
    uint32_t max_entries;           // 最大条目数
} bloom_block_t;

// === 可扩展布隆过滤器层 ===
typedef struct scalable_bloom_layer_s {
    uint8_t *bits;                  // 位数组
    size_t bit_count;               // 位数组大小
    uint32_t hash_count;            // 哈希函数数量
    uint64_t max_entries;           // 最大条目数
    uint64_t current_entries;       // 当前条目数
    uint32_t false_positive_rate;   // 误判率
    struct scalable_bloom_layer_s *next; // 下一层
} scalable_bloom_layer_t;

// === 优化的布隆过滤器主结构 ===
typedef struct optimized_bloom_filter_s {
    bloom_filter_config_t config;   // 配置信息
    bloom_filter_stats_t stats;     // 统计信息
    
    // 基础参数
    size_t bit_count;               // 位数组大小
    uint32_t hash_count;            // 哈希函数数量
    uint64_t max_entries;           // 最大条目数
    uint64_t current_entries;       // 当前条目数
    
    // 不同类型的实现
    union {
        struct {
            uint8_t *bits;          // 标准位数组
        } standard;
        
        struct {
            bloom_block_t *blocks;  // 块数组
            uint32_t block_count;   // 块数量
            uint32_t block_mask;    // 块掩码(用于快速取模)
        } blocked;
        
        struct {
            uint64_t *registers;    // 寄存器数组
            uint32_t register_count;// 寄存器数量
            uint32_t register_mask; // 寄存器掩码
        } register_blocked;
        
        struct {
            scalable_bloom_layer_t *first_layer; // 第一层
            scalable_bloom_layer_t *current_layer; // 当前层
            uint32_t layer_count;   // 层数量
            double growth_factor;   // 增长因子
        } scalable;
        
        struct {
            uint32_t *counters;     // 计数器数组
            uint32_t max_count;     // 最大计数值
        } counting;
    };
    
    // 性能优化
    uint32_t *hash_seeds;           // 哈希种子数组
    uint64_t salt;                  // 盐值
    
    // 线程安全
    pthread_rwlock_t lock;          // 读写锁
    bool thread_safe;               // 是否线程安全
    
    // 元数据
    uint64_t creation_time;         // 创建时间
    char name[64];                  // 过滤器名称
} optimized_bloom_filter_t;

// === 布隆过滤器参数计算器 ===
typedef struct bloom_parameter_calculator_s {
    uint64_t expected_entries;      // 预期条目数
    uint32_t target_fpr;           // 目标误判率
    
    // 计算结果
    size_t optimal_bit_count;       // 最优位数组大小
    uint32_t optimal_hash_count;    // 最优哈希函数数量
    double bits_per_element;        // 每元素位数
    double actual_fpr;              // 实际误判率
    size_t memory_requirement;      // 内存需求
    
    // 性能预测
    double insert_ops_per_sec;      // 预计插入性能
    double query_ops_per_sec;       // 预计查询性能
    uint32_t cache_misses_per_op;   // 每操作缓存未命中数
} bloom_parameter_calculator_t;

// === 布隆过滤器API ===

/**
 * 创建默认配置
 * @param expected_entries 预期条目数
 * @param false_positive_rate 误判率(万分之一)
 * @return 配置结构体
 */
bloom_filter_config_t bloom_filter_create_default_config(uint64_t expected_entries, uint32_t false_positive_rate);

/**
 * 优化配置参数
 * @param config 待优化的配置
 * @param optimization_target 优化目标
 * @return 成功返回0，失败返回错误码
 */
int bloom_filter_optimize_config(bloom_filter_config_t *config, bloom_optimization_flags_t optimization_target);

/**
 * 计算最优参数
 * @param calculator 参数计算器
 * @return 错误码
 */
int bloom_filter_calculate_parameters(bloom_parameter_calculator_t *calculator);

/**
 * 创建优化的布隆过滤器
 * @param config 配置信息
 * @return 布隆过滤器指针，失败返回NULL
 */
optimized_bloom_filter_t* bloom_filter_create_optimized(const bloom_filter_config_t *config);

/**
 * 销毁优化的布隆过滤器
 * @param filter 布隆过滤器指针
 */
void bloom_filter_destroy_optimized(optimized_bloom_filter_t *filter);

/**
 * 向布隆过滤器添加元素
 * @param filter 布隆过滤器指针
 * @param key 键值
 * @param key_len 键值长度
 * @return 错误码
 */
int bloom_filter_add_optimized(optimized_bloom_filter_t *filter, const void *key, size_t key_len);

/**
 * 检查元素是否可能存在
 * @param filter 布隆过滤器指针
 * @param key 键值
 * @param key_len 键值长度
 * @return true表示可能存在，false表示肯定不存在
 */
bool bloom_filter_may_contain_optimized(optimized_bloom_filter_t *filter, const void *key, size_t key_len);

/**
 * 删除元素（仅支持计数布隆过滤器）
 * @param filter 布隆过滤器指针
 * @param key 键值
 * @param key_len 键值长度
 * @return 错误码
 */
int bloom_filter_remove_optimized(optimized_bloom_filter_t *filter, const void *key, size_t key_len);

/**
 * 批量添加元素
 * @param filter 过滤器指针
 * @param keys 键数组
 * @param key_lens 键长度数组
 * @param count 元素数量
 * @return 成功添加的元素数量
 */
uint64_t bloom_filter_add_batch_optimized(optimized_bloom_filter_t *filter, 
                                         const void **keys, const size_t *key_lens, uint64_t count);

/**
 * 批量查询元素
 * @param filter 过滤器指针
 * @param keys 键数组
 * @param key_lens 键长度数组
 * @param results 结果数组
 * @param count 元素数量
 * @return 成功查询的元素数量
 */
uint64_t bloom_filter_query_batch_optimized(optimized_bloom_filter_t *filter,
                                           const void **keys, const size_t *key_lens, 
                                           bool *results, uint64_t count);

/**
 * 获取统计信息
 * @param filter 过滤器指针
 * @param stats 统计信息输出
 * @return 成功返回0，失败返回错误码
 */
int bloom_filter_get_stats_optimized(optimized_bloom_filter_t *filter, bloom_filter_stats_t *stats);

/**
 * 重置统计信息
 * @param filter 过滤器指针
 * @return 成功返回0，失败返回错误码
 */
int bloom_filter_reset_stats_optimized(optimized_bloom_filter_t *filter);

/**
 * 自动调整过滤器大小
 * @param filter 过滤器指针
 * @return 成功返回0，失败返回错误码
 */
int bloom_filter_auto_resize_optimized(optimized_bloom_filter_t *filter);

/**
 * 估算内存使用量
 * @param config 配置参数
 * @return 估算的内存使用量(字节)
 */
size_t bloom_filter_estimate_memory_optimized(const bloom_filter_config_t *config);

/**
 * 序列化过滤器
 * @param filter 过滤器指针
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return 成功返回序列化大小，失败返回-1
 */
ssize_t bloom_filter_serialize_optimized(optimized_bloom_filter_t *filter, void *buffer, size_t buffer_size);

/**
 * 反序列化过滤器
 * @param buffer 输入缓冲区
 * @param buffer_size 缓冲区大小
 * @return 成功返回过滤器指针，失败返回NULL
 */
optimized_bloom_filter_t* bloom_filter_deserialize_optimized(const void *buffer, size_t buffer_size);

/**
 * 合并两个过滤器
 * @param filter1 第一个过滤器
 * @param filter2 第二个过滤器
 * @return 成功返回合并后的过滤器，失败返回NULL
 */
optimized_bloom_filter_t* bloom_filter_merge_optimized(optimized_bloom_filter_t *filter1, 
                                                      optimized_bloom_filter_t *filter2);

/**
 * 比较两个过滤器的相似度
 * @param filter1 第一个过滤器
 * @param filter2 第二个过滤器
 * @return 相似度(0.0-1.0)
 */
double bloom_filter_similarity_optimized(optimized_bloom_filter_t *filter1, optimized_bloom_filter_t *filter2);

// === 高级哈希函数API ===

/**
 * MurmurHash3 32位版本
 * @param key 键
 * @param key_len 键长度
 * @param seed 种子
 * @return 哈希值
 */
uint32_t bloom_hash_murmur3_32(const void *key, size_t key_len, uint32_t seed);

/**
 * MurmurHash3 64位版本
 * @param key 键
 * @param key_len 键长度
 * @param seed 种子
 * @return 哈希值
 */
uint64_t bloom_hash_murmur3_64(const void *key, size_t key_len, uint32_t seed);

/**
 * FNV-1a 32位版本
 * @param key 键
 * @param key_len 键长度
 * @param seed 种子
 * @return 哈希值
 */
uint32_t bloom_hash_fnv1a_32(const void *key, size_t key_len, uint32_t seed);

/**
 * FNV-1a 64位版本
 * @param key 键
 * @param key_len 键长度
 * @param seed 种子
 * @return 哈希值
 */
uint64_t bloom_hash_fnv1a_64(const void *key, size_t key_len, uint32_t seed);

/**
 * xxHash 32位版本
 * @param key 键
 * @param key_len 键长度
 * @param seed 种子
 * @return 哈希值
 */
uint32_t bloom_hash_xxhash_32(const void *key, size_t key_len, uint32_t seed);

/**
 * xxHash 64位版本
 * @param key 键
 * @param key_len 键长度
 * @param seed 种子
 * @return 哈希值
 */
uint64_t bloom_hash_xxhash_64(const void *key, size_t key_len, uint32_t seed);

/**
 * 自动选择最优哈希函数
 * @param expected_entries 预期条目数
 * @param hash_count 哈希函数数量
 * @return 推荐的哈希函数类型
 */
bloom_hash_type_t bloom_hash_auto_select(uint64_t expected_entries, uint32_t hash_count);

// === 性能测试和基准测试API ===

/**
 * 运行性能基准测试
 * @param filter 过滤器指针
 * @param test_data_size 测试数据大小
 * @return 成功返回0，失败返回错误码
 */
int bloom_filter_benchmark_optimized(optimized_bloom_filter_t *filter, uint64_t test_data_size);

/**
 * 测试误判率
 * @param filter 过滤器指针
 * @param test_size 测试大小
 * @return 实际测得的误判率
 */
double bloom_filter_test_false_positive_rate(optimized_bloom_filter_t *filter, uint64_t test_size);

/**
 * 内存使用情况分析
 * @param filter 过滤器指针
 * @param detailed 是否输出详细信息
 * @return 成功返回0，失败返回错误码
 */
int bloom_filter_analyze_memory_usage(optimized_bloom_filter_t *filter, bool detailed);

#ifdef __cplusplus
}
#endif

#endif // __LSM_BLOOM_FILTER_OPTIMIZED_H__ 