/*
 * @Author: Lzww0608
 * @Date: 2025-6-11 16:42:46
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-11 16:42:48
 * @Description: ConcordKV io_uring 真正异步I/O层 - 实现文件
 */

#include "kv_io_uring.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/eventfd.h>
#include <signal.h>

// === 内部工具函数 ===
static uint64_t get_timestamp_us(void);
static double timespec_to_seconds(const struct timespec *ts);
static void update_stats_operation(kv_uring_stats_t *stats, kv_uring_op_type_t op_type, 
                                  size_t bytes, uint64_t latency_us);
static kv_uring_request_t* create_request(kv_uring_t *uring);
static void free_request(kv_uring_t *uring, kv_uring_request_t *request);
static int submit_request(kv_uring_t *uring, kv_uring_request_t *request);
static void* completion_thread_func(void *arg);

// === 工具函数实现 ===
static uint64_t get_timestamp_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

uint64_t kv_uring_get_timestamp_us(void) {
    return get_timestamp_us();
}

static double timespec_to_seconds(const struct timespec *ts) {
    return (double)ts->tv_sec + (double)ts->tv_nsec / 1000000000.0;
}

static void update_stats_operation(kv_uring_stats_t *stats, kv_uring_op_type_t op_type, 
                                  size_t bytes, uint64_t latency_us) {
    stats->total_operations++;
    
    switch (op_type) {
        case KV_URING_OP_READ:
            stats->read_count++;
            stats->bytes_read += bytes;
            break;
        case KV_URING_OP_WRITE:
            stats->write_count++;
            stats->bytes_written += bytes;
            break;
        case KV_URING_OP_READV:
            stats->readv_count++;
            stats->bytes_read += bytes;
            break;
        case KV_URING_OP_WRITEV:
            stats->writev_count++;
            stats->bytes_written += bytes;
            break;
        case KV_URING_OP_FSYNC:
        case KV_URING_OP_FDATASYNC:
            stats->sync_count++;
            break;
        default:
            break;
    }
    
    // 更新延迟统计
    stats->total_complete_time += latency_us;
    if (stats->total_operations > 0) {
        stats->avg_complete_latency = (double)stats->total_complete_time / stats->total_operations;
    }
}

// === 配置管理函数实现 ===
kv_uring_config_t* kv_uring_config_create(void) {
    kv_uring_config_t *config = malloc(sizeof(kv_uring_config_t));
    if (!config) {
        return NULL;
    }
    
    // 设置默认值
    memset(config, 0, sizeof(kv_uring_config_t));
    config->queue_depth = KV_URING_DEFAULT_QUEUE_DEPTH;
    config->sq_poll_idle = 1000; // 1ms
    config->use_sq_poll = false;
    config->use_iopoll = false;
    config->use_kernel_sqe_thread = false;
    config->cq_entries_multiplier = 2;
    config->worker_threads = 1;
    config->timeout_ms = KV_URING_TIMEOUT_MS;
    config->buffer_size = KV_URING_DEFAULT_BUFFER_SIZE;
    config->allocator = NULL;
    
    return config;
}

void kv_uring_config_destroy(kv_uring_config_t *config) {
    if (config) {
        free(config);
    }
}

int kv_uring_config_set_queue_depth(kv_uring_config_t *config, uint32_t depth) {
    if (!config) {
        return KV_ERR_PARAM;
    }
    
    if (depth < KV_URING_MIN_QUEUE_DEPTH || depth > KV_URING_MAX_QUEUE_DEPTH) {
        return KV_ERR_PARAM;
    }
    
    config->queue_depth = depth;
    return KV_SUCCESS;
}

int kv_uring_config_set_sq_poll(kv_uring_config_t *config, bool enable, uint32_t idle_ms) {
    if (!config) {
        return KV_ERR_PARAM;
    }
    
    config->use_sq_poll = enable;
    if (enable) {
        config->sq_poll_idle = idle_ms;
    }
    
    return KV_SUCCESS;
}

int kv_uring_config_set_iopoll(kv_uring_config_t *config, bool enable) {
    if (!config) {
        return KV_ERR_PARAM;
    }
    
    config->use_iopoll = enable;
    return KV_SUCCESS;
}

int kv_uring_config_set_buffer_size(kv_uring_config_t *config, size_t size) {
    if (!config || size == 0 || size > KV_URING_MAX_BUFFER_SIZE) {
        return KV_ERR_PARAM;
    }
    
    config->buffer_size = size;
    return KV_SUCCESS;
}

int kv_uring_config_validate(const kv_uring_config_t *config) {
    if (!config) {
        return KV_ERR_PARAM;
    }
    
    if (config->queue_depth < KV_URING_MIN_QUEUE_DEPTH || 
        config->queue_depth > KV_URING_MAX_QUEUE_DEPTH) {
        return KV_ERR_PARAM;
    }
    
    if (config->buffer_size == 0 || config->buffer_size > KV_URING_MAX_BUFFER_SIZE) {
        return KV_ERR_PARAM;
    }
    
    if (config->timeout_ms == 0) {
        return KV_ERR_PARAM;
    }
    
    return KV_SUCCESS;
}

// === 请求管理函数实现 ===
static kv_uring_request_t* create_request(kv_uring_t *uring) {
    kv_uring_request_t *request = NULL;
    
    pthread_mutex_lock(&uring->request_lock);
    
    // 尝试从空闲池中获取
    if (uring->free_requests) {
        request = uring->free_requests;
        uring->free_requests = request->next;
        uring->free_count--;
    }
    
    pthread_mutex_unlock(&uring->request_lock);
    
    // 如果空闲池为空，分配新的请求
    if (!request) {
        request = malloc(sizeof(kv_uring_request_t));
        if (!request) {
            return NULL;
        }
    }
    
    // 初始化请求
    memset(request, 0, sizeof(kv_uring_request_t));
    request->request_id = __sync_fetch_and_add(&uring->next_request_id, 1);
    request->status = KV_URING_REQ_PENDING;
    clock_gettime(CLOCK_MONOTONIC, &request->submit_time);
    
    return request;
}

static void free_request(kv_uring_t *uring, kv_uring_request_t *request) {
    if (!request) {
        return;
    }
    
    // 清理资源
    if (request->owns_buffer && request->buffer) {
        free(request->buffer);
    }
    
    if (request->iovecs) {
        free(request->iovecs);
    }
    
    pthread_mutex_lock(&uring->request_lock);
    
    // 放回空闲池
    request->next = uring->free_requests;
    uring->free_requests = request;
    uring->free_count++;
    
    pthread_mutex_unlock(&uring->request_lock);
}

// === 生命周期管理函数实现 ===
kv_uring_t* kv_uring_create(const kv_uring_config_t *config) {
    if (!config || kv_uring_config_validate(config) != KV_SUCCESS) {
        return NULL;
    }
    
    kv_uring_t *uring = malloc(sizeof(kv_uring_t));
    if (!uring) {
        return NULL;
    }
    
    memset(uring, 0, sizeof(kv_uring_t));
    
    // 复制配置
    uring->config = *config;
    
    // 初始化互斥锁
    if (pthread_mutex_init(&uring->ring_lock, NULL) != 0 ||
        pthread_mutex_init(&uring->request_lock, NULL) != 0 ||
        pthread_mutex_init(&uring->stats_lock, NULL) != 0 ||
        pthread_mutex_init(&uring->batch_lock, NULL) != 0 ||
        pthread_mutex_init(&uring->completion_lock, NULL) != 0) {
        free(uring);
        return NULL;
    }
    
    // 初始化条件变量
    if (pthread_cond_init(&uring->completion_cond, NULL) != 0) {
        pthread_mutex_destroy(&uring->ring_lock);
        pthread_mutex_destroy(&uring->request_lock);
        pthread_mutex_destroy(&uring->stats_lock);
        pthread_mutex_destroy(&uring->batch_lock);
        pthread_mutex_destroy(&uring->completion_lock);
        free(uring);
        return NULL;
    }
    
    // 初始化io_uring
    struct io_uring_params params;
    memset(&params, 0, sizeof(params));
    
    if (config->use_sq_poll) {
        params.flags |= IORING_SETUP_SQPOLL;
        params.sq_thread_idle = config->sq_poll_idle;
    }
    
    if (config->use_iopoll) {
        params.flags |= IORING_SETUP_IOPOLL;
    }
    
    if (config->cq_entries_multiplier > 1) {
        params.cq_entries = config->queue_depth * config->cq_entries_multiplier;
        params.flags |= IORING_SETUP_CQSIZE;
    }
    
    int ret = io_uring_queue_init_params(config->queue_depth, &uring->ring, &params);
    if (ret < 0) {
        pthread_mutex_destroy(&uring->ring_lock);
        pthread_mutex_destroy(&uring->request_lock);
        pthread_mutex_destroy(&uring->stats_lock);
        pthread_mutex_destroy(&uring->batch_lock);
        pthread_mutex_destroy(&uring->completion_lock);
        pthread_cond_destroy(&uring->completion_cond);
        free(uring);
        return NULL;
    }
    
    uring->is_initialized = true;
    uring->next_request_id = 1;
    
    return uring;
}

void kv_uring_destroy(kv_uring_t *uring) {
    if (!uring) {
        return;
    }
    
    // 停止完成处理线程
    if (uring->completion_thread_running) {
        kv_uring_stop(uring);
    }
    
    // 清理io_uring
    if (uring->is_initialized) {
        io_uring_queue_exit(&uring->ring);
    }
    
    // 清理空闲请求池
    while (uring->free_requests) {
        kv_uring_request_t *next = uring->free_requests->next;
        free(uring->free_requests);
        uring->free_requests = next;
    }
    
    // 销毁锁和条件变量
    pthread_mutex_destroy(&uring->ring_lock);
    pthread_mutex_destroy(&uring->request_lock);
    pthread_mutex_destroy(&uring->stats_lock);
    pthread_mutex_destroy(&uring->batch_lock);
    pthread_mutex_destroy(&uring->completion_lock);
    pthread_cond_destroy(&uring->completion_cond);
    
    free(uring);
}

static void* completion_thread_func(void *arg) {
    kv_uring_t *uring = (kv_uring_t*)arg;
    struct io_uring_cqe *cqe;
    
    while (uring->completion_thread_running) {
        // 等待完成事件
        int ret = io_uring_wait_cqe(&uring->ring, &cqe);
        if (ret < 0) {
            if (ret == -EINTR) {
                continue;
            }
            break;
        }
        
        // 处理完成事件
        kv_uring_request_t *request = (kv_uring_request_t*)io_uring_cqe_get_data(cqe);
        if (request) {
            clock_gettime(CLOCK_MONOTONIC, &request->complete_time);
            request->result = cqe->res;
            request->status = (cqe->res < 0) ? KV_URING_REQ_FAILED : KV_URING_REQ_COMPLETED;
            
            // 更新统计信息
            pthread_mutex_lock(&uring->stats_lock);
            struct timespec diff;
            diff.tv_sec = request->complete_time.tv_sec - request->submit_time.tv_sec;
            diff.tv_nsec = request->complete_time.tv_nsec - request->submit_time.tv_nsec;
            if (diff.tv_nsec < 0) {
                diff.tv_sec--;
                diff.tv_nsec += 1000000000;
            }
            uint64_t latency_us = (uint64_t)diff.tv_sec * 1000000ULL + (uint64_t)diff.tv_nsec / 1000ULL;
            
            size_t bytes = (request->result > 0) ? (size_t)request->result : 0;
            update_stats_operation(&uring->stats, request->op_type, bytes, latency_us);
            uring->stats.completions_total++;
            pthread_mutex_unlock(&uring->stats_lock);
            
            // 调用回调函数
            if (request->callback) {
                request->callback(request, request->result, request->user_data);
            }
            
            // 释放请求
            free_request(uring, request);
        }
        
        io_uring_cqe_seen(&uring->ring, cqe);
    }
    
    return NULL;
}

int kv_uring_start(kv_uring_t *uring) {
    if (!uring || !uring->is_initialized) {
        return KV_ERR_PARAM;
    }
    
    if (uring->completion_thread_running) {
        return KV_SUCCESS;
    }
    
    uring->completion_thread_running = true;
    
    int ret = pthread_create(&uring->completion_thread, NULL, completion_thread_func, uring);
    if (ret != 0) {
        uring->completion_thread_running = false;
        return KV_ERR_MEM;
    }
    
    return KV_SUCCESS;
}

int kv_uring_stop(kv_uring_t *uring) {
    if (!uring || !uring->completion_thread_running) {
        return KV_ERR_PARAM;
    }
    
    uring->completion_thread_running = false;
    
    // 提交一个NOP操作来唤醒完成线程
    struct io_uring_sqe *sqe = io_uring_get_sqe(&uring->ring);
    if (sqe) {
        io_uring_prep_nop(sqe);
        io_uring_submit(&uring->ring);
    }
    
    pthread_join(uring->completion_thread, NULL);
    
    return KV_SUCCESS;
}

// === 提交请求函数实现 ===
static int submit_request(kv_uring_t *uring, kv_uring_request_t *request) {
    pthread_mutex_lock(&uring->ring_lock);
    
    struct io_uring_sqe *sqe = io_uring_get_sqe(&uring->ring);
    if (!sqe) {
        pthread_mutex_unlock(&uring->ring_lock);
        pthread_mutex_lock(&uring->stats_lock);
        uring->stats.queue_full_count++;
        pthread_mutex_unlock(&uring->stats_lock);
        return KV_ERR_OVERFLOW;
    }
    
    // 设置SQE
    request->sqe = sqe;
    io_uring_sqe_set_data(sqe, request);
    
    switch (request->op_type) {
        case KV_URING_OP_READ:
            io_uring_prep_read(sqe, request->fd, request->buffer, request->size, request->offset);
            break;
        case KV_URING_OP_WRITE:
            io_uring_prep_write(sqe, request->fd, request->buffer, request->size, request->offset);
            break;
        case KV_URING_OP_READV:
            io_uring_prep_readv(sqe, request->fd, request->iovecs, request->iovcnt, request->offset);
            break;
        case KV_URING_OP_WRITEV:
            io_uring_prep_writev(sqe, request->fd, request->iovecs, request->iovcnt, request->offset);
            break;
        case KV_URING_OP_FSYNC:
            io_uring_prep_fsync(sqe, request->fd, 0);
            break;
        case KV_URING_OP_FDATASYNC:
            io_uring_prep_fsync(sqe, request->fd, IORING_FSYNC_DATASYNC);
            break;
        default:
            pthread_mutex_unlock(&uring->ring_lock);
            return KV_ERR_PARAM;
    }
    
    request->status = KV_URING_REQ_SUBMITTED;
    
    // 提交请求
    int ret = io_uring_submit(&uring->ring);
    
    pthread_mutex_unlock(&uring->ring_lock);
    
    if (ret >= 0) {
        pthread_mutex_lock(&uring->stats_lock);
        uring->stats.submissions_total++;
        pthread_mutex_unlock(&uring->stats_lock);
        return KV_SUCCESS;
    }
    
    return KV_ERR_IO;
}

// === 基础异步I/O操作实现 ===
kv_uring_request_t* kv_uring_read_async(kv_uring_t *uring, int fd, void *buffer, 
                                        size_t size, off_t offset,
                                        kv_uring_callback_t callback, void *user_data) {
    if (!uring || fd < 0 || !buffer || size == 0) {
        return NULL;
    }
    
    kv_uring_request_t *request = create_request(uring);
    if (!request) {
        return NULL;
    }
    
    request->op_type = KV_URING_OP_READ;
    request->fd = fd;
    request->buffer = buffer;
    request->size = size;
    request->offset = offset;
    request->callback = callback;
    request->user_data = user_data;
    request->owns_buffer = false;
    
    if (submit_request(uring, request) != KV_SUCCESS) {
        free_request(uring, request);
        return NULL;
    }
    
    return request;
}

kv_uring_request_t* kv_uring_write_async(kv_uring_t *uring, int fd, const void *buffer, 
                                         size_t size, off_t offset,
                                         kv_uring_callback_t callback, void *user_data) {
    if (!uring || fd < 0 || !buffer || size == 0) {
        return NULL;
    }
    
    kv_uring_request_t *request = create_request(uring);
    if (!request) {
        return NULL;
    }
    
    request->op_type = KV_URING_OP_WRITE;
    request->fd = fd;
    request->buffer = (void*)buffer;
    request->size = size;
    request->offset = offset;
    request->callback = callback;
    request->user_data = user_data;
    request->owns_buffer = false;
    
    if (submit_request(uring, request) != KV_SUCCESS) {
        free_request(uring, request);
        return NULL;
    }
    
    return request;
}

kv_uring_request_t* kv_uring_readv_async(kv_uring_t *uring, int fd, struct iovec *iovecs, 
                                         int iovcnt, off_t offset,
                                         kv_uring_callback_t callback, void *user_data) {
    if (!uring || fd < 0 || !iovecs || iovcnt <= 0) {
        return NULL;
    }
    
    kv_uring_request_t *request = create_request(uring);
    if (!request) {
        return NULL;
    }
    
    request->op_type = KV_URING_OP_READV;
    request->fd = fd;
    request->iovecs = iovecs;
    request->iovcnt = iovcnt;
    request->offset = offset;
    request->callback = callback;
    request->user_data = user_data;
    
    if (submit_request(uring, request) != KV_SUCCESS) {
        free_request(uring, request);
        return NULL;
    }
    
    return request;
}

kv_uring_request_t* kv_uring_writev_async(kv_uring_t *uring, int fd, const struct iovec *iovecs, 
                                          int iovcnt, off_t offset,
                                          kv_uring_callback_t callback, void *user_data) {
    if (!uring || fd < 0 || !iovecs || iovcnt <= 0) {
        return NULL;
    }
    
    kv_uring_request_t *request = create_request(uring);
    if (!request) {
        return NULL;
    }
    
    request->op_type = KV_URING_OP_WRITEV;
    request->fd = fd;
    request->iovecs = (struct iovec*)iovecs;
    request->iovcnt = iovcnt;
    request->offset = offset;
    request->callback = callback;
    request->user_data = user_data;
    
    if (submit_request(uring, request) != KV_SUCCESS) {
        free_request(uring, request);
        return NULL;
    }
    
    return request;
}

kv_uring_request_t* kv_uring_fsync_async(kv_uring_t *uring, int fd,
                                         kv_uring_callback_t callback, void *user_data) {
    if (!uring || fd < 0) {
        return NULL;
    }
    
    kv_uring_request_t *request = create_request(uring);
    if (!request) {
        return NULL;
    }
    
    request->op_type = KV_URING_OP_FSYNC;
    request->fd = fd;
    request->callback = callback;
    request->user_data = user_data;
    
    if (submit_request(uring, request) != KV_SUCCESS) {
        free_request(uring, request);
        return NULL;
    }
    
    return request;
}

// === 统计信息函数实现 ===
int kv_uring_get_stats(kv_uring_t *uring, kv_uring_stats_t *stats) {
    if (!uring || !stats) {
        return KV_ERR_PARAM;
    }
    
    pthread_mutex_lock(&uring->stats_lock);
    *stats = uring->stats;
    pthread_mutex_unlock(&uring->stats_lock);
    
    return KV_SUCCESS;
}

int kv_uring_reset_stats(kv_uring_t *uring) {
    if (!uring) {
        return KV_ERR_PARAM;
    }
    
    pthread_mutex_lock(&uring->stats_lock);
    memset(&uring->stats, 0, sizeof(kv_uring_stats_t));
    pthread_mutex_unlock(&uring->stats_lock);
    
    return KV_SUCCESS;
}

// === 工具函数实现 ===
bool kv_uring_is_supported(void) {
    struct io_uring ring;
    int ret = io_uring_queue_init(1, &ring, 0);
    if (ret < 0) {
        return false;
    }
    
    io_uring_queue_exit(&ring);
    return true;
}

const char* kv_uring_op_name(kv_uring_op_type_t op_type) {
    switch (op_type) {
        case KV_URING_OP_READ: return "READ";
        case KV_URING_OP_WRITE: return "WRITE";
        case KV_URING_OP_READV: return "READV";
        case KV_URING_OP_WRITEV: return "WRITEV";
        case KV_URING_OP_FSYNC: return "FSYNC";
        case KV_URING_OP_FDATASYNC: return "FDATASYNC";
        case KV_URING_OP_OPENAT: return "OPENAT";
        case KV_URING_OP_CLOSE: return "CLOSE";
        case KV_URING_OP_FALLOCATE: return "FALLOCATE";
        default: return "UNKNOWN";
    }
}

const char* kv_uring_status_name(kv_uring_req_status_t status) {
    switch (status) {
        case KV_URING_REQ_PENDING: return "PENDING";
        case KV_URING_REQ_SUBMITTED: return "SUBMITTED";
        case KV_URING_REQ_COMPLETED: return "COMPLETED";
        case KV_URING_REQ_FAILED: return "FAILED";
        case KV_URING_REQ_CANCELLED: return "CANCELLED";
        default: return "UNKNOWN";
    }
}

kv_uring_req_status_t kv_uring_request_status(const kv_uring_request_t *request) {
    if (!request) {
        return KV_URING_REQ_FAILED;
    }
    return request->status;
}

int kv_uring_request_result(const kv_uring_request_t *request) {
    if (!request) {
        return -EINVAL;
    }
    return request->result;
}

void kv_uring_request_destroy(kv_uring_request_t *request) {
    // 注意：这个函数不应该被直接调用，请求会在完成处理线程中自动释放
    // 这里只是为了API完整性而保留
    if (request) {
        // 标记请求已取消，防止回调被调用
        request->status = KV_URING_REQ_CANCELLED;
    }
}

// === 同步等待函数实现 ===
int kv_uring_wait_request(kv_uring_t *uring, kv_uring_request_t *request, uint32_t timeout_ms) {
    if (!uring || !request) {
        return KV_ERR_PARAM;
    }
    
    struct io_uring_cqe *cqe;
    struct __kernel_timespec ts;
    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = (timeout_ms % 1000) * 1000000;
    
    int ret = io_uring_wait_cqe_timeout(&uring->ring, &cqe, &ts);
    if (ret < 0) {
        if (ret == -ETIME) {
            return KV_ERR_TIMEOUT;
        }
        return KV_ERR_IO;
    }
    
    kv_uring_request_t *completed_req = (kv_uring_request_t*)io_uring_cqe_get_data(cqe);
    if (completed_req == request) {
        clock_gettime(CLOCK_MONOTONIC, &request->complete_time);
        request->result = cqe->res;
        request->status = (cqe->res < 0) ? KV_URING_REQ_FAILED : KV_URING_REQ_COMPLETED;
        
        // 调用回调函数
        if (request->callback) {
            request->callback(request, request->result, request->user_data);
        }
    }
    
    io_uring_cqe_seen(&uring->ring, cqe);
    
    return (request->status == KV_URING_REQ_COMPLETED) ? KV_SUCCESS : KV_ERR_IO;
} 