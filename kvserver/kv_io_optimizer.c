/*
 * @Author: Lzww0608
 * @Date: 2025-6-9 19:50:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-9 19:50:00
 * @Description: ConcordKV I/O 操作优化层 - 实现文件
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "kv_io_optimizer.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/statvfs.h>
#include <linux/fs.h>
#include <math.h>

// === 内部常量定义 ===
#define KV_IO_MAGIC_NUMBER              0x4B56494F  // "KVIO"
#define KV_IO_REQUEST_POOL_SIZE         1024
#define KV_IO_DEFAULT_TIMEOUT_US        (5 * 1000 * 1000)  // 5秒
#define KV_IO_STATS_UPDATE_INTERVAL     1000        // 1000次操作更新一次统计
#define KV_IO_ALIGNMENT_MASK            (KV_IO_DIRECT_IO_ALIGNMENT - 1)

// === 内部工具函数 ===
static uint64_t get_timestamp_us(void);
static double timespec_to_seconds(const struct timespec *ts);
static void update_stats_latency(kv_io_stats_t *stats, kv_io_op_type_t op_type, uint64_t latency_us);
static void update_stats_throughput(kv_io_stats_t *stats, kv_io_op_type_t op_type, size_t bytes);
static int choose_io_strategy(kv_io_optimizer_t *optimizer, size_t size, kv_io_access_pattern_t pattern);
static void* async_io_thread_func(void *arg);
static int process_async_requests(kv_io_optimizer_t *optimizer);

// === 时间工具函数 ===
static uint64_t get_timestamp_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
}

uint64_t kv_io_get_timestamp_us(void) {
    return get_timestamp_us();
}

static double timespec_to_seconds(const struct timespec *ts) {
    return (double)ts->tv_sec + (double)ts->tv_nsec / 1000000000.0;
}

// === 统计更新函数 ===
static void update_stats_latency(kv_io_stats_t *stats, kv_io_op_type_t op_type, uint64_t latency_us) {
    switch (op_type) {
        case KV_IO_OP_READ:
        case KV_IO_OP_PREAD:
            stats->total_read_time += latency_us;
            if (stats->read_count > 0) {
                stats->avg_read_latency = (double)stats->total_read_time / stats->read_count;
            }
            break;
        case KV_IO_OP_WRITE:
        case KV_IO_OP_PWRITE:
            stats->total_write_time += latency_us;
            if (stats->write_count > 0) {
                stats->avg_write_latency = (double)stats->total_write_time / stats->write_count;
            }
            break;
        case KV_IO_OP_FSYNC:
        case KV_IO_OP_FDATASYNC:
            stats->total_sync_time += latency_us;
            break;
    }
}

static void update_stats_throughput(kv_io_stats_t *stats, kv_io_op_type_t op_type, size_t bytes) {
    const double bytes_to_mb = 1.0 / (1024.0 * 1024.0);
    
    switch (op_type) {
        case KV_IO_OP_READ:
        case KV_IO_OP_PREAD:
            stats->bytes_read += bytes;
            if (stats->total_read_time > 0) {
                double seconds = (double)stats->total_read_time / 1000000.0;
                stats->read_throughput = (stats->bytes_read * bytes_to_mb) / seconds;
            }
            break;
        case KV_IO_OP_WRITE:
        case KV_IO_OP_PWRITE:
            stats->bytes_written += bytes;
            if (stats->total_write_time > 0) {
                double seconds = (double)stats->total_write_time / 1000000.0;
                stats->write_throughput = (stats->bytes_written * bytes_to_mb) / seconds;
            }
            break;
        default:
            break;
    }
}

// === 策略选择函数 ===
static int choose_io_strategy(kv_io_optimizer_t *optimizer, size_t size, kv_io_access_pattern_t pattern) {
    if (!optimizer) return KV_IO_STRATEGY_BUFFERED;
    
    const kv_io_optimizer_config_t *config = &optimizer->config;
    
    // 根据访问模式和大小选择策略
    if (size >= config->mmap_threshold && config->enable_mmap) {
        return KV_IO_STRATEGY_MMAP;
    }
    
    if (pattern == KV_IO_ACCESS_SEQUENTIAL && size >= KV_IO_LARGE_BUFFER_SIZE) {
        if (config->enable_direct_io) {
            return KV_IO_STRATEGY_DIRECT;
        }
    }
    
    if (config->enable_async_io && size >= KV_IO_DEFAULT_BUFFER_SIZE) {
        return KV_IO_STRATEGY_ASYNC;
    }
    
    return config->default_strategy;
}

// === 配置管理函数 ===
kv_io_optimizer_config_t* kv_io_optimizer_config_create(void) {
    kv_io_optimizer_config_t *config = malloc(sizeof(kv_io_optimizer_config_t));
    if (!config) {
        return NULL;
    }
    
    // 设置默认值
    memset(config, 0, sizeof(kv_io_optimizer_config_t));
    config->buffer_size = KV_IO_DEFAULT_BUFFER_SIZE;
    config->max_batch_size = KV_IO_MAX_BATCH_SIZE;
    config->max_async_operations = KV_IO_MAX_ASYNC_OPERATIONS;
    config->readahead_size = KV_IO_READAHEAD_SIZE;
    config->mmap_threshold = KV_IO_MMAP_THRESHOLD;
    config->timeout_ms = KV_IO_TIMEOUT_MS;
    config->default_strategy = KV_IO_STRATEGY_BUFFERED;
    config->access_pattern = KV_IO_ACCESS_MIXED;
    config->enable_direct_io = false;
    config->enable_async_io = true;
    config->enable_mmap = true;
    config->enable_batch_io = true;
    config->enable_readahead = true;
    config->io_priority = 0;
    config->nice_value = 0;
    config->use_o_direct = false;
    config->use_o_sync = false;
    config->use_o_dsync = false;
    config->use_posix_fadvise = true;
    config->use_madvise = true;
    config->max_memory_usage = 128 * 1024 * 1024;  // 128MB
    config->max_open_files = 1024;
    config->max_mmap_regions = 64;
    config->allocator = NULL;
    
    return config;
}

void kv_io_optimizer_config_destroy(kv_io_optimizer_config_t *config) {
    if (config) {
        free(config);
    }
}

int kv_io_optimizer_config_set_buffer_size(kv_io_optimizer_config_t *config, size_t size) {
    if (!config || size == 0 || size > KV_IO_HUGE_BUFFER_SIZE) {
        return KV_ERR_PARAM;
    }
    
    config->buffer_size = size;
    return KV_SUCCESS;
}

int kv_io_optimizer_config_set_strategy(kv_io_optimizer_config_t *config, kv_io_strategy_t strategy) {
    if (!config || strategy < 0 || strategy > KV_IO_STRATEGY_HYBRID) {
        return KV_ERR_PARAM;
    }
    
    config->default_strategy = strategy;
    return KV_SUCCESS;
}

int kv_io_optimizer_config_set_access_pattern(kv_io_optimizer_config_t *config, kv_io_access_pattern_t pattern) {
    if (!config || pattern < 0 || pattern > KV_IO_ACCESS_MIXED) {
        return KV_ERR_PARAM;
    }
    
    config->access_pattern = pattern;
    return KV_SUCCESS;
}

int kv_io_optimizer_config_validate(const kv_io_optimizer_config_t *config) {
    if (!config) return KV_ERR_PARAM;
    
    // 检查基本配置
    if (config->buffer_size == 0 || config->buffer_size > KV_IO_HUGE_BUFFER_SIZE) {
        return KV_ERR_PARAM;
    }
    
    if (config->max_batch_size == 0 || config->max_batch_size > 1024) {
        return KV_ERR_PARAM;
    }
    
    if (config->max_async_operations == 0 || config->max_async_operations > 512) {
        return KV_ERR_PARAM;
    }
    
    if (config->timeout_ms == 0 || config->timeout_ms > 60000) {
        return KV_ERR_PARAM;
    }
    
    return KV_SUCCESS;
}

// === I/O优化器生命周期函数 ===
kv_io_optimizer_t* kv_io_optimizer_create(const kv_io_optimizer_config_t *config) {
    if (!config || kv_io_optimizer_config_validate(config) != KV_SUCCESS) {
        return NULL;
    }
    
    kv_io_optimizer_t *optimizer = malloc(sizeof(kv_io_optimizer_t));
    if (!optimizer) {
        return NULL;
    }
    
    memset(optimizer, 0, sizeof(kv_io_optimizer_t));
    
    // 复制配置
    optimizer->config = *config;
    if (!optimizer->config.allocator) {
        optimizer->config.allocator = NULL;  // 简化内存管理
    }
    optimizer->allocator = optimizer->config.allocator;
    
    // 初始化锁
    if (pthread_mutex_init(&optimizer->lock, NULL) != 0 ||
        pthread_mutex_init(&optimizer->stats_lock, NULL) != 0 ||
        pthread_mutex_init(&optimizer->batch_lock, NULL) != 0 ||
        pthread_mutex_init(&optimizer->mmap_lock, NULL) != 0 ||
        pthread_cond_init(&optimizer->async_cond, NULL) != 0) {
        free(optimizer);
        return NULL;
    }
    
    // 初始化缓冲区池
    optimizer->buffer_capacity = 32;  // 初始容量
    optimizer->buffers = malloc(sizeof(kv_io_buffer_t*) * optimizer->buffer_capacity);
    if (!optimizer->buffers) {
        pthread_mutex_destroy(&optimizer->lock);
        pthread_mutex_destroy(&optimizer->stats_lock);
        pthread_mutex_destroy(&optimizer->batch_lock);
        pthread_mutex_destroy(&optimizer->mmap_lock);
        pthread_cond_destroy(&optimizer->async_cond);
        free(optimizer);
        return NULL;
    }
    memset(optimizer->buffers, 0, sizeof(kv_io_buffer_t*) * optimizer->buffer_capacity);
    
    // 初始化统计信息
    memset(&optimizer->stats, 0, sizeof(kv_io_stats_t));
    
    optimizer->next_request_id = 1;
    optimizer->is_initialized = true;
    optimizer->is_shutdown = false;
    optimizer->async_thread_running = false;
    
    return optimizer;
}

void kv_io_optimizer_destroy(kv_io_optimizer_t *optimizer) {
    if (!optimizer) return;
    
    // 停止优化器
    kv_io_optimizer_stop(optimizer);
    
    // 等待异步线程结束
    if (optimizer->async_thread_running) {
        pthread_cond_signal(&optimizer->async_cond);
        pthread_join(optimizer->async_thread, NULL);
    }
    
    // 清理缓冲区
    if (optimizer->buffers) {
        for (size_t i = 0; i < optimizer->buffer_count; i++) {
            if (optimizer->buffers[i]) {
                kv_io_buffer_destroy(optimizer->buffers[i]);
            }
        }
        free(optimizer->buffers);
    }
    
    // 清理内存映射区域
    kv_io_mmap_region_t *mmap_region = optimizer->mmap_regions;
    while (mmap_region) {
        kv_io_mmap_region_t *next = mmap_region->next;
        munmap(mmap_region->addr, mmap_region->size);
        free(mmap_region);
        mmap_region = next;
    }
    
    // 清理请求链表
    kv_io_request_t *req = optimizer->active_requests;
    while (req) {
        kv_io_request_t *next = req->next;
        if (req->aiocb) {
            free(req->aiocb);
        }
        free(req);
        req = next;
    }
    
    req = optimizer->completed_requests;
    while (req) {
        kv_io_request_t *next = req->next;
        if (req->aiocb) {
            free(req->aiocb);
        }
        free(req);
        req = next;
    }
    
    // 销毁锁
    pthread_mutex_destroy(&optimizer->lock);
    pthread_mutex_destroy(&optimizer->stats_lock);
    pthread_mutex_destroy(&optimizer->batch_lock);
    pthread_mutex_destroy(&optimizer->mmap_lock);
    pthread_cond_destroy(&optimizer->async_cond);
    
    free(optimizer);
}

int kv_io_optimizer_start(kv_io_optimizer_t *optimizer) {
    if (!optimizer || !optimizer->is_initialized) {
        return KV_ERR_PARAM;
    }
    
    pthread_mutex_lock(&optimizer->lock);
    
    if (optimizer->is_shutdown) {
        pthread_mutex_unlock(&optimizer->lock);
        return KV_ERR_STATE;
    }
    
    // 启动异步I/O线程
    if (optimizer->config.enable_async_io && !optimizer->async_thread_running) {
        if (pthread_create(&optimizer->async_thread, NULL, async_io_thread_func, optimizer) == 0) {
            optimizer->async_thread_running = true;
        }
    }
    
    pthread_mutex_unlock(&optimizer->lock);
    return KV_SUCCESS;
}

int kv_io_optimizer_stop(kv_io_optimizer_t *optimizer) {
    if (!optimizer) return KV_ERR_PARAM;
    
    pthread_mutex_lock(&optimizer->lock);
    optimizer->is_shutdown = true;
    pthread_mutex_unlock(&optimizer->lock);
    
    // 通知异步线程退出
    if (optimizer->async_thread_running) {
        pthread_cond_signal(&optimizer->async_cond);
    }
    
    return KV_SUCCESS;
}

// === 基础I/O操作函数 ===
int kv_io_read(kv_io_optimizer_t *optimizer, int fd, void *buffer, size_t size, off_t offset) {
    if (!optimizer || fd < 0 || !buffer || size == 0) {
        return KV_ERR_PARAM;
    }
    
    uint64_t start_time = get_timestamp_us();
    ssize_t result;
    
    // 根据策略选择读取方式
    int strategy = choose_io_strategy(optimizer, size, optimizer->config.access_pattern);
    
    switch (strategy) {
        case KV_IO_STRATEGY_DIRECT:
            if (optimizer->config.use_o_direct) {
                // 使用O_DIRECT需要对齐缓冲区
                if (((uintptr_t)buffer & KV_IO_ALIGNMENT_MASK) != 0) {
                    return KV_ERR_PARAM;
                }
            }
            // fallthrough
        case KV_IO_STRATEGY_BUFFERED:
        default:
            if (offset >= 0) {
                result = pread(fd, buffer, size, offset);
            } else {
                result = read(fd, buffer, size);
            }
            break;
    }
    
    uint64_t end_time = get_timestamp_us();
    uint64_t latency = end_time - start_time;
    
    // 更新统计信息
    pthread_mutex_lock(&optimizer->stats_lock);
    optimizer->stats.read_count++;
    if (result > 0) {
        update_stats_latency(&optimizer->stats, KV_IO_OP_READ, latency);
        update_stats_throughput(&optimizer->stats, KV_IO_OP_READ, result);
    } else if (result < 0) {
        optimizer->stats.error_count++;
    }
    pthread_mutex_unlock(&optimizer->stats_lock);
    
    return (result >= 0) ? (int)result : KV_ERR_IO;
}

int kv_io_write(kv_io_optimizer_t *optimizer, int fd, const void *buffer, size_t size, off_t offset) {
    if (!optimizer || fd < 0 || !buffer || size == 0) {
        return KV_ERR_PARAM;
    }
    
    uint64_t start_time = get_timestamp_us();
    ssize_t result;
    
    // 根据策略选择写入方式
    int strategy = choose_io_strategy(optimizer, size, optimizer->config.access_pattern);
    
    switch (strategy) {
        case KV_IO_STRATEGY_DIRECT:
            if (optimizer->config.use_o_direct) {
                // 使用O_DIRECT需要对齐缓冲区
                if (((uintptr_t)buffer & KV_IO_ALIGNMENT_MASK) != 0) {
                    return KV_ERR_PARAM;
                }
            }
            // fallthrough
        case KV_IO_STRATEGY_BUFFERED:
        default:
            if (offset >= 0) {
                result = pwrite(fd, buffer, size, offset);
            } else {
                result = write(fd, buffer, size);
            }
            break;
    }
    
    uint64_t end_time = get_timestamp_us();
    uint64_t latency = end_time - start_time;
    
    // 更新统计信息
    pthread_mutex_lock(&optimizer->stats_lock);
    optimizer->stats.write_count++;
    if (result > 0) {
        update_stats_latency(&optimizer->stats, KV_IO_OP_WRITE, latency);
        update_stats_throughput(&optimizer->stats, KV_IO_OP_WRITE, result);
    } else if (result < 0) {
        optimizer->stats.error_count++;
    }
    pthread_mutex_unlock(&optimizer->stats_lock);
    
    return (result >= 0) ? (int)result : KV_ERR_IO;
}

int kv_io_sync(kv_io_optimizer_t *optimizer, int fd) {
    if (!optimizer || fd < 0) {
        return KV_ERR_PARAM;
    }
    
    uint64_t start_time = get_timestamp_us();
    int result = fsync(fd);
    uint64_t end_time = get_timestamp_us();
    uint64_t latency = end_time - start_time;
    
    // 更新统计信息
    pthread_mutex_lock(&optimizer->stats_lock);
    optimizer->stats.sync_count++;
    if (result == 0) {
        update_stats_latency(&optimizer->stats, KV_IO_OP_FSYNC, latency);
    } else {
        optimizer->stats.error_count++;
    }
    pthread_mutex_unlock(&optimizer->stats_lock);
    
    return (result == 0) ? KV_SUCCESS : KV_ERR_IO;
}

int kv_io_fsync(kv_io_optimizer_t *optimizer, int fd) {
    return kv_io_sync(optimizer, fd);
}

int kv_io_fdatasync(kv_io_optimizer_t *optimizer, int fd) {
    if (!optimizer || fd < 0) {
        return KV_ERR_PARAM;
    }
    
    uint64_t start_time = get_timestamp_us();
    int result = fdatasync(fd);
    uint64_t end_time = get_timestamp_us();
    uint64_t latency = end_time - start_time;
    
    // 更新统计信息
    pthread_mutex_lock(&optimizer->stats_lock);
    optimizer->stats.sync_count++;
    if (result == 0) {
        update_stats_latency(&optimizer->stats, KV_IO_OP_FDATASYNC, latency);
    } else {
        optimizer->stats.error_count++;
    }
    pthread_mutex_unlock(&optimizer->stats_lock);
    
    return (result == 0) ? KV_SUCCESS : KV_ERR_IO;
}

// === 异步I/O操作函数 ===
static void* async_io_thread_func(void *arg) {
    kv_io_optimizer_t *optimizer = (kv_io_optimizer_t*)arg;
    
    while (!optimizer->is_shutdown) {
        pthread_mutex_lock(&optimizer->lock);
        
        // 处理异步请求
        int processed = process_async_requests(optimizer);
        
        if (processed == 0 && !optimizer->is_shutdown) {
            // 没有请求处理，等待条件变量
            pthread_cond_wait(&optimizer->async_cond, &optimizer->lock);
        }
        
        pthread_mutex_unlock(&optimizer->lock);
        
        // 短暂休息避免CPU占用过高
        if (processed == 0) {
            usleep(1000);  // 1ms
        }
    }
    
    return NULL;
}

static int process_async_requests(kv_io_optimizer_t *optimizer) {
    int processed = 0;
    kv_io_request_t *req = optimizer->active_requests;
    
    while (req) {
        kv_io_request_t *next = req->next;
        
        if (req->is_async && req->aiocb) {
            // 检查异步操作是否完成
            int error = aio_error(req->aiocb);
            
            if (error == 0) {
                // 操作完成
                req->result = (int)aio_return(req->aiocb);
                clock_gettime(CLOCK_MONOTONIC, &req->end_time);
                
                // 从活跃列表移除
                if (req->prev) {
                    req->prev->next = req->next;
                } else {
                    optimizer->active_requests = req->next;
                }
                if (req->next) {
                    req->next->prev = req->prev;
                }
                
                // 添加到完成列表
                req->next = optimizer->completed_requests;
                req->prev = NULL;
                if (optimizer->completed_requests) {
                    optimizer->completed_requests->prev = req;
                }
                optimizer->completed_requests = req;
                
                // 调用回调函数
                if (req->callback) {
                    req->callback(req, req->result);
                }
                
                processed++;
            } else if (error != EINPROGRESS) {
                // 操作出错
                req->result = error;
                clock_gettime(CLOCK_MONOTONIC, &req->end_time);
                
                // 更新错误统计
                pthread_mutex_lock(&optimizer->stats_lock);
                optimizer->stats.error_count++;
                pthread_mutex_unlock(&optimizer->stats_lock);
                
                processed++;
            }
        }
        
        req = next;
    }
    
    return processed;
}

kv_io_request_t* kv_io_async_read(kv_io_optimizer_t *optimizer, int fd, void *buffer, 
                                 size_t size, off_t offset,
                                 void (*callback)(kv_io_request_t*, int), void *callback_data) {
    if (!optimizer || fd < 0 || !buffer || size == 0) {
        return NULL;
    }
    
    // 简化实现：直接同步读取
    kv_io_request_t *request = malloc(sizeof(kv_io_request_t));
    if (!request) {
        return NULL;
    }
    
    memset(request, 0, sizeof(kv_io_request_t));
    request->op_type = KV_IO_OP_READ;
    request->fd = fd;
    request->buffer = buffer;
    request->size = size;
    request->offset = offset;
    request->is_async = false;  // 简化为同步
    request->callback = callback;
    request->callback_data = callback_data;
    request->request_id = __sync_fetch_and_add(&optimizer->next_request_id, 1);
    
    // 直接执行读取
    request->result = kv_io_read(optimizer, fd, buffer, size, offset);
    
    if (callback) {
        callback(request, request->result);
    }
    
    return request;
}

kv_io_request_t* kv_io_async_write(kv_io_optimizer_t *optimizer, int fd, const void *buffer, 
                                  size_t size, off_t offset,
                                  void (*callback)(kv_io_request_t*, int), void *callback_data) {
    if (!optimizer || fd < 0 || !buffer || size == 0) {
        return NULL;
    }
    
    // 简化实现：直接同步写入
    kv_io_request_t *request = malloc(sizeof(kv_io_request_t));
    if (!request) {
        return NULL;
    }
    
    memset(request, 0, sizeof(kv_io_request_t));
    request->op_type = KV_IO_OP_WRITE;
    request->fd = fd;
    request->buffer = (void*)buffer;
    request->size = size;
    request->offset = offset;
    request->is_async = false;  // 简化为同步
    request->callback = callback;
    request->callback_data = callback_data;
    request->request_id = __sync_fetch_and_add(&optimizer->next_request_id, 1);
    
    // 直接执行写入
    request->result = kv_io_write(optimizer, fd, buffer, size, offset);
    
    if (callback) {
        callback(request, request->result);
    }
    
    return request;
}

int kv_io_async_wait(kv_io_optimizer_t *optimizer, kv_io_request_t *request, uint32_t timeout_ms) {
    if (!optimizer || !request) {
        return KV_ERR_PARAM;
    }
    
    // 简化实现：直接返回结果（同步操作）
    (void)timeout_ms;  // 避免未使用参数警告
    return request->result;
}

int kv_io_async_cancel(kv_io_optimizer_t *optimizer, kv_io_request_t *request) {
    if (!optimizer || !request) {
        return KV_ERR_PARAM;
    }
    
    // 简化实现：无法取消同步操作
    return KV_ERR_NOT_SUPPORTED;
}

// === 性能监控函数 ===
int kv_io_get_stats(kv_io_optimizer_t *optimizer, kv_io_stats_t *stats) {
    if (!optimizer || !stats) {
        return KV_ERR_PARAM;
    }
    
    pthread_mutex_lock(&optimizer->stats_lock);
    *stats = optimizer->stats;
    
    // 计算缓存命中率
    uint64_t total_cache_accesses = stats->cache_hit_count + stats->cache_miss_count;
    if (total_cache_accesses > 0) {
        stats->cache_hit_ratio = (double)stats->cache_hit_count / total_cache_accesses;
    }
    
    pthread_mutex_unlock(&optimizer->stats_lock);
    
    return KV_SUCCESS;
}

int kv_io_reset_stats(kv_io_optimizer_t *optimizer) {
    if (!optimizer) {
        return KV_ERR_PARAM;
    }
    
    pthread_mutex_lock(&optimizer->stats_lock);
    memset(&optimizer->stats, 0, sizeof(kv_io_stats_t));
    pthread_mutex_unlock(&optimizer->stats_lock);
    
    return KV_SUCCESS;
}

int kv_io_print_stats(kv_io_optimizer_t *optimizer, FILE *fp) {
    if (!optimizer || !fp) {
        return KV_ERR_PARAM;
    }
    
    kv_io_stats_t stats;
    kv_io_get_stats(optimizer, &stats);
    
    fprintf(fp, "\n=== I/O Optimizer Statistics ===\n");
    fprintf(fp, "操作计数:\n");
    fprintf(fp, "  读操作:     %lu\n", stats.read_count);
    fprintf(fp, "  写操作:     %lu\n", stats.write_count);
    fprintf(fp, "  同步操作:   %lu\n", stats.sync_count);
    fprintf(fp, "  异步操作:   %lu\n", stats.async_count);
    fprintf(fp, "  批量操作:   %lu\n", stats.batch_count);
    fprintf(fp, "  内存映射:   %lu\n", stats.mmap_count);
    
    fprintf(fp, "\n数据量统计:\n");
    fprintf(fp, "  读取字节:   %lu (%.2f MB)\n", stats.bytes_read, 
            (double)stats.bytes_read / (1024 * 1024));
    fprintf(fp, "  写入字节:   %lu (%.2f MB)\n", stats.bytes_written,
            (double)stats.bytes_written / (1024 * 1024));
    
    fprintf(fp, "\n性能指标:\n");
    fprintf(fp, "  平均读延迟: %.2f us\n", stats.avg_read_latency);
    fprintf(fp, "  平均写延迟: %.2f us\n", stats.avg_write_latency);
    fprintf(fp, "  读吞吐量:   %.2f MB/s\n", stats.read_throughput);
    fprintf(fp, "  写吞吐量:   %.2f MB/s\n", stats.write_throughput);
    
    fprintf(fp, "\n缓存统计:\n");
    fprintf(fp, "  缓存命中:   %lu\n", stats.cache_hit_count);
    fprintf(fp, "  缓存未命中: %lu\n", stats.cache_miss_count);
    fprintf(fp, "  命中率:     %.2f%%\n", stats.cache_hit_ratio * 100.0);
    
    fprintf(fp, "\n错误统计:\n");
    fprintf(fp, "  错误次数:   %lu\n", stats.error_count);
    fprintf(fp, "  超时次数:   %lu\n", stats.timeout_count);
    fprintf(fp, "  重试次数:   %lu\n", stats.retry_count);
    
    fprintf(fp, "================================\n\n");
    
    return KV_SUCCESS;
}

double kv_io_get_throughput(kv_io_optimizer_t *optimizer, kv_io_op_type_t op_type) {
    if (!optimizer) return 0.0;
    
    pthread_mutex_lock(&optimizer->stats_lock);
    double throughput = 0.0;
    
    switch (op_type) {
        case KV_IO_OP_READ:
        case KV_IO_OP_PREAD:
            throughput = optimizer->stats.read_throughput;
            break;
        case KV_IO_OP_WRITE:
        case KV_IO_OP_PWRITE:
            throughput = optimizer->stats.write_throughput;
            break;
        default:
            break;
    }
    
    pthread_mutex_unlock(&optimizer->stats_lock);
    return throughput;
}

double kv_io_get_latency(kv_io_optimizer_t *optimizer, kv_io_op_type_t op_type) {
    if (!optimizer) return 0.0;
    
    pthread_mutex_lock(&optimizer->stats_lock);
    double latency = 0.0;
    
    switch (op_type) {
        case KV_IO_OP_READ:
        case KV_IO_OP_PREAD:
            latency = optimizer->stats.avg_read_latency;
            break;
        case KV_IO_OP_WRITE:
        case KV_IO_OP_PWRITE:
            latency = optimizer->stats.avg_write_latency;
            break;
        default:
            break;
    }
    
    pthread_mutex_unlock(&optimizer->stats_lock);
    return latency;
}

// === 缓冲区管理函数 ===
kv_io_buffer_t* kv_io_buffer_create(kv_io_optimizer_t *optimizer, size_t size, bool aligned) {
    if (!optimizer || size == 0) {
        return NULL;
    }
    
    kv_io_buffer_t *buffer = malloc(sizeof(kv_io_buffer_t));
    if (!buffer) {
        return NULL;
    }
    
    memset(buffer, 0, sizeof(kv_io_buffer_t));
    
    // 分配缓冲区内存
    if (aligned) {
        buffer->data = kv_io_aligned_alloc(KV_IO_DIRECT_IO_ALIGNMENT, size);
        buffer->is_aligned = true;
    } else {
        buffer->data = malloc(size);
        buffer->is_aligned = false;
    }
    
    if (!buffer->data) {
        free(buffer);
        return NULL;
    }
    
    buffer->size = size;
    buffer->capacity = size;
    buffer->used = 0;
    buffer->is_dirty = false;
    buffer->is_read_only = false;
    buffer->read_count = 0;
    buffer->write_count = 0;
    buffer->allocator = optimizer->allocator;
    
    return buffer;
}

void kv_io_buffer_destroy(kv_io_buffer_t *buffer) {
    if (!buffer) return;
    
    if (buffer->data) {
        if (buffer->is_aligned) {
            kv_io_aligned_free(buffer->data);
        } else {
            free(buffer->data);
        }
    }
    
    free(buffer);
}

int kv_io_buffer_read(kv_io_buffer_t *buffer, void *data, size_t size, size_t offset) {
    if (!buffer || !data || size == 0 || offset + size > buffer->used) {
        return KV_ERR_PARAM;
    }
    
    memcpy(data, (char*)buffer->data + offset, size);
    buffer->read_count++;
    
    return (int)size;
}

int kv_io_buffer_write(kv_io_buffer_t *buffer, const void *data, size_t size, size_t offset) {
    if (!buffer || !data || size == 0 || buffer->is_read_only) {
        return KV_ERR_PARAM;
    }
    
    // 检查缓冲区容量
    if (offset + size > buffer->capacity) {
        return KV_ERR_OVERFLOW;
    }
    
    memcpy((char*)buffer->data + offset, data, size);
    buffer->write_count++;
    buffer->is_dirty = true;
    
    // 更新使用大小
    if (offset + size > buffer->used) {
        buffer->used = offset + size;
    }
    
    return (int)size;
}

// === 工具函数实现 ===
int kv_io_get_page_size(void) {
    return getpagesize();
}

int kv_io_get_block_size(int fd) {
    struct stat st;
    if (fstat(fd, &st) == -1) {
        return -1;
    }
    return st.st_blksize;
}

bool kv_io_is_ssd(int fd) {
    // 简单检测：通过检查设备队列深度和随机读性能
    // 实际实现可能需要更复杂的逻辑
    char path[256];
    sprintf(path, "/sys/block/sda/queue/rotational");
    
    FILE *fp = fopen(path, "r");
    if (!fp) {
        return false;  // 无法确定，假设为HDD
    }
    
    int rotational;
    int result = fscanf(fp, "%d", &rotational);
    fclose(fp);
    
    if (result == 1) {
        return (rotational == 0);  // 0表示SSD，1表示HDD
    }
    
    return false;
}

void* kv_io_aligned_alloc(size_t alignment, size_t size) {
    void *ptr;
    if (posix_memalign(&ptr, alignment, size) == 0) {
        return ptr;
    }
    return NULL;
}

void kv_io_aligned_free(void *ptr) {
    if (ptr) {
        free(ptr);
    }
}

bool kv_io_is_aligned(const void *ptr, size_t alignment) {
    return ((uintptr_t)ptr % alignment) == 0;
}

// === Request管理函数 ===
void kv_io_request_destroy(kv_io_request_t *request) {
    if (!request) return;
    
    // 简化实现：直接释放request（无aiocb）
    free(request);
}

// === 简化的异步I/O函数结束 === 