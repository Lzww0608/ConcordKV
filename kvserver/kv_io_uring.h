/*
 * @Author: Lzww0608
 * @Date: 2025-6-11 16:18:17
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-11 16:18:17
 * @Description: ConcordKV io_uring 真正异步I/O层 - 头文件
 */

#ifndef KV_IO_URING_H
#define KV_IO_URING_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <liburing.h>

#include "kv_error.h"

#ifdef __cplusplus
extern "C" {
#endif

// === io_uring 配置常量 ===
#define KV_URING_DEFAULT_QUEUE_DEPTH    (64)
#define KV_URING_MAX_QUEUE_DEPTH        (4096)
#define KV_URING_MIN_QUEUE_DEPTH        (1)
#define KV_URING_DEFAULT_BUFFER_SIZE    (64 * 1024)     // 64KB
#define KV_URING_MAX_BUFFER_SIZE        (16 * 1024 * 1024)  // 16MB
#define KV_URING_MAX_BATCH_SIZE         (256)
#define KV_URING_TIMEOUT_MS             (5000)          // 5秒超时

// === io_uring 操作类型 ===
typedef enum {
    KV_URING_OP_READ = 0,
    KV_URING_OP_WRITE,
    KV_URING_OP_READV,
    KV_URING_OP_WRITEV,
    KV_URING_OP_FSYNC,
    KV_URING_OP_FDATASYNC,
    KV_URING_OP_OPENAT,
    KV_URING_OP_CLOSE,
    KV_URING_OP_FALLOCATE
} kv_uring_op_type_t;

// === io_uring 请求状态 ===
typedef enum {
    KV_URING_REQ_PENDING = 0,
    KV_URING_REQ_SUBMITTED,
    KV_URING_REQ_COMPLETED,
    KV_URING_REQ_FAILED,
    KV_URING_REQ_CANCELLED
} kv_uring_req_status_t;

// === io_uring 配置 ===
typedef struct kv_uring_config_s {
    uint32_t queue_depth;                   // 队列深度
    uint32_t sq_poll_idle;                  // SQ轮询空闲时间
    bool use_sq_poll;                       // 启用SQ轮询模式
    bool use_iopoll;                        // 启用IO轮询
    bool use_kernel_sqe_thread;             // 使用内核SQE线程
    uint32_t cq_entries_multiplier;         // CQ条目倍数
    uint32_t worker_threads;                // 工作线程数
    uint32_t timeout_ms;                    // 操作超时时间
    size_t buffer_size;                     // 默认缓冲区大小
    
    // 内存管理
    void *allocator;
} kv_uring_config_t;

// 前向声明
struct kv_uring_s;
struct kv_uring_request_s;

// === io_uring 请求回调 ===
typedef void (*kv_uring_callback_t)(struct kv_uring_request_s *req, int result, void *data);

// === io_uring 请求结构 ===
typedef struct kv_uring_request_s {
    // 基本信息
    uint64_t request_id;                    // 请求ID
    kv_uring_op_type_t op_type;            // 操作类型
    kv_uring_req_status_t status;          // 请求状态
    
    // 文件操作参数
    int fd;                                 // 文件描述符
    void *buffer;                           // 数据缓冲区
    size_t size;                            // 数据大小
    off_t offset;                           // 文件偏移量
    
    // vectored I/O参数
    struct iovec *iovecs;                   // iovec数组
    int iovcnt;                             // iovec数量
    
    // 回调和用户数据
    kv_uring_callback_t callback;          // 完成回调
    void *user_data;                        // 用户数据
    
    // 结果和时间统计
    int result;                             // 操作结果
    struct timespec submit_time;            // 提交时间
    struct timespec complete_time;          // 完成时间
    
    // 内部使用
    struct io_uring_sqe *sqe;              // SQE指针
    struct io_uring_cqe *cqe;              // CQE指针
    bool owns_buffer;                       // 是否拥有缓冲区
    
    // 链表节点
    struct kv_uring_request_s *next;
    struct kv_uring_request_s *prev;
} kv_uring_request_t;

// === io_uring 批量请求 ===
typedef struct kv_uring_batch_s {
    kv_uring_request_t **requests;          // 请求数组
    size_t count;                           // 请求数量
    size_t capacity;                        // 容量
    bool is_submitted;                      // 是否已提交
    pthread_mutex_t lock;                   // 互斥锁
} kv_uring_batch_t;

// === io_uring 统计信息 ===
typedef struct kv_uring_stats_s {
    // 操作计数
    uint64_t read_count;                    // 读操作次数
    uint64_t write_count;                   // 写操作次数
    uint64_t readv_count;                   // readv操作次数
    uint64_t writev_count;                  // writev操作次数
    uint64_t sync_count;                    // 同步操作次数
    uint64_t total_operations;              // 总操作次数
    
    // 数据量统计
    uint64_t bytes_read;                    // 读取字节数
    uint64_t bytes_written;                 // 写入字节数
    
    // 时间统计（微秒）
    uint64_t total_submit_time;             // 总提交时间
    uint64_t total_complete_time;           // 总完成时间
    uint64_t total_queue_time;              // 总队列时间
    
    // 队列统计
    uint64_t submissions_total;             // 总提交数
    uint64_t completions_total;             // 总完成数
    uint64_t submissions_batched;           // 批量提交数
    uint64_t queue_full_count;              // 队列满次数
    
    // 错误统计
    uint64_t error_count;                   // 错误次数
    uint64_t timeout_count;                 // 超时次数
    uint64_t cancelled_count;               // 取消次数
    
    // 性能指标
    double avg_submit_latency;              // 平均提交延迟
    double avg_complete_latency;            // 平均完成延迟
    double avg_queue_depth;                 // 平均队列深度
    double read_throughput;                 // 读吞吐量(MB/s)
    double write_throughput;                // 写吞吐量(MB/s)
} kv_uring_stats_t;

// === io_uring 主结构 ===
typedef struct kv_uring_s {
    // io_uring实例
    struct io_uring ring;                   // liburing环
    
    // 配置信息
    kv_uring_config_t config;              // 配置
    
    // 请求管理
    kv_uring_request_t *active_requests;    // 活跃请求链表
    kv_uring_request_t *free_requests;      // 空闲请求池
    uint64_t next_request_id;               // 下一个请求ID
    size_t active_count;                    // 活跃请求数
    size_t free_count;                      // 空闲请求数
    
    // 批量操作
    kv_uring_batch_t *pending_batches;      // 待处理批次
    size_t batch_count;                     // 批次数量
    
    // 统计信息
    kv_uring_stats_t stats;                 // 统计信息
    
    // 线程安全
    pthread_mutex_t ring_lock;              // 环锁
    pthread_mutex_t request_lock;           // 请求锁
    pthread_mutex_t stats_lock;             // 统计锁
    pthread_mutex_t batch_lock;             // 批量操作锁
    
    // 事件处理
    pthread_t completion_thread;            // 完成处理线程
    bool completion_thread_running;         // 完成线程状态
    pthread_cond_t completion_cond;         // 完成条件变量
    pthread_mutex_t completion_lock;        // 完成处理锁
    
    // 状态标志
    bool is_initialized;                    // 是否已初始化
    bool is_shutdown;                       // 是否关闭
    
    // 内存管理
    void *allocator;
} kv_uring_t;

// === 配置管理函数 ===
kv_uring_config_t* kv_uring_config_create(void);
void kv_uring_config_destroy(kv_uring_config_t *config);
int kv_uring_config_set_queue_depth(kv_uring_config_t *config, uint32_t depth);
int kv_uring_config_set_sq_poll(kv_uring_config_t *config, bool enable, uint32_t idle_ms);
int kv_uring_config_set_iopoll(kv_uring_config_t *config, bool enable);
int kv_uring_config_set_buffer_size(kv_uring_config_t *config, size_t size);
int kv_uring_config_validate(const kv_uring_config_t *config);

// === 生命周期管理函数 ===
kv_uring_t* kv_uring_create(const kv_uring_config_t *config);
void kv_uring_destroy(kv_uring_t *uring);
int kv_uring_start(kv_uring_t *uring);
int kv_uring_stop(kv_uring_t *uring);

// === 基础异步I/O操作 ===
kv_uring_request_t* kv_uring_read_async(kv_uring_t *uring, int fd, void *buffer, 
                                        size_t size, off_t offset,
                                        kv_uring_callback_t callback, void *user_data);
kv_uring_request_t* kv_uring_write_async(kv_uring_t *uring, int fd, const void *buffer, 
                                         size_t size, off_t offset,
                                         kv_uring_callback_t callback, void *user_data);
kv_uring_request_t* kv_uring_readv_async(kv_uring_t *uring, int fd, struct iovec *iovecs, 
                                         int iovcnt, off_t offset,
                                         kv_uring_callback_t callback, void *user_data);
kv_uring_request_t* kv_uring_writev_async(kv_uring_t *uring, int fd, const struct iovec *iovecs, 
                                          int iovcnt, off_t offset,
                                          kv_uring_callback_t callback, void *user_data);
kv_uring_request_t* kv_uring_fsync_async(kv_uring_t *uring, int fd,
                                         kv_uring_callback_t callback, void *user_data);

// === 批量异步I/O操作 ===
kv_uring_batch_t* kv_uring_batch_create(kv_uring_t *uring, size_t capacity);
void kv_uring_batch_destroy(kv_uring_batch_t *batch);
int kv_uring_batch_add_read(kv_uring_batch_t *batch, int fd, void *buffer, 
                           size_t size, off_t offset, 
                           kv_uring_callback_t callback, void *user_data);
int kv_uring_batch_add_write(kv_uring_batch_t *batch, int fd, const void *buffer, 
                            size_t size, off_t offset,
                            kv_uring_callback_t callback, void *user_data);
int kv_uring_batch_submit(kv_uring_t *uring, kv_uring_batch_t *batch);

// === 请求管理函数 ===
int kv_uring_wait_request(kv_uring_t *uring, kv_uring_request_t *request, uint32_t timeout_ms);
int kv_uring_cancel_request(kv_uring_t *uring, kv_uring_request_t *request);
void kv_uring_request_destroy(kv_uring_request_t *request);
kv_uring_req_status_t kv_uring_request_status(const kv_uring_request_t *request);
int kv_uring_request_result(const kv_uring_request_t *request);

// === 完成处理函数 ===
int kv_uring_submit_and_wait(kv_uring_t *uring, unsigned wait_nr, uint32_t timeout_ms);
int kv_uring_submit(kv_uring_t *uring);
int kv_uring_wait_completions(kv_uring_t *uring, unsigned wait_nr, uint32_t timeout_ms);
int kv_uring_peek_completions(kv_uring_t *uring);

// === 统计信息函数 ===
int kv_uring_get_stats(kv_uring_t *uring, kv_uring_stats_t *stats);
int kv_uring_reset_stats(kv_uring_t *uring);
int kv_uring_print_stats(kv_uring_t *uring, FILE *fp);
double kv_uring_get_throughput(kv_uring_t *uring, kv_uring_op_type_t op_type);
double kv_uring_get_latency(kv_uring_t *uring, kv_uring_op_type_t op_type);

// === 工具函数 ===
uint64_t kv_uring_get_timestamp_us(void);
bool kv_uring_is_supported(void);
int kv_uring_probe_features(kv_uring_t *uring);
const char* kv_uring_op_name(kv_uring_op_type_t op_type);
const char* kv_uring_status_name(kv_uring_req_status_t status);

// === 调试函数 ===
int kv_uring_set_debug_level(kv_uring_t *uring, int level);
int kv_uring_dump_state(kv_uring_t *uring, FILE *fp);
int kv_uring_validate_state(kv_uring_t *uring);

#ifdef __cplusplus
}
#endif

#endif // KV_IO_URING_H 