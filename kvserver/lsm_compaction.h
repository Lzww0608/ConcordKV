/*
 * @Author: Lzww0608  
 * @Date: 2025-6-2 23:26:08
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-5 21:13:22
 * @Description: ConcordKV LSM-Tree 压缩机制实现
 */
#ifndef __LSM_COMPACTION_H__
#define __LSM_COMPACTION_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <pthread.h>

// 前向声明，避免循环依赖
typedef struct lsm_memtable_s lsm_memtable_t;
typedef struct lsm_memtable_manager_s lsm_memtable_manager_t;
typedef struct sstable_reader_s sstable_reader_t;

#include "kv_store.h"
#include "kv_error.h"

#ifdef __cplusplus
extern "C" {
#endif

// === 压缩相关常量 ===
#define LSM_MAX_LEVELS                  7       // 最大层级数
#define LSM_LEVEL0_FILE_LIMIT          4       // Level-0文件数量限制
#define LSM_LEVEL_SIZE_MULTIPLIER      10      // 层级大小倍数
#define LSM_MAX_FILES_PER_LEVEL        100     // 每层最大文件数
#define LSM_MAX_FILENAME               256     // 最大文件名长度
#define LSM_COMPACTION_THREAD_COUNT    2       // 压缩线程数

// 压缩策略类型
typedef enum {
    LSM_COMPACTION_NONE = 0,        // 无压缩
    LSM_COMPACTION_SIZE_TIERED,     // 大小分层压缩
    LSM_COMPACTION_LEVELED,         // 层级压缩（简化版）
    LSM_COMPACTION_UNIVERSAL        // 通用压缩（预留）
} lsm_compaction_strategy_t;

// 压缩任务类型
typedef enum {
    LSM_COMPACTION_TASK_NONE = 0,       // 无任务
    LSM_COMPACTION_TASK_LEVEL0,         // Level-0压缩（MemTable -> SSTable）
    LSM_COMPACTION_TASK_LEVEL_N,        // Level-N压缩
    LSM_COMPACTION_TASK_MAJOR,          // 主压缩
    LSM_COMPACTION_TASK_MANUAL          // 手动压缩
} lsm_compaction_task_type_t;

// 压缩任务优先级
typedef enum {
    LSM_COMPACTION_PRIORITY_LOW = 0,    // 低优先级
    LSM_COMPACTION_PRIORITY_NORMAL,     // 普通优先级
    LSM_COMPACTION_PRIORITY_HIGH,       // 高优先级
    LSM_COMPACTION_PRIORITY_URGENT      // 紧急优先级
} lsm_compaction_priority_t;

// 压缩任务状态
typedef enum {
    LSM_COMPACTION_STATUS_PENDING = 0,  // 等待执行
    LSM_COMPACTION_STATUS_RUNNING,      // 正在执行
    LSM_COMPACTION_STATUS_COMPLETED,    // 执行完成
    LSM_COMPACTION_STATUS_FAILED,       // 执行失败
    LSM_COMPACTION_STATUS_CANCELLED     // 已取消
} lsm_compaction_status_t;

// === 核心数据结构 ===

// SSTable文件元信息
typedef struct lsm_sstable_meta_s {
    char filename[LSM_MAX_FILENAME];    // 文件路径
    uint64_t file_id;                   // 文件ID
    int level;                          // 层级
    uint64_t file_size;                 // 文件大小
    uint64_t entry_count;               // 条目数量
    uint64_t min_seq_num;               // 最小序列号
    uint64_t max_seq_num;               // 最大序列号
    char *min_key;                      // 最小键
    size_t min_key_len;                 // 最小键长度
    char *max_key;                      // 最大键
    size_t max_key_len;                 // 最大键长度
    uint64_t creation_time;             // 创建时间
    sstable_reader_t *reader;           // SSTable读取器（延迟加载）
} lsm_sstable_meta_t;

// 层级管理器
typedef struct lsm_level_manager_s {
    lsm_sstable_meta_t **levels[LSM_MAX_LEVELS];    // 各层级SSTable数组
    int level_file_count[LSM_MAX_LEVELS];           // 各层级文件数量
    uint64_t level_size[LSM_MAX_LEVELS];            // 各层级总大小
    uint64_t next_file_id;                          // 下一个文件ID
    
    char data_dir[LSM_MAX_FILENAME];                // 数据目录
    pthread_rwlock_t lock;                          // 读写锁
} lsm_level_manager_t;

// 压缩任务
typedef struct lsm_compaction_task_s {
    uint64_t task_id;                               // 任务ID
    lsm_compaction_task_type_t type;                // 任务类型
    lsm_compaction_priority_t priority;             // 优先级
    lsm_compaction_status_t status;                 // 状态
    
    // 任务参数
    int source_level;                               // 源层级
    int target_level;                               // 目标层级
    lsm_sstable_meta_t **input_files;              // 输入文件数组
    int input_file_count;                           // 输入文件数量
    lsm_memtable_t *input_memtable;                 // 输入MemTable（仅Level-0压缩）
    
    // 输出信息
    lsm_sstable_meta_t **output_files;             // 输出文件数组
    int output_file_count;                          // 输出文件数量
    
    // 统计信息
    uint64_t start_time;                            // 开始时间
    uint64_t end_time;                              // 结束时间
    uint64_t bytes_read;                            // 读取字节数
    uint64_t bytes_written;                         // 写入字节数
    int error_code;                                 // 错误码
    char error_message[256];                        // 错误信息
    
    pthread_mutex_t lock;                           // 任务锁
    struct lsm_compaction_task_s *next;             // 下一个任务（用于队列）
} lsm_compaction_task_t;

// 压缩配置
typedef struct lsm_compaction_config_s {
    lsm_compaction_strategy_t strategy;             // 压缩策略
    uint64_t level0_file_limit;                     // Level-0文件数量限制
    uint64_t level_size_multiplier;                 // 层级大小倍数
    uint64_t max_level_bytes[LSM_MAX_LEVELS];       // 各层级最大字节数
    bool enable_background_compaction;              // 是否启用后台压缩
    int background_thread_count;                    // 后台线程数
    int compression;                                // 压缩算法
    bool enable_bloom_filter;                       // 是否启用布隆过滤器
    uint32_t bloom_filter_bits_per_key;             // 布隆过滤器每键位数
} lsm_compaction_config_t;

// 压缩统计信息
typedef struct lsm_compaction_stats_s {
    uint64_t total_compactions;                     // 总压缩次数
    uint64_t level0_compactions;                    // Level-0压缩次数
    uint64_t level_n_compactions;                   // Level-N压缩次数
    uint64_t bytes_compacted;                       // 压缩字节数
    uint64_t bytes_written;                         // 写入字节数
    uint64_t total_compaction_time;                 // 总压缩时间（微秒）
    uint64_t pending_tasks;                         // 待处理任务数
    uint64_t running_tasks;                         // 正在执行任务数
    uint64_t completed_tasks;                       // 已完成任务数
    uint64_t failed_tasks;                          // 失败任务数
} lsm_compaction_stats_t;

// 压缩调度器
typedef struct lsm_compaction_scheduler_s {
    lsm_memtable_manager_t *memtable_manager;       // MemTable管理器
    lsm_level_manager_t *level_manager;             // 层级管理器
    lsm_compaction_config_t config;                 // 压缩配置
    
    // 任务队列
    lsm_compaction_task_t *task_queue_head;         // 任务队列头
    lsm_compaction_task_t *task_queue_tail;         // 任务队列尾
    pthread_mutex_t queue_lock;                     // 队列锁
    pthread_cond_t queue_cond;                      // 队列条件变量
    
    // 后台线程
    pthread_t *worker_threads;                      // 工作线程数组
    bool shutdown;                                  // 关闭标志
    pthread_mutex_t shutdown_lock;                  // 关闭锁
    
    // 统计信息
    lsm_compaction_stats_t stats;                   // 压缩统计
    uint64_t next_task_id;                          // 下一个任务ID
    
    pthread_rwlock_t stats_lock;                    // 统计锁
} lsm_compaction_scheduler_t;

// === 层级管理器API ===

/**
 * 创建层级管理器
 * @param data_dir 数据目录
 * @return 成功返回管理器指针，失败返回NULL
 */
lsm_level_manager_t* lsm_level_manager_create(const char *data_dir);

/**
 * 销毁层级管理器
 * @param manager 管理器指针
 */
void lsm_level_manager_destroy(lsm_level_manager_t *manager);

/**
 * 添加SSTable到指定层级
 * @param manager 管理器指针
 * @param level 层级
 * @param sstable_meta SSTable元信息
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_level_manager_add_sstable(lsm_level_manager_t *manager, 
                                   int level, lsm_sstable_meta_t *sstable_meta);

/**
 * 从指定层级移除SSTable
 * @param manager 管理器指针
 * @param level 层级
 * @param sstable_meta SSTable元信息
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_level_manager_remove_sstable(lsm_level_manager_t *manager,
                                      int level, lsm_sstable_meta_t *sstable_meta);

/**
 * 查找键值对
 * @param manager 管理器指针
 * @param key 键
 * @param key_len 键长度
 * @param value 输出值
 * @param value_len 输出值长度
 * @param seq_num 输出序列号
 * @param deleted 输出删除标记
 * @return 成功返回KV_ERR_NONE，未找到返回KV_ERR_NOT_FOUND，失败返回错误码
 */
int lsm_level_manager_get(lsm_level_manager_t *manager,
                          const char *key, size_t key_len,
                          char **value, size_t *value_len,
                          uint64_t *seq_num, bool *deleted);

/**
 * 检查层级是否需要压缩
 * @param manager 管理器指针
 * @param level 层级
 * @return true表示需要压缩，false表示不需要
 */
bool lsm_level_manager_needs_compaction(lsm_level_manager_t *manager, int level);

/**
 * 获取层级文件数量
 * @param manager 管理器指针
 * @param level 层级
 * @return 文件数量
 */
int lsm_level_manager_get_file_count(lsm_level_manager_t *manager, int level);

// === SSTable元信息API ===

/**
 * 创建SSTable元信息
 * @param filename 文件路径
 * @param level 层级
 * @param file_id 文件ID
 * @return 成功返回元信息指针，失败返回NULL
 */
lsm_sstable_meta_t* lsm_sstable_meta_create(const char *filename, int level, uint64_t file_id);

/**
 * 销毁SSTable元信息
 * @param meta 元信息指针
 */
void lsm_sstable_meta_destroy(lsm_sstable_meta_t *meta);

/**
 * 从SSTable文件加载元信息
 * @param meta 元信息指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_sstable_meta_load_from_file(lsm_sstable_meta_t *meta);

/**
 * 检查键是否在SSTable范围内
 * @param meta 元信息指针
 * @param key 键
 * @param key_len 键长度
 * @return true表示在范围内，false表示不在范围内
 */
bool lsm_sstable_meta_contains_key(lsm_sstable_meta_t *meta, 
                                    const char *key, size_t key_len);

// === 压缩任务API ===

/**
 * 创建压缩任务
 * @param type 任务类型
 * @param priority 优先级
 * @return 成功返回任务指针，失败返回NULL
 */
lsm_compaction_task_t* lsm_compaction_task_create(lsm_compaction_task_type_t type,
                                                   lsm_compaction_priority_t priority);

/**
 * 销毁压缩任务
 * @param task 任务指针
 */
void lsm_compaction_task_destroy(lsm_compaction_task_t *task);

/**
 * 设置Level-0压缩任务参数
 * @param task 任务指针
 * @param memtable 输入MemTable
 * @param output_level 输出层级
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_compaction_task_set_level0_params(lsm_compaction_task_t *task,
                                           lsm_memtable_t *memtable,
                                           int output_level);

/**
 * 设置Level-N压缩任务参数
 * @param task 任务指针
 * @param source_level 源层级
 * @param target_level 目标层级
 * @param input_files 输入文件数组
 * @param input_file_count 输入文件数量
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_compaction_task_set_level_n_params(lsm_compaction_task_t *task,
                                            int source_level, int target_level,
                                            lsm_sstable_meta_t **input_files,
                                            int input_file_count);

// === 压缩调度器API ===

/**
 * 创建默认压缩配置
 * @return 默认配置结构
 */
lsm_compaction_config_t lsm_compaction_default_config(void);

/**
 * 创建压缩调度器
 * @param memtable_manager MemTable管理器
 * @param level_manager 层级管理器
 * @param config 压缩配置（可为NULL使用默认配置）
 * @return 成功返回调度器指针，失败返回NULL
 */
lsm_compaction_scheduler_t* lsm_compaction_scheduler_create(
    lsm_memtable_manager_t *memtable_manager,
    lsm_level_manager_t *level_manager,
    const lsm_compaction_config_t *config);

/**
 * 销毁压缩调度器
 * @param scheduler 调度器指针
 */
void lsm_compaction_scheduler_destroy(lsm_compaction_scheduler_t *scheduler);

/**
 * 启动压缩调度器
 * @param scheduler 调度器指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_compaction_scheduler_start(lsm_compaction_scheduler_t *scheduler);

/**
 * 停止压缩调度器
 * @param scheduler 调度器指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_compaction_scheduler_stop(lsm_compaction_scheduler_t *scheduler);

/**
 * 提交压缩任务
 * @param scheduler 调度器指针
 * @param task 压缩任务
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_compaction_scheduler_submit_task(lsm_compaction_scheduler_t *scheduler,
                                         lsm_compaction_task_t *task);

/**
 * 触发压缩检查
 * @param scheduler 调度器指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_compaction_scheduler_trigger_check(lsm_compaction_scheduler_t *scheduler);

/**
 * 手动压缩指定层级
 * @param scheduler 调度器指针
 * @param level 层级
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_compaction_scheduler_manual_compact(lsm_compaction_scheduler_t *scheduler, int level);

/**
 * 获取压缩统计信息
 * @param scheduler 调度器指针
 * @param stats 输出统计信息
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_compaction_scheduler_get_stats(lsm_compaction_scheduler_t *scheduler,
                                       lsm_compaction_stats_t *stats);

// === 压缩执行函数 ===

/**
 * 执行Level-0压缩（MemTable -> SSTable）
 * @param scheduler 调度器指针
 * @param task 压缩任务
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_compaction_execute_level0(lsm_compaction_scheduler_t *scheduler,
                                  lsm_compaction_task_t *task);

/**
 * 执行Level-N压缩
 * @param scheduler 调度器指针
 * @param task 压缩任务
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_compaction_execute_level_n(lsm_compaction_scheduler_t *scheduler,
                                   lsm_compaction_task_t *task);

/**
 * 执行大小分层压缩策略
 * @param scheduler 调度器指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_compaction_size_tiered_strategy(lsm_compaction_scheduler_t *scheduler);

/**
 * 执行层级压缩策略
 * @param scheduler 调度器指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_compaction_leveled_strategy(lsm_compaction_scheduler_t *scheduler);

// === 多线程压缩API ===

/**
 * 后台压缩工作线程入口函数
 * @param scheduler_ptr 压缩调度器指针
 * @return 线程退出值
 */
void* compaction_worker_thread(void *scheduler_ptr);

/**
 * 并行执行多个压缩任务
 * @param scheduler 调度器
 * @param tasks 任务数组
 * @param task_count 任务数量
 * @param timeout_ms 超时时间（毫秒）
 * @return 错误码
 */
int lsm_compaction_execute_parallel(lsm_compaction_scheduler_t *scheduler,
                                    lsm_compaction_task_t **tasks,
                                    int task_count,
                                    uint32_t timeout_ms);

/**
 * 获取下一个待处理任务（线程安全）
 * @param scheduler 调度器
 * @param timeout_ms 等待超时时间（毫秒）
 * @return 任务指针，超时或停止时返回NULL
 */
lsm_compaction_task_t* lsm_compaction_scheduler_get_next_task(lsm_compaction_scheduler_t *scheduler,
                                                             uint32_t timeout_ms);

/**
 * 标记任务完成（线程安全）
 * @param scheduler 调度器
 * @param task 任务
 * @param result 执行结果
 */
void lsm_compaction_scheduler_complete_task(lsm_compaction_scheduler_t *scheduler,
                                            lsm_compaction_task_t *task,
                                            int result);

/**
 * 检查工作线程健康状态
 * @param scheduler 调度器
 * @return 健康的线程数量
 */
int lsm_compaction_scheduler_check_worker_health(lsm_compaction_scheduler_t *scheduler);

// === 工具函数 ===

/**
 * 生成SSTable文件名
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @param data_dir 数据目录
 * @param level 层级
 * @param file_id 文件ID
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_compaction_format_filename(char *buffer, size_t buffer_size,
                                   const char *data_dir, int level, uint64_t file_id);

/**
 * 比较两个键的大小
 * @param key1 键1
 * @param key1_len 键1长度
 * @param key2 键2
 * @param key2_len 键2长度
 * @return 负数表示key1<key2，0表示相等，正数表示key1>key2
 */
int lsm_compaction_key_compare(const char *key1, size_t key1_len,
                               const char *key2, size_t key2_len);

#ifdef __cplusplus
}
#endif

#endif /* __LSM_COMPACTION_H__ */ 