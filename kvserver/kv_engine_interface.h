/*
 * @Author: Lzww0608  
 * @Date: 2025-5-30 22:22:24
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-19 11:29:31
 * @Description: ConcordKV 统一存储引擎接口定义
 */
#ifndef __KV_ENGINE_INTERFACE_H__
#define __KV_ENGINE_INTERFACE_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "kv_error.h"

#ifdef __cplusplus
extern "C" {
#endif

// 存储引擎类型枚举
typedef enum {
    KV_ENGINE_ARRAY    = 0,    // 数组存储引擎
    KV_ENGINE_RBTREE   = 1,    // 红黑树存储引擎  
    KV_ENGINE_HASH     = 2,    // 哈希表存储引擎
    KV_ENGINE_BTREE    = 3,    // B+树存储引擎 (待实现)
    KV_ENGINE_LSM      = 4,    // LSM树存储引擎 (待实现)
    KV_ENGINE_MAX      = 5     // 引擎类型数量
} kv_engine_type_t;

// 存储引擎状态
typedef enum {
    KV_ENGINE_STATE_INIT       = 0,    // 初始化状态
    KV_ENGINE_STATE_RUNNING    = 1,    // 运行状态
    KV_ENGINE_STATE_COMPACTING = 2,    // 压缩状态(LSM-Tree使用)
    KV_ENGINE_STATE_FLUSHING   = 3,    // 刷盘状态
    KV_ENGINE_STATE_ERROR      = 4,    // 错误状态
    KV_ENGINE_STATE_SHUTDOWN   = 5     // 关闭状态
} kv_engine_state_t;

// 键值对结构
typedef struct kv_pair {
    char *key;           // 键
    size_t key_len;      // 键长度
    char *value;         // 值
    size_t value_len;    // 值长度
    uint64_t seq_num;    // 序列号(用于LSM-Tree)
    uint8_t deleted;     // 删除标记(墓碑标记)
} kv_pair_t;

// 批量操作结构
typedef struct kv_batch {
    kv_pair_t *pairs;    // 键值对数组
    size_t count;        // 数量
    size_t capacity;     // 容量
} kv_batch_t;

// 扫描范围结构
typedef struct kv_range {
    char *start_key;     // 起始键
    size_t start_len;    // 起始键长度
    char *end_key;       // 结束键
    size_t end_len;      // 结束键长度
    bool start_inclusive; // 起始键是否包含
    bool end_inclusive;   // 结束键是否包含
    size_t limit;        // 返回数量限制
} kv_range_t;

// 迭代器结构
typedef struct kv_iterator {
    void *engine_handle;      // 存储引擎句柄
    void *iter_state;         // 迭代器状态
    kv_pair_t current;        // 当前键值对
    bool valid;               // 迭代器是否有效
    int (*next)(struct kv_iterator *iter);     // 下一个元素
    int (*prev)(struct kv_iterator *iter);     // 上一个元素
    void (*seek)(struct kv_iterator *iter, const char *key); // 定位到指定键
    void (*destroy)(struct kv_iterator *iter); // 销毁迭代器
} kv_iterator_t;

// 存储引擎统计信息
typedef struct kv_engine_stats {
    uint64_t total_keys;         // 总键数量
    uint64_t total_size;         // 总数据大小(字节)
    uint64_t memory_usage;       // 内存使用量(字节)
    uint64_t disk_usage;         // 磁盘使用量(字节)
    uint64_t read_count;         // 读操作计数
    uint64_t write_count;        // 写操作计数
    uint64_t delete_count;       // 删除操作计数
    double read_latency_avg;     // 平均读延迟(毫秒)
    double write_latency_avg;    // 平均写延迟(毫秒)
    
    // LSM-Tree特有统计
    uint32_t levels;             // LSM层数
    uint64_t compaction_count;   // 压缩次数
    uint64_t flush_count;        // 刷盘次数
    
    // B+Tree特有统计  
    uint32_t height;             // 树高度
    uint64_t internal_nodes;     // 内部节点数
    uint64_t leaf_nodes;         // 叶子节点数
} kv_engine_stats_t;

// 存储引擎配置
typedef struct kv_engine_config {
    kv_engine_type_t type;       // 引擎类型
    size_t memory_limit;         // 内存限制(字节)
    size_t cache_size;           // 缓存大小(字节)
    bool enable_compression;     // 是否启用压缩
    bool enable_checksum;        // 是否启用校验和
    char *data_dir;              // 数据目录
    
    // LSM-Tree特有配置
    size_t memtable_size;        // MemTable大小限制
    size_t level0_file_limit;    // Level0文件数限制
    double level_size_multiplier; // 层大小倍数
    
    // B+Tree特有配置
    uint32_t page_size;          // 页面大小
    uint32_t max_keys_per_node;  // 每个节点最大键数
    
    // Hash特有配置
    uint32_t initial_buckets;    // 初始桶数量
    double load_factor;          // 负载因子
} kv_engine_config_t;

// 前向声明存储引擎句柄
typedef struct kv_engine kv_engine_t;

// 存储引擎虚函数表
typedef struct kv_engine_vtable {
    // === 基础CRUD操作 ===
    int (*set)(kv_engine_t *engine, const char *key, const char *value);
    char* (*get)(kv_engine_t *engine, const char *key);
    int (*delete)(kv_engine_t *engine, const char *key);
    int (*update)(kv_engine_t *engine, const char *key, const char *value);
    
    // === 批量操作 ===
    int (*batch_set)(kv_engine_t *engine, const kv_batch_t *batch);
    int (*batch_get)(kv_engine_t *engine, kv_batch_t *batch); // 输入键，输出值
    int (*batch_delete)(kv_engine_t *engine, const char **keys, size_t count);
    
    // === 迭代器操作 ===
    kv_iterator_t* (*create_iterator)(kv_engine_t *engine);
    int (*range_scan)(kv_engine_t *engine, const kv_range_t *range, kv_batch_t *result);
    int (*prefix_scan)(kv_engine_t *engine, const char *prefix, kv_batch_t *result);
    
    // === 统计信息 ===
    int (*count)(kv_engine_t *engine);
    size_t (*size)(kv_engine_t *engine);
    size_t (*memory_usage)(kv_engine_t *engine);
    int (*get_stats)(kv_engine_t *engine, kv_engine_stats_t *stats);
    
    // === 生命周期管理 ===
    int (*init)(kv_engine_t *engine, const kv_engine_config_t *config);
    int (*destroy)(kv_engine_t *engine);
    int (*flush)(kv_engine_t *engine);        // 强制刷盘
    int (*compact)(kv_engine_t *engine);      // 手动压缩(LSM-Tree)
    int (*sync)(kv_engine_t *engine);         // 同步到磁盘
    
    // === 事务支持 ===
    int (*begin_transaction)(kv_engine_t *engine, void **txn_handle);
    int (*commit_transaction)(kv_engine_t *engine, void *txn_handle);
    int (*rollback_transaction)(kv_engine_t *engine, void *txn_handle);
    
    // === 备份和恢复 ===
    int (*create_snapshot)(kv_engine_t *engine, const char *snapshot_path);
    int (*restore_snapshot)(kv_engine_t *engine, const char *snapshot_path);
    
    // === 引擎特定操作 ===
    int (*engine_specific)(kv_engine_t *engine, const char *operation, void *params);
    
    // === 监控相关接口 ===
    int (*init_metrics)(kv_engine_t *engine, void *metrics_manager);
    int (*collect_metrics)(kv_engine_t *engine, void *metrics_set);
    int (*reset_metrics)(kv_engine_t *engine);
    int (*get_engine_specific_metrics)(kv_engine_t *engine, void *metrics_data);
} kv_engine_vtable_t;

// 存储引擎主结构
struct kv_engine {
    kv_engine_type_t type;           // 引擎类型
    kv_engine_state_t state;         // 引擎状态
    kv_engine_config_t config;       // 引擎配置
    kv_engine_vtable_t *vtable;      // 虚函数表
    void *engine_data;               // 引擎私有数据
    pthread_rwlock_t lock;           // 引擎读写锁
    kv_engine_stats_t stats;         // 统计信息
    char name[64];                   // 引擎名称
    
    // === 监控相关字段 ===
    void *metrics_manager;           // 监控管理器引用
    void *metrics_set;               // 该引擎的指标集
    bool metrics_enabled;            // 监控开关
    uint64_t last_metrics_update;    // 上次指标更新时间
};

// === 存储引擎工厂函数 ===
kv_engine_t* kv_engine_create(kv_engine_type_t type, const kv_engine_config_t *config);
int kv_engine_destroy(kv_engine_t *engine);

// === 通用工具函数 ===
const char* kv_engine_type_to_string(kv_engine_type_t type);
const char* kv_engine_state_to_string(kv_engine_state_t state);

// === 键值对操作工具函数 ===
kv_pair_t* kv_pair_create(const char *key, const char *value);
void kv_pair_destroy(kv_pair_t *pair);
kv_pair_t* kv_pair_copy(const kv_pair_t *src);

// === 批量操作工具函数 ===
kv_batch_t* kv_batch_create(size_t capacity);
void kv_batch_destroy(kv_batch_t *batch);
int kv_batch_add(kv_batch_t *batch, const char *key, const char *value);
void kv_batch_clear(kv_batch_t *batch);

// === 配置管理函数 ===
kv_engine_config_t* kv_engine_config_create(kv_engine_type_t type);
void kv_engine_config_destroy(kv_engine_config_t *config);
int kv_engine_config_set_string(kv_engine_config_t *config, const char *key, const char *value);
int kv_engine_config_set_int(kv_engine_config_t *config, const char *key, int64_t value);
int kv_engine_config_set_bool(kv_engine_config_t *config, const char *key, bool value);

// === 统一接口包装宏 ===
#define KV_ENGINE_SET(engine, key, value) \
    ((engine)->vtable->set ? (engine)->vtable->set(engine, key, value) : KV_ERR_NOT_SUPPORTED)

#define KV_ENGINE_GET(engine, key) \
    ((engine)->vtable->get ? (engine)->vtable->get(engine, key) : NULL)

#define KV_ENGINE_DELETE(engine, key) \
    ((engine)->vtable->delete ? (engine)->vtable->delete(engine, key) : KV_ERR_NOT_SUPPORTED)

#define KV_ENGINE_COUNT(engine) \
    ((engine)->vtable->count ? (engine)->vtable->count(engine) : -1)

#define KV_ENGINE_FLUSH(engine) \
    ((engine)->vtable->flush ? (engine)->vtable->flush(engine) : KV_ERR_NOT_SUPPORTED)

#define KV_ENGINE_COMPACT(engine) \
    ((engine)->vtable->compact ? (engine)->vtable->compact(engine) : KV_ERR_NOT_SUPPORTED)

// === 并发安全包装 ===
#define KV_ENGINE_READ_LOCK(engine) pthread_rwlock_rdlock(&(engine)->lock)
#define KV_ENGINE_WRITE_LOCK(engine) pthread_rwlock_wrlock(&(engine)->lock)
#define KV_ENGINE_UNLOCK(engine) pthread_rwlock_unlock(&(engine)->lock)

// === 错误处理增强 ===
#define KV_ENGINE_CHECK_VALID(engine) \
    do { \
        if (!(engine) || (engine)->state == KV_ENGINE_STATE_ERROR || \
            (engine)->state == KV_ENGINE_STATE_SHUTDOWN) { \
            KV_ERROR(KV_ERR_PARAM, "Invalid engine state"); \
            return KV_ERR_PARAM; \
        } \
    } while(0)

#define KV_ENGINE_CHECK_VALID_NULL(engine) \
    do { \
        if (!(engine) || (engine)->state == KV_ENGINE_STATE_ERROR || \
            (engine)->state == KV_ENGINE_STATE_SHUTDOWN) { \
            KV_ERROR(KV_ERR_PARAM, "Invalid engine state"); \
            return NULL; \
        } \
    } while(0)

#ifdef __cplusplus
}
#endif

#endif // __KV_ENGINE_INTERFACE_H__ 