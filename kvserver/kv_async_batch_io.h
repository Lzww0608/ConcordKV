/*
 * @Author: Lzww0608
 * @Date: 2025-6-11 17:00:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-11 17:00:00
 * @Description: ConcordKV 异步批量I/O操作层 - 统一接口
 */

#ifndef KV_ASYNC_BATCH_IO_H
#define KV_ASYNC_BATCH_IO_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/types.h>

#include "kv_error.h"
#include "kv_io_uring.h"

#ifdef __cplusplus
extern "C" {
#endif

// === 异步批量操作配置 ===
#define KV_ASYNC_BATCH_DEFAULT_SIZE         (256)
#define KV_ASYNC_BATCH_MAX_SIZE             (4096)
#define KV_ASYNC_BATCH_DEFAULT_TIMEOUT_MS   (10000)
#define KV_ASYNC_BATCH_MAX_CONCURRENT       (64)

// === 异步批量操作类型 ===
typedef enum {
    KV_ASYNC_BATCH_PUT = 0,
    KV_ASYNC_BATCH_GET,
    KV_ASYNC_BATCH_DELETE,
    KV_ASYNC_BATCH_UPDATE,
    KV_ASYNC_BATCH_SYNC
} kv_async_batch_op_type_t;

// === 异步批量操作状态 ===
typedef enum {
    KV_ASYNC_BATCH_PENDING = 0,
    KV_ASYNC_BATCH_SUBMITTED,
    KV_ASYNC_BATCH_COMPLETING,
    KV_ASYNC_BATCH_COMPLETED,
    KV_ASYNC_BATCH_FAILED,
    KV_ASYNC_BATCH_CANCELLED
} kv_async_batch_status_t;

// 前向声明
struct kv_async_batch_context_s;

// === 异步批量操作项 ===
typedef struct kv_async_batch_operation_s {
    kv_async_batch_op_type_t op_type;       // 操作类型
    char *key;                              // 键
    size_t key_len;                         // 键长度
    char *value;                            // 值
    size_t value_len;                       // 值长度
    int result_code;                        // 结果码
    void *user_data;                        // 用户数据
} kv_async_batch_operation_t;

// === 异步批量操作回调 ===
typedef void (*kv_async_batch_callback_t)(
    struct kv_async_batch_operation_s *op,
    int result,
    void *user_data
);

// === 异步批量操作完成回调 ===
typedef void (*kv_async_batch_complete_callback_t)(
    struct kv_async_batch_context_s *ctx,
    size_t completed_count,
    size_t failed_count,
    void *user_data
);

// === 异步批量操作条目 ===
typedef struct kv_async_batch_entry_s {
    // 操作信息
    kv_async_batch_op_type_t op_type;       // 操作类型
    uint64_t sequence_number;               // 序列号
    
    // 键值数据
    char *key;                              // 键
    size_t key_len;                         // 键长度
    char *value;                            // 值
    size_t value_len;                       // 值长度
    
    // 异步操作相关
    kv_uring_request_t *io_request;         // io_uring请求
    kv_async_batch_callback_t callback;     // 完成回调
    void *user_data;                        // 用户数据
    
    // 结果信息
    int result_code;                        // 结果码
    size_t bytes_processed;                 // 处理字节数
    struct timespec start_time;             // 开始时间
    struct timespec complete_time;          // 完成时间
    
    // 内存管理
    bool owns_key;                          // 是否拥有键内存
    bool owns_value;                        // 是否拥有值内存
    
    // 链表节点
    struct kv_async_batch_entry_s *next;
} kv_async_batch_entry_t;

// === 异步批量操作配置 ===
typedef struct kv_async_batch_config_s {
    // 基本配置
    size_t max_batch_size;                  // 最大批次大小
    size_t max_memory_usage;                // 最大内存使用量
    uint32_t timeout_ms;                    // 操作超时时间
    uint32_t max_concurrent_batches;        // 最大并发批次数
    
    // I/O配置
    uint32_t io_queue_depth;                // I/O队列深度
    bool enable_io_polling;                 // 启用I/O轮询
    bool enable_batch_fsync;                // 启用批量同步
    uint32_t fsync_interval_ms;             // 同步间隔
    
    // 优化配置
    bool enable_deduplication;              // 启用去重
    bool enable_sorting;                    // 启用排序
    bool enable_compression;                // 启用压缩
    bool enable_prefetching;                // 启用预取
    
    // 错误处理
    bool fail_fast;                         // 快速失败
    uint32_t max_retry_count;               // 最大重试次数
    uint32_t retry_delay_ms;                // 重试延迟
    
    // 内存管理
    void *(*alloc_func)(size_t);            // 内存分配函数
    void (*free_func)(void*);               // 内存释放函数
} kv_async_batch_config_t;

// === 异步批量操作统计 ===
typedef struct kv_async_batch_stats_s {
    // 操作计数
    uint64_t total_operations;              // 总操作数
    uint64_t completed_operations;          // 完成操作数
    uint64_t failed_operations;             // 失败操作数
    uint64_t cancelled_operations;          // 取消操作数
    
    // 批次统计
    uint64_t total_batches;                 // 总批次数
    uint64_t completed_batches;             // 完成批次数
    uint64_t failed_batches;                // 失败批次数
    
    // 数据量统计
    uint64_t bytes_read;                    // 读取字节数
    uint64_t bytes_written;                 // 写入字节数
    uint64_t keys_processed;                // 处理键数
    
    // 时间统计（微秒）
    uint64_t total_submit_time;             // 总提交时间
    uint64_t total_complete_time;           // 总完成时间
    uint64_t total_wait_time;               // 总等待时间
    
    // 性能指标
    double avg_batch_size;                  // 平均批次大小
    double avg_latency_us;                  // 平均延迟
    double throughput_ops_per_sec;          // 吞吐量（操作/秒）
    double throughput_mb_per_sec;           // 吞吐量（MB/秒）
    
    // 资源使用
    size_t peak_memory_usage;               // 峰值内存使用
    size_t current_memory_usage;            // 当前内存使用
    uint32_t active_batches;                // 活跃批次数
    uint32_t queued_operations;             // 排队操作数
} kv_async_batch_stats_t;

// === 异步批量操作上下文 ===
typedef struct kv_async_batch_context_s {
    // 基本信息
    uint64_t batch_id;                      // 批次ID
    kv_async_batch_status_t status;         // 批次状态
    
    // 配置和统计
    kv_async_batch_config_t config;         // 配置
    kv_async_batch_stats_t stats;           // 统计信息
    
    // 操作管理
    kv_async_batch_entry_t *entries;        // 操作条目链表
    size_t entry_count;                     // 条目数量
    size_t max_batch_size;                  // 最大批次大小
    size_t completed_count;                 // 完成数量
    size_t failed_count;                    // 失败数量
    
    // I/O管理
    kv_uring_t *io_uring;                   // io_uring实例
    kv_uring_batch_t *io_batch;             // I/O批次
    
    // 回调管理
    kv_async_batch_complete_callback_t complete_callback;  // 完成回调
    void *complete_user_data;               // 完成回调用户数据
    
    // 同步控制
    pthread_mutex_t context_lock;           // 上下文锁
    pthread_cond_t completion_cond;         // 完成条件变量
    volatile bool is_complete;              // 是否完成
    
    // 内存管理
    size_t memory_usage;                    // 内存使用量
    void *memory_pool;                      // 内存池
    
    // 链表节点（用于管理器）
    struct kv_async_batch_context_s *next;
} kv_async_batch_context_t;

// === 异步批量操作管理器 ===
typedef struct kv_async_batch_manager_s {
    // I/O后端
    kv_uring_t *io_uring;                   // io_uring实例
    
    // 配置信息
    kv_async_batch_config_t default_config; // 默认配置
    
    // 批次管理
    kv_async_batch_context_t *active_batches; // 活跃批次链表
    kv_async_batch_context_t *free_batches;   // 空闲批次池
    uint64_t next_batch_id;                 // 下一个批次ID
    size_t active_count;                    // 活跃批次数
    size_t free_count;                      // 空闲批次数
    
    // 全局统计
    kv_async_batch_stats_t global_stats;    // 全局统计
    
    // 线程安全
    pthread_mutex_t manager_lock;           // 管理器锁
    pthread_mutex_t stats_lock;             // 统计锁
    
    // 工作线程
    pthread_t completion_thread;            // 完成处理线程
    bool completion_thread_running;         // 线程运行状态
    pthread_cond_t work_cond;               // 工作条件变量
    pthread_mutex_t work_lock;              // 工作锁
    
    // 状态标志
    bool is_initialized;                    // 是否已初始化
    bool is_shutdown;                       // 是否关闭
} kv_async_batch_manager_t;

// ================================================================
// 配置管理接口
// ================================================================

/**
 * 创建默认异步批量操作配置
 */
kv_async_batch_config_t kv_async_batch_default_config(void);

/**
 * 验证异步批量操作配置
 */
int kv_async_batch_config_validate(const kv_async_batch_config_t *config);

/**
 * 复制异步批量操作配置
 */
int kv_async_batch_config_copy(kv_async_batch_config_t *dst, 
                               const kv_async_batch_config_t *src);

// ================================================================
// 管理器接口
// ================================================================

/**
 * 创建异步批量操作管理器
 */
kv_async_batch_manager_t* kv_async_batch_manager_create(const kv_async_batch_config_t *config);

/**
 * 销毁异步批量操作管理器
 */
void kv_async_batch_manager_destroy(kv_async_batch_manager_t *manager);

/**
 * 启动异步批量操作管理器
 */
int kv_async_batch_manager_start(kv_async_batch_manager_t *manager);

/**
 * 停止异步批量操作管理器
 */
int kv_async_batch_manager_stop(kv_async_batch_manager_t *manager);

// ================================================================
// 异步批量操作接口
// ================================================================

/**
 * 创建异步批量操作上下文
 */
kv_async_batch_context_t* kv_async_batch_create(kv_async_batch_manager_t *manager,
                                                size_t max_batch_size);

/**
 * 销毁异步批量操作上下文
 */
void kv_async_batch_destroy(kv_async_batch_context_t *ctx);

/**
 * 添加异步PUT操作
 */
int kv_async_batch_put(kv_async_batch_context_t *ctx,
                       const char *key, size_t key_len,
                       const char *value, size_t value_len,
                       kv_async_batch_callback_t callback,
                       void *user_data);

/**
 * 添加异步GET操作
 */
int kv_async_batch_get(kv_async_batch_context_t *ctx,
                       const char *key, size_t key_len,
                       char **value, size_t *value_len,
                       kv_async_batch_callback_t callback,
                       void *user_data);

/**
 * 添加异步DELETE操作
 */
int kv_async_batch_delete(kv_async_batch_context_t *ctx,
                          const char *key, size_t key_len,
                          kv_async_batch_callback_t callback,
                          void *user_data);

/**
 * 提交异步批量操作
 */
int kv_async_batch_submit(kv_async_batch_context_t *ctx,
                          kv_async_batch_complete_callback_t complete_callback,
                          void *user_data);

/**
 * 等待异步批量操作完成
 */
int kv_async_batch_wait(kv_async_batch_context_t *ctx, uint32_t timeout_ms);

/**
 * 取消异步批量操作
 */
int kv_async_batch_cancel(kv_async_batch_context_t *ctx);

// ================================================================
// 状态和统计接口
// ================================================================

/**
 * 获取异步批量操作状态
 */
kv_async_batch_status_t kv_async_batch_get_status(const kv_async_batch_context_t *ctx);

/**
 * 获取异步批量操作统计信息
 */
int kv_async_batch_get_stats(const kv_async_batch_context_t *ctx, 
                             kv_async_batch_stats_t *stats);

/**
 * 获取管理器全局统计信息
 */
int kv_async_batch_manager_get_stats(const kv_async_batch_manager_t *manager,
                                     kv_async_batch_stats_t *stats);

/**
 * 重置统计信息
 */
int kv_async_batch_reset_stats(kv_async_batch_context_t *ctx);

/**
 * 打印统计信息
 */
int kv_async_batch_print_stats(const kv_async_batch_context_t *ctx, FILE *fp);

// ================================================================
// 工具接口
// ================================================================

/**
 * 获取操作类型名称
 */
const char* kv_async_batch_op_name(kv_async_batch_op_type_t op_type);

/**
 * 获取状态名称
 */
const char* kv_async_batch_status_name(kv_async_batch_status_t status);

/**
 * 获取当前时间戳（微秒）
 */
uint64_t kv_async_batch_get_timestamp_us(void);

#ifdef __cplusplus
}
#endif

#endif // KV_ASYNC_BATCH_IO_H 