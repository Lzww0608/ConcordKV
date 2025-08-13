/*
 * @Author: Lzww0608
 * @Date: 2025-6-25 12:58:13
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-25 12:58:13
 * @Description: ConcordKV 分片感知存储引擎实现 - 透明分片路由
 */

#include "shard_aware_engine.h"
#include "../kv_memory.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <assert.h>

// 调试宏
#ifdef DEBUG
#define SHARD_AWARE_DEBUG(fmt, ...) printf("[SHARD_AWARE_DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define SHARD_AWARE_DEBUG(fmt, ...)
#endif

// === 分片实例管理 ===

static shard_instance_t* shard_instance_create(uint32_t shard_id, 
                                              const kv_engine_config_t *config) {
    shard_instance_t *instance = kv_store_malloc(sizeof(shard_instance_t));
    if (!instance) {
        return NULL;
    }
    
    memset(instance, 0, sizeof(shard_instance_t));
    
    instance->shard_id = shard_id;
    
    // 创建底层存储引擎（使用真正的工厂函数）
    instance->engine = kv_engine_create(config->type, config);
    if (!instance->engine) {
        kv_store_free(instance);
        return NULL;
    }
    
    // 初始化分片锁
    if (pthread_rwlock_init(&instance->shard_lock, NULL) != 0) {
        kv_engine_destroy(instance->engine);
        kv_store_free(instance);
        return NULL;
    }
    
    instance->is_active = true;
    
    SHARD_AWARE_DEBUG("Created shard instance %u", shard_id);
    
    return instance;
}

static void shard_instance_destroy(shard_instance_t *instance) {
    if (!instance) return;
    
    // 销毁底层引擎
    if (instance->engine) {
        kv_engine_destroy(instance->engine);
    }
    
    // 销毁锁
    pthread_rwlock_destroy(&instance->shard_lock);
    
    kv_store_free(instance);
    
    SHARD_AWARE_DEBUG("Destroyed shard instance %u", instance->shard_id);
}

// === 分片感知引擎实现 ===

shard_aware_engine_t* shard_aware_engine_create(shard_config_manager_t *config_manager) {
    if (!config_manager) {
        return NULL;
    }
    
    shard_aware_engine_t *shard_engine = kv_store_malloc(sizeof(shard_aware_engine_t));
    if (!shard_engine) {
        return NULL;
    }
    
    memset(shard_engine, 0, sizeof(shard_aware_engine_t));
    
    // 设置基础引擎信息
    shard_engine->base_engine.type = KV_ENGINE_MAX; // 特殊类型表示分片引擎
    shard_engine->base_engine.state = KV_ENGINE_STATE_INIT;
    strncpy(shard_engine->base_engine.name, "ShardAwareEngine", 
            sizeof(shard_engine->base_engine.name) - 1);
    
    shard_engine->config_manager = config_manager;
    shard_engine->shard_state = SHARD_ENGINE_STATE_INIT;
    
    // 获取当前配置
    const shard_config_t *config = shard_config_get_current(config_manager);
    if (!config) {
        kv_store_free(shard_engine);
        return NULL;
    }
    
    shard_engine->shard_count = config->shard_count;
    
    // 创建哈希管理器
    shard_engine->hash_manager = shard_hash_manager_create(
        config->shard_count,
        config->hash_cache_size,
        config->hash_cache_ttl
    );
    if (!shard_engine->hash_manager) {
        kv_store_free(shard_engine);
        return NULL;
    }
    
    // 分配分片实例数组
    shard_engine->shards = kv_store_malloc(sizeof(shard_instance_t*) * config->shard_count);
    if (!shard_engine->shards) {
        shard_hash_manager_destroy(shard_engine->hash_manager);
        kv_store_free(shard_engine);
        return NULL;
    }
    memset(shard_engine->shards, 0, sizeof(shard_instance_t*) * config->shard_count);
    
    // 初始化分片数组锁
    if (pthread_rwlock_init(&shard_engine->shards_lock, NULL) != 0) {
        kv_store_free(shard_engine->shards);
        shard_hash_manager_destroy(shard_engine->hash_manager);
        kv_store_free(shard_engine);
        return NULL;
    }
    
    // 设置性能配置
    shard_engine->enable_parallel_operations = config->enable_batch_operations;
    shard_engine->max_parallel_threads = config->max_concurrent_operations;
    
    SHARD_AWARE_DEBUG("Created shard aware engine with %u shards", config->shard_count);
    
    return shard_engine;
}

void shard_aware_engine_destroy(shard_aware_engine_t *shard_engine) {
    if (!shard_engine) return;
    
    // 停止引擎
    shard_aware_engine_stop(shard_engine);
    
    // 销毁所有分片实例
    pthread_rwlock_wrlock(&shard_engine->shards_lock);
    for (uint32_t i = 0; i < shard_engine->shard_count; i++) {
        if (shard_engine->shards[i]) {
            shard_instance_destroy(shard_engine->shards[i]);
        }
    }
    pthread_rwlock_unlock(&shard_engine->shards_lock);
    
    // 销毁锁
    pthread_rwlock_destroy(&shard_engine->shards_lock);
    
    // 销毁哈希管理器
    if (shard_engine->hash_manager) {
        shard_hash_manager_destroy(shard_engine->hash_manager);
    }
    
    // 释放内存
    kv_store_free(shard_engine->shards);
    kv_store_free(shard_engine);
    
    SHARD_AWARE_DEBUG("Destroyed shard aware engine");
}

int shard_aware_engine_init(shard_aware_engine_t *shard_engine) {
    if (!shard_engine) {
        return -1;
    }
    
    const shard_config_t *config = shard_config_get_current(shard_engine->config_manager);
    if (!config) {
        return -1;
    }
    
    // 创建所有分片实例
    pthread_rwlock_wrlock(&shard_engine->shards_lock);
    
    for (uint32_t i = 0; i < shard_engine->shard_count; i++) {
        // 为每个分片创建引擎配置
        kv_engine_config_t *engine_config = shard_config_create_engine_config(config, i);
        if (!engine_config) {
            pthread_rwlock_unlock(&shard_engine->shards_lock);
            return -1;
        }
        
        // 创建分片实例
        shard_engine->shards[i] = shard_instance_create(i, engine_config);
        
        // 立即释放配置（不管创建是否成功）
        kv_engine_config_destroy(engine_config);
        
        // 检查创建结果
        if (!shard_engine->shards[i]) {
            pthread_rwlock_unlock(&shard_engine->shards_lock);
            return -1;
        }
    }
    
    pthread_rwlock_unlock(&shard_engine->shards_lock);
    
    shard_engine->shard_state = SHARD_ENGINE_STATE_RUNNING;
    shard_engine->base_engine.state = KV_ENGINE_STATE_RUNNING;
    
    SHARD_AWARE_DEBUG("Initialized shard aware engine with %u shards", shard_engine->shard_count);
    
    return 0;
}

int shard_aware_engine_start(shard_aware_engine_t *shard_engine) {
    if (!shard_engine) {
        return -1;
    }
    
    if (shard_engine->shard_state != SHARD_ENGINE_STATE_INIT) {
        return shard_aware_engine_init(shard_engine);
    }
    
    return 0;
}

int shard_aware_engine_stop(shard_aware_engine_t *shard_engine) {
    if (!shard_engine) {
        return -1;
    }
    
    shard_engine->shard_state = SHARD_ENGINE_STATE_SHUTDOWN;
    shard_engine->base_engine.state = KV_ENGINE_STATE_SHUTDOWN;
    
    SHARD_AWARE_DEBUG("Stopped shard aware engine");
    
    return 0;
}

// === 分片路由实现 ===

uint32_t shard_aware_engine_get_shard_id(shard_aware_engine_t *shard_engine, const char *key) {
    if (!shard_engine || !key) {
        return 0;
    }
    
    return shard_hash_get_shard_id(shard_engine->hash_manager, key, strlen(key));
}

kv_engine_t* shard_aware_engine_get_shard_engine(shard_aware_engine_t *shard_engine, 
                                                 uint32_t shard_id) {
    if (!shard_engine || shard_id >= shard_engine->shard_count) {
        return NULL;
    }
    
    pthread_rwlock_rdlock(&shard_engine->shards_lock);
    
    shard_instance_t *instance = shard_engine->shards[shard_id];
    kv_engine_t *engine = (instance && instance->is_active) ? instance->engine : NULL;
    
    pthread_rwlock_unlock(&shard_engine->shards_lock);
    
    return engine;
}

// === 基础CRUD操作实现 ===

int shard_aware_engine_set(shard_aware_engine_t *shard_engine, 
                          const char *key, 
                          const char *value) {
    if (!shard_engine || !key || !value) {
        return -1;
    }
    
    // 获取分片ID
    uint32_t shard_id = shard_aware_engine_get_shard_id(shard_engine, key);
    
    // 获取分片引擎
    kv_engine_t *engine = shard_aware_engine_get_shard_engine(shard_engine, shard_id);
    if (!engine) {
        return -1;
    }
    
    // 执行操作
    int result = KV_ENGINE_SET(engine, key, value);
    
    // 更新统计
    shard_engine->total_operations++;
    if (result == 0 && shard_id < shard_engine->shard_count) {
        pthread_rwlock_rdlock(&shard_engine->shards_lock);
        if (shard_engine->shards[shard_id]) {
            shard_engine->shards[shard_id]->operation_count++;
        }
        pthread_rwlock_unlock(&shard_engine->shards_lock);
    } else if (result != 0) {
        shard_engine->failed_operations++;
    }
    
    SHARD_AWARE_DEBUG("Set key '%s' in shard %u: %s", key, shard_id, 
                     result == 0 ? "success" : "failed");
    
    return result;
}

char* shard_aware_engine_get(shard_aware_engine_t *shard_engine, const char *key) {
    if (!shard_engine || !key) {
        return NULL;
    }
    
    // 获取分片ID
    uint32_t shard_id = shard_aware_engine_get_shard_id(shard_engine, key);
    
    // 获取分片引擎
    kv_engine_t *engine = shard_aware_engine_get_shard_engine(shard_engine, shard_id);
    if (!engine) {
        return NULL;
    }
    
    // 执行操作
    char *result = KV_ENGINE_GET(engine, key);
    
    // 更新统计
    shard_engine->total_operations++;
    if (shard_id < shard_engine->shard_count) {
        pthread_rwlock_rdlock(&shard_engine->shards_lock);
        if (shard_engine->shards[shard_id]) {
            shard_engine->shards[shard_id]->operation_count++;
        }
        pthread_rwlock_unlock(&shard_engine->shards_lock);
    }
    
    SHARD_AWARE_DEBUG("Get key '%s' from shard %u: %s", key, shard_id, 
                     result ? "found" : "not found");
    
    return result;
}

int shard_aware_engine_delete(shard_aware_engine_t *shard_engine, const char *key) {
    if (!shard_engine || !key) {
        return -1;
    }
    
    // 获取分片ID
    uint32_t shard_id = shard_aware_engine_get_shard_id(shard_engine, key);
    
    // 获取分片引擎
    kv_engine_t *engine = shard_aware_engine_get_shard_engine(shard_engine, shard_id);
    if (!engine) {
        return -1;
    }
    
    // 执行操作
    int result = KV_ENGINE_DELETE(engine, key);
    
    // 更新统计
    shard_engine->total_operations++;
    if (result == 0 && shard_id < shard_engine->shard_count) {
        pthread_rwlock_rdlock(&shard_engine->shards_lock);
        if (shard_engine->shards[shard_id]) {
            shard_engine->shards[shard_id]->operation_count++;
        }
        pthread_rwlock_unlock(&shard_engine->shards_lock);
    } else if (result != 0) {
        shard_engine->failed_operations++;
    }
    
    SHARD_AWARE_DEBUG("Delete key '%s' from shard %u: %s", key, shard_id, 
                     result == 0 ? "success" : "failed");
    
    return result;
}

int shard_aware_engine_update(shard_aware_engine_t *shard_engine, 
                             const char *key, 
                             const char *value) {
    // 对于更新操作，我们可以直接使用set操作
    return shard_aware_engine_set(shard_engine, key, value);
}

// === 统计信息实现 ===

uint64_t shard_aware_engine_count(shard_aware_engine_t *shard_engine) {
    if (!shard_engine) {
        return 0;
    }
    
    uint64_t total_count = 0;
    
    pthread_rwlock_rdlock(&shard_engine->shards_lock);
    
    for (uint32_t i = 0; i < shard_engine->shard_count; i++) {
        if (shard_engine->shards[i] && shard_engine->shards[i]->is_active) {
            kv_engine_t *engine = shard_engine->shards[i]->engine;
            if (engine) {
                int count = KV_ENGINE_COUNT(engine);
                if (count > 0) {
                    total_count += count;
                }
            }
        }
    }
    
    pthread_rwlock_unlock(&shard_engine->shards_lock);
    
    return total_count;
}

size_t shard_aware_engine_memory_usage(shard_aware_engine_t *shard_engine) {
    if (!shard_engine) {
        return 0;
    }
    
    size_t total_memory = 0;
    
    pthread_rwlock_rdlock(&shard_engine->shards_lock);
    
    for (uint32_t i = 0; i < shard_engine->shard_count; i++) {
        if (shard_engine->shards[i] && shard_engine->shards[i]->is_active) {
            total_memory += shard_engine->shards[i]->memory_usage;
        }
    }
    
    pthread_rwlock_unlock(&shard_engine->shards_lock);
    
    return total_memory;
}

// === 工具函数实现 ===

const char* shard_engine_state_to_string(shard_engine_state_t state) {
    switch (state) {
        case SHARD_ENGINE_STATE_INIT:       return "Init";
        case SHARD_ENGINE_STATE_RUNNING:    return "Running";
        case SHARD_ENGINE_STATE_REBALANCING: return "Rebalancing";
        case SHARD_ENGINE_STATE_ERROR:      return "Error";
        case SHARD_ENGINE_STATE_SHUTDOWN:   return "Shutdown";
        default:                            return "Unknown";
    }
}

cross_shard_result_t* cross_shard_result_create(uint32_t shard_count) {
    cross_shard_result_t *result = kv_store_malloc(sizeof(cross_shard_result_t));
    if (!result) {
        return NULL;
    }
    
    memset(result, 0, sizeof(cross_shard_result_t));
    result->total_shards = shard_count;
    
    // 创建结果批次
    result->results = kv_batch_create(1000); // 默认容量
    if (!result->results) {
        kv_store_free(result);
        return NULL;
    }
    
    return result;
}

void cross_shard_result_destroy(cross_shard_result_t *result) {
    if (!result) return;
    
    if (result->results) {
        kv_batch_destroy(result->results);
    }
    
    kv_store_free(result);
}

// === 简化的批量操作和范围查询实现 ===

int shard_aware_engine_batch_set(shard_aware_engine_t *shard_engine,
                                const kv_batch_t *batch,
                                cross_shard_result_t *result) {
    if (!shard_engine || !batch || !result) {
        return -1;
    }
    
    // 简化实现：逐个处理批量操作中的每个键值对
    for (size_t i = 0; i < batch->count; i++) {
        int ret = shard_aware_engine_set(shard_engine, 
                                       batch->pairs[i].key, 
                                       batch->pairs[i].value);
        if (ret == 0) {
            result->successful_shards++;
        } else {
            result->failed_shards++;
        }
    }
    
    shard_engine->cross_shard_operations++;
    
    return (result->failed_shards == 0) ? 0 : -1;
}

int shard_aware_engine_batch_get(shard_aware_engine_t *shard_engine,
                                const char **keys,
                                size_t key_count,
                                cross_shard_result_t *result) {
    if (!shard_engine || !keys || !result) {
        return -1;
    }
    
    // 简化实现：逐个获取每个键
    for (size_t i = 0; i < key_count; i++) {
        char *value = shard_aware_engine_get(shard_engine, keys[i]);
        if (value) {
            // 添加到结果中
            kv_batch_add(result->results, keys[i], value);
            free(value); // 释放get返回的内存
            result->successful_shards++;
        } else {
            result->failed_shards++;
        }
    }
    
    shard_engine->cross_shard_operations++;
    
    return 0;
}

int shard_aware_engine_batch_delete(shard_aware_engine_t *shard_engine,
                                   const char **keys,
                                   size_t key_count,
                                   cross_shard_result_t *result) {
    if (!shard_engine || !keys || !result) {
        return -1;
    }
    
    // 简化实现：逐个删除每个键
    for (size_t i = 0; i < key_count; i++) {
        int ret = shard_aware_engine_delete(shard_engine, keys[i]);
        if (ret == 0) {
            result->successful_shards++;
        } else {
            result->failed_shards++;
        }
    }
    
    shard_engine->cross_shard_operations++;
    
    return (result->failed_shards == 0) ? 0 : -1;
}

// 其他函数的简化实现
int shard_aware_engine_range_scan(shard_aware_engine_t *shard_engine,
                                 const kv_range_t *range,
                                 cross_shard_result_t *result) {
    (void)shard_engine;
    (void)range;
    (void)result;
    // TODO: 实现范围扫描功能
    return -1;
}

int shard_aware_engine_prefix_scan(shard_aware_engine_t *shard_engine,
                                  const char *prefix,
                                  cross_shard_result_t *result) {
    (void)shard_engine;
    (void)prefix;
    (void)result;
    // TODO: 实现前缀扫描功能
    return -1;
}

int shard_aware_engine_get_shard_stats(shard_aware_engine_t *shard_engine,
                                      uint32_t shard_id,
                                      kv_engine_stats_t *stats) {
    (void)shard_engine;
    (void)shard_id;
    (void)stats;
    // TODO: 实现分片统计功能
    return -1;
}

int shard_aware_engine_add_shard(shard_aware_engine_t *shard_engine,
                                uint32_t shard_id,
                                const kv_engine_config_t *engine_config) {
    (void)shard_engine;
    (void)shard_id;
    (void)engine_config;
    // TODO: 实现动态添加分片功能
    return -1;
}

int shard_aware_engine_remove_shard(shard_aware_engine_t *shard_engine,
                                   uint32_t shard_id) {
    (void)shard_engine;
    (void)shard_id;
    // TODO: 实现动态移除分片功能
    return -1;
}

int shard_aware_engine_rebalance(shard_aware_engine_t *shard_engine) {
    (void)shard_engine;
    // TODO: 实现分片重平衡功能
    return -1;
}

int shard_aware_engine_update_config(shard_aware_engine_t *shard_engine,
                                    const shard_config_t *new_config) {
    (void)shard_engine;
    (void)new_config;
    // TODO: 实现配置更新功能
    return -1;
}
