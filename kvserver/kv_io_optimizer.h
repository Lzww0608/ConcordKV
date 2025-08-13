/*
 * @Author: Lzww0608
 * @Date: 2025-6-10 16:15:11
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-10 16:15:11
 * @Description: ConcordKV I/O 操作优化层 - 头文件
 */

#ifndef KV_IO_OPTIMIZER_H
#define KV_IO_OPTIMIZER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <aio.h>
#include <signal.h>

#include "kv_error.h"

#ifdef __cplusplus
extern "C" {
#endif

// === I/O优化配置 ===
#define KV_IO_DEFAULT_BUFFER_SIZE           (64 * 1024)     // 64KB
#define KV_IO_LARGE_BUFFER_SIZE             (1024 * 1024)   // 1MB
#define KV_IO_HUGE_BUFFER_SIZE              (16 * 1024 * 1024)  // 16MB
#define KV_IO_MAX_BATCH_SIZE                (256)
#define KV_IO_MAX_ASYNC_OPERATIONS          (64)
#define KV_IO_READAHEAD_SIZE                (1 * 1024 * 1024)   // 1MB
#define KV_IO_MMAP_THRESHOLD                (4 * 1024 * 1024)   // 4MB
#define KV_IO_DIRECT_IO_ALIGNMENT           (512)
#define KV_IO_TIMEOUT_MS                    (5000)           // 5秒超时

// === I/O操作类型 ===
typedef enum {
    KV_IO_OP_READ = 0,
    KV_IO_OP_WRITE,
    KV_IO_OP_FSYNC,
    KV_IO_OP_FDATASYNC,
    KV_IO_OP_PREAD,
    KV_IO_OP_PWRITE
} kv_io_op_type_t;

// === I/O访问模式 ===
typedef enum {
    KV_IO_ACCESS_SEQUENTIAL = 0,    // 顺序访问
    KV_IO_ACCESS_RANDOM,           // 随机访问
    KV_IO_ACCESS_MIXED            // 混合访问
} kv_io_access_pattern_t;

// === I/O优化策略 ===
typedef enum {
    KV_IO_STRATEGY_BUFFERED = 0,   // 缓冲I/O
    KV_IO_STRATEGY_DIRECT,         // 直接I/O
    KV_IO_STRATEGY_ASYNC,          // 异步I/O
    KV_IO_STRATEGY_MMAP,           // 内存映射
    KV_IO_STRATEGY_BATCH,          // 批量I/O
    KV_IO_STRATEGY_HYBRID          // 混合策略
} kv_io_strategy_t;

// === I/O请求结构 ===
typedef struct kv_io_request_s {
    kv_io_op_type_t op_type;       // 操作类型
    int fd;                        // 文件描述符
    void *buffer;                  // 数据缓冲区
    size_t size;                   // 数据大小
    off_t offset;                  // 文件偏移量
    
    // 异步I/O相关
    struct aiocb *aiocb;           // 异步I/O控制块
    bool is_async;                 // 是否异步操作
    void (*callback)(struct kv_io_request_s *req, int result);  // 回调函数
    void *callback_data;           // 回调数据
    
    // 状态和统计
    int result;                    // 操作结果
    struct timespec start_time;    // 开始时间
    struct timespec end_time;      // 结束时间
    uint64_t request_id;           // 请求ID
    
    // 链表节点
    struct kv_io_request_s *next;
    struct kv_io_request_s *prev;
} kv_io_request_t;

// === 批量I/O请求 ===
typedef struct kv_io_batch_s {
    kv_io_request_t **requests;    // 请求数组
    size_t count;                  // 请求数量
    size_t capacity;               // 容量
    bool is_submitted;             // 是否已提交
    pthread_mutex_t lock;          // 互斥锁
} kv_io_batch_t;

// === 内存映射区域 ===
typedef struct kv_io_mmap_region_s {
    void *addr;                    // 映射地址
    size_t size;                   // 映射大小
    int fd;                        // 文件描述符
    off_t offset;                  // 文件偏移量
    int prot;                      // 保护标志
    int flags;                     // 映射标志
    
    // 访问统计
    uint64_t access_count;         // 访问次数
    struct timespec last_access;   // 最后访问时间
    
    // 链表节点
    struct kv_io_mmap_region_s *next;
} kv_io_mmap_region_t;

// === I/O缓冲区 ===
typedef struct kv_io_buffer_s {
    void *data;                    // 数据指针
    size_t size;                   // 缓冲区大小
    size_t used;                   // 已使用大小
    size_t capacity;               // 容量
    bool is_aligned;               // 是否内存对齐
    
    // 缓冲区状态
    bool is_dirty;                 // 是否脏数据
    bool is_read_only;             // 是否只读
    
    // 统计信息
    uint64_t read_count;           // 读取次数
    uint64_t write_count;          // 写入次数
    
    // 内存管理
    void *allocator;
} kv_io_buffer_t;

// === I/O性能统计 ===
typedef struct kv_io_stats_s {
    // 操作计数
    uint64_t read_count;           // 读操作次数
    uint64_t write_count;          // 写操作次数
    uint64_t sync_count;           // 同步次数
    uint64_t async_count;          // 异步操作次数
    uint64_t batch_count;          // 批量操作次数
    uint64_t mmap_count;           // 内存映射次数
    
    // 数据量统计
    uint64_t bytes_read;           // 读取字节数
    uint64_t bytes_written;        // 写入字节数
    
    // 时间统计（微秒）
    uint64_t total_read_time;      // 总读取时间
    uint64_t total_write_time;     // 总写入时间
    uint64_t total_sync_time;      // 总同步时间
    
    // 错误统计
    uint64_t error_count;          // 错误次数
    uint64_t timeout_count;        // 超时次数
    uint64_t retry_count;          // 重试次数
    
    // 性能指标
    double avg_read_latency;       // 平均读延迟
    double avg_write_latency;      // 平均写延迟
    double read_throughput;        // 读吞吐量(MB/s)
    double write_throughput;       // 写吞吐量(MB/s)
    
    // 缓存命中率
    uint64_t cache_hit_count;      // 缓存命中次数
    uint64_t cache_miss_count;     // 缓存未命中次数
    double cache_hit_ratio;        // 缓存命中率
} kv_io_stats_t;

// === I/O优化器配置 ===
typedef struct kv_io_optimizer_config_s {
    // 基础配置
    size_t buffer_size;            // 缓冲区大小
    size_t max_batch_size;         // 最大批量大小
    size_t max_async_operations;   // 最大异步操作数
    size_t readahead_size;         // 预读大小
    size_t mmap_threshold;         // 内存映射阈值
    uint32_t timeout_ms;           // 超时时间
    
    // 策略配置
    kv_io_strategy_t default_strategy;     // 默认策略
    kv_io_access_pattern_t access_pattern; // 访问模式
    bool enable_direct_io;         // 启用直接I/O
    bool enable_async_io;          // 启用异步I/O
    bool enable_mmap;              // 启用内存映射
    bool enable_batch_io;          // 启用批量I/O
    bool enable_readahead;         // 启用预读
    
    // 性能调优
    int io_priority;               // I/O优先级
    int nice_value;                // 进程优先级
    bool use_o_direct;             // 使用O_DIRECT
    bool use_o_sync;               // 使用O_SYNC
    bool use_o_dsync;              // 使用O_DSYNC
    bool use_posix_fadvise;        // 使用posix_fadvise
    bool use_madvise;              // 使用madvise
    
    // 资源限制
    size_t max_memory_usage;       // 最大内存使用
    size_t max_open_files;         // 最大打开文件数
    size_t max_mmap_regions;       // 最大内存映射区域数
    
    // 内存管理
    void *allocator;
} kv_io_optimizer_config_t;

// === I/O优化器主结构 ===
typedef struct kv_io_optimizer_s {
    // 配置信息
    kv_io_optimizer_config_t config;
    
    // 请求管理
    kv_io_request_t *active_requests;      // 活跃请求链表
    kv_io_request_t *completed_requests;   // 完成请求链表
    uint64_t next_request_id;              // 下一个请求ID
    
    // 批量操作
    kv_io_batch_t *pending_batches;        // 待处理批次
    size_t batch_count;                    // 批次数量
    
    // 内存映射
    kv_io_mmap_region_t *mmap_regions;     // 内存映射区域
    size_t mmap_count;                     // 映射区域数量
    
    // 缓冲区管理
    kv_io_buffer_t **buffers;              // 缓冲区池
    size_t buffer_count;                   // 缓冲区数量
    size_t buffer_capacity;                // 缓冲区容量
    
    // 统计信息
    kv_io_stats_t stats;
    
    // 线程安全
    pthread_mutex_t lock;                  // 主锁
    pthread_mutex_t stats_lock;            // 统计锁
    pthread_mutex_t batch_lock;            // 批量操作锁
    pthread_mutex_t mmap_lock;             // 内存映射锁
    
    // 异步I/O支持
    pthread_t async_thread;                // 异步处理线程
    bool async_thread_running;             // 异步线程状态
    pthread_cond_t async_cond;             // 异步条件变量
    
    // 状态标志
    bool is_initialized;                   // 是否已初始化
    bool is_shutdown;                      // 是否关闭
    
    // 内存管理
    void *allocator;
} kv_io_optimizer_t;

// === 函数声明 ===

// 配置管理
kv_io_optimizer_config_t* kv_io_optimizer_config_create(void);
void kv_io_optimizer_config_destroy(kv_io_optimizer_config_t *config);
int kv_io_optimizer_config_set_buffer_size(kv_io_optimizer_config_t *config, size_t size);
int kv_io_optimizer_config_set_strategy(kv_io_optimizer_config_t *config, kv_io_strategy_t strategy);
int kv_io_optimizer_config_set_access_pattern(kv_io_optimizer_config_t *config, kv_io_access_pattern_t pattern);
int kv_io_optimizer_config_validate(const kv_io_optimizer_config_t *config);

// I/O优化器生命周期
kv_io_optimizer_t* kv_io_optimizer_create(const kv_io_optimizer_config_t *config);
void kv_io_optimizer_destroy(kv_io_optimizer_t *optimizer);
int kv_io_optimizer_start(kv_io_optimizer_t *optimizer);
int kv_io_optimizer_stop(kv_io_optimizer_t *optimizer);

// 基础I/O操作
int kv_io_read(kv_io_optimizer_t *optimizer, int fd, void *buffer, size_t size, off_t offset);
int kv_io_write(kv_io_optimizer_t *optimizer, int fd, const void *buffer, size_t size, off_t offset);
int kv_io_sync(kv_io_optimizer_t *optimizer, int fd);
int kv_io_fsync(kv_io_optimizer_t *optimizer, int fd);
int kv_io_fdatasync(kv_io_optimizer_t *optimizer, int fd);

// 异步I/O操作
kv_io_request_t* kv_io_async_read(kv_io_optimizer_t *optimizer, int fd, void *buffer, 
                                 size_t size, off_t offset, 
                                 void (*callback)(kv_io_request_t*, int), void *callback_data);
kv_io_request_t* kv_io_async_write(kv_io_optimizer_t *optimizer, int fd, const void *buffer, 
                                  size_t size, off_t offset,
                                  void (*callback)(kv_io_request_t*, int), void *callback_data);
int kv_io_async_wait(kv_io_optimizer_t *optimizer, kv_io_request_t *request, uint32_t timeout_ms);
int kv_io_async_cancel(kv_io_optimizer_t *optimizer, kv_io_request_t *request);
void kv_io_request_destroy(kv_io_request_t *request);

// 批量I/O操作
kv_io_batch_t* kv_io_batch_create(kv_io_optimizer_t *optimizer, size_t capacity);
void kv_io_batch_destroy(kv_io_batch_t *batch);
int kv_io_batch_add_read(kv_io_batch_t *batch, int fd, void *buffer, size_t size, off_t offset);
int kv_io_batch_add_write(kv_io_batch_t *batch, int fd, const void *buffer, size_t size, off_t offset);
int kv_io_batch_submit(kv_io_optimizer_t *optimizer, kv_io_batch_t *batch);
int kv_io_batch_wait(kv_io_optimizer_t *optimizer, kv_io_batch_t *batch, uint32_t timeout_ms);

// 内存映射操作
void* kv_io_mmap(kv_io_optimizer_t *optimizer, int fd, size_t size, off_t offset, int prot, int flags);
int kv_io_munmap(kv_io_optimizer_t *optimizer, void *addr, size_t size);
int kv_io_madvise(kv_io_optimizer_t *optimizer, void *addr, size_t size, int advice);
int kv_io_msync(kv_io_optimizer_t *optimizer, void *addr, size_t size, int flags);

// 缓冲区管理
kv_io_buffer_t* kv_io_buffer_create(kv_io_optimizer_t *optimizer, size_t size, bool aligned);
void kv_io_buffer_destroy(kv_io_buffer_t *buffer);
int kv_io_buffer_read(kv_io_buffer_t *buffer, void *data, size_t size, size_t offset);
int kv_io_buffer_write(kv_io_buffer_t *buffer, const void *data, size_t size, size_t offset);
int kv_io_buffer_flush(kv_io_optimizer_t *optimizer, kv_io_buffer_t *buffer, int fd);
int kv_io_buffer_clear(kv_io_buffer_t *buffer);

// 预读优化
int kv_io_readahead(kv_io_optimizer_t *optimizer, int fd, off_t offset, size_t size);
int kv_io_posix_fadvise(kv_io_optimizer_t *optimizer, int fd, off_t offset, size_t size, int advice);

// 文件操作优化
int kv_io_open_optimized(kv_io_optimizer_t *optimizer, const char *pathname, int flags, mode_t mode);
int kv_io_close_optimized(kv_io_optimizer_t *optimizer, int fd);
int kv_io_fallocate(kv_io_optimizer_t *optimizer, int fd, off_t offset, off_t len);
int kv_io_ftruncate(kv_io_optimizer_t *optimizer, int fd, off_t length);

// 性能监控
int kv_io_get_stats(kv_io_optimizer_t *optimizer, kv_io_stats_t *stats);
int kv_io_reset_stats(kv_io_optimizer_t *optimizer);
int kv_io_print_stats(kv_io_optimizer_t *optimizer, FILE *fp);
double kv_io_get_throughput(kv_io_optimizer_t *optimizer, kv_io_op_type_t op_type);
double kv_io_get_latency(kv_io_optimizer_t *optimizer, kv_io_op_type_t op_type);

// 工具函数
uint64_t kv_io_get_timestamp_us(void);
int kv_io_get_page_size(void);
int kv_io_get_block_size(int fd);
int kv_io_get_filesystem_type(int fd, char *buffer, size_t buffer_size);
bool kv_io_is_ssd(int fd);
int kv_io_get_device_stats(int fd, kv_io_stats_t *stats);

// 内存对齐
void* kv_io_aligned_alloc(size_t alignment, size_t size);
void kv_io_aligned_free(void *ptr);
bool kv_io_is_aligned(const void *ptr, size_t alignment);

// 调试和诊断
int kv_io_set_debug_level(kv_io_optimizer_t *optimizer, int level);
int kv_io_dump_state(kv_io_optimizer_t *optimizer, FILE *fp);
int kv_io_validate_state(kv_io_optimizer_t *optimizer);

#ifdef __cplusplus
}
#endif

#endif // KV_IO_OPTIMIZER_H 