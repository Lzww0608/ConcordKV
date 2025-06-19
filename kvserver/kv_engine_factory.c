/*
 * @Author: Lzww0608  
 * @Date: 2025-5-30 22:31:57
 * @LastEditors: Lzww0608
 * @LastEditTime:2025-6-19 11:48:51
 * @Description: ConcordKV 存储引擎工厂实现
 */
#define _GNU_SOURCE     // 启用扩展函数
#define _POSIX_C_SOURCE 200809L  // 启用POSIX扩展

#include "kv_engine_interface.h"
#include "kv_engine_metrics.h"  // 监控管理器
#include "kv_store.h"
#include "rbtree_adapter.h"  // 包含完整的rbtree定义
#include "btree_adapter.h"   // B+Tree适配器
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <sys/time.h>

// === 前向声明 ===
static int array_engine_init(kv_engine_t *engine, const kv_engine_config_t *config);
static int rbtree_engine_init(kv_engine_t *engine, const kv_engine_config_t *config);
static int hash_engine_init(kv_engine_t *engine, const kv_engine_config_t *config);
static int btree_engine_init(kv_engine_t *engine, const kv_engine_config_t *config);

// === 监控相关前向声明 ===
static int array_init_metrics(kv_engine_t *engine, void *metrics_manager);
static int rbtree_init_metrics(kv_engine_t *engine, void *metrics_manager);
static int hash_init_metrics(kv_engine_t *engine, void *metrics_manager);
static int btree_init_metrics(kv_engine_t *engine, void *metrics_manager);

// === 监控辅助函数 ===
static inline uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static inline void record_operation_metrics(kv_engine_t *engine, 
                                           const char *operation_type,
                                           uint64_t start_time,
                                           int result) {
    if (!engine->metrics_enabled || !engine->metrics_manager) {
        return;
    }
    
    kv_engine_metrics_manager_t *manager = (kv_engine_metrics_manager_t*)engine->metrics_manager;
    double latency_ms = (get_time_us() - start_time) / 1000.0;
    
    if (strcmp(operation_type, "read") == 0) {
        kv_engine_metrics_record_read(manager, engine->type, latency_ms);
    } else if (strcmp(operation_type, "write") == 0) {
        kv_engine_metrics_record_write(manager, engine->type, latency_ms);
    } else if (strcmp(operation_type, "delete") == 0) {
        kv_engine_metrics_record_delete(manager, engine->type, latency_ms);
    }
    
    // 记录错误
    if (result != KV_ERR_NONE) {
        const char *error_type = (result == KV_ERR_NOT_FOUND) ? "not_found" : "error";
        kv_engine_metrics_record_error(manager, engine->type, error_type);
    }
}

// === 引擎类型字符串映射 ===
static const char* engine_type_names[] = {
    "Array",
    "RBTree", 
    "Hash",
    "BTree",
    "LSM",
    "Unknown"
};

static const char* engine_state_names[] = {
    "Init",
    "Running",
    "Compacting",
    "Flushing", 
    "Error",
    "Shutdown"
};

// === Array存储引擎适配器 ===
static int array_set(kv_engine_t *engine, const char *key, const char *value) {
    KV_ENGINE_CHECK_VALID(engine);
    if (!key || !value) return KV_ERR_PARAM;
    
    uint64_t start_time = get_time_us();
    
    array_t *arr = (array_t*)engine->engine_data;
    int ret = kvs_array_set(arr, (char*)key, (char*)value);
    
    // 更新统计信息
    if (ret == 0) {
        engine->stats.write_count++;
    }
    
    int result = ret == 0 ? KV_ERR_NONE : KV_ERR_SYS;
    
    // 记录监控指标
    record_operation_metrics(engine, "write", start_time, result);
    
    // 更新内存使用量
    if (engine->metrics_enabled && engine->metrics_manager && ret == 0) {
        kv_engine_metrics_manager_t *manager = (kv_engine_metrics_manager_t*)engine->metrics_manager;
        size_t memory_usage = kvs_array_memory_usage(arr);
        kv_engine_metrics_update_memory_usage(manager, engine->type, memory_usage);
    }
    
    return result;
}

static char* array_get(kv_engine_t *engine, const char *key) {
    KV_ENGINE_CHECK_VALID_NULL(engine);
    if (!key) return NULL;
    
    uint64_t start_time = get_time_us();
    
    array_t *arr = (array_t*)engine->engine_data;
    char *value = kvs_array_get(arr, (char*)key);
    
    // 更新统计信息
    engine->stats.read_count++;
    
    // 记录监控指标
    int result = value ? KV_ERR_NONE : KV_ERR_NOT_FOUND;
    record_operation_metrics(engine, "read", start_time, result);
    
    return value;
}

static int array_delete(kv_engine_t *engine, const char *key) {
    KV_ENGINE_CHECK_VALID(engine);
    if (!key) return KV_ERR_PARAM;
    
    uint64_t start_time = get_time_us();
    
    array_t *arr = (array_t*)engine->engine_data;
    
    // 先检查键是否存在
    char *existing_value = kvs_array_get(arr, (char*)key);
    if (existing_value == NULL) {
        // 键不存在
        int result = KV_ERR_NOT_FOUND;
        record_operation_metrics(engine, "delete", start_time, result);
        return result;
    }
    
    // 键存在，执行删除
    int ret = kvs_array_delete(arr, (char*)key);
    
    // 更新统计信息
    if (ret == 0) {
        engine->stats.delete_count++;
    }
    
    int result = ret == 0 ? KV_ERR_NONE : KV_ERR_SYS;
    
    // 记录监控指标
    record_operation_metrics(engine, "delete", start_time, result);
    
    // 更新内存使用量
    if (engine->metrics_enabled && engine->metrics_manager && ret == 0) {
        kv_engine_metrics_manager_t *manager = (kv_engine_metrics_manager_t*)engine->metrics_manager;
        size_t memory_usage = kvs_array_memory_usage(arr);
        kv_engine_metrics_update_memory_usage(manager, engine->type, memory_usage);
    }
    
    return result;
}

static int array_update(kv_engine_t *engine, const char *key, const char *value) {
    KV_ENGINE_CHECK_VALID(engine);
    if (!key || !value) return KV_ERR_PARAM;
    
    array_t *arr = (array_t*)engine->engine_data;
    int ret = kvs_array_modify(arr, (char*)key, (char*)value);
    
    return ret == 0 ? KV_ERR_NONE : (ret > 0 ? KV_ERR_NOT_FOUND : KV_ERR_SYS);
}

static int array_count(kv_engine_t *engine) {
    KV_ENGINE_CHECK_VALID(engine);
    
    array_t *arr = (array_t*)engine->engine_data;
    return kvs_array_count(arr);
}

static int array_destroy_impl(kv_engine_t *engine) {
    if (!engine || !engine->engine_data) return KV_ERR_PARAM;
    
    array_t *arr = (array_t*)engine->engine_data;
    kv_store_array_destroy(arr);
    kv_store_free(arr);
    engine->engine_data = NULL;
    engine->state = KV_ENGINE_STATE_SHUTDOWN;
    
    return KV_ERR_NONE;
}

static kv_engine_vtable_t array_vtable = {
    .set = array_set,
    .get = array_get,
    .delete = array_delete,
    .update = array_update,
    .count = array_count,
    .destroy = array_destroy_impl,
    // 其他函数指针设为NULL，表示不支持
    .batch_set = NULL,
    .batch_get = NULL,
    .batch_delete = NULL,
    .create_iterator = NULL,
    .range_scan = NULL,
    .prefix_scan = NULL,
    .size = NULL,
    .memory_usage = NULL,
    .get_stats = NULL,
    .init = array_engine_init,
    .flush = NULL,
    .compact = NULL,
    .sync = NULL,
    .begin_transaction = NULL,
    .commit_transaction = NULL,
    .rollback_transaction = NULL,
    .create_snapshot = NULL,
    .restore_snapshot = NULL,
    .engine_specific = NULL,
    // 监控相关接口
    .init_metrics = array_init_metrics,
    .collect_metrics = NULL,
    .reset_metrics = NULL,
    .get_engine_specific_metrics = NULL
};

// === RBTree存储引擎适配器 ===
static int rbtree_set(kv_engine_t *engine, const char *key, const char *value) {
    KV_ENGINE_CHECK_VALID(engine);
    if (!key || !value) return KV_ERR_PARAM;
    
    rbtree_t *tree = (rbtree_t*)engine->engine_data;
    int ret = kvs_rbtree_set(tree, (char*)key, (char*)value);
    
    if (ret == 0) {
        engine->stats.write_count++;
    }
    
    return ret == 0 ? KV_ERR_NONE : KV_ERR_SYS;
}

static char* rbtree_get(kv_engine_t *engine, const char *key) {
    KV_ENGINE_CHECK_VALID_NULL(engine);
    if (!key) return NULL;
    
    rbtree_t *tree = (rbtree_t*)engine->engine_data;
    char *value = kvs_rbtree_get(tree, (char*)key);
    
    engine->stats.read_count++;
    
    return value;
}

static int rbtree_delete(kv_engine_t *engine, const char *key) {
    KV_ENGINE_CHECK_VALID(engine);
    if (!key) return KV_ERR_PARAM;
    
    rbtree_t *tree = (rbtree_t*)engine->engine_data;
    
    // 先检查键是否存在
    char *existing_value = kvs_rbtree_get(tree, (char*)key);
    if (existing_value == NULL) {
        // 键不存在
        return KV_ERR_NOT_FOUND;
    }
    
    // 键存在，执行删除
    int ret = kvs_rbtree_delete(tree, (char*)key);
    
    if (ret == 0) {
        engine->stats.delete_count++;
        return KV_ERR_NONE;
    }
    
    // 删除失败
    return KV_ERR_SYS;
}

static int rbtree_update(kv_engine_t *engine, const char *key, const char *value) {
    KV_ENGINE_CHECK_VALID(engine);
    if (!key || !value) return KV_ERR_PARAM;
    
    rbtree_t *tree = (rbtree_t*)engine->engine_data;
    int ret = kvs_rbtree_modify(tree, (char*)key, (char*)value);
    
    return ret == 0 ? KV_ERR_NONE : (ret > 0 ? KV_ERR_NOT_FOUND : KV_ERR_SYS);
}

static int rbtree_count(kv_engine_t *engine) {
    KV_ENGINE_CHECK_VALID(engine);
    
    rbtree_t *tree = (rbtree_t*)engine->engine_data;
    return kvs_rbtree_count(tree);
}

static int rbtree_destroy_impl(kv_engine_t *engine) {
    if (!engine || !engine->engine_data) return KV_ERR_PARAM;
    
    rbtree_t *tree = (rbtree_t*)engine->engine_data;
    
    // 现在可以安全调用修复后的kv_store_rbtree_destroy函数
    kv_store_rbtree_destroy(tree);
    kv_store_free(tree);
    engine->engine_data = NULL;
    engine->state = KV_ENGINE_STATE_SHUTDOWN;
    
    return KV_ERR_NONE;
}

static kv_engine_vtable_t rbtree_vtable = {
    .set = rbtree_set,
    .get = rbtree_get,
    .delete = rbtree_delete,
    .update = rbtree_update,
    .count = rbtree_count,
    .destroy = rbtree_destroy_impl,
    .init = rbtree_engine_init,
    // 其他函数指针设为NULL
};

// === Hash存储引擎适配器 ===
static int hash_set(kv_engine_t *engine, const char *key, const char *value) {
    KV_ENGINE_CHECK_VALID(engine);
    if (!key || !value) return KV_ERR_PARAM;
    
    hashtable_t *hash = (hashtable_t*)engine->engine_data;
    int ret = kvs_hash_set(hash, (char*)key, (char*)value);
    
    if (ret == 0) {
        engine->stats.write_count++;
    }
    
    return ret == 0 ? KV_ERR_NONE : KV_ERR_SYS;
}

static char* hash_get(kv_engine_t *engine, const char *key) {
    KV_ENGINE_CHECK_VALID_NULL(engine);
    if (!key) return NULL;
    
    hashtable_t *hash = (hashtable_t*)engine->engine_data;
    char *value = kvs_hash_get(hash, (char*)key);
    
    engine->stats.read_count++;
    
    return value;
}

static int hash_delete(kv_engine_t *engine, const char *key) {
    KV_ENGINE_CHECK_VALID(engine);
    if (!key) return KV_ERR_PARAM;
    
    hashtable_t *hash = (hashtable_t*)engine->engine_data;
    
    // 先检查键是否存在
    char *existing_value = kvs_hash_get(hash, (char*)key);
    if (existing_value == NULL) {
        // 键不存在
        return KV_ERR_NOT_FOUND;
    }
    
    // 键存在，执行删除
    int ret = kvs_hash_delete(hash, (char*)key);
    
    if (ret == 0) {
        engine->stats.delete_count++;
        return KV_ERR_NONE;
    }
    
    // 删除失败
    return KV_ERR_SYS;
}

static int hash_update(kv_engine_t *engine, const char *key, const char *value) {
    KV_ENGINE_CHECK_VALID(engine);
    if (!key || !value) return KV_ERR_PARAM;
    
    hashtable_t *hash = (hashtable_t*)engine->engine_data;
    int ret = kvs_hash_modify(hash, (char*)key, (char*)value);
    
    return ret == 0 ? KV_ERR_NONE : (ret > 0 ? KV_ERR_NOT_FOUND : KV_ERR_SYS);
}

static int hash_count(kv_engine_t *engine) {
    KV_ENGINE_CHECK_VALID(engine);
    
    hashtable_t *hash = (hashtable_t*)engine->engine_data;
    return kvs_hash_count(hash);
}

static int hash_destroy_impl(kv_engine_t *engine) {
    if (!engine || !engine->engine_data) return KV_ERR_PARAM;
    
    hashtable_t *hash = (hashtable_t*)engine->engine_data;
    kv_store_hash_destroy(hash);
    kv_store_free(hash);
    engine->engine_data = NULL;
    engine->state = KV_ENGINE_STATE_SHUTDOWN;
    
    return KV_ERR_NONE;
}

static kv_engine_vtable_t hash_vtable = {
    .set = hash_set,
    .get = hash_get,
    .delete = hash_delete,
    .update = hash_update,
    .count = hash_count,
    .destroy = hash_destroy_impl,
    .init = hash_engine_init,
    // 其他函数指针设为NULL
};

// === BTree存储引擎适配器 ===
static int btree_adapter_set(kv_engine_t *engine, const char *key, const char *value) {
    KV_ENGINE_CHECK_VALID(engine);
    if (!key || !value) return KV_ERR_PARAM;
    
    btree_t *tree = (btree_t*)engine->engine_data;
    int ret = kvs_btree_set(tree, (char*)key, (char*)value);
    
    if (ret == 0) {
        engine->stats.write_count++;
    }
    
    return ret == 0 ? KV_ERR_NONE : KV_ERR_SYS;
}

static char* btree_adapter_get(kv_engine_t *engine, const char *key) {
    KV_ENGINE_CHECK_VALID_NULL(engine);
    if (!key) return NULL;
    
    btree_t *tree = (btree_t*)engine->engine_data;
    char *value = kvs_btree_get(tree, (char*)key);
    
    engine->stats.read_count++;
    
    return value;
}

static int btree_adapter_delete(kv_engine_t *engine, const char *key) {
    KV_ENGINE_CHECK_VALID(engine);
    if (!key) return KV_ERR_PARAM;
    
    btree_t *tree = (btree_t*)engine->engine_data;
    
    // 先检查键是否存在
    char *existing_value = kvs_btree_get(tree, (char*)key);
    if (existing_value == NULL) {
        // 键不存在
        return KV_ERR_NOT_FOUND;
    }
    
    // 键存在，执行删除
    int ret = kvs_btree_delete(tree, (char*)key);
    
    if (ret == 0) {
        engine->stats.delete_count++;
        return KV_ERR_NONE;
    }
    
    // 删除失败
    return KV_ERR_SYS;
}

static int btree_adapter_update(kv_engine_t *engine, const char *key, const char *value) {
    KV_ENGINE_CHECK_VALID(engine);
    if (!key || !value) return KV_ERR_PARAM;
    
    btree_t *tree = (btree_t*)engine->engine_data;
    int ret = kvs_btree_modify(tree, (char*)key, (char*)value);
    
    return ret == 0 ? KV_ERR_NONE : (ret > 0 ? KV_ERR_NOT_FOUND : KV_ERR_SYS);
}

static int btree_adapter_count(kv_engine_t *engine) {
    KV_ENGINE_CHECK_VALID(engine);
    
    btree_t *tree = (btree_t*)engine->engine_data;
    return kvs_btree_count(tree);
}

static int btree_adapter_destroy_impl(kv_engine_t *engine) {
    if (!engine || !engine->engine_data) return KV_ERR_PARAM;
    
    btree_t *tree = (btree_t*)engine->engine_data;
    
    // 直接调用kv_store_btree_destroy，它内部会调用btree_destroy
    kv_store_btree_destroy(tree);
    engine->engine_data = NULL;
    engine->state = KV_ENGINE_STATE_SHUTDOWN;
    
    return KV_ERR_NONE;
}

static kv_engine_vtable_t btree_vtable = {
    .set = btree_adapter_set,
    .get = btree_adapter_get,
    .delete = btree_adapter_delete,
    .update = btree_adapter_update,
    .count = btree_adapter_count,
    .destroy = btree_adapter_destroy_impl,
    .init = btree_engine_init,
    // 其他函数指针设为NULL
};

// === 引擎初始化函数 ===
static int array_engine_init(kv_engine_t *engine, const kv_engine_config_t *config) {
    if (!engine) return KV_ERR_PARAM;
    
    array_t *arr = (array_t*)kv_store_malloc(sizeof(array_t));
    if (!arr) {
        KV_ERROR(KV_ERR_MEM, "Failed to allocate array engine");
        return KV_ERR_MEM;
    }
    
    if (kv_store_array_create(arr) != 0) {
        kv_store_free(arr);
        KV_ERROR(KV_ERR_SYS, "Failed to create array storage");
        return KV_ERR_SYS;
    }
    
    engine->engine_data = arr;
    engine->state = KV_ENGINE_STATE_RUNNING;
    strncpy(engine->name, "Array Engine", sizeof(engine->name) - 1);
    
    return KV_ERR_NONE;
}

static int rbtree_engine_init(kv_engine_t *engine, const kv_engine_config_t *config) {
    if (!engine) return KV_ERR_PARAM;
    
    // 分配rbtree结构体的空间 (3个指针 + 1个int，大约32字节，分配64字节保险)
    rbtree_t *tree = (rbtree_t*)kv_store_malloc(64);
    if (!tree) {
        KV_ERROR(KV_ERR_MEM, "Failed to allocate rbtree engine");
        return KV_ERR_MEM;
    }
    
    if (kv_store_rbtree_create(tree) != 0) {
        kv_store_free(tree);
        KV_ERROR(KV_ERR_SYS, "Failed to create rbtree storage");
        return KV_ERR_SYS;
    }
    
    engine->engine_data = tree;
    engine->state = KV_ENGINE_STATE_RUNNING;
    strncpy(engine->name, "RBTree Engine", sizeof(engine->name) - 1);
    
    return KV_ERR_NONE;
}

static int hash_engine_init(kv_engine_t *engine, const kv_engine_config_t *config) {
    if (!engine) return KV_ERR_PARAM;
    
    hashtable_t *hash = (hashtable_t*)kv_store_malloc(sizeof(hashtable_t));
    if (!hash) {
        KV_ERROR(KV_ERR_MEM, "Failed to allocate hash engine");
        return KV_ERR_MEM;
    }
    
    if (kv_store_hash_create(hash) != 0) {
        kv_store_free(hash);
        KV_ERROR(KV_ERR_SYS, "Failed to create hash storage");
        return KV_ERR_SYS;
    }
    
    engine->engine_data = hash;
    engine->state = KV_ENGINE_STATE_RUNNING;
    strncpy(engine->name, "Hash Engine", sizeof(engine->name) - 1);
    
    return KV_ERR_NONE;
}

static int btree_engine_init(kv_engine_t *engine, const kv_engine_config_t *config) {
    if (!engine) return KV_ERR_PARAM;
    
    // 使用配置中的参数或默认值
    int order = BTREE_DEFAULT_ORDER;
    if (config && config->max_keys_per_node > 0) {
        order = config->max_keys_per_node;
    }
    
    // 直接创建B+Tree
    btree_t *tree = kv_store_btree_create(order);
    if (!tree) {
        KV_ERROR(KV_ERR_SYS, "Failed to create btree storage");
        return KV_ERR_SYS;
    }
    
    engine->engine_data = tree;
    engine->state = KV_ENGINE_STATE_RUNNING;
    strncpy(engine->name, "BTree Engine", sizeof(engine->name) - 1);
    
    return KV_ERR_NONE;
}

// === 工厂函数实现 ===
kv_engine_t* kv_engine_create(kv_engine_type_t type, const kv_engine_config_t *config) {
    if (type >= KV_ENGINE_MAX) {
        KV_ERROR(KV_ERR_PARAM, "Invalid engine type: %d", type);
        return NULL;
    }
    
    kv_engine_t *engine = (kv_engine_t*)kv_store_malloc(sizeof(kv_engine_t));
    if (!engine) {
        KV_ERROR(KV_ERR_MEM, "Failed to allocate engine");
        return NULL;
    }
    
    memset(engine, 0, sizeof(kv_engine_t));
    engine->type = type;
    engine->state = KV_ENGINE_STATE_INIT;
    
    // 初始化读写锁
    if (pthread_rwlock_init(&engine->lock, NULL) != 0) {
        kv_store_free(engine);
        KV_ERROR(KV_ERR_SYS, "Failed to initialize rwlock");
        return NULL;
    }
    
    // 设置配置
    if (config) {
        engine->config = *config;
    } else {
        // 默认配置
        engine->config.type = type;
        engine->config.memory_limit = 64 * 1024 * 1024; // 64MB
        engine->config.cache_size = 16 * 1024 * 1024;   // 16MB
        engine->config.enable_compression = false;
        engine->config.enable_checksum = true;
        engine->config.data_dir = NULL;
    }
    
    // 设置虚函数表
    switch (type) {
        case KV_ENGINE_ARRAY:
            engine->vtable = &array_vtable;
            break;
        case KV_ENGINE_RBTREE:
            engine->vtable = &rbtree_vtable;
            break;
        case KV_ENGINE_HASH:
            engine->vtable = &hash_vtable;
            break;
        case KV_ENGINE_BTREE:
            engine->vtable = &btree_vtable;
            break;
        case KV_ENGINE_LSM:
            // 这些引擎待实现
            kv_store_free(engine);
            KV_ERROR(KV_ERR_NOT_SUPPORTED, "Engine type %s not implemented yet", 
                    kv_engine_type_to_string(type));
            return NULL;
        default:
            kv_store_free(engine);
            KV_ERROR(KV_ERR_PARAM, "Unknown engine type: %d", type);
            return NULL;
    }
    
    // 初始化引擎
    if (engine->vtable->init && engine->vtable->init(engine, &engine->config) != KV_ERR_NONE) {
        pthread_rwlock_destroy(&engine->lock);
        kv_store_free(engine);
        return NULL;
    }
    
    KV_LOG_INFO_MSG("Created engine: %s", engine->name);
    return engine;
}

int kv_engine_destroy(kv_engine_t *engine) {
    if (!engine) return KV_ERR_PARAM;
    
    KV_LOG_INFO_MSG("Destroying engine: %s", engine->name);
    
    int ret = KV_ERR_NONE;
    if (engine->vtable && engine->vtable->destroy) {
        ret = engine->vtable->destroy(engine);
    }
    
    pthread_rwlock_destroy(&engine->lock);
    kv_store_free(engine);
    
    return ret;
}

// === 工具函数实现 ===
const char* kv_engine_type_to_string(kv_engine_type_t type) {
    if (type >= KV_ENGINE_MAX) {
        return engine_type_names[KV_ENGINE_MAX];
    }
    return engine_type_names[type];
}

const char* kv_engine_state_to_string(kv_engine_state_t state) {
    if (state > KV_ENGINE_STATE_SHUTDOWN) {
        return "Unknown";
    }
    return engine_state_names[state];
}

// === 键值对操作工具函数 ===
kv_pair_t* kv_pair_create(const char *key, const char *value) {
    if (!key || !value) return NULL;
    
    kv_pair_t *pair = (kv_pair_t*)kv_store_malloc(sizeof(kv_pair_t));
    if (!pair) return NULL;
    
    memset(pair, 0, sizeof(kv_pair_t));
    
    pair->key_len = strlen(key);
    pair->value_len = strlen(value);
    
    pair->key = (char*)kv_store_malloc(pair->key_len + 1);
    pair->value = (char*)kv_store_malloc(pair->value_len + 1);
    
    if (!pair->key || !pair->value) {
        if (pair->key) kv_store_free(pair->key);
        if (pair->value) kv_store_free(pair->value);
        kv_store_free(pair);
        return NULL;
    }
    
    strncpy(pair->key, key, pair->key_len + 1);
    strncpy(pair->value, value, pair->value_len + 1);
    
    return pair;
}

void kv_pair_destroy(kv_pair_t *pair) {
    if (!pair) return;
    
    if (pair->key) kv_store_free(pair->key);
    if (pair->value) kv_store_free(pair->value);
    kv_store_free(pair);
}

kv_pair_t* kv_pair_copy(const kv_pair_t *src) {
    if (!src) return NULL;
    
    return kv_pair_create(src->key, src->value);
}

// === 批量操作工具函数 ===
kv_batch_t* kv_batch_create(size_t capacity) {
    if (capacity == 0) capacity = 16; // 默认容量
    
    kv_batch_t *batch = (kv_batch_t*)kv_store_malloc(sizeof(kv_batch_t));
    if (!batch) return NULL;
    
    batch->pairs = (kv_pair_t*)kv_store_malloc(sizeof(kv_pair_t) * capacity);
    if (!batch->pairs) {
        kv_store_free(batch);
        return NULL;
    }
    
    memset(batch->pairs, 0, sizeof(kv_pair_t) * capacity);
    batch->count = 0;
    batch->capacity = capacity;
    
    return batch;
}

void kv_batch_destroy(kv_batch_t *batch) {
    if (!batch) return;
    
    for (size_t i = 0; i < batch->count; i++) {
        if (batch->pairs[i].key) kv_store_free(batch->pairs[i].key);
        if (batch->pairs[i].value) kv_store_free(batch->pairs[i].value);
    }
    
    if (batch->pairs) kv_store_free(batch->pairs);
    kv_store_free(batch);
}

int kv_batch_add(kv_batch_t *batch, const char *key, const char *value) {
    if (!batch || !key || !value) return KV_ERR_PARAM;
    
    if (batch->count >= batch->capacity) {
        // 扩容
        size_t new_capacity = batch->capacity * 2;
        kv_pair_t *new_pairs = (kv_pair_t*)kv_store_malloc(sizeof(kv_pair_t) * new_capacity);
        if (!new_pairs) return KV_ERR_MEM;
        
        memcpy(new_pairs, batch->pairs, sizeof(kv_pair_t) * batch->count);
        memset(new_pairs + batch->count, 0, sizeof(kv_pair_t) * (new_capacity - batch->count));
        
        kv_store_free(batch->pairs);
        batch->pairs = new_pairs;
        batch->capacity = new_capacity;
    }
    
    kv_pair_t *pair = &batch->pairs[batch->count];
    pair->key_len = strlen(key);
    pair->value_len = strlen(value);
    
    pair->key = (char*)kv_store_malloc(pair->key_len + 1);
    pair->value = (char*)kv_store_malloc(pair->value_len + 1);
    
    if (!pair->key || !pair->value) {
        if (pair->key) kv_store_free(pair->key);
        if (pair->value) kv_store_free(pair->value);
        return KV_ERR_MEM;
    }
    
    strncpy(pair->key, key, pair->key_len + 1);
    strncpy(pair->value, value, pair->value_len + 1);
    
    batch->count++;
    return KV_ERR_NONE;
}

void kv_batch_clear(kv_batch_t *batch) {
    if (!batch) return;
    
    for (size_t i = 0; i < batch->count; i++) {
        if (batch->pairs[i].key) {
            kv_store_free(batch->pairs[i].key);
            batch->pairs[i].key = NULL;
        }
        if (batch->pairs[i].value) {
            kv_store_free(batch->pairs[i].value);
            batch->pairs[i].value = NULL;
        }
    }
    
    batch->count = 0;
}

// === 配置管理函数 ===
kv_engine_config_t* kv_engine_config_create(kv_engine_type_t type) {
    kv_engine_config_t *config = (kv_engine_config_t*)kv_store_malloc(sizeof(kv_engine_config_t));
    if (!config) return NULL;
    
    memset(config, 0, sizeof(kv_engine_config_t));
    config->type = type;
    
    // 设置默认值
    config->memory_limit = 64 * 1024 * 1024;  // 64MB
    config->cache_size = 16 * 1024 * 1024;    // 16MB
    config->enable_compression = false;
    config->enable_checksum = true;
    
    // 引擎特定默认值
    switch (type) {
        case KV_ENGINE_LSM:
            config->memtable_size = 16 * 1024 * 1024;  // 16MB
            config->level0_file_limit = 4;
            config->level_size_multiplier = 10.0;
            break;
        case KV_ENGINE_BTREE:
            config->page_size = 4096;  // 4KB
            config->max_keys_per_node = 128;
            break;
        case KV_ENGINE_HASH:
            config->initial_buckets = 1024;
            config->load_factor = 0.75;
            break;
        default:
            break;
    }
    
    return config;
}

void kv_engine_config_destroy(kv_engine_config_t *config) {
    if (!config) return;
    
    if (config->data_dir) {
        kv_store_free(config->data_dir);
    }
    kv_store_free(config);
}

// === 监控接口实现 ===

static int array_init_metrics(kv_engine_t *engine, void *metrics_manager) {
    if (!engine || !metrics_manager) return KV_ERR_PARAM;
    
    engine->metrics_manager = metrics_manager;
    engine->metrics_enabled = true;
    engine->last_metrics_update = get_time_us();
    
    // 注册引擎到监控管理器
    kv_engine_metrics_manager_t *manager = (kv_engine_metrics_manager_t*)metrics_manager;
    return kv_engine_metrics_register_engine(manager, KV_ENGINE_ARRAY, engine->name);
}

static int rbtree_init_metrics(kv_engine_t *engine, void *metrics_manager) {
    if (!engine || !metrics_manager) return KV_ERR_PARAM;
    
    engine->metrics_manager = metrics_manager;
    engine->metrics_enabled = true;
    engine->last_metrics_update = get_time_us();
    
    // 注册引擎到监控管理器
    kv_engine_metrics_manager_t *manager = (kv_engine_metrics_manager_t*)metrics_manager;
    return kv_engine_metrics_register_engine(manager, KV_ENGINE_RBTREE, engine->name);
}

static int hash_init_metrics(kv_engine_t *engine, void *metrics_manager) {
    if (!engine || !metrics_manager) return KV_ERR_PARAM;
    
    engine->metrics_manager = metrics_manager;
    engine->metrics_enabled = true;
    engine->last_metrics_update = get_time_us();
    
    // 注册引擎到监控管理器
    kv_engine_metrics_manager_t *manager = (kv_engine_metrics_manager_t*)metrics_manager;
    return kv_engine_metrics_register_engine(manager, KV_ENGINE_HASH, engine->name);
}

static int btree_init_metrics(kv_engine_t *engine, void *metrics_manager) {
    if (!engine || !metrics_manager) return KV_ERR_PARAM;
    
    engine->metrics_manager = metrics_manager;
    engine->metrics_enabled = true;
    engine->last_metrics_update = get_time_us();
    
    // 注册引擎到监控管理器
    kv_engine_metrics_manager_t *manager = (kv_engine_metrics_manager_t*)metrics_manager;
    return kv_engine_metrics_register_engine(manager, KV_ENGINE_BTREE, engine->name);
} 