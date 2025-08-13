/*
 * @Author: Lzww0608  
 * @Date: 2025-6-2 23:26:22
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-3 22:23:28
 * @Description: ConcordKV LSM-Tree MemTable管理器实现
 */
#ifndef __LSM_MEMTABLE_MANAGER_H__
#define __LSM_MEMTABLE_MANAGER_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <pthread.h>
#include "lsm_memtable.h"
#include "kv_store.h"
#include "kv_error.h"

#ifdef __cplusplus
extern "C" {
#endif

// 前向声明
typedef struct lsm_memtable_manager_s lsm_memtable_manager_t;

// 管理器配置参数
typedef struct lsm_memtable_manager_config_s {
    size_t memtable_max_size;          // 单个MemTable最大大小（字节）
    int max_immutable_count;           // 最大不可变MemTable数量
    bool auto_freeze;                  // 是否自动冻结满的MemTable
    uint64_t seq_num_start;            // 起始序列号
    char name_prefix[32];              // MemTable名称前缀
} lsm_memtable_manager_config_t;

// 管理器统计信息
typedef struct lsm_memtable_manager_stats_s {
    uint64_t total_memtables;          // 总MemTable数量
    uint64_t active_memtables;         // 活跃MemTable数量
    uint64_t immutable_memtables;      // 不可变MemTable数量
    uint64_t total_entries;            // 总条目数
    uint64_t total_size;               // 总数据大小（字节）
    uint64_t total_memory_usage;       // 总内存使用量（字节）
    uint64_t freeze_count;             // 冻结操作计数
    uint64_t switch_count;             // 切换操作计数
    uint64_t flush_count;              // 刷写操作计数
    uint64_t write_count;              // 写操作计数
    uint64_t read_count;               // 读操作计数
    uint64_t delete_count;             // 删除操作计数
} lsm_memtable_manager_stats_t;

// MemTable管理器结构
typedef struct lsm_memtable_manager_s {
    lsm_memtable_t *active;            // 活跃内存表
    lsm_memtable_t **immutable;        // 不可变内存表数组
    int immutable_count;               // 当前不可变表数量
    int max_immutable_count;           // 最大不可变表数量
    
    lsm_memtable_manager_config_t config; // 配置参数
    pthread_rwlock_t lock;             // 读写锁
    uint64_t global_seq_num;           // 全局序列号
    
    lsm_memtable_manager_stats_t stats; // 统计信息
    char name[64];                     // 管理器名称
    uint64_t creation_time;            // 创建时间戳
} lsm_memtable_manager_t;

// MemTable查找结果
typedef struct lsm_memtable_lookup_result_s {
    char *value;                       // 查找到的值
    size_t value_len;                  // 值长度
    uint64_t seq_num;                  // 序列号
    bool deleted;                      // 是否被删除
    bool found;                        // 是否找到
    lsm_memtable_t *source_memtable;   // 来源MemTable
} lsm_memtable_lookup_result_t;

// === 管理器生命周期管理 ===

/**
 * 创建默认配置
 * @return 默认配置结构
 */
lsm_memtable_manager_config_t lsm_memtable_manager_default_config(void);

/**
 * 创建MemTable管理器
 * @param config 配置参数（可为NULL使用默认配置）
 * @param name 管理器名称（可选，用于调试）
 * @return 成功返回管理器指针，失败返回NULL
 */
lsm_memtable_manager_t* lsm_memtable_manager_create(const lsm_memtable_manager_config_t *config,
                                                     const char *name);

/**
 * 销毁MemTable管理器
 * @param manager 管理器指针
 */
void lsm_memtable_manager_destroy(lsm_memtable_manager_t *manager);

// === 基础数据操作 ===

/**
 * 插入或更新键值对
 * @param manager 管理器指针
 * @param key 键
 * @param key_len 键长度
 * @param value 值
 * @param value_len 值长度
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_memtable_manager_put(lsm_memtable_manager_t *manager, 
                              const char *key, size_t key_len,
                              const char *value, size_t value_len);

/**
 * 查找键值对
 * @param manager 管理器指针
 * @param key 键
 * @param key_len 键长度
 * @param result 输出查找结果
 * @return 成功返回KV_ERR_NONE，未找到返回KV_ERR_NOT_FOUND，失败返回错误码
 */
int lsm_memtable_manager_get(lsm_memtable_manager_t *manager,
                              const char *key, size_t key_len,
                              lsm_memtable_lookup_result_t *result);

/**
 * 删除键值对（设置墓碑标记）
 * @param manager 管理器指针
 * @param key 键
 * @param key_len 键长度
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_memtable_manager_delete(lsm_memtable_manager_t *manager,
                                 const char *key, size_t key_len);

// === MemTable管理操作 ===

/**
 * 手动冻结当前活跃MemTable
 * @param manager 管理器指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_memtable_manager_freeze_active(lsm_memtable_manager_t *manager);

/**
 * 获取最老的不可变MemTable（用于刷写到磁盘）
 * @param manager 管理器指针
 * @return 成功返回最老的不可变MemTable，没有则返回NULL
 */
lsm_memtable_t* lsm_memtable_manager_get_oldest_immutable(lsm_memtable_manager_t *manager);

/**
 * 移除最老的不可变MemTable（刷写完成后调用）
 * @param manager 管理器指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_memtable_manager_remove_oldest_immutable(lsm_memtable_manager_t *manager);

/**
 * 移除指定的不可变MemTable（避免并发冲突）
 * @param manager 管理器指针
 * @param memtable 要移除的MemTable指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_memtable_manager_remove_specific_immutable(lsm_memtable_manager_t *manager,
                                                    lsm_memtable_t *memtable);

/**
 * 强制切换到新的活跃MemTable
 * @param manager 管理器指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_memtable_manager_switch_active(lsm_memtable_manager_t *manager);

// === 状态查询 ===

/**
 * 检查是否需要冻结活跃MemTable
 * @param manager 管理器指针
 * @return true表示需要冻结，false表示不需要
 */
bool lsm_memtable_manager_should_freeze(lsm_memtable_manager_t *manager);

/**
 * 检查是否需要刷写不可变MemTable
 * @param manager 管理器指针
 * @return true表示需要刷写，false表示不需要
 */
bool lsm_memtable_manager_should_flush(lsm_memtable_manager_t *manager);

/**
 * 获取管理器统计信息
 * @param manager 管理器指针
 * @param stats 输出统计信息
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_memtable_manager_get_stats(lsm_memtable_manager_t *manager,
                                    lsm_memtable_manager_stats_t *stats);

/**
 * 获取当前活跃MemTable数量
 * @param manager 管理器指针
 * @return 活跃MemTable数量
 */
int lsm_memtable_manager_get_active_count(lsm_memtable_manager_t *manager);

/**
 * 获取当前不可变MemTable数量
 * @param manager 管理器指针
 * @return 不可变MemTable数量
 */
int lsm_memtable_manager_get_immutable_count(lsm_memtable_manager_t *manager);

// === 辅助函数 ===

/**
 * 清理查找结果
 * @param result 查找结果指针
 */
void lsm_memtable_lookup_result_cleanup(lsm_memtable_lookup_result_t *result);

/**
 * 初始化查找结果
 * @param result 查找结果指针
 */
void lsm_memtable_lookup_result_init(lsm_memtable_lookup_result_t *result);

/**
 * 刷新所有挂起的MemTable写入
 * @param manager 管理器指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_memtable_manager_flush_all(lsm_memtable_manager_t *manager);

/**
 * 打印管理器状态（调试用）
 * @param manager 管理器指针
 */
void lsm_memtable_manager_print_status(lsm_memtable_manager_t *manager);

/**
 * 兼容性函数：创建MemTable管理器（简化版）
 * 注意：这是为LSM-Tree集成提供的兼容性接口
 * @param memtable_size MemTable最大大小
 * @param max_immutable_count 最大不可变MemTable数
 * @return 成功返回管理器指针，失败返回NULL
 */
lsm_memtable_manager_t* lsm_memtable_manager_create_compat(size_t memtable_size, int max_immutable_count);

/**
 * 兼容性函数：查找键值对（简化版）
 * 注意：这是为LSM-Tree集成提供的兼容性接口
 * @param manager 管理器指针
 * @param key 键
 * @param key_len 键长度
 * @param value 输出值缓冲区指针
 * @param value_len 输出值长度指针
 * @param seq_num 输出序列号指针
 * @param deleted 输出删除标记指针
 * @return 成功返回KV_ERR_NONE，未找到返回KV_ERR_NOT_FOUND，失败返回错误码
 */
int lsm_memtable_manager_get_compat(lsm_memtable_manager_t *manager,
                                    const char *key, size_t key_len,
                                    char **value, size_t *value_len,
                                    uint64_t *seq_num, bool *deleted);

/**
 * 兼容性函数：插入键值对（简化版）
 * 注意：这是为LSM-Tree集成提供的兼容性接口
 * @param manager 管理器指针
 * @param key 键
 * @param key_len 键长度
 * @param value 值
 * @param value_len 值长度
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_memtable_manager_put_compat(lsm_memtable_manager_t *manager,
                                    const char *key, size_t key_len,
                                    const char *value, size_t value_len);

/**
 * 兼容性函数：删除键值对（简化版）
 * 注意：这是为LSM-Tree集成提供的兼容性接口
 * @param manager 管理器指针
 * @param key 键
 * @param key_len 键长度
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_memtable_manager_delete_compat(lsm_memtable_manager_t *manager,
                                       const char *key, size_t key_len);

#ifdef __cplusplus
}
#endif

#endif // __LSM_MEMTABLE_MANAGER_H__ 