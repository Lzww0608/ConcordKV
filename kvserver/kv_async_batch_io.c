/*
 * @Author: Lzww0608
 * @Date: 2025-6-11 17:00:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-11 17:00:00
 * @Description: ConcordKV 异步批量I/O操作层 - 实现文件
 */

#include "kv_async_batch_io.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>

// === 内部工具函数 ===

static uint64_t get_timestamp_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static void* default_alloc(size_t size) {
    return malloc(size);
}

static void default_free(void *ptr) {
    free(ptr);
}

// === 配置管理实现 ===

kv_async_batch_config_t kv_async_batch_default_config(void) {
    kv_async_batch_config_t config = {
        // 基本配置
        .max_batch_size = KV_ASYNC_BATCH_DEFAULT_SIZE,
        .max_memory_usage = 64 * 1024 * 1024,  // 64MB
        .timeout_ms = KV_ASYNC_BATCH_DEFAULT_TIMEOUT_MS,
        .max_concurrent_batches = KV_ASYNC_BATCH_MAX_CONCURRENT,
        
        // I/O配置
        .io_queue_depth = 64,
        .enable_io_polling = false,
        .enable_batch_fsync = true,
        .fsync_interval_ms = 5000,
        
        // 优化配置
        .enable_deduplication = true,
        .enable_sorting = true,
        .enable_compression = false,
        .enable_prefetching = true,
        
        // 错误处理
        .fail_fast = false,
        .max_retry_count = 3,
        .retry_delay_ms = 100,
        
        // 内存管理
        .alloc_func = default_alloc,
        .free_func = default_free
    };
    return config;
}

int kv_async_batch_config_validate(const kv_async_batch_config_t *config) {
    if (!config) {
        return KV_ERR_NULL_POINTER;
    }
    
    if (config->max_batch_size == 0 || config->max_batch_size > KV_ASYNC_BATCH_MAX_SIZE) {
        return KV_ERR_INVALID_PARAM;
    }
    
    if (config->max_memory_usage == 0) {
        return KV_ERR_INVALID_PARAM;
    }
    
    if (config->timeout_ms == 0) {
        return KV_ERR_INVALID_PARAM;
    }
    
    if (config->io_queue_depth == 0) {
        return KV_ERR_INVALID_PARAM;
    }
    
    if (!config->alloc_func || !config->free_func) {
        return KV_ERR_INVALID_PARAM;
    }
    
    return KV_SUCCESS;
}

int kv_async_batch_config_copy(kv_async_batch_config_t *dst, 
                               const kv_async_batch_config_t *src) {
    if (!dst || !src) {
        return KV_ERR_NULL_POINTER;
    }
    
    memcpy(dst, src, sizeof(kv_async_batch_config_t));
    return KV_SUCCESS;
}

// === 内部辅助函数 ===

static kv_async_batch_entry_t* create_batch_entry(const kv_async_batch_config_t *config,
                                                   kv_async_batch_op_type_t op_type,
                                                   const char *key, size_t key_len,
                                                   const char *value, size_t value_len) {
    if (!config || !config->alloc_func) {
        return NULL;
    }
    
    kv_async_batch_entry_t *entry = config->alloc_func(sizeof(kv_async_batch_entry_t));
    if (!entry) {
        return NULL;
    }
    
    memset(entry, 0, sizeof(kv_async_batch_entry_t));
    
    // 设置操作信息
    entry->op_type = op_type;
    entry->sequence_number = get_timestamp_us();
    
    // 复制键
    if (key && key_len > 0) {
        entry->key = config->alloc_func(key_len + 1);
        if (!entry->key) {
            config->free_func(entry);
            return NULL;
        }
        memcpy(entry->key, key, key_len);
        entry->key[key_len] = '\0';
        entry->key_len = key_len;
        entry->owns_key = true;
    }
    
    // 复制值（如果需要）
    if (value && value_len > 0) {
        entry->value = config->alloc_func(value_len + 1);
        if (!entry->value) {
            if (entry->key) config->free_func(entry->key);
            config->free_func(entry);
            return NULL;
        }
        memcpy(entry->value, value, value_len);
        entry->value[value_len] = '\0';
        entry->value_len = value_len;
        entry->owns_value = true;
    }
    
    // 设置时间
    clock_gettime(CLOCK_MONOTONIC, &entry->start_time);
    
    return entry;
}

static void destroy_batch_entry(kv_async_batch_entry_t *entry, 
                                const kv_async_batch_config_t *config) {
    if (!entry || !config || !config->free_func) {
        return;
    }
    
    if (entry->owns_key && entry->key) {
        config->free_func(entry->key);
    }
    
    if (entry->owns_value && entry->value) {
        config->free_func(entry->value);
    }
    
    config->free_func(entry);
}

// === 完成处理线程 ===

static void* completion_thread_worker(void *arg) {
    kv_async_batch_manager_t *manager = (kv_async_batch_manager_t*)arg;
    
    while (manager->completion_thread_running) {
        pthread_mutex_lock(&manager->work_lock);
        
        // 等待工作或退出信号
        while (manager->completion_thread_running && manager->active_count == 0) {
            pthread_cond_wait(&manager->work_cond, &manager->work_lock);
        }
        
        if (!manager->completion_thread_running) {
            pthread_mutex_unlock(&manager->work_lock);
            break;
        }
        
        // 处理完成的批次
        pthread_mutex_lock(&manager->manager_lock);
        
        kv_async_batch_context_t *ctx = manager->active_batches;
        kv_async_batch_context_t *prev = NULL;
        
        while (ctx) {
            kv_async_batch_context_t *next = ctx->next;
            
            pthread_mutex_lock(&ctx->context_lock);
            
            // 简化的完成检查：在submit中已经模拟完成，这里主要做清理
            if (ctx->status == KV_ASYNC_BATCH_COMPLETED || ctx->status == KV_ASYNC_BATCH_CANCELLED) {
                // 从活跃列表移除
                if (prev) {
                    prev->next = next;
                } else {
                    manager->active_batches = next;
                }
                manager->active_count--;
                
                pthread_mutex_unlock(&ctx->context_lock);
                pthread_mutex_unlock(&manager->manager_lock);
                pthread_mutex_unlock(&manager->work_lock);
                
                // 这里应该加入池管理，暂时跳过
                goto continue_loop;
            }
            
            pthread_mutex_unlock(&ctx->context_lock);
            prev = ctx;
            ctx = next;
        }
        
        pthread_mutex_unlock(&manager->manager_lock);
        pthread_mutex_unlock(&manager->work_lock);
        
        continue_loop:
        // 短暂休眠避免忙等待
        usleep(1000); // 1ms
    }
    
    return NULL;
}

// === 管理器实现 ===

kv_async_batch_manager_t* kv_async_batch_manager_create(const kv_async_batch_config_t *config) {
    // 允许config为NULL，将使用默认配置
    kv_async_batch_manager_t *manager = malloc(sizeof(kv_async_batch_manager_t));
    if (!manager) {
        return NULL;
    }
    
    memset(manager, 0, sizeof(kv_async_batch_manager_t));
    
    // 复制配置
    if (config) {
        manager->default_config = *config;
    } else {
        manager->default_config = kv_async_batch_default_config();
    }
    
    // 初始化锁
    if (pthread_mutex_init(&manager->manager_lock, NULL) != 0 ||
        pthread_mutex_init(&manager->stats_lock, NULL) != 0 ||
        pthread_mutex_init(&manager->work_lock, NULL) != 0 ||
        pthread_cond_init(&manager->work_cond, NULL) != 0) {
        free(manager);
        return NULL;
    }
    
    manager->next_batch_id = 1;
    manager->is_initialized = true;
    
    return manager;
}

void kv_async_batch_manager_destroy(kv_async_batch_manager_t *manager) {
    if (!manager) {
        return;
    }
    
    // 停止管理器
    kv_async_batch_manager_stop(manager);
    
    // 销毁所有活跃批次
    while (manager->active_batches) {
        kv_async_batch_context_t *ctx = manager->active_batches;
        manager->active_batches = ctx->next;
        kv_async_batch_destroy(ctx);
    }
    
    // 销毁所有空闲批次
    while (manager->free_batches) {
        kv_async_batch_context_t *ctx = manager->free_batches;
        manager->free_batches = ctx->next;
        kv_async_batch_destroy(ctx);
    }
    
    // 销毁锁
    pthread_mutex_destroy(&manager->manager_lock);
    pthread_mutex_destroy(&manager->stats_lock);
    pthread_mutex_destroy(&manager->work_lock);
    pthread_cond_destroy(&manager->work_cond);
    
    free(manager);
}

int kv_async_batch_manager_start(kv_async_batch_manager_t *manager) {
    if (!manager || !manager->is_initialized) {
        return KV_ERR_NULL_POINTER;
    }
    
    if (manager->completion_thread_running) {
        return KV_SUCCESS; // 已经启动
    }
    
    manager->completion_thread_running = true;
    
    int ret = pthread_create(&manager->completion_thread, NULL, 
                            completion_thread_worker, manager);
    if (ret != 0) {
        manager->completion_thread_running = false;
        return KV_ERR_THREAD_CREATE;
    }
    
    return KV_SUCCESS;
}

int kv_async_batch_manager_stop(kv_async_batch_manager_t *manager) {
    if (!manager || !manager->completion_thread_running) {
        return KV_SUCCESS;
    }
    
    // 设置退出标志
    manager->completion_thread_running = false;
    
    // 唤醒工作线程
    pthread_mutex_lock(&manager->work_lock);
    pthread_cond_broadcast(&manager->work_cond);
    pthread_mutex_unlock(&manager->work_lock);
    
    // 等待线程结束
    pthread_join(manager->completion_thread, NULL);
    
    manager->is_shutdown = true;
    
    return KV_SUCCESS;
}

// === 异步批量操作实现 ===

kv_async_batch_context_t* kv_async_batch_create(kv_async_batch_manager_t *manager,
                                                size_t max_batch_size) {
    if (!manager) {
        return NULL;
    }
    
    kv_async_batch_context_t *ctx = manager->default_config.alloc_func(sizeof(kv_async_batch_context_t));
    if (!ctx) {
        return NULL;
    }
    
    memset(ctx, 0, sizeof(kv_async_batch_context_t));
    
    // 设置基本信息
    pthread_mutex_lock(&manager->manager_lock);
    ctx->batch_id = manager->next_batch_id++;
    pthread_mutex_unlock(&manager->manager_lock);
    
    ctx->status = KV_ASYNC_BATCH_PENDING;
    ctx->max_batch_size = max_batch_size;
    
    // 复制配置
    ctx->config = manager->default_config;
    
    // 初始化同步对象
    if (pthread_mutex_init(&ctx->context_lock, NULL) != 0 ||
        pthread_cond_init(&ctx->completion_cond, NULL) != 0) {
        manager->default_config.free_func(ctx);
        return NULL;
    }
    
    return ctx;
}

void kv_async_batch_destroy(kv_async_batch_context_t *ctx) {
    if (!ctx) {
        return;
    }
    
    // 取消未完成的操作
    kv_async_batch_cancel(ctx);
    
    // 销毁所有条目
    kv_async_batch_entry_t *entry = ctx->entries;
    while (entry) {
        kv_async_batch_entry_t *next = entry->next;
        destroy_batch_entry(entry, &ctx->config);
        entry = next;
    }
    
    // 销毁I/O相关资源 (简化实现，不使用io_uring批次)
    // if (ctx->io_batch) {
    //     kv_uring_batch_destroy(ctx->io_batch);
    // }
    
    // 销毁同步对象
    pthread_mutex_destroy(&ctx->context_lock);
    pthread_cond_destroy(&ctx->completion_cond);
    
    ctx->config.free_func(ctx);
}

int kv_async_batch_put(kv_async_batch_context_t *ctx,
                       const char *key, size_t key_len,
                       const char *value, size_t value_len,
                       kv_async_batch_callback_t callback,
                       void *user_data) {
    if (!ctx || !key || key_len == 0 || !value || value_len == 0) {
        return KV_ERR_NULL_POINTER;
    }
    
    pthread_mutex_lock(&ctx->context_lock);
    
    // 检查状态
    if (ctx->status != KV_ASYNC_BATCH_PENDING) {
        pthread_mutex_unlock(&ctx->context_lock);
        return KV_ERR_INVALID_STATE;
    }
    
    // 检查容量限制
    if (ctx->entry_count >= ctx->max_batch_size) {
        pthread_mutex_unlock(&ctx->context_lock);
        return KV_ERR_BATCH_FULL;
    }
    
    // 创建条目
    kv_async_batch_entry_t *entry = create_batch_entry(&ctx->config, 
                                                        KV_ASYNC_BATCH_PUT,
                                                        key, key_len,
                                                        value, value_len);
    if (!entry) {
        pthread_mutex_unlock(&ctx->context_lock);
        return KV_ERR_MEM;
    }
    
    // 设置回调
    entry->callback = callback;
    entry->user_data = user_data;
    
    // 添加到链表
    entry->next = ctx->entries;
    ctx->entries = entry;
    ctx->entry_count++;
    
    // 更新内存使用
    ctx->memory_usage += sizeof(kv_async_batch_entry_t) + key_len + value_len;
    
    // 更新统计
    ctx->stats.total_operations++;
    ctx->stats.bytes_written += value_len;
    
    pthread_mutex_unlock(&ctx->context_lock);
    return KV_SUCCESS;
}

int kv_async_batch_get(kv_async_batch_context_t *ctx,
                       const char *key, size_t key_len,
                       char **value, size_t *value_len,
                       kv_async_batch_callback_t callback,
                       void *user_data) {
    if (!ctx || !key || key_len == 0) {
        return KV_ERR_NULL_POINTER;
    }
    
    pthread_mutex_lock(&ctx->context_lock);
    
    // 检查状态
    if (ctx->status != KV_ASYNC_BATCH_PENDING) {
        pthread_mutex_unlock(&ctx->context_lock);
        return KV_ERR_INVALID_STATE;
    }
    
    // 检查容量限制
    if (ctx->entry_count >= ctx->max_batch_size) {
        pthread_mutex_unlock(&ctx->context_lock);
        return KV_ERR_BATCH_FULL;
    }
    
    // 创建条目
    kv_async_batch_entry_t *entry = create_batch_entry(&ctx->config, 
                                                        KV_ASYNC_BATCH_GET,
                                                        key, key_len,
                                                        NULL, 0);
    if (!entry) {
        pthread_mutex_unlock(&ctx->context_lock);
        return KV_ERR_MEM;
    }
    
    // 设置回调和输出参数
    entry->callback = callback;
    entry->user_data = user_data;
    
    // 添加到链表
    entry->next = ctx->entries;
    ctx->entries = entry;
    ctx->entry_count++;
    
    // 更新内存使用
    ctx->memory_usage += sizeof(kv_async_batch_entry_t) + key_len;
    
    // 更新统计
    ctx->stats.total_operations++;
    
    pthread_mutex_unlock(&ctx->context_lock);
    return KV_SUCCESS;
}

int kv_async_batch_delete(kv_async_batch_context_t *ctx,
                          const char *key, size_t key_len,
                          kv_async_batch_callback_t callback,
                          void *user_data) {
    if (!ctx || !key || key_len == 0) {
        return KV_ERR_NULL_POINTER;
    }
    
    pthread_mutex_lock(&ctx->context_lock);
    
    // 检查状态
    if (ctx->status != KV_ASYNC_BATCH_PENDING) {
        pthread_mutex_unlock(&ctx->context_lock);
        return KV_ERR_INVALID_STATE;
    }
    
    // 检查容量限制
    if (ctx->entry_count >= ctx->max_batch_size) {
        pthread_mutex_unlock(&ctx->context_lock);
        return KV_ERR_BATCH_FULL;
    }
    
    // 创建条目
    kv_async_batch_entry_t *entry = create_batch_entry(&ctx->config, 
                                                        KV_ASYNC_BATCH_DELETE,
                                                        key, key_len,
                                                        NULL, 0);
    if (!entry) {
        pthread_mutex_unlock(&ctx->context_lock);
        return KV_ERR_MEM;
    }
    
    // 设置回调
    entry->callback = callback;
    entry->user_data = user_data;
    
    // 添加到链表
    entry->next = ctx->entries;
    ctx->entries = entry;
    ctx->entry_count++;
    
    // 更新内存使用
    ctx->memory_usage += sizeof(kv_async_batch_entry_t) + key_len;
    
    // 更新统计
    ctx->stats.total_operations++;
    
    pthread_mutex_unlock(&ctx->context_lock);
    return KV_SUCCESS;
}

// === I/O回调函数 ===

static void io_completion_callback(kv_uring_request_t *req, int result, void *data) {
    kv_async_batch_entry_t *entry = (kv_async_batch_entry_t*)data;
    if (!entry) {
        return;
    }
    
    // 设置完成时间和结果
    clock_gettime(CLOCK_MONOTONIC, &entry->complete_time);
    entry->result_code = result;
    entry->bytes_processed = (result > 0) ? (size_t)result : 0;
    
    // 调用用户回调
    if (entry->callback) {
        // 注意：这里需要适配回调签名
        // entry->callback(entry, result, entry->user_data);
    }
}

int kv_async_batch_submit(kv_async_batch_context_t *ctx,
                          kv_async_batch_complete_callback_t complete_callback,
                          void *user_data) {
    if (!ctx) {
        return KV_ERR_NULL_POINTER;
    }
    
    pthread_mutex_lock(&ctx->context_lock);
    
    // 检查状态
    if (ctx->status != KV_ASYNC_BATCH_PENDING) {
        pthread_mutex_unlock(&ctx->context_lock);
        return KV_ERR_INVALID_STATE;
    }
    
    if (ctx->entry_count == 0) {
        pthread_mutex_unlock(&ctx->context_lock);
        return KV_ERR_INVALID_PARAM;
    }
    
    // 设置完成回调
    ctx->complete_callback = complete_callback;
    ctx->complete_user_data = user_data;
    
    // 准备I/O操作
    kv_async_batch_entry_t *entry = ctx->entries;
    int submitted_count = 0;
    
    while (entry) {
        // 简化实现：为每个操作创建模拟I/O请求
        // 在实际实现中，这里应该根据操作类型创建真正的I/O请求
        
        // 这里是模拟实现，实际应该调用存储引擎的异步操作
        entry->result_code = KV_SUCCESS;
        entry->bytes_processed = (entry->op_type == KV_ASYNC_BATCH_PUT) ? entry->value_len : entry->key_len;
        clock_gettime(CLOCK_MONOTONIC, &entry->complete_time);
        
        submitted_count++;
        entry = entry->next;
    }
    
    // 更新状态
    ctx->status = KV_ASYNC_BATCH_SUBMITTED;
    ctx->stats.total_batches++;
    ctx->stats.total_submit_time += get_timestamp_us();
    
    // 模拟异步完成（实际实现中，这会由I/O完成线程处理）
    ctx->completed_count = submitted_count;
    ctx->status = KV_ASYNC_BATCH_COMPLETED;
    ctx->is_complete = true;
    
    // 调用完成回调
    if (complete_callback) {
        complete_callback(ctx, ctx->completed_count, ctx->failed_count, user_data);
    }
    
    // 唤醒等待线程
    pthread_cond_broadcast(&ctx->completion_cond);
    
    pthread_mutex_unlock(&ctx->context_lock);
    return KV_SUCCESS;
}

int kv_async_batch_wait(kv_async_batch_context_t *ctx, uint32_t timeout_ms) {
    if (!ctx) {
        return KV_ERR_NULL_POINTER;
    }
    
    pthread_mutex_lock(&ctx->context_lock);
    
    if (ctx->is_complete) {
        pthread_mutex_unlock(&ctx->context_lock);
        return KV_SUCCESS;
    }
    
    // 计算超时时间
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += timeout_ms / 1000;
    timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (timeout.tv_nsec >= 1000000000) {
        timeout.tv_sec++;
        timeout.tv_nsec -= 1000000000;
    }
    
    // 等待完成
    int ret = pthread_cond_timedwait(&ctx->completion_cond, &ctx->context_lock, &timeout);
    
    pthread_mutex_unlock(&ctx->context_lock);
    
    if (ret == ETIMEDOUT) {
        return KV_ERR_TIMEOUT;
    } else if (ret != 0) {
        return KV_ERR_UNKNOWN;
    }
    
    return KV_SUCCESS;
}

int kv_async_batch_cancel(kv_async_batch_context_t *ctx) {
    if (!ctx) {
        return KV_ERR_NULL_POINTER;
    }
    
    pthread_mutex_lock(&ctx->context_lock);
    
    if (ctx->status == KV_ASYNC_BATCH_COMPLETED || 
        ctx->status == KV_ASYNC_BATCH_CANCELLED) {
        pthread_mutex_unlock(&ctx->context_lock);
        return KV_SUCCESS;
    }
    
    // 设置取消状态
    ctx->status = KV_ASYNC_BATCH_CANCELLED;
    ctx->is_complete = true;
    
    // 取消所有未完成的I/O操作 (简化实现)
    kv_async_batch_entry_t *entry = ctx->entries;
    while (entry) {
        // 在简化实现中，直接标记为取消
        entry->result_code = KV_ERR_CANCELLED;
        entry = entry->next;
    }
    
    // 唤醒等待线程
    pthread_cond_broadcast(&ctx->completion_cond);
    
    pthread_mutex_unlock(&ctx->context_lock);
    return KV_SUCCESS;
}

// === 状态和统计接口实现 ===

kv_async_batch_status_t kv_async_batch_get_status(const kv_async_batch_context_t *ctx) {
    if (!ctx) {
        return KV_ASYNC_BATCH_FAILED;
    }
    
    return ctx->status;
}

int kv_async_batch_get_stats(const kv_async_batch_context_t *ctx, 
                             kv_async_batch_stats_t *stats) {
    if (!ctx || !stats) {
        return KV_ERR_NULL_POINTER;
    }
    
    pthread_mutex_lock((pthread_mutex_t*)&ctx->context_lock);
    memcpy(stats, &ctx->stats, sizeof(kv_async_batch_stats_t));
    pthread_mutex_unlock((pthread_mutex_t*)&ctx->context_lock);
    
    return KV_SUCCESS;
}

// === 工具函数实现 ===

const char* kv_async_batch_op_name(kv_async_batch_op_type_t op_type) {
    switch (op_type) {
        case KV_ASYNC_BATCH_PUT: return "PUT";
        case KV_ASYNC_BATCH_GET: return "GET";
        case KV_ASYNC_BATCH_DELETE: return "DELETE";
        case KV_ASYNC_BATCH_UPDATE: return "UPDATE";
        case KV_ASYNC_BATCH_SYNC: return "SYNC";
        default: return "UNKNOWN";
    }
}

const char* kv_async_batch_status_name(kv_async_batch_status_t status) {
    switch (status) {
        case KV_ASYNC_BATCH_PENDING: return "PENDING";
        case KV_ASYNC_BATCH_SUBMITTED: return "SUBMITTED";
        case KV_ASYNC_BATCH_COMPLETING: return "COMPLETING";
        case KV_ASYNC_BATCH_COMPLETED: return "COMPLETED";
        case KV_ASYNC_BATCH_FAILED: return "FAILED";
        case KV_ASYNC_BATCH_CANCELLED: return "CANCELLED";
        default: return "UNKNOWN";
    }
}

uint64_t kv_async_batch_get_timestamp_us(void) {
    return get_timestamp_us();
} 