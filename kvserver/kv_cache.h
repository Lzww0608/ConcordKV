/*
 * @Author: Lzww0608  
 * @Date: 2025-6-9 19:23:38
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-12 16:44:43
 * @Description: ConcordKV 高性能缓存层接口定义
 */
#ifndef __KV_CACHE_H__
#define __KV_CACHE_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include "kv_error.h"
#include "kv_memory.h"

#ifdef __cplusplus
extern "C" {
#endif


// 缓存策略类型
typedef enum {
    KV_CACHE_POLICY_LRU    = 0,    // 最近最少使用
    KV_CACHE_POLICY_LFU    = 1,    // 最少使用频率
    KV_CACHE_POLICY_FIFO   = 2,    // 先进先出
    KV_CACHE_POLICY_RANDOM = 3,    // 随机淘汰
    KV_CACHE_POLICY_CLOCK  = 4,    // 时钟替换算法
    KV_CACHE_POLICY_ARC    = 5,    // 自适应替换缓存
    KV_CACHE_POLICY_MAX    = 6     // 策略数量
} kv_cache_policy_t;

// 缓存状态
typedef enum {
    KV_CACHE_STATE_INIT        = 0,    // 初始化状态
    KV_CACHE_STATE_RUNNING     = 1,    // 运行状态
    KV_CACHE_STATE_FULL        = 2,    // 缓存已满
    KV_CACHE_STATE_CLEANING    = 3,    // 清理状态
    KV_CACHE_STATE_SHUTDOWN    = 4     // 关闭状态
} kv_cache_state_t;

// 缓存条目状态
typedef enum {
    KV_CACHE_ENTRY_VALID   = 0,    // 有效条目
    KV_CACHE_ENTRY_EXPIRED = 1,    // 已过期
    KV_CACHE_ENTRY_DELETED = 2     // 已删除
} kv_cache_entry_state_t;

// 前向声明
typedef struct kv_cache_entry kv_cache_entry_t;
typedef struct kv_cache kv_cache_t;

// 缓存条目结构
struct kv_cache_entry {
    // 键值数据
    char *key;                      // 键
    size_t key_len;                 // 键长度
    char *value;                    // 值
    size_t value_len;               // 值长度
    
    // 元数据
    uint64_t hash;                  // 键的哈希值
    kv_cache_entry_state_t state;   // 条目状态
    
    // 时间信息
    uint64_t create_time;           // 创建时间戳（微秒）
    uint64_t access_time;           // 最后访问时间戳（微秒）
    uint64_t update_time;           // 最后更新时间戳（微秒）
    uint64_t ttl;                   // 生存时间（微秒，0表示永不过期）
    
    // LRU链表指针
    kv_cache_entry_t *prev;         // 前驱节点
    kv_cache_entry_t *next;         // 后继节点
    
    // LFU频率信息
    uint64_t frequency;             // 访问频率计数
    uint64_t frequency_time;        // 频率更新时间戳
    
    // CLOCK算法信息
    uint8_t reference_bit;          // 引用位（CLOCK算法）
    
    // ARC算法信息
    uint8_t arc_list_type;          // ARC列表类型 (0=T1, 1=T2, 2=B1, 3=B2)
    
    // 哈希表链接
    kv_cache_entry_t *hash_next;    // 哈希表链表下一个节点
    
    // 引用计数和锁
    uint32_t ref_count;             // 引用计数
    pthread_mutex_t entry_lock;     // 条目级锁
};

// 缓存统计信息
typedef struct kv_cache_stats {
    // 基础统计
    uint64_t total_requests;        // 总请求数
    uint64_t cache_hits;            // 缓存命中数
    uint64_t cache_misses;          // 缓存未命中数
    uint64_t evictions;             // 淘汰次数
    uint64_t expirations;           // 过期次数
    
    // 操作统计
    uint64_t get_count;             // get操作次数
    uint64_t set_count;             // set操作次数
    uint64_t delete_count;          // delete操作次数
    uint64_t update_count;          // update操作次数
    
    // 性能统计
    uint64_t avg_get_time;          // 平均get时间（纳秒）
    uint64_t avg_set_time;          // 平均set时间（纳秒）
    uint64_t max_get_time;          // 最大get时间（纳秒）
    uint64_t max_set_time;          // 最大set时间（纳秒）
    
    // 内存统计
    size_t current_entries;         // 当前条目数
    size_t max_entries;             // 最大条目数
    size_t current_memory;          // 当前内存使用（字节）
    size_t max_memory;              // 最大内存限制（字节）
    
    // 时间统计
    uint64_t start_time;            // 缓存启动时间
    uint64_t last_cleanup_time;     // 最后清理时间
} kv_cache_stats_t;

// 缓存配置
typedef struct kv_cache_config {
    // 基础配置
    kv_cache_policy_t policy;       // 缓存策略
    size_t max_entries;             // 最大条目数（0表示无限制）
    size_t max_memory;              // 最大内存（字节，0表示无限制）
    size_t hash_table_size;         // 哈希表大小
    
    // 超时配置
    uint64_t default_ttl;           // 默认TTL（微秒，0表示永不过期）
    uint64_t cleanup_interval;      // 清理间隔（微秒）
    uint64_t max_idle_time;         // 最大空闲时间（微秒）
    
    // 性能配置
    bool enable_stats;              // 是否启用统计
    bool enable_compression;        // 是否启用压缩
    bool thread_safe;               // 是否线程安全
    uint32_t hash_seed;             // 哈希种子
    
    // 淘汰配置
    float eviction_factor;          // 淘汰因子（0.1-0.5，淘汰百分比）
    uint32_t min_eviction_count;    // 最小淘汰数量
    uint32_t max_eviction_count;    // 最大淘汰数量
    
    // LFU特有配置
    uint32_t frequency_decay_time;  // 频率衰减时间（秒）
    float frequency_decay_factor;   // 频率衰减因子（0.5-0.9）
} kv_cache_config_t;

// 哈希表结构
typedef struct kv_cache_hashtable {
    kv_cache_entry_t **buckets;     // 桶数组
    size_t bucket_count;            // 桶数量
    size_t entry_count;             // 条目数量
    uint32_t hash_seed;             // 哈希种子
    pthread_rwlock_t table_lock;    // 哈希表读写锁
} kv_cache_hashtable_t;

// LRU链表结构
typedef struct kv_cache_lru_list {
    kv_cache_entry_t *head;         // 头节点（最新）
    kv_cache_entry_t *tail;         // 尾节点（最旧）
    size_t size;                    // 链表大小
    pthread_mutex_t list_lock;      // 链表锁
} kv_cache_lru_list_t;

// LFU频率管理器
typedef struct kv_cache_frequency_manager {
    uint64_t *frequency_buckets;    // 频率桶
    size_t bucket_count;            // 桶数量
    uint64_t min_frequency;         // 最小频率
    uint64_t max_frequency;         // 最大频率
    uint64_t total_accesses;        // 总访问次数
    pthread_mutex_t freq_lock;      // 频率管理锁
} kv_cache_frequency_manager_t;

// CLOCK算法管理器
typedef struct kv_cache_clock_manager {
    kv_cache_entry_t **entries;     // 环形条目数组
    size_t capacity;                // 环形缓冲区容量
    size_t current_size;            // 当前条目数量
    size_t clock_hand;              // 时钟指针位置
    pthread_mutex_t clock_lock;     // CLOCK算法锁
} kv_cache_clock_manager_t;

// ARC算法管理器
typedef struct kv_cache_arc_manager {
    // T1: 最近访问的页面（LRU部分）
    kv_cache_lru_list_t *t1_list;   // T1链表
    // T2: 频繁访问的页面（LFU部分）
    kv_cache_lru_list_t *t2_list;   // T2链表
    // B1: T1的历史记录
    kv_cache_lru_list_t *b1_list;   // B1链表
    // B2: T2的历史记录
    kv_cache_lru_list_t *b2_list;   // B2链表
    
    size_t p;                       // 自适应参数
    size_t c;                       // 缓存容量
    pthread_mutex_t arc_lock;       // ARC算法锁
} kv_cache_arc_manager_t;

// 主缓存结构
struct kv_cache {
    // 配置和状态
    kv_cache_config_t config;       // 缓存配置
    kv_cache_state_t state;         // 缓存状态
    
    // 数据结构
    kv_cache_hashtable_t *hashtable; // 哈希表
    kv_cache_lru_list_t *lru_list;   // LRU链表
    kv_cache_frequency_manager_t *freq_mgr; // LFU频率管理器
    kv_cache_clock_manager_t *clock_mgr;    // CLOCK算法管理器
    kv_cache_arc_manager_t *arc_mgr;        // ARC算法管理器
    
    // 统计信息
    kv_cache_stats_t stats;         // 统计信息
    
    // 线程管理
    pthread_rwlock_t cache_lock;    // 缓存全局读写锁
    pthread_t cleanup_thread;       // 清理线程
    bool cleanup_running;           // 清理线程是否运行
    pthread_cond_t cleanup_cond;    // 清理条件变量
    pthread_mutex_t cleanup_mutex;  // 清理互斥锁
    
    // 内存管理
    void *(*malloc_func)(size_t);   // 自定义malloc
    void (*free_func)(void*);       // 自定义free
    
    // 时间管理
    uint64_t (*get_time_func)(void); // 获取时间函数
    
    // 调试信息
    char name[64];                  // 缓存名称
    uint32_t magic;                 // 魔数用于验证
};

// 魔数定义
#define KV_CACHE_MAGIC 0x4B564348  // "KVCH"

// === 核心API函数 ===

/**
 * 创建缓存实例
 * @param config 缓存配置
 * @return 缓存实例指针，失败返回NULL
 */
kv_cache_t* kv_cache_create(const kv_cache_config_t *config);

/**
 * 销毁缓存实例
 * @param cache 缓存实例
 */
void kv_cache_destroy(kv_cache_t *cache);

/**
 * 从缓存获取值
 * @param cache 缓存实例
 * @param key 键
 * @param key_len 键长度
 * @param value_out 输出值指针
 * @param value_len_out 输出值长度指针
 * @return 成功返回KV_SUCCESS，失败返回错误码
 */
int kv_cache_get(kv_cache_t *cache, const char *key, size_t key_len, 
                 char **value_out, size_t *value_len_out);

/**
 * 向缓存设置键值对
 * @param cache 缓存实例
 * @param key 键
 * @param key_len 键长度
 * @param value 值
 * @param value_len 值长度
 * @param ttl 生存时间（微秒，0表示使用默认TTL）
 * @return 成功返回KV_SUCCESS，失败返回错误码
 */
int kv_cache_set(kv_cache_t *cache, const char *key, size_t key_len,
                 const char *value, size_t value_len, uint64_t ttl);

/**
 * 从缓存删除键值对
 * @param cache 缓存实例
 * @param key 键
 * @param key_len 键长度
 * @return 成功返回KV_SUCCESS，失败返回错误码
 */
int kv_cache_delete(kv_cache_t *cache, const char *key, size_t key_len);

/**
 * 检查键是否存在
 * @param cache 缓存实例
 * @param key 键
 * @param key_len 键长度
 * @return 存在返回true，不存在返回false
 */
bool kv_cache_exists(kv_cache_t *cache, const char *key, size_t key_len);

/**
 * 清空缓存
 * @param cache 缓存实例
 * @return 成功返回KV_SUCCESS，失败返回错误码
 */
int kv_cache_clear(kv_cache_t *cache);

/**
 * 获取缓存统计信息
 * @param cache 缓存实例
 * @param stats_out 输出统计信息
 * @return 成功返回KV_SUCCESS，失败返回错误码
 */
int kv_cache_get_stats(kv_cache_t *cache, kv_cache_stats_t *stats_out);

/**
 * 重置缓存统计信息
 * @param cache 缓存实例
 * @return 成功返回KV_SUCCESS，失败返回错误码
 */
int kv_cache_reset_stats(kv_cache_t *cache);

// === 高级API函数 ===

/**
 * 批量设置键值对
 * @param cache 缓存实例
 * @param keys 键数组
 * @param key_lens 键长度数组
 * @param values 值数组
 * @param value_lens 值长度数组
 * @param count 键值对数量
 * @param ttl 生存时间（微秒，0表示使用默认TTL）
 * @return 成功返回KV_SUCCESS，失败返回错误码
 */
int kv_cache_set_batch(kv_cache_t *cache, 
                       const char **keys, const size_t *key_lens,
                       const char **values, const size_t *value_lens,
                       size_t count, uint64_t ttl);

/**
 * 批量获取值
 * @param cache 缓存实例
 * @param keys 键数组
 * @param key_lens 键长度数组
 * @param values_out 输出值数组
 * @param value_lens_out 输出值长度数组
 * @param count 键数量
 * @return 成功返回找到的键数量，失败返回-1
 */
int kv_cache_get_batch(kv_cache_t *cache,
                       const char **keys, const size_t *key_lens,
                       char ***values_out, size_t **value_lens_out,
                       size_t count);

/**
 * 批量删除键值对
 * @param cache 缓存实例
 * @param keys 键数组
 * @param key_lens 键长度数组
 * @param count 键数量
 * @return 成功返回删除的键数量，失败返回-1
 */
int kv_cache_delete_batch(kv_cache_t *cache,
                          const char **keys, const size_t *key_lens,
                          size_t count);

/**
 * 设置键的TTL
 * @param cache 缓存实例
 * @param key 键
 * @param key_len 键长度
 * @param ttl 新的TTL（微秒）
 * @return 成功返回KV_SUCCESS，失败返回错误码
 */
int kv_cache_set_ttl(kv_cache_t *cache, const char *key, size_t key_len, uint64_t ttl);

/**
 * 获取键的TTL
 * @param cache 缓存实例
 * @param key 键
 * @param key_len 键长度
 * @param ttl_out 输出TTL指针
 * @return 成功返回KV_SUCCESS，失败返回错误码
 */
int kv_cache_get_ttl(kv_cache_t *cache, const char *key, size_t key_len, uint64_t *ttl_out);

/**
 * 手动触发清理过期条目
 * @param cache 缓存实例
 * @return 成功返回清理的条目数量，失败返回-1
 */
int kv_cache_cleanup_expired(kv_cache_t *cache);

/**
 * 手动触发淘汰LRU/LFU条目
 * @param cache 缓存实例
 * @param count 要淘汰的条目数量
 * @return 成功返回实际淘汰的条目数量，失败返回-1
 */
int kv_cache_evict_entries(kv_cache_t *cache, size_t count);

// === 配置和工具函数 ===

/**
 * 创建默认缓存配置
 * @param policy 缓存策略
 * @return 配置结构体指针，需要调用者释放
 */
kv_cache_config_t* kv_cache_config_create_default(kv_cache_policy_t policy);

/**
 * 销毁缓存配置
 * @param config 配置结构体指针
 */
void kv_cache_config_destroy(kv_cache_config_t *config);

/**
 * 验证缓存配置
 * @param config 配置结构体指针
 * @return 有效返回true，无效返回false
 */
bool kv_cache_config_validate(const kv_cache_config_t *config);

/**
 * 计算缓存命中率
 * @param stats 统计信息
 * @return 命中率（0.0-1.0）
 */
double kv_cache_hit_ratio(const kv_cache_stats_t *stats);

/**
 * 获取缓存策略名称
 * @param policy 缓存策略
 * @return 策略名称字符串
 */
const char* kv_cache_policy_name(kv_cache_policy_t policy);

/**
 * 获取缓存状态名称
 * @param state 缓存状态
 * @return 状态名称字符串
 */
const char* kv_cache_state_name(kv_cache_state_t state);

// === 调试和诊断函数 ===

/**
 * 打印缓存统计信息
 * @param cache 缓存实例
 * @param fp 输出文件指针
 */
void kv_cache_print_stats(kv_cache_t *cache, FILE *fp);

/**
 * 验证缓存完整性
 * @param cache 缓存实例
 * @return 完整返回true，损坏返回false
 */
bool kv_cache_validate_integrity(kv_cache_t *cache);

/**
 * 获取内存使用详情
 * @param cache 缓存实例
 * @param total_out 总内存使用输出
 * @param data_out 数据内存使用输出
 * @param meta_out 元数据内存使用输出
 * @return 成功返回KV_SUCCESS，失败返回错误码
 */
int kv_cache_get_memory_usage(kv_cache_t *cache, 
                              size_t *total_out, size_t *data_out, size_t *meta_out);

// === 线程安全宏定义 ===

#define KV_CACHE_LOCK_READ(cache) \
    do { if ((cache)->config.thread_safe) pthread_rwlock_rdlock(&(cache)->cache_lock); } while(0)

#define KV_CACHE_LOCK_WRITE(cache) \
    do { if ((cache)->config.thread_safe) pthread_rwlock_wrlock(&(cache)->cache_lock); } while(0)

#define KV_CACHE_UNLOCK(cache) \
    do { if ((cache)->config.thread_safe) pthread_rwlock_unlock(&(cache)->cache_lock); } while(0)

#define KV_CACHE_ENTRY_LOCK(entry) \
    do { pthread_mutex_lock(&(entry)->entry_lock); } while(0)

#define KV_CACHE_ENTRY_UNLOCK(entry) \
    do { pthread_mutex_unlock(&(entry)->entry_lock); } while(0)

// === 常量定义 ===

// 默认配置值
#define KV_CACHE_DEFAULT_MAX_ENTRIES    10000
#define KV_CACHE_DEFAULT_MAX_MEMORY     (100 * 1024 * 1024)  // 100MB
#define KV_CACHE_DEFAULT_HASH_SIZE      4096
#define KV_CACHE_DEFAULT_TTL            (3600 * 1000000ULL)   // 1小时（微秒）
#define KV_CACHE_DEFAULT_CLEANUP_INTERVAL (60 * 1000000ULL)  // 60秒（微秒）
#define KV_CACHE_DEFAULT_EVICTION_FACTOR 0.1f                // 10%
#define KV_CACHE_DEFAULT_HASH_SEED      0x12345678

// 时间常量
#define KV_CACHE_MICROSECOND            1ULL
#define KV_CACHE_MILLISECOND            (1000ULL * KV_CACHE_MICROSECOND)
#define KV_CACHE_SECOND                 (1000ULL * KV_CACHE_MILLISECOND)
#define KV_CACHE_MINUTE                 (60ULL * KV_CACHE_SECOND)
#define KV_CACHE_HOUR                   (60ULL * KV_CACHE_MINUTE)

// 性能常量
#define KV_CACHE_MAX_KEY_LENGTH         1024
#define KV_CACHE_MAX_VALUE_LENGTH       (1024 * 1024)  // 1MB
#define KV_CACHE_MIN_HASH_SIZE          64
#define KV_CACHE_MAX_HASH_SIZE          (1024 * 1024)  // 1M桶

// 魔数定义
#define KV_CACHE_MAGIC 0x4B564348  // "KVCH"

#ifdef __cplusplus
}
#endif

#endif /* __KV_CACHE_H__ */ 