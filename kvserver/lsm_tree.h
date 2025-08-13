/*
 * @Author: Lzww0608  
 * @Date: 2025-6-3 14:30:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-7 17:37:06
 * @Description: ConcordKV LSM-Tree主控制器实现
 */
#ifndef __LSM_TREE_H__
#define __LSM_TREE_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <pthread.h>

#include "kv_store.h"
#include "kv_error.h"
#include "lsm_memtable.h"
#include "lsm_memtable_manager.h"
#include "lsm_compaction.h"

#ifdef __cplusplus
extern "C" {
#endif

// === LSM-Tree配置参数 ===

#define LSM_TREE_DEFAULT_MEMTABLE_SIZE      (2 * 1024 * 1024)  // 2MB
#define LSM_TREE_DEFAULT_MAX_IMMUTABLE      6                   // 最大不可变MemTable数
#define LSM_TREE_DEFAULT_L0_FILE_LIMIT      4                   // Level-0文件数限制
#define LSM_TREE_DEFAULT_WRITE_BUFFER_SIZE  (4 * 1024 * 1024)  // 4MB写入缓冲
#define LSM_TREE_DEFAULT_BLOCK_SIZE         4096                // 4KB块大小
#define LSM_TREE_DEFAULT_WAL_SIZE_LIMIT     (64 * 1024 * 1024) // 64MB WAL限制

// === 前向声明 ===
typedef struct lsm_tree_s lsm_tree_t;
typedef struct lsm_wal_s lsm_wal_t;
typedef struct lsm_manifest_s lsm_manifest_t;

// === LSM-Tree配置结构 ===
typedef struct lsm_tree_config_s {
    // 基础配置
    char data_dir[256];                     // 数据目录
    char wal_dir[256];                      // WAL目录
    
    // MemTable配置
    size_t memtable_size;                   // MemTable最大大小
    int max_immutable_count;                // 最大不可变MemTable数量
    
    // 压缩配置
    lsm_compaction_config_t compaction;     // 压缩配置
    
    // WAL配置
    bool enable_wal;                        // 是否启用WAL
    size_t wal_size_limit;                  // WAL文件大小限制
    bool sync_writes;                       // 是否同步写入
    
    // 性能配置
    size_t write_buffer_size;               // 写入缓冲区大小
    size_t block_size;                      // 块大小
    bool enable_compression;                // 是否启用压缩
    
    // 错误恢复配置
    bool enable_paranoid_checks;            // 是否启用严格检查
    int max_recovery_attempts;              // 最大恢复尝试次数
} lsm_tree_config_t;

// === LSM-Tree统计信息 ===
typedef struct lsm_tree_stats_s {
    // 基础统计
    uint64_t total_reads;                   // 总读取次数
    uint64_t total_writes;                  // 总写入次数
    uint64_t total_deletes;                 // 总删除次数
    uint64_t total_bytes_read;              // 总读取字节数
    uint64_t total_bytes_written;           // 总写入字节数
    
    // MemTable统计
    lsm_memtable_manager_stats_t memtable_stats;    // MemTable统计
    
    // 压缩统计
    lsm_compaction_stats_t compaction_stats; // 压缩统计
    
    // 缓存统计
    uint64_t cache_hits;                    // 缓存命中次数
    uint64_t cache_misses;                  // 缓存未命中次数
    
    // 错误统计
    uint64_t read_errors;                   // 读取错误次数
    uint64_t write_errors;                  // 写入错误次数
    uint64_t recovery_count;                // 恢复次数
} lsm_tree_stats_t;

// === 简化WAL实现（用于崩溃恢复） ===
typedef enum {
    LSM_WAL_RECORD_PUT = 1,                 // 写入记录
    LSM_WAL_RECORD_DELETE,                  // 删除记录
    LSM_WAL_RECORD_CHECKPOINT,              // 检查点记录
    LSM_WAL_RECORD_COMMIT                   // 提交记录
} lsm_wal_record_type_t;

typedef struct lsm_wal_record_s {
    lsm_wal_record_type_t type;             // 记录类型
    uint64_t seq_num;                       // 序列号
    uint64_t timestamp;                     // 时间戳
    uint32_t key_len;                       // 键长度
    uint32_t value_len;                     // 值长度
    uint32_t crc32;                         // CRC32校验和
    // 之后是键和值的数据
} lsm_wal_record_t;

typedef struct lsm_wal_s {
    int fd;                                 // 文件描述符
    char filename[256];                     // WAL文件名
    size_t file_size;                       // 当前文件大小
    size_t max_size;                        // 最大文件大小
    uint64_t seq_num;                       // 当前序列号
    pthread_mutex_t lock;                   // WAL锁
    bool sync_writes;                       // 是否同步写入
} lsm_wal_t;

// === 简化Manifest实现（元数据管理） ===
typedef struct lsm_manifest_s {
    int fd;                                 // 文件描述符
    char filename[256];                     // Manifest文件名
    uint64_t version;                       // 版本号
    pthread_mutex_t lock;                   // Manifest锁
    
    // 当前LSM-Tree状态
    int level_file_counts[LSM_MAX_LEVELS];  // 各层级文件数量
    char current_memtable_id[64];           // 当前MemTable ID
    uint64_t next_file_id;                  // 下一个文件ID
} lsm_manifest_t;

// === LSM-Tree主控制器 ===
typedef struct lsm_tree_s {
    // 核心组件
    lsm_memtable_manager_t *mem_mgr;        // MemTable管理器
    lsm_level_manager_t *level_mgr;         // 层级管理器
    lsm_compaction_scheduler_t *compactor;  // 压缩调度器
    lsm_wal_t *wal;                         // WAL日志
    lsm_manifest_t *manifest;               // 元数据管理
    
    // 配置和状态
    lsm_tree_config_t config;               // 配置
    lsm_tree_stats_t stats;                 // 统计信息
    bool is_open;                           // 是否已打开
    
    // 并发控制
    pthread_rwlock_t tree_lock;             // 树级别的读写锁
    pthread_mutex_t stats_lock;             // 统计锁
    
    // 错误恢复状态
    bool in_recovery;                       // 是否在恢复过程中
    uint64_t recovery_seq_num;              // 恢复序列号
} lsm_tree_t;

// === LSM-Tree生命周期管理 ===

/**
 * 创建默认LSM-Tree配置
 * @param data_dir 数据目录
 * @return LSM-Tree配置
 */
lsm_tree_config_t lsm_tree_default_config(const char *data_dir);

/**
 * 创建LSM-Tree实例
 * @param config 配置参数
 * @return 成功返回LSM-Tree指针，失败返回NULL
 */
lsm_tree_t* lsm_tree_create(const lsm_tree_config_t *config);

/**
 * 打开LSM-Tree（包含崩溃恢复）
 * @param tree LSM-Tree指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_tree_open(lsm_tree_t *tree);

/**
 * 关闭LSM-Tree
 * @param tree LSM-Tree指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_tree_close(lsm_tree_t *tree);

/**
 * 销毁LSM-Tree实例
 * @param tree LSM-Tree指针
 */
void lsm_tree_destroy(lsm_tree_t *tree);

// === LSM-Tree基础操作 ===

/**
 * 插入或更新键值对
 * @param tree LSM-Tree指针
 * @param key 键
 * @param key_len 键长度
 * @param value 值
 * @param value_len 值长度
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_tree_put(lsm_tree_t *tree, const char *key, size_t key_len,
                 const char *value, size_t value_len);

/**
 * 查找键值对
 * @param tree LSM-Tree指针
 * @param key 键
 * @param key_len 键长度
 * @param value 输出值缓冲区指针
 * @param value_len 输出值长度指针
 * @return 成功返回KV_ERR_NONE，未找到返回KV_ERR_NOT_FOUND，失败返回错误码
 */
int lsm_tree_get(lsm_tree_t *tree, const char *key, size_t key_len,
                 char **value, size_t *value_len);

/**
 * 删除键值对
 * @param tree LSM-Tree指针
 * @param key 键
 * @param key_len 键长度
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_tree_delete(lsm_tree_t *tree, const char *key, size_t key_len);

// === LSM-Tree管理操作 ===

/**
 * 手动触发压缩
 * @param tree LSM-Tree指针
 * @param level 要压缩的层级（-1表示自动选择）
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_tree_compact(lsm_tree_t *tree, int level);

/**
 * 创建快照（检查点）
 * @param tree LSM-Tree指针
 * @param snapshot_name 快照名称
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_tree_create_snapshot(lsm_tree_t *tree, const char *snapshot_name);

/**
 * 刷新所有挂起的写入
 * @param tree LSM-Tree指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_tree_flush(lsm_tree_t *tree);

/**
 * 获取LSM-Tree统计信息
 * @param tree LSM-Tree指针
 * @param stats 输出统计信息
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_tree_get_stats(lsm_tree_t *tree, lsm_tree_stats_t *stats);

// === 崩溃恢复相关API ===

/**
 * 检查LSM-Tree是否需要恢复
 * @param config 配置参数
 * @return true表示需要恢复，false表示不需要
 */
bool lsm_tree_needs_recovery(const lsm_tree_config_t *config);

/**
 * 执行崩溃恢复
 * @param tree LSM-Tree指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_tree_recover(lsm_tree_t *tree);

// === WAL相关API ===

/**
 * 创建WAL实例
 * @param wal_dir WAL目录
 * @param max_size 最大文件大小
 * @param sync_writes 是否同步写入
 * @return 成功返回WAL指针，失败返回NULL
 */
lsm_wal_t* lsm_wal_create(const char *wal_dir, size_t max_size, bool sync_writes);

/**
 * 创建WAL实例（支持恢复模式）
 * @param wal_dir WAL目录
 * @param max_size 最大文件大小
 * @param sync_writes 是否同步写入
 * @param recovery_mode 是否为恢复模式
 * @return 成功返回WAL指针，失败返回NULL
 */
lsm_wal_t* lsm_wal_create_with_recovery(const char *wal_dir, size_t max_size, bool sync_writes, bool recovery_mode);

/**
 * 销毁WAL实例
 * @param wal WAL指针
 */
void lsm_wal_destroy(lsm_wal_t *wal);

/**
 * 写入WAL记录
 * @param wal WAL指针
 * @param type 记录类型
 * @param key 键
 * @param key_len 键长度
 * @param value 值
 * @param value_len 值长度
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_wal_write(lsm_wal_t *wal, lsm_wal_record_type_t type,
                  const char *key, size_t key_len,
                  const char *value, size_t value_len);

/**
 * 回放WAL记录进行恢复
 * @param wal WAL指针
 * @param tree LSM-Tree指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_wal_replay(lsm_wal_t *wal, lsm_tree_t *tree);

// === Manifest相关API ===

/**
 * 创建Manifest实例
 * @param data_dir 数据目录
 * @return 成功返回Manifest指针，失败返回NULL
 */
lsm_manifest_t* lsm_manifest_create(const char *data_dir);

/**
 * 销毁Manifest实例
 * @param manifest Manifest指针
 */
void lsm_manifest_destroy(lsm_manifest_t *manifest);

/**
 * 保存LSM-Tree当前状态到Manifest
 * @param manifest Manifest指针
 * @param tree LSM-Tree指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_manifest_save_state(lsm_manifest_t *manifest, lsm_tree_t *tree);

/**
 * 从Manifest加载LSM-Tree状态
 * @param manifest Manifest指针
 * @param tree LSM-Tree指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_manifest_load_state(lsm_manifest_t *manifest, lsm_tree_t *tree);

// === 工具函数 ===

/**
 * 验证LSM-Tree配置的有效性
 * @param config 配置参数
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_tree_validate_config(const lsm_tree_config_t *config);

/**
 * 获取LSM-Tree版本信息
 * @return 版本字符串
 */
const char* lsm_tree_version(void);

// === 批量写入操作 ===

#define LSM_TREE_DEFAULT_BATCH_SIZE     100        // 默认批次大小
#define LSM_TREE_MAX_BATCH_SIZE         10000      // 最大批次大小  
#define LSM_TREE_MIN_BATCH_SIZE         1          // 最小批次大小

// 批量操作类型
typedef enum {
    LSM_BATCH_OP_PUT = 1,                          // 写入操作
    LSM_BATCH_OP_DELETE                            // 删除操作  
} lsm_batch_op_type_t;

// 批量操作条目
typedef struct lsm_batch_entry_s {
    lsm_batch_op_type_t op_type;                   // 操作类型
    char *key;                                     // 键（拷贝存储）
    size_t key_len;                                // 键长度
    char *value;                                   // 值（拷贝存储，DELETE操作时为NULL）
    size_t value_len;                              // 值长度（DELETE操作时为0）
    uint64_t seq_num;                              // 序列号（内部使用）
} lsm_batch_entry_t;

// 批量写入配置
typedef struct lsm_batch_config_s {
    size_t max_batch_size;                         // 最大批次大小
    size_t max_batch_memory;                       // 最大批次内存使用（字节）
    bool enable_sorting;                           // 是否启用键排序优化
    bool enable_deduplication;                     // 是否启用重复键去重
    bool atomic_commit;                            // 是否原子提交
    bool sync_wal;                                 // 是否同步写入WAL
} lsm_batch_config_t;

// 批量写入操作句柄
typedef struct lsm_batch_writer_s {
    lsm_tree_t *tree;                              // 关联的LSM-Tree
    lsm_batch_config_t config;                     // 批量写入配置
    lsm_batch_entry_t *entries;                    // 批量条目数组
    size_t entry_count;                            // 当前条目数量
    size_t entry_capacity;                         // 条目数组容量
    size_t total_memory;                           // 当前内存使用量
    bool is_sorted;                                // 是否已排序
    pthread_mutex_t batch_lock;                    // 批次锁
    
    // 统计信息
    uint64_t total_batches;                        // 总批次数
    uint64_t total_entries;                        // 总条目数
    uint64_t total_memory_used;                    // 总内存使用量
    uint64_t commit_errors;                        // 提交错误数
} lsm_batch_writer_t;

// 批量写入结果
typedef struct lsm_batch_result_s {
    size_t committed_entries;                      // 成功提交的条目数
    size_t failed_entries;                         // 失败的条目数
    int first_error_code;                          // 第一个错误码
    size_t first_error_index;                      // 第一个错误的条目索引
    uint64_t commit_time_us;                       // 提交耗时（微秒）
    size_t wal_writes;                             // WAL写入次数
} lsm_batch_result_t;

/**
 * 创建默认批量写入配置
 * @return 批量写入配置
 */
lsm_batch_config_t lsm_batch_default_config(void);

/**
 * 创建批量写入器
 * @param tree LSM-Tree指针
 * @param config 批量写入配置，传入NULL使用默认配置
 * @return 成功返回批量写入器指针，失败返回NULL
 */
lsm_batch_writer_t* lsm_batch_writer_create(lsm_tree_t *tree, const lsm_batch_config_t *config);

/**
 * 销毁批量写入器
 * @param writer 批量写入器指针
 */
void lsm_batch_writer_destroy(lsm_batch_writer_t *writer);

/**
 * 添加PUT操作到批次
 * @param writer 批量写入器指针
 * @param key 键
 * @param key_len 键长度
 * @param value 值
 * @param value_len 值长度
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_batch_put(lsm_batch_writer_t *writer, const char *key, size_t key_len,
                  const char *value, size_t value_len);

/**
 * 添加DELETE操作到批次
 * @param writer 批量写入器指针
 * @param key 键
 * @param key_len 键长度
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_batch_delete(lsm_batch_writer_t *writer, const char *key, size_t key_len);

/**
 * 提交批量操作
 * @param writer 批量写入器指针
 * @param result 提交结果，传入NULL表示不需要详细结果
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_batch_commit(lsm_batch_writer_t *writer, lsm_batch_result_t *result);

/**
 * 清空批次（不提交）
 * @param writer 批量写入器指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_batch_clear(lsm_batch_writer_t *writer);

/**
 * 获取批次当前状态
 * @param writer 批量写入器指针
 * @param entry_count 返回当前条目数，可为NULL
 * @param memory_usage 返回当前内存使用量，可为NULL
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_batch_get_status(lsm_batch_writer_t *writer, size_t *entry_count, size_t *memory_usage);

/**
 * 获取批量写入器统计信息
 * @param writer 批量写入器指针
 * @param total_batches 返回总批次数，可为NULL
 * @param total_entries 返回总条目数，可为NULL
 * @param total_memory 返回总内存使用量，可为NULL
 * @param commit_errors 返回提交错误数，可为NULL
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_batch_get_stats(lsm_batch_writer_t *writer, uint64_t *total_batches, 
                        uint64_t *total_entries, uint64_t *total_memory, uint64_t *commit_errors);

#ifdef __cplusplus
}
#endif

#endif // __LSM_TREE_H__ 