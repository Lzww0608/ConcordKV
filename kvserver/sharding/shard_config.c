/*
 * @Author: Lzww0608
 * @Date: 2025-6-25 12:58:13
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-25 12:58:13
 * @Description: ConcordKV 分片配置管理模块实现 - 动态配置与热更新
 */

#include "shard_config.h"
#include "../kv_memory.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

// 调试宏
#ifdef DEBUG
#define SHARD_CONFIG_DEBUG(fmt, ...) printf("[SHARD_CONFIG_DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define SHARD_CONFIG_DEBUG(fmt, ...)
#endif

// === 配置管理器实现 ===

shard_config_manager_t* shard_config_manager_create(const char *config_file_path) {
    shard_config_manager_t *manager = kv_store_malloc(sizeof(shard_config_manager_t));
    if (!manager) {
        return NULL;
    }
    
    memset(manager, 0, sizeof(shard_config_manager_t));
    
    // 设置配置文件路径
    if (config_file_path) {
        strncpy(manager->config_file_path, config_file_path, sizeof(manager->config_file_path) - 1);
        manager->config_file_path[sizeof(manager->config_file_path) - 1] = '\0';
    }
    
    // 初始化读写锁
    if (pthread_rwlock_init(&manager->config_lock, NULL) != 0) {
        kv_store_free(manager);
        return NULL;
    }
    
    // 创建默认配置
    manager->config = shard_config_create_default();
    if (!manager->config) {
        pthread_rwlock_destroy(&manager->config_lock);
        kv_store_free(manager);
        return NULL;
    }
    
    manager->config_version = 1;
    manager->last_update = time(NULL);
    manager->config_changed = false;
    
    SHARD_CONFIG_DEBUG("Created shard config manager with file: %s", 
                      config_file_path ? config_file_path : "none");
    
    return manager;
}

void shard_config_manager_destroy(shard_config_manager_t *manager) {
    if (!manager) return;
    
    // 销毁配置
    if (manager->config) {
        shard_config_destroy(manager->config);
    }
    
    // 销毁锁
    pthread_rwlock_destroy(&manager->config_lock);
    
    // 释放内存
    kv_store_free(manager);
    
    SHARD_CONFIG_DEBUG("Destroyed shard config manager");
}

shard_config_t* shard_config_create_default(void) {
    shard_config_t *config = kv_store_malloc(sizeof(shard_config_t));
    if (!config) {
        return NULL;
    }
    
    memset(config, 0, sizeof(shard_config_t));
    
    // === 基础分片配置 ===
    config->enabled = true;
    config->shard_count = 16;  // 默认16个分片
    config->strategy = SHARD_STRATEGY_HASH;
    config->virtual_nodes = 160;
    config->replication_factor = 3;
    
    // === 哈希配置 ===
    config->hash_seed = 0x9E3779B9;  // 与Go层保持一致
    strncpy(config->hash_algorithm, "murmur3", sizeof(config->hash_algorithm) - 1);
    config->enable_hash_cache = true;
    config->hash_cache_size = 10000;
    config->hash_cache_ttl = 300;
    
    // === 负载均衡配置 ===
    config->balance_strategy = SHARD_BALANCE_LEAST_LOADED;
    config->load_threshold = 0.8;
    config->rebalance_interval = 300;  // 5分钟
    config->enable_auto_rebalance = true;
    config->migration_batch_size = 1000;
    
    // === 性能配置 ===
    config->max_concurrent_operations = 1000;
    config->operation_timeout = 5000;  // 5秒
    config->enable_batch_operations = true;
    config->batch_size = 100;
    
    // === 容错配置 ===
    config->max_retries = 3;
    config->retry_interval = 100;  // 100毫秒
    config->enable_failover = true;
    config->health_check_interval = 30;  // 30秒
    
    // === 监控配置 ===
    config->enable_metrics = true;
    config->metrics_collection_interval = 60;  // 1分钟
    strncpy(config->metrics_output_dir, "/tmp/concord_metrics", 
            sizeof(config->metrics_output_dir) - 1);
    config->enable_detailed_stats = false;
    
    // === 持久化配置 ===
    config->enable_persistence = true;
    strncpy(config->persistence_dir, "/tmp/concord_shards", 
            sizeof(config->persistence_dir) - 1);
    config->persistence_interval = 300;  // 5分钟
    config->enable_compression = false;
    
    // === 分片引擎配置 ===
    config->shard_engine_type = KV_ENGINE_HASH;  // 默认使用哈希引擎
    
    // 为每个分片分配引擎配置数组
    config->shard_engine_configs = kv_store_malloc(
        sizeof(kv_engine_config_t*) * config->shard_count);
    if (!config->shard_engine_configs) {
        kv_store_free(config);
        return NULL;
    }
    
    // 初始化每个分片的引擎配置
    for (uint32_t i = 0; i < config->shard_count; i++) {
        config->shard_engine_configs[i] = kv_engine_config_create(config->shard_engine_type);
        if (!config->shard_engine_configs[i]) {
            // 清理已分配的配置
            for (uint32_t j = 0; j < i; j++) {
                kv_engine_config_destroy(config->shard_engine_configs[j]);
            }
            kv_store_free(config->shard_engine_configs);
            kv_store_free(config);
            return NULL;
        }
    }
    
    SHARD_CONFIG_DEBUG("Created default shard config with %u shards", config->shard_count);
    
    return config;
}

void shard_config_destroy(shard_config_t *config) {
    if (!config) return;
    
    // 销毁分片引擎配置数组
    if (config->shard_engine_configs) {
        for (uint32_t i = 0; i < config->shard_count; i++) {
            if (config->shard_engine_configs[i]) {
                kv_engine_config_destroy(config->shard_engine_configs[i]);
                config->shard_engine_configs[i] = NULL;  // 避免双重释放
            }
        }
        kv_store_free(config->shard_engine_configs);
        config->shard_engine_configs = NULL;  // 避免双重释放
    }
    
    // 清空持久化目录字符串数组 (数组不能赋值NULL，只需清空内容)
    if (config->persistence_dir[0] != '\0') {
        config->persistence_dir[0] = '\0';  // 清空字符串
    }
    
    // 释放配置结构本身
    kv_store_free(config);
    
    SHARD_CONFIG_DEBUG("Destroyed shard config");
}

const shard_config_t* shard_config_get_current(shard_config_manager_t *manager) {
    if (!manager) return NULL;
    
    pthread_rwlock_rdlock(&manager->config_lock);
    const shard_config_t *config = manager->config;
    pthread_rwlock_unlock(&manager->config_lock);
    
    return config;
}

int shard_config_update(shard_config_manager_t *manager, const shard_config_t *new_config) {
    if (!manager || !new_config) {
        return -1;
    }
    
    // 验证新配置
    char error_msg[1024];
    if (!shard_config_validate(new_config, error_msg, sizeof(error_msg))) {
        SHARD_CONFIG_DEBUG("Config validation failed: %s", error_msg);
        return -1;
    }
    
    pthread_rwlock_wrlock(&manager->config_lock);
    
    // 保存旧配置
    shard_config_t *old_config = manager->config;
    
    // 复制新配置（简化实现，这里直接赋值指针）
    manager->config = (shard_config_t*)new_config;
    manager->config_version++;
    manager->last_update = time(NULL);
    manager->config_changed = true;
    
    pthread_rwlock_unlock(&manager->config_lock);
    
    // 清理旧配置
    if (old_config) {
        shard_config_destroy(old_config);
    }
    
    SHARD_CONFIG_DEBUG("Updated shard config to version %llu", manager->config_version);
    
    return 0;
}

int shard_config_load_from_file(shard_config_manager_t *manager, const char *file_path) {
    if (!manager || !file_path) {
        return -1;
    }
    
    FILE *fp = fopen(file_path, "r");
    if (!fp) {
        SHARD_CONFIG_DEBUG("Failed to open config file: %s", file_path);
        return -1;
    }
    
    // 简化实现：这里应该解析配置文件格式（JSON/YAML/INI等）
    // 目前只是创建默认配置
    shard_config_t *new_config = shard_config_create_default();
    if (!new_config) {
        fclose(fp);
        return -1;
    }
    
    fclose(fp);
    
    // 更新配置
    int result = shard_config_update(manager, new_config);
    
    SHARD_CONFIG_DEBUG("Loaded config from file: %s", file_path);
    
    return result;
}

bool shard_config_validate(const shard_config_t *config, 
                          char *error_msg, 
                          size_t error_msg_size) {
    if (!config) {
        snprintf(error_msg, error_msg_size, "Config is NULL");
        return false;
    }
    
    // 验证分片数量
    if (config->shard_count == 0 || config->shard_count > 65536) {
        snprintf(error_msg, error_msg_size, 
                "Invalid shard count: %u (must be 1-65536)", config->shard_count);
        return false;
    }
    
    // 验证副本因子
    if (config->replication_factor == 0 || config->replication_factor > 10) {
        snprintf(error_msg, error_msg_size, 
                "Invalid replication factor: %u (must be 1-10)", config->replication_factor);
        return false;
    }
    
    // 验证虚拟节点数量
    if (config->virtual_nodes == 0 || config->virtual_nodes > 1000) {
        snprintf(error_msg, error_msg_size, 
                "Invalid virtual nodes: %u (must be 1-1000)", config->virtual_nodes);
        return false;
    }
    
    // 验证负载阈值
    if (config->load_threshold <= 0.0 || config->load_threshold > 1.0) {
        snprintf(error_msg, error_msg_size, 
                "Invalid load threshold: %.2f (must be 0.0-1.0)", config->load_threshold);
        return false;
    }
    
    // 验证超时配置
    if (config->operation_timeout == 0 || config->operation_timeout > 300000) {
        snprintf(error_msg, error_msg_size, 
                "Invalid operation timeout: %u (must be 1-300000ms)", config->operation_timeout);
        return false;
    }
    
    // 验证引擎配置
    if (!config->shard_engine_configs) {
        snprintf(error_msg, error_msg_size, "Shard engine configs is NULL");
        return false;
    }
    
    return true;
}

const char* shard_strategy_to_string(shard_strategy_t strategy) {
    switch (strategy) {
        case SHARD_STRATEGY_HASH:       return "hash";
        case SHARD_STRATEGY_RANGE:      return "range";
        case SHARD_STRATEGY_CONSISTENT: return "consistent";
        case SHARD_STRATEGY_DIRECTORY:  return "directory";
        case SHARD_STRATEGY_HYBRID:     return "hybrid";
        default:                        return "unknown";
    }
}

void shard_config_print(const shard_config_t *config) {
    if (!config) {
        printf("Shard config is NULL\n");
        return;
    }
    
    printf("=== Shard Configuration ===\n");
    printf("Enabled: %s\n", config->enabled ? "true" : "false");
    printf("Shard count: %u\n", config->shard_count);
    printf("Strategy: %s\n", shard_strategy_to_string(config->strategy));
    printf("Virtual nodes: %u\n", config->virtual_nodes);
    printf("Replication factor: %u\n", config->replication_factor);
    printf("Hash algorithm: %s\n", config->hash_algorithm);
    printf("Hash cache: %s (size: %zu, TTL: %u)\n", 
           config->enable_hash_cache ? "enabled" : "disabled",
           config->hash_cache_size, config->hash_cache_ttl);
    printf("Load threshold: %.2f\n", config->load_threshold);
    printf("Auto rebalance: %s (interval: %u)\n",
           config->enable_auto_rebalance ? "enabled" : "disabled",
           config->rebalance_interval);
    printf("Operation timeout: %u ms\n", config->operation_timeout);
    printf("Batch operations: %s (size: %zu)\n",
           config->enable_batch_operations ? "enabled" : "disabled",
           config->batch_size);
    printf("Metrics: %s (interval: %u)\n",
           config->enable_metrics ? "enabled" : "disabled",
           config->metrics_collection_interval);
    printf("Persistence: %s (dir: %s, interval: %u)\n",
           config->enable_persistence ? "enabled" : "disabled",
           config->persistence_dir, config->persistence_interval);
    printf("Engine type: %s\n", kv_engine_type_to_string(config->shard_engine_type));
    printf("===========================\n");
}

kv_engine_config_t* shard_config_create_engine_config(const shard_config_t *shard_config, 
                                                      uint32_t shard_id) {
    if (!shard_config || shard_id >= shard_config->shard_count) {
        return NULL;
    }
    
    // 总是创建新的引擎配置，避免共享指针问题
    kv_engine_config_t *engine_config = kv_engine_config_create(shard_config->shard_engine_type);
    if (!engine_config) {
        return NULL;
    }
    
    // 根据分片配置调整引擎配置
    if (shard_config->enable_persistence) {
        // 设置数据目录为分片特定目录
        char shard_data_dir[512];
        snprintf(shard_data_dir, sizeof(shard_data_dir), 
                "%s/shard_%u", shard_config->persistence_dir, shard_id);
        
        if (engine_config->data_dir) {
            free(engine_config->data_dir);
        }
        engine_config->data_dir = strdup(shard_data_dir);
    }
    
    // 设置缓存大小（平均分配）
    if (shard_config->shard_count > 0) {
        engine_config->cache_size = engine_config->cache_size / shard_config->shard_count;
        engine_config->memory_limit = engine_config->memory_limit / shard_config->shard_count;
    }
    
    // 设置压缩选项
    engine_config->enable_compression = shard_config->enable_compression;
    
    SHARD_CONFIG_DEBUG("Created engine config for shard %u", shard_id);
    
    return engine_config;
}
