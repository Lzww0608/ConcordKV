/*
 * @Author: Lzww0608  
 * @Date: 2025-6-17 10:30:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-17 10:30:00
 * @Description: ConcordKV 存储引擎监控指标管理器实现
 */

#include "kv_engine_metrics.h"
#include "kv_error.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <assert.h>

// 默认配置常量
#define DEFAULT_COLLECTION_INTERVAL_MS  1000
#define DEFAULT_RETENTION_SAMPLES       1000
#define DEFAULT_HTTP_HOST              "0.0.0.0"
#define DEFAULT_HTTP_PORT              9090
#define DEFAULT_HTTP_PATH              "/metrics"
#define DEFAULT_MAX_CONNECTIONS        10
#define DEFAULT_TIMEOUT_SECONDS        30

// 默认延迟桶边界(毫秒)
static const double DEFAULT_LATENCY_BUCKETS[] = {
    0.1, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0
};

// === 前向声明 ===
static void* metrics_collection_thread(void *arg);
static int create_engine_metrics_set(kv_engine_metrics_manager_t *manager, 
                                    kv_engine_type_t type, 
                                    const char *name);
static void destroy_engine_metrics_set(kv_engine_metrics_set_t *metrics_set);
static int create_latency_histogram(concord_metrics_repo_t *repo, 
                                   const char *name, 
                                   const char *description,
                                   const double *buckets,
                                   int bucket_count,
                                   concord_metric_t **metric);

// === 工具函数实现 ===

uint64_t kv_engine_metrics_get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

int kv_engine_metrics_format_metric_name(kv_engine_type_t type, 
                                        const char *metric_name, 
                                        char *buffer, 
                                        size_t size) {
    if (!metric_name || !buffer || size == 0) {
        return -1;
    }
    
    const char *engine_name = "unknown";
    switch (type) {
        case KV_ENGINE_ARRAY: engine_name = "array"; break;
        case KV_ENGINE_HASH: engine_name = "hash"; break;
        case KV_ENGINE_RBTREE: engine_name = "rbtree"; break;
        case KV_ENGINE_BTREE: engine_name = "btree"; break;
        case KV_ENGINE_LSM: engine_name = "lsm"; break;
        default: break;
    }
    
    return snprintf(buffer, size, "concordkv_%s_%s", engine_name, metric_name);
}

// === 配置管理实现 ===

kv_engine_metrics_config_t kv_engine_metrics_config_default(void) {
    kv_engine_metrics_config_t config = {0};
    
    // 基础配置
    config.monitoring_enabled = true;
    config.collection_interval_ms = DEFAULT_COLLECTION_INTERVAL_MS;
    config.retention_samples = DEFAULT_RETENTION_SAMPLES;
    config.enable_latency_histograms = true;
    config.enable_engine_specific = true;
    config.enable_memory_tracking = true;
    config.enable_alerts = false;
    
    // 延迟桶配置
    config.latency_bucket_count = sizeof(DEFAULT_LATENCY_BUCKETS) / sizeof(double);
    memcpy(config.latency_buckets, DEFAULT_LATENCY_BUCKETS, 
           config.latency_bucket_count * sizeof(double));
    
    // 告警阈值
    config.high_latency_threshold_ms = 100.0;
    config.high_memory_threshold_mb = 1024;
    config.error_rate_threshold = 0.01;
    
    // HTTP服务器配置
    config.http_config.enabled = true;
    strncpy(config.http_config.host, DEFAULT_HTTP_HOST, sizeof(config.http_config.host) - 1);
    config.http_config.port = DEFAULT_HTTP_PORT;
    strncpy(config.http_config.path, DEFAULT_HTTP_PATH, sizeof(config.http_config.path) - 1);
    config.http_config.max_connections = DEFAULT_MAX_CONNECTIONS;
    config.http_config.timeout_seconds = DEFAULT_TIMEOUT_SECONDS;
    
    return config;
}

// === 管理器生命周期实现 ===

kv_engine_metrics_manager_t* kv_engine_metrics_manager_create(void) {
    kv_engine_metrics_manager_t *manager = calloc(1, sizeof(kv_engine_metrics_manager_t));
    if (!manager) {
        return NULL;
    }
    
    // 创建metrics仓库
    manager->repo = concord_metrics_repo_create(128);
    if (!manager->repo) {
        free(manager);
        return NULL;
    }
    
    // 初始化读写锁
    if (pthread_rwlock_init(&manager->lock, NULL) != 0) {
        concord_metrics_repo_destroy(manager->repo);
        free(manager);
        return NULL;
    }
    
    // 设置默认配置
    manager->config = kv_engine_metrics_config_default();
    
    // 初始化状态
    manager->running = false;
    manager->collection_thread_running = false;
    manager->total_metrics_count = 0;
    manager->collection_count = 0;
    manager->last_collection_time = 0;
    manager->start_time = time(NULL);
    manager->http_server_fd = -1;
    manager->http_server_running = false;
    
    // 初始化引擎指标数组
    for (int i = 0; i < KV_ENGINE_MAX; i++) {
        manager->engine_metrics[i] = NULL;
    }
    
    return manager;
}

void kv_engine_metrics_manager_destroy(kv_engine_metrics_manager_t *manager) {
    if (!manager) {
        return;
    }
    
    // 停止管理器
    kv_engine_metrics_manager_stop(manager);
    
    // 销毁所有引擎指标集
    pthread_rwlock_wrlock(&manager->lock);
    for (int i = 0; i < KV_ENGINE_MAX; i++) {
        if (manager->engine_metrics[i]) {
            destroy_engine_metrics_set(manager->engine_metrics[i]);
            manager->engine_metrics[i] = NULL;
        }
    }
    pthread_rwlock_unlock(&manager->lock);
    
    // 销毁metrics仓库
    if (manager->repo) {
        concord_metrics_repo_destroy(manager->repo);
    }
    
    // 销毁锁
    pthread_rwlock_destroy(&manager->lock);
    
    free(manager);
}

int kv_engine_metrics_manager_start(kv_engine_metrics_manager_t *manager) {
    if (!manager) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_wrlock(&manager->lock);
    
    if (manager->running) {
        pthread_rwlock_unlock(&manager->lock);
        return KV_ERR_NONE; // 已经运行
    }
    
    // 启动HTTP服务器
    if (manager->config.http_config.enabled) {
        if (kv_engine_metrics_start_http_server(manager) != 0) {
            pthread_rwlock_unlock(&manager->lock);
            return KV_ERR_SYS;
        }
    }
    
    // 启动收集线程
    manager->collection_thread_running = true;
    if (pthread_create(&manager->collection_thread, NULL, 
                      metrics_collection_thread, manager) != 0) {
        manager->collection_thread_running = false;
        kv_engine_metrics_stop_http_server(manager);
        pthread_rwlock_unlock(&manager->lock);
        return KV_ERR_SYS;
    }
    
    manager->running = true;
    pthread_rwlock_unlock(&manager->lock);
    
    return KV_ERR_NONE;
}

int kv_engine_metrics_manager_stop(kv_engine_metrics_manager_t *manager) {
    if (!manager) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_wrlock(&manager->lock);
    
    if (!manager->running) {
        pthread_rwlock_unlock(&manager->lock);
        return KV_ERR_NONE; // 已经停止
    }
    
    // 停止收集线程
    manager->collection_thread_running = false;
    pthread_rwlock_unlock(&manager->lock);
    
    if (manager->collection_thread) {
        pthread_join(manager->collection_thread, NULL);
    }
    
    // 停止HTTP服务器
    kv_engine_metrics_stop_http_server(manager);
    
    pthread_rwlock_wrlock(&manager->lock);
    manager->running = false;
    pthread_rwlock_unlock(&manager->lock);
    
    return KV_ERR_NONE;
}

// === 引擎注册管理实现 ===

int kv_engine_metrics_register_engine(kv_engine_metrics_manager_t *manager, 
                                     kv_engine_type_t type, 
                                     const char *name) {
    if (!manager || type >= KV_ENGINE_MAX || !name) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_wrlock(&manager->lock);
    
    // 检查是否已注册
    if (manager->engine_metrics[type] != NULL) {
        pthread_rwlock_unlock(&manager->lock);
        return KV_ERR_NONE; // 已注册
    }
    
    // 创建引擎指标集
    int ret = create_engine_metrics_set(manager, type, name);
    
    pthread_rwlock_unlock(&manager->lock);
    
    return ret;
}

int kv_engine_metrics_unregister_engine(kv_engine_metrics_manager_t *manager, 
                                       kv_engine_type_t type) {
    if (!manager || type >= KV_ENGINE_MAX) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_wrlock(&manager->lock);
    
    if (manager->engine_metrics[type]) {
        destroy_engine_metrics_set(manager->engine_metrics[type]);
        manager->engine_metrics[type] = NULL;
    }
    
    pthread_rwlock_unlock(&manager->lock);
    
    return KV_ERR_NONE;
}

bool kv_engine_metrics_is_engine_registered(kv_engine_metrics_manager_t *manager, 
                                           kv_engine_type_t type) {
    if (!manager || type >= KV_ENGINE_MAX) {
        return false;
    }
    
    pthread_rwlock_rdlock(&manager->lock);
    bool registered = (manager->engine_metrics[type] != NULL);
    pthread_rwlock_unlock(&manager->lock);
    
    return registered;
}

// === 指标收集实现 ===

int kv_engine_metrics_record_read(kv_engine_metrics_manager_t *manager, 
                                 kv_engine_type_t type, 
                                 double latency_ms) {
    if (!manager || type >= KV_ENGINE_MAX) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_rdlock(&manager->lock);
    
    kv_engine_metrics_set_t *metrics = manager->engine_metrics[type];
    if (!metrics || !metrics->initialized) {
        pthread_rwlock_unlock(&manager->lock);
        return KV_ERR_NOT_FOUND;
    }
    
    // 记录读操作计数
    if (metrics->read_count) {
        concord_metrics_counter_inc(metrics->read_count, 1);
    }
    
    // 记录读延迟
    if (metrics->read_latency && manager->config.enable_latency_histograms) {
        concord_metrics_histogram_observe(metrics->read_latency, latency_ms);
    }
    
    // 记录QPS
    if (metrics->throughput_qps) {
        concord_metrics_meter_mark(metrics->throughput_qps, 1);
    }
    
    pthread_rwlock_unlock(&manager->lock);
    
    return KV_ERR_NONE;
}

int kv_engine_metrics_record_write(kv_engine_metrics_manager_t *manager, 
                                  kv_engine_type_t type, 
                                  double latency_ms) {
    if (!manager || type >= KV_ENGINE_MAX) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_rdlock(&manager->lock);
    
    kv_engine_metrics_set_t *metrics = manager->engine_metrics[type];
    if (!metrics || !metrics->initialized) {
        pthread_rwlock_unlock(&manager->lock);
        return KV_ERR_NOT_FOUND;
    }
    
    // 记录写操作计数
    if (metrics->write_count) {
        concord_metrics_counter_inc(metrics->write_count, 1);
    }
    
    // 记录写延迟
    if (metrics->write_latency && manager->config.enable_latency_histograms) {
        concord_metrics_histogram_observe(metrics->write_latency, latency_ms);
    }
    
    // 记录QPS
    if (metrics->throughput_qps) {
        concord_metrics_meter_mark(metrics->throughput_qps, 1);
    }
    
    pthread_rwlock_unlock(&manager->lock);
    
    return KV_ERR_NONE;
}

int kv_engine_metrics_record_delete(kv_engine_metrics_manager_t *manager, 
                                   kv_engine_type_t type, 
                                   double latency_ms) {
    if (!manager || type >= KV_ENGINE_MAX) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_rdlock(&manager->lock);
    
    kv_engine_metrics_set_t *metrics = manager->engine_metrics[type];
    if (!metrics || !metrics->initialized) {
        pthread_rwlock_unlock(&manager->lock);
        return KV_ERR_NOT_FOUND;
    }
    
    // 记录删除操作计数
    if (metrics->delete_count) {
        concord_metrics_counter_inc(metrics->delete_count, 1);
    }
    
    // 记录删除延迟
    if (metrics->delete_latency && manager->config.enable_latency_histograms) {
        concord_metrics_histogram_observe(metrics->delete_latency, latency_ms);
    }
    
    // 记录QPS
    if (metrics->throughput_qps) {
        concord_metrics_meter_mark(metrics->throughput_qps, 1);
    }
    
    pthread_rwlock_unlock(&manager->lock);
    
    return KV_ERR_NONE;
}

int kv_engine_metrics_update_memory_usage(kv_engine_metrics_manager_t *manager, 
                                         kv_engine_type_t type, 
                                         size_t bytes) {
    if (!manager || type >= KV_ENGINE_MAX) {
        return KV_ERR_PARAM;
    }
    
    if (!manager->config.enable_memory_tracking) {
        return KV_ERR_NONE; // 内存跟踪未启用
    }
    
    pthread_rwlock_rdlock(&manager->lock);
    
    kv_engine_metrics_set_t *metrics = manager->engine_metrics[type];
    if (!metrics || !metrics->initialized) {
        pthread_rwlock_unlock(&manager->lock);
        return KV_ERR_NOT_FOUND;
    }
    
    // 更新内存使用量
    if (metrics->memory_usage) {
        concord_metrics_gauge_set(metrics->memory_usage, (double)bytes);
    }
    
    pthread_rwlock_unlock(&manager->lock);
    
    return KV_ERR_NONE;
}

int kv_engine_metrics_record_error(kv_engine_metrics_manager_t *manager, 
                                  kv_engine_type_t type, 
                                  const char *error_type) {
    if (!manager || type >= KV_ENGINE_MAX || !error_type) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_rdlock(&manager->lock);
    
    kv_engine_metrics_set_t *metrics = manager->engine_metrics[type];
    if (!metrics || !metrics->initialized) {
        pthread_rwlock_unlock(&manager->lock);
        return KV_ERR_NOT_FOUND;
    }
    
    // 记录通用错误计数
    if (metrics->error_count) {
        concord_metrics_counter_inc(metrics->error_count, 1);
    }
    
    // 根据错误类型记录特定计数
    if (strcmp(error_type, "timeout") == 0 && metrics->timeout_count) {
        concord_metrics_counter_inc(metrics->timeout_count, 1);
    } else if (strcmp(error_type, "not_found") == 0 && metrics->not_found_count) {
        concord_metrics_counter_inc(metrics->not_found_count, 1);
    }
    
    pthread_rwlock_unlock(&manager->lock);
    
    return KV_ERR_NONE;
}

// === 引擎特有指标实现 ===

int kv_engine_metrics_record_lsm_compaction(kv_engine_metrics_manager_t *manager) {
    if (!manager) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_rdlock(&manager->lock);
    
    kv_engine_metrics_set_t *metrics = manager->engine_metrics[KV_ENGINE_LSM];
    if (!metrics || !metrics->initialized || !manager->config.enable_engine_specific) {
        pthread_rwlock_unlock(&manager->lock);
        return KV_ERR_NOT_FOUND;
    }
    
    if (metrics->engine_specific.lsm.compaction_count) {
        concord_metrics_counter_inc(metrics->engine_specific.lsm.compaction_count, 1);
    }
    
    pthread_rwlock_unlock(&manager->lock);
    
    return KV_ERR_NONE;
}

int kv_engine_metrics_record_btree_split(kv_engine_metrics_manager_t *manager) {
    if (!manager) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_rdlock(&manager->lock);
    
    kv_engine_metrics_set_t *metrics = manager->engine_metrics[KV_ENGINE_BTREE];
    if (!metrics || !metrics->initialized || !manager->config.enable_engine_specific) {
        pthread_rwlock_unlock(&manager->lock);
        return KV_ERR_NOT_FOUND;
    }
    
    if (metrics->engine_specific.btree.node_splits) {
        concord_metrics_counter_inc(metrics->engine_specific.btree.node_splits, 1);
    }
    
    pthread_rwlock_unlock(&manager->lock);
    
    return KV_ERR_NONE;
}

int kv_engine_metrics_record_hash_collision(kv_engine_metrics_manager_t *manager) {
    if (!manager) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_rdlock(&manager->lock);
    
    kv_engine_metrics_set_t *metrics = manager->engine_metrics[KV_ENGINE_HASH];
    if (!metrics || !metrics->initialized || !manager->config.enable_engine_specific) {
        pthread_rwlock_unlock(&manager->lock);
        return KV_ERR_NOT_FOUND;
    }
    
    if (metrics->engine_specific.hash.bucket_collisions) {
        concord_metrics_counter_inc(metrics->engine_specific.hash.bucket_collisions, 1);
    }
    
    pthread_rwlock_unlock(&manager->lock);
    
    return KV_ERR_NONE;
}

// === 查询导出实现 ===

int kv_engine_metrics_export(kv_engine_metrics_manager_t *manager, 
                            concord_metrics_format_t format, 
                            char *buffer, 
                            size_t size) {
    if (!manager || !buffer || size == 0) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_rdlock(&manager->lock);
    int ret = concord_metrics_dump(manager->repo, format, buffer, size);
    pthread_rwlock_unlock(&manager->lock);
    
    return (ret == 0) ? KV_ERR_NONE : KV_ERR_SYS;
}

// === HTTP服务实现 ===

int kv_engine_metrics_start_http_server(kv_engine_metrics_manager_t *manager) {
    if (!manager) {
        return KV_ERR_PARAM;
    }
    
    // 启动metrics仓库的HTTP服务器
    int ret = concord_metrics_start_server(manager->repo, 
                                          manager->config.http_config.host,
                                          manager->config.http_config.port,
                                          manager->config.http_config.path);
    
    if (ret == 0) {
        manager->http_server_running = true;
    }
    
    return (ret == 0) ? KV_ERR_NONE : KV_ERR_SYS;
}

int kv_engine_metrics_stop_http_server(kv_engine_metrics_manager_t *manager) {
    if (!manager) {
        return KV_ERR_PARAM;
    }
    
    if (manager->http_server_running) {
        int ret = concord_metrics_stop_server(manager->repo);
        manager->http_server_running = false;
        return (ret == 0) ? KV_ERR_NONE : KV_ERR_SYS;
    }
    
    return KV_ERR_NONE;
}

// === 内部辅助函数实现 ===

static void* metrics_collection_thread(void *arg) {
    kv_engine_metrics_manager_t *manager = (kv_engine_metrics_manager_t*)arg;
    
    while (manager->collection_thread_running) {
        // 更新收集统计
        pthread_rwlock_wrlock(&manager->lock);
        manager->collection_count++;
        manager->last_collection_time = kv_engine_metrics_get_time_us();
        pthread_rwlock_unlock(&manager->lock);
        
        // 等待下一次收集
        usleep(manager->config.collection_interval_ms * 1000);
    }
    
    return NULL;
}

static int create_engine_metrics_set(kv_engine_metrics_manager_t *manager, 
                                    kv_engine_type_t type, 
                                    const char *name) {
    kv_engine_metrics_set_t *metrics = calloc(1, sizeof(kv_engine_metrics_set_t));
    if (!metrics) {
        return KV_ERR_MEM;
    }
    
    char metric_name[256];
    // concord_histogram_config_t hist_config = {  // 注释掉未使用的变量
    //     .bucket_count = 10,
    //     .min_value = 0.0,
    //     .max_value = 1000.0
    // };
    
    // 创建基础CRUD指标
    kv_engine_metrics_format_metric_name(type, "read_count", metric_name, sizeof(metric_name));
    metrics->read_count = concord_metrics_create_counter(manager->repo, metric_name, 
                                                        "Total read operations", 0);
    
    kv_engine_metrics_format_metric_name(type, "write_count", metric_name, sizeof(metric_name));
    metrics->write_count = concord_metrics_create_counter(manager->repo, metric_name, 
                                                         "Total write operations", 0);
    
    kv_engine_metrics_format_metric_name(type, "delete_count", metric_name, sizeof(metric_name));
    metrics->delete_count = concord_metrics_create_counter(manager->repo, metric_name, 
                                                          "Total delete operations", 0);
    
    // 创建性能指标
    if (manager->config.enable_latency_histograms) {
        kv_engine_metrics_format_metric_name(type, "read_latency", metric_name, sizeof(metric_name));
        create_latency_histogram(manager->repo, metric_name, "Read latency histogram", 
                               manager->config.latency_buckets, 
                               manager->config.latency_bucket_count,
                               &metrics->read_latency);
        
        kv_engine_metrics_format_metric_name(type, "write_latency", metric_name, sizeof(metric_name));
        create_latency_histogram(manager->repo, metric_name, "Write latency histogram", 
                               manager->config.latency_buckets, 
                               manager->config.latency_bucket_count,
                               &metrics->write_latency);
    }
    
    kv_engine_metrics_format_metric_name(type, "throughput_qps", metric_name, sizeof(metric_name));
    metrics->throughput_qps = concord_metrics_create_meter(manager->repo, metric_name, 
                                                          "Operations per second");
    
    // 创建内存指标
    if (manager->config.enable_memory_tracking) {
        kv_engine_metrics_format_metric_name(type, "memory_usage", metric_name, sizeof(metric_name));
        metrics->memory_usage = concord_metrics_create_gauge(manager->repo, metric_name, 
                                                            "Memory usage in bytes", 0.0);
    }
    
    // 创建错误指标
    kv_engine_metrics_format_metric_name(type, "error_count", metric_name, sizeof(metric_name));
    metrics->error_count = concord_metrics_create_counter(manager->repo, metric_name, 
                                                         "Total error count", 0);
    
    // 设置元数据
    metrics->engine_type = type;
    strncpy(metrics->engine_name, name, sizeof(metrics->engine_name) - 1);
    metrics->initialized = true;
    metrics->creation_time = time(NULL);
    
    manager->engine_metrics[type] = metrics;
    manager->total_metrics_count++;
    
    return KV_ERR_NONE;
}

static void destroy_engine_metrics_set(kv_engine_metrics_set_t *metrics_set) {
    if (metrics_set) {
        // metrics仓库销毁时会自动清理所有指标，这里只需要释放结构体
        free(metrics_set);
    }
}

static int create_latency_histogram(concord_metrics_repo_t *repo, 
                                   const char *name, 
                                   const char *description,
                                   const double *buckets,
                                   int bucket_count,
                                   concord_metric_t **metric) {
    concord_histogram_config_t config = {
        .min = buckets[0],
        .max = buckets[bucket_count - 1],
        .bucket_count = bucket_count
    };
    
    *metric = concord_metrics_create_histogram(repo, name, description, &config);
    return (*metric != NULL) ? 0 : -1;
} 