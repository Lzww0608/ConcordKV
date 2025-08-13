/*
 * @Author: Lzww0608  
 * @Date: 2025-6-16 16:45:13
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-16 16:52:03
 * @Description: ConcordKV 存储引擎配置系统实现  
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "kv_engine_config.h"
#include "kv_memory.h"
#include "kv_error.h"
#include "../common/config/parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <assert.h>

// 全局引擎管理器实例
static kv_engine_manager_t *g_engine_manager = NULL;

// === 字符串映射表 ===
static const char* engine_type_strings[] = {
    [KV_ENGINE_ARRAY]  = "array",
    [KV_ENGINE_RBTREE] = "rbtree", 
    [KV_ENGINE_HASH]   = "hash",
    [KV_ENGINE_BTREE]  = "btree",
    [KV_ENGINE_LSM]    = "lsm"
};

static const char* strategy_strings[] = {
    [KV_ENGINE_STRATEGY_FIXED]        = "fixed",
    [KV_ENGINE_STRATEGY_ADAPTIVE]     = "adaptive", 
    [KV_ENGINE_STRATEGY_LOAD_BALANCED] = "load_balanced"
};

// === 内部辅助函数 ===

/**
 * 创建默认引擎配置
 */
static kv_engine_config_t* create_default_engine_config(kv_engine_type_t type) {
    kv_engine_config_t *config = kv_store_malloc(sizeof(kv_engine_config_t));
    if (!config) return NULL;
    
    memset(config, 0, sizeof(kv_engine_config_t));
    config->type = type;
    config->memory_limit = KV_ENGINE_DEFAULT_MEMORY_LIMIT / 5;  // 平均分配
    config->cache_size = KV_ENGINE_DEFAULT_CACHE_SIZE / 5;
    config->enable_compression = false;
    config->enable_checksum = false;
    config->data_dir = strdup(KV_ENGINE_DEFAULT_DATA_DIR);
    
    // 引擎特定默认配置
    switch (type) {
        case KV_ENGINE_ARRAY:
            // Array 引擎配置
            break;
        case KV_ENGINE_RBTREE:
            // RBTree 引擎配置
            break;
        case KV_ENGINE_HASH:
            config->initial_buckets = 1024;
            config->load_factor = 0.75;
            break;
        case KV_ENGINE_BTREE:
            config->page_size = 4096;
            config->max_keys_per_node = 128;
            break;
        case KV_ENGINE_LSM:
            config->memtable_size = 64 * 1024 * 1024;  // 64MB
            config->level0_file_limit = 4;
            config->level_size_multiplier = 10.0;
            break;
        default:
            break;
    }
    
    return config;
}

/**
 * 从配置对象加载引擎特定配置
 */
static int load_engine_config_from_config(concord_config_t *cfg, const char *prefix, 
                                         kv_engine_config_t *engine_config) {
    if (!cfg || !prefix || !engine_config) return -1;
    
    char key[256];
    
    // 通用配置
    snprintf(key, sizeof(key), "%s.memory_limit", prefix);
    engine_config->memory_limit = concord_config_get_int(cfg, key, engine_config->memory_limit);
    
    snprintf(key, sizeof(key), "%s.cache_size", prefix);
    engine_config->cache_size = concord_config_get_int(cfg, key, engine_config->cache_size);
    
    snprintf(key, sizeof(key), "%s.enable_compression", prefix);
    engine_config->enable_compression = concord_config_get_bool(cfg, key, engine_config->enable_compression);
    
    snprintf(key, sizeof(key), "%s.enable_checksum", prefix);
    engine_config->enable_checksum = concord_config_get_bool(cfg, key, engine_config->enable_checksum);
    
    snprintf(key, sizeof(key), "%s.data_dir", prefix);
    const char *data_dir = concord_config_get_string(cfg, key, engine_config->data_dir);
    if (data_dir && strcmp(data_dir, engine_config->data_dir) != 0) {
        free(engine_config->data_dir);
        engine_config->data_dir = strdup(data_dir);
    }
    
    // 引擎特定配置
    switch (engine_config->type) {
        case KV_ENGINE_HASH:
            snprintf(key, sizeof(key), "%s.initial_buckets", prefix);
            engine_config->initial_buckets = concord_config_get_int(cfg, key, engine_config->initial_buckets);
            
            snprintf(key, sizeof(key), "%s.load_factor", prefix);
            engine_config->load_factor = concord_config_get_float(cfg, key, engine_config->load_factor);
            break;
            
        case KV_ENGINE_BTREE:
            snprintf(key, sizeof(key), "%s.page_size", prefix);
            engine_config->page_size = concord_config_get_int(cfg, key, engine_config->page_size);
            
            snprintf(key, sizeof(key), "%s.max_keys_per_node", prefix);
            engine_config->max_keys_per_node = concord_config_get_int(cfg, key, engine_config->max_keys_per_node);
            break;
            
        case KV_ENGINE_LSM:
            snprintf(key, sizeof(key), "%s.memtable_size", prefix);
            engine_config->memtable_size = concord_config_get_int(cfg, key, engine_config->memtable_size);
            
            snprintf(key, sizeof(key), "%s.level0_file_limit", prefix);
            engine_config->level0_file_limit = concord_config_get_int(cfg, key, engine_config->level0_file_limit);
            
            snprintf(key, sizeof(key), "%s.level_size_multiplier", prefix);
            engine_config->level_size_multiplier = concord_config_get_float(cfg, key, engine_config->level_size_multiplier);
            break;
            
        default:
            break;
    }
    
    return 0;
}

// === 公共API实现 ===

kv_engine_global_config_t* kv_engine_global_config_create(void) {
    kv_engine_global_config_t *config = kv_store_malloc(sizeof(kv_engine_global_config_t));
    if (!config) return NULL;
    
    memset(config, 0, sizeof(kv_engine_global_config_t));
    
    // 设置默认值
    config->strategy = KV_ENGINE_STRATEGY_FIXED;
    config->default_engine = KV_ENGINE_HASH;  // 默认使用Hash引擎
    config->max_engines = KV_ENGINE_MAX;
    config->enable_statistics = true;
    config->enable_monitoring = false;
    
    config->global_memory_limit = KV_ENGINE_DEFAULT_MEMORY_LIMIT;
    config->global_cache_size = KV_ENGINE_DEFAULT_CACHE_SIZE;
    config->worker_threads = KV_ENGINE_DEFAULT_WORKER_THREADS;
    config->io_threads = KV_ENGINE_DEFAULT_IO_THREADS;
    
    config->data_directory = strdup(KV_ENGINE_DEFAULT_DATA_DIR);
    config->enable_wal = false;
    config->enable_sync = false;
    config->sync_interval = KV_ENGINE_DEFAULT_SYNC_INTERVAL;
    
    config->listen_host = strdup(KV_ENGINE_DEFAULT_LISTEN_HOST);
    config->listen_port = KV_ENGINE_DEFAULT_LISTEN_PORT;
    config->max_connections = KV_ENGINE_DEFAULT_MAX_CONNECTIONS;
    config->connection_timeout = KV_ENGINE_DEFAULT_CONNECTION_TIMEOUT;
    
    // 创建各引擎默认配置
    config->array_config = create_default_engine_config(KV_ENGINE_ARRAY);
    config->rbtree_config = create_default_engine_config(KV_ENGINE_RBTREE);
    config->hash_config = create_default_engine_config(KV_ENGINE_HASH);
    config->btree_config = create_default_engine_config(KV_ENGINE_BTREE);
    config->lsm_config = create_default_engine_config(KV_ENGINE_LSM);
    
    return config;
}

void kv_engine_global_config_destroy(kv_engine_global_config_t *config) {
    if (!config) return;
    
    // 释放字符串字段
    if (config->data_directory) {
        free(config->data_directory);
    }
    if (config->listen_host) {
        free(config->listen_host);
    }
    
    // 释放引擎配置
    if (config->array_config) {
        kv_engine_config_destroy(config->array_config);
    }
    if (config->rbtree_config) {
        kv_engine_config_destroy(config->rbtree_config);
    }
    if (config->hash_config) {
        kv_engine_config_destroy(config->hash_config);
    }
    if (config->btree_config) {
        kv_engine_config_destroy(config->btree_config);
    }
    if (config->lsm_config) {
        kv_engine_config_destroy(config->lsm_config);
    }
    
    kv_store_free(config);
}

kv_engine_global_config_t* kv_engine_config_load_from_file(const char *filename) {
    if (!filename) return NULL;
    
    // 加载配置文件
    concord_config_t *cfg = concord_config_load_with_format(filename, CONFIG_FORMAT_AUTO);
    if (!cfg) {
        fprintf(stderr, "Failed to load config file: %s\n", filename);
        return NULL;
    }
    
    // 创建全局配置对象
    kv_engine_global_config_t *config = kv_engine_global_config_create();
    if (!config) {
        concord_config_destroy(cfg);
        return NULL;
    }
    
    // 加载基础配置
    const char *strategy_str = concord_config_get_string(cfg, "engine.strategy", "fixed");
    config->strategy = kv_engine_strategy_from_string(strategy_str);
    
    const char *default_engine_str = concord_config_get_string(cfg, "engine.default", "hash");
    config->default_engine = kv_engine_type_from_string(default_engine_str);
    
    config->max_engines = concord_config_get_int(cfg, "engine.max_engines", config->max_engines);
    config->enable_statistics = concord_config_get_bool(cfg, "engine.enable_statistics", config->enable_statistics);
    config->enable_monitoring = concord_config_get_bool(cfg, "engine.enable_monitoring", config->enable_monitoring);
    
    // 性能配置
    config->global_memory_limit = concord_config_get_int(cfg, "performance.memory_limit", config->global_memory_limit);
    config->global_cache_size = concord_config_get_int(cfg, "performance.cache_size", config->global_cache_size);
    config->worker_threads = concord_config_get_int(cfg, "performance.worker_threads", config->worker_threads);
    config->io_threads = concord_config_get_int(cfg, "performance.io_threads", config->io_threads);
    
    // 持久化配置
    const char *data_dir = concord_config_get_string(cfg, "storage.data_directory", config->data_directory);
    if (data_dir && strcmp(data_dir, config->data_directory) != 0) {
        free(config->data_directory);
        config->data_directory = strdup(data_dir);
    }
    
    config->enable_wal = concord_config_get_bool(cfg, "storage.enable_wal", config->enable_wal);
    config->enable_sync = concord_config_get_bool(cfg, "storage.enable_sync", config->enable_sync);
    config->sync_interval = concord_config_get_int(cfg, "storage.sync_interval", config->sync_interval);
    
    // 网络配置
    const char *listen_host = concord_config_get_string(cfg, "network.listen_host", config->listen_host);
    if (listen_host && strcmp(listen_host, config->listen_host) != 0) {
        free(config->listen_host);
        config->listen_host = strdup(listen_host);
    }
    
    config->listen_port = concord_config_get_int(cfg, "network.listen_port", config->listen_port);
    config->max_connections = concord_config_get_int(cfg, "network.max_connections", config->max_connections);
    config->connection_timeout = concord_config_get_int(cfg, "network.connection_timeout", config->connection_timeout);
    
    // 加载各引擎特定配置
    load_engine_config_from_config(cfg, "engines.array", config->array_config);
    load_engine_config_from_config(cfg, "engines.rbtree", config->rbtree_config);
    load_engine_config_from_config(cfg, "engines.hash", config->hash_config);
    load_engine_config_from_config(cfg, "engines.btree", config->btree_config);
    load_engine_config_from_config(cfg, "engines.lsm", config->lsm_config);
    
    concord_config_destroy(cfg);
    return config;
}

int kv_engine_config_save_to_file(kv_engine_global_config_t *config, const char *filename) {
    if (!config || !filename) return -1;
    
    concord_config_t *cfg = concord_config_init();
    if (!cfg) return -1;
    
    // 保存基础配置
    concord_config_set_string(cfg, "engine.strategy", kv_engine_strategy_to_string(config->strategy));
    concord_config_set_string(cfg, "engine.default", kv_engine_type_to_string_ex(config->default_engine));
    concord_config_set_int(cfg, "engine.max_engines", config->max_engines);
    concord_config_set_bool(cfg, "engine.enable_statistics", config->enable_statistics);
    concord_config_set_bool(cfg, "engine.enable_monitoring", config->enable_monitoring);
    
    // 性能配置
    concord_config_set_int(cfg, "performance.memory_limit", config->global_memory_limit);
    concord_config_set_int(cfg, "performance.cache_size", config->global_cache_size);
    concord_config_set_int(cfg, "performance.worker_threads", config->worker_threads);
    concord_config_set_int(cfg, "performance.io_threads", config->io_threads);
    
    // 持久化配置
    concord_config_set_string(cfg, "storage.data_directory", config->data_directory);
    concord_config_set_bool(cfg, "storage.enable_wal", config->enable_wal);
    concord_config_set_bool(cfg, "storage.enable_sync", config->enable_sync);
    concord_config_set_int(cfg, "storage.sync_interval", config->sync_interval);
    
    // 网络配置
    concord_config_set_string(cfg, "network.listen_host", config->listen_host);
    concord_config_set_int(cfg, "network.listen_port", config->listen_port);
    concord_config_set_int(cfg, "network.max_connections", config->max_connections);
    concord_config_set_int(cfg, "network.connection_timeout", config->connection_timeout);
    
    // 引擎特定配置（简化版，只保存主要参数）
    if (config->hash_config) {
        concord_config_set_int(cfg, "engines.hash.memory_limit", config->hash_config->memory_limit);
        concord_config_set_int(cfg, "engines.hash.cache_size", config->hash_config->cache_size);
        concord_config_set_int(cfg, "engines.hash.initial_buckets", config->hash_config->initial_buckets);
        concord_config_set_float(cfg, "engines.hash.load_factor", config->hash_config->load_factor);
    }
    
    int result = concord_config_save_with_format(cfg, filename, CONFIG_FORMAT_AUTO);
    concord_config_destroy(cfg);
    
    return result;
}

int kv_engine_config_load_from_env(kv_engine_global_config_t *config, const char *prefix) {
    if (!config || !prefix) return -1;
    
    // 简化版环境变量加载，只加载关键配置
    char env_var[256];
    const char *env_val;
    
    // 默认引擎
    snprintf(env_var, sizeof(env_var), "%sDEFAULT_ENGINE", prefix);
    env_val = getenv(env_var);
    if (env_val) {
        config->default_engine = kv_engine_type_from_string(env_val);
    }
    
    // 监听端口
    snprintf(env_var, sizeof(env_var), "%sLISTEN_PORT", prefix);
    env_val = getenv(env_var);
    if (env_val) {
        config->listen_port = atoi(env_val);
    }
    
    // 数据目录
    snprintf(env_var, sizeof(env_var), "%sDATA_DIR", prefix);
    env_val = getenv(env_var);
    if (env_val) {
        free(config->data_directory);
        config->data_directory = strdup(env_val);
    }
    
    return 0;
}

int kv_engine_config_validate(kv_engine_global_config_t *config) {
    if (!config) return -1;
    
    // 验证基本配置
    if (config->default_engine >= KV_ENGINE_MAX) {
        fprintf(stderr, "Invalid default engine type: %d\n", config->default_engine);
        return -1;
    }
    
    if (config->strategy > KV_ENGINE_STRATEGY_LOAD_BALANCED) {
        fprintf(stderr, "Invalid engine strategy: %d\n", config->strategy);
        return -1;
    }
    
    if (config->worker_threads <= 0 || config->worker_threads > 128) {
        fprintf(stderr, "Invalid worker threads: %d\n", config->worker_threads);
        return -1;
    }
    
    if (config->listen_port <= 0 || config->listen_port > 65535) {
        fprintf(stderr, "Invalid listen port: %d\n", config->listen_port);
        return -1;
    }
    
    if (!config->data_directory || strlen(config->data_directory) == 0) {
        fprintf(stderr, "Data directory not specified\n");
        return -1;
    }
    
    // 验证数据目录是否可访问
    if (access(config->data_directory, F_OK) != 0) {
        fprintf(stderr, "Data directory does not exist: %s\n", config->data_directory);
        return -1;
    }
    
    return 0;
}

void kv_engine_config_print(kv_engine_global_config_t *config) {
    if (!config) return;
    
    printf("=== ConcordKV Engine Configuration ===\n");
    printf("Strategy: %s\n", kv_engine_strategy_to_string(config->strategy));
    printf("Default Engine: %s\n", kv_engine_type_to_string_ex(config->default_engine));
    printf("Max Engines: %zu\n", config->max_engines);
    printf("Statistics: %s\n", config->enable_statistics ? "Enabled" : "Disabled");
    printf("Monitoring: %s\n", config->enable_monitoring ? "Enabled" : "Disabled");
    
    printf("\n--- Performance ---\n");
    printf("Memory Limit: %zu MB\n", config->global_memory_limit / (1024 * 1024));
    printf("Cache Size: %zu MB\n", config->global_cache_size / (1024 * 1024));
    printf("Worker Threads: %d\n", config->worker_threads);
    printf("IO Threads: %d\n", config->io_threads);
    
    printf("\n--- Storage ---\n");
    printf("Data Directory: %s\n", config->data_directory);
    printf("WAL: %s\n", config->enable_wal ? "Enabled" : "Disabled");
    printf("Sync: %s\n", config->enable_sync ? "Enabled" : "Disabled");
    printf("Sync Interval: %d seconds\n", config->sync_interval);
    
    printf("\n--- Network ---\n");
    printf("Listen: %s:%d\n", config->listen_host, config->listen_port);
    printf("Max Connections: %d\n", config->max_connections);
    printf("Connection Timeout: %d seconds\n", config->connection_timeout);
    
    printf("\n--- Engine Configs ---\n");
    if (config->hash_config) {
        printf("Hash Engine:\n");
        printf("  Memory: %zu MB\n", config->hash_config->memory_limit / (1024 * 1024));
        printf("  Buckets: %u\n", config->hash_config->initial_buckets);
        printf("  Load Factor: %.2f\n", config->hash_config->load_factor);
    }
    
    printf("=====================================\n");
}

// === 引擎管理器实现 ===

kv_engine_manager_t* kv_engine_manager_create(kv_engine_global_config_t *config) {
    if (!config) return NULL;
    
    kv_engine_manager_t *manager = kv_store_malloc(sizeof(kv_engine_manager_t));
    if (!manager) return NULL;
    
    memset(manager, 0, sizeof(kv_engine_manager_t));
    manager->config = config;
    manager->current_engine = config->default_engine;
    manager->start_time = time(NULL);
    
    // 初始化引擎数组
    manager->engines = kv_store_malloc(sizeof(kv_engine_t*) * KV_ENGINE_MAX);
    if (!manager->engines) {
        kv_store_free(manager);
        return NULL;
    }
    memset(manager->engines, 0, sizeof(kv_engine_t*) * KV_ENGINE_MAX);
    
    // 初始化读写锁
    if (pthread_rwlock_init(&manager->lock, NULL) != 0) {
        kv_store_free(manager->engines);
        kv_store_free(manager);
        return NULL;
    }
    
    return manager;
}

void kv_engine_manager_destroy(kv_engine_manager_t *manager) {
    if (!manager) return;
    
    // 销毁所有引擎
    if (manager->engines) {
        for (int i = 0; i < KV_ENGINE_MAX; i++) {
            if (manager->engines[i]) {
                kv_engine_destroy(manager->engines[i]);
            }
        }
        kv_store_free(manager->engines);
    }
    
    // 销毁锁
    pthread_rwlock_destroy(&manager->lock);
    
    // 销毁原始配置
    if (manager->raw_config) {
        concord_config_destroy(manager->raw_config);
    }
    
    kv_store_free(manager);
}

int kv_engine_manager_init_engines(kv_engine_manager_t *manager) {
    if (!manager || !manager->config) return -1;
    
    pthread_rwlock_wrlock(&manager->lock);
    
    // 根据配置初始化需要的引擎
    kv_engine_config_t *configs[] = {
        manager->config->array_config,
        manager->config->rbtree_config,
        manager->config->hash_config,
        manager->config->btree_config,
        manager->config->lsm_config
    };
    
    for (int i = 0; i < KV_ENGINE_MAX; i++) {
        if (configs[i] && !manager->engines[i]) {
            manager->engines[i] = kv_engine_create(i, configs[i]);
            if (manager->engines[i]) {
                manager->engine_count++;
                printf("Initialized %s engine successfully\n", 
                       kv_engine_type_to_string_ex(i));
            } else {
                fprintf(stderr, "Failed to initialize %s engine\n", 
                        kv_engine_type_to_string_ex(i));
            }
        }
    }
    
    pthread_rwlock_unlock(&manager->lock);
    
    return manager->engine_count > 0 ? 0 : -1;
}

kv_engine_t* kv_engine_manager_get_current(kv_engine_manager_t *manager) {
    if (!manager) return NULL;
    
    pthread_rwlock_rdlock(&manager->lock);
    kv_engine_t *engine = manager->engines[manager->current_engine];
    pthread_rwlock_unlock(&manager->lock);
    
    return engine;
}

kv_engine_t* kv_engine_manager_get_engine(kv_engine_manager_t *manager, kv_engine_type_t type) {
    if (!manager || type >= KV_ENGINE_MAX) return NULL;
    
    pthread_rwlock_rdlock(&manager->lock);
    kv_engine_t *engine = manager->engines[type];
    pthread_rwlock_unlock(&manager->lock);
    
    return engine;
}

int kv_engine_manager_switch_engine(kv_engine_manager_t *manager, kv_engine_type_t type) {
    if (!manager || type >= KV_ENGINE_MAX) return -1;
    
    pthread_rwlock_wrlock(&manager->lock);
    
    if (!manager->engines[type]) {
        pthread_rwlock_unlock(&manager->lock);
        return -1;
    }
    
    manager->current_engine = type;
    manager->engine_switches++;
    
    pthread_rwlock_unlock(&manager->lock);
    
    printf("Switched to %s engine\n", kv_engine_type_to_string_ex(type));
    return 0;
}

kv_engine_t* kv_engine_manager_select_optimal(kv_engine_manager_t *manager, int operation_type) {
    if (!manager) return NULL;
    
    // 简化版策略：根据操作类型选择引擎
    kv_engine_type_t optimal_type = manager->config->default_engine;
    
    switch (manager->config->strategy) {
        case KV_ENGINE_STRATEGY_FIXED:
            optimal_type = manager->config->default_engine;
            break;
            
        case KV_ENGINE_STRATEGY_ADAPTIVE:
            // 根据操作类型选择最优引擎
            switch (operation_type) {
                case KV_OPERATION_READ:
                    optimal_type = KV_ENGINE_HASH;  // Hash 读取快
                    break;
                case KV_OPERATION_WRITE:
                    optimal_type = KV_ENGINE_LSM;   // LSM 写入快
                    break;
                case KV_OPERATION_SCAN:
                    optimal_type = KV_ENGINE_BTREE; // BTree 范围查询快
                    break;
                default:
                    optimal_type = manager->config->default_engine;
                    break;
            }
            break;
            
        case KV_ENGINE_STRATEGY_LOAD_BALANCED:
            // 简化的负载均衡：轮询
            optimal_type = (manager->total_operations % manager->engine_count) % KV_ENGINE_MAX;
            break;
    }
    
    return kv_engine_manager_get_engine(manager, optimal_type);
}

int kv_engine_manager_get_stats(kv_engine_manager_t *manager, kv_engine_stats_t *stats) {
    if (!manager || !stats) return -1;
    
    memset(stats, 0, sizeof(kv_engine_stats_t));
    
    pthread_rwlock_rdlock(&manager->lock);
    
    // 聚合所有引擎的统计信息
    for (int i = 0; i < KV_ENGINE_MAX; i++) {
        if (manager->engines[i]) {
            kv_engine_stats_t engine_stats;
            if (manager->engines[i]->vtable->get_stats && 
                manager->engines[i]->vtable->get_stats(manager->engines[i], &engine_stats) == 0) {
                stats->total_keys += engine_stats.total_keys;
                stats->total_size += engine_stats.total_size;
                stats->memory_usage += engine_stats.memory_usage;
                stats->disk_usage += engine_stats.disk_usage;
                stats->read_count += engine_stats.read_count;
                stats->write_count += engine_stats.write_count;
                stats->delete_count += engine_stats.delete_count;
            }
        }
    }
    
    pthread_rwlock_unlock(&manager->lock);
    
    return 0;
}

// === 辅助函数实现 ===

kv_engine_type_t kv_engine_type_from_string(const char *type_str) {
    if (!type_str) return KV_ENGINE_HASH;  // 默认值
    
    for (int i = 0; i < KV_ENGINE_MAX; i++) {
        if (strcasecmp(type_str, engine_type_strings[i]) == 0) {
            return i;
        }
    }
    
    return KV_ENGINE_HASH;  // 默认值
}

const char* kv_engine_type_to_string_ex(kv_engine_type_t type) {
    if (type >= KV_ENGINE_MAX) return "unknown";
    return engine_type_strings[type];
}

kv_engine_strategy_t kv_engine_strategy_from_string(const char *strategy_str) {
    if (!strategy_str) return KV_ENGINE_STRATEGY_FIXED;
    
    for (int i = 0; i <= KV_ENGINE_STRATEGY_LOAD_BALANCED; i++) {
        if (strcasecmp(strategy_str, strategy_strings[i]) == 0) {
            return i;
        }
    }
    
    return KV_ENGINE_STRATEGY_FIXED;
}

const char* kv_engine_strategy_to_string(kv_engine_strategy_t strategy) {
    if (strategy > KV_ENGINE_STRATEGY_LOAD_BALANCED) return "unknown";
    return strategy_strings[strategy];
}

// === 全局接口 ===

/**
 * 获取全局引擎管理器
 */
kv_engine_manager_t* kv_engine_get_global_manager(void) {
    return g_engine_manager;
}

/**
 * 设置全局引擎管理器
 */
void kv_engine_set_global_manager(kv_engine_manager_t *manager) {
    g_engine_manager = manager;
} 