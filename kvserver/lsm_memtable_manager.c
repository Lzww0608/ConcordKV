/*
 * @Author: Lzww0608  
 * @Date: 2025-6-1 12:00:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-3 22:22:55
 * @Description: ConcordKV LSM-Tree MemTable管理器实现
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "lsm_memtable_manager.h"
#include "kv_store.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

// 默认配置常量
#define DEFAULT_MEMTABLE_MAX_SIZE    (4 * 1024 * 1024)  // 4MB
#define DEFAULT_MAX_IMMUTABLE_COUNT  5                   // 最多5个不可变MemTable
#define DEFAULT_SEQ_NUM_START        1                   // 起始序列号

// === 内部辅助函数声明 ===
static uint64_t get_timestamp_us(void);
static int create_new_active_memtable(lsm_memtable_manager_t *manager);
static int add_immutable_memtable(lsm_memtable_manager_t *manager, lsm_memtable_t *memtable);
static int remove_immutable_memtable_at(lsm_memtable_manager_t *manager, int index);
static void update_manager_stats(lsm_memtable_manager_t *manager);

// === 配置管理 ===
lsm_memtable_manager_config_t lsm_memtable_manager_default_config(void) {
    lsm_memtable_manager_config_t config;
    config.memtable_max_size = DEFAULT_MEMTABLE_MAX_SIZE;
    config.max_immutable_count = DEFAULT_MAX_IMMUTABLE_COUNT;
    config.auto_freeze = true;
    config.seq_num_start = DEFAULT_SEQ_NUM_START;
    strncpy(config.name_prefix, "memtable", sizeof(config.name_prefix) - 1);
    config.name_prefix[sizeof(config.name_prefix) - 1] = '\0';
    return config;
}

// === 管理器生命周期管理 ===
lsm_memtable_manager_t* lsm_memtable_manager_create(const lsm_memtable_manager_config_t *config,
                                                     const char *name) {
    lsm_memtable_manager_t *manager = kv_store_malloc(sizeof(lsm_memtable_manager_t));
    if (!manager) return NULL;
    
    // 初始化基础字段
    memset(manager, 0, sizeof(lsm_memtable_manager_t));
    
    // 设置配置
    if (config) {
        manager->config = *config;
    } else {
        manager->config = lsm_memtable_manager_default_config();
    }
    
    // 验证配置参数
    if (manager->config.memtable_max_size == 0) {
        manager->config.memtable_max_size = DEFAULT_MEMTABLE_MAX_SIZE;
    }
    if (manager->config.max_immutable_count <= 0) {
        manager->config.max_immutable_count = DEFAULT_MAX_IMMUTABLE_COUNT;
    }
    
    // 设置管理器属性
    manager->max_immutable_count = manager->config.max_immutable_count;
    manager->global_seq_num = manager->config.seq_num_start;
    manager->creation_time = get_timestamp_us();
    
    // 设置名称
    if (name) {
        strncpy(manager->name, name, sizeof(manager->name) - 1);
        manager->name[sizeof(manager->name) - 1] = '\0';
    } else {
        snprintf(manager->name, sizeof(manager->name), "manager_%lu", manager->creation_time);
    }
    
    // 分配不可变MemTable数组
    manager->immutable = kv_store_malloc(sizeof(lsm_memtable_t*) * manager->max_immutable_count);
    if (!manager->immutable) {
        kv_store_free(manager);
        return NULL;
    }
    memset(manager->immutable, 0, sizeof(lsm_memtable_t*) * manager->max_immutable_count);
    
    // 初始化读写锁
    if (pthread_rwlock_init(&manager->lock, NULL) != 0) {
        kv_store_free(manager->immutable);
        kv_store_free(manager);
        return NULL;
    }
    
    // 创建初始活跃MemTable
    if (create_new_active_memtable(manager) != KV_ERR_NONE) {
        pthread_rwlock_destroy(&manager->lock);
        kv_store_free(manager->immutable);
        kv_store_free(manager);
        return NULL;
    }
    
    // 初始化统计信息
    memset(&manager->stats, 0, sizeof(manager->stats));
    manager->stats.total_memtables = 1;  // 一个活跃MemTable
    manager->stats.active_memtables = 1;
    
    return manager;
}

void lsm_memtable_manager_destroy(lsm_memtable_manager_t *manager) {
    if (!manager) return;
    
    // 获取写锁
    pthread_rwlock_wrlock(&manager->lock);
    
    // 销毁活跃MemTable
    if (manager->active) {
        lsm_memtable_destroy(manager->active);
    }
    
    // 销毁所有不可变MemTable
    for (int i = 0; i < manager->immutable_count; i++) {
        if (manager->immutable[i]) {
            lsm_memtable_destroy(manager->immutable[i]);
        }
    }
    
    // 释放数组
    kv_store_free(manager->immutable);
    
    // 释放锁并销毁
    pthread_rwlock_unlock(&manager->lock);
    pthread_rwlock_destroy(&manager->lock);
    
    // 销毁管理器
    kv_store_free(manager);
}

// === 基础数据操作 ===
int lsm_memtable_manager_put(lsm_memtable_manager_t *manager, 
                              const char *key, size_t key_len,
                              const char *value, size_t value_len) {
    if (!manager || !key || key_len == 0) return KV_ERR_PARAM;
    
    pthread_rwlock_wrlock(&manager->lock);
    
    // 检查是否需要自动冻结
    if (manager->config.auto_freeze && lsm_memtable_is_full(manager->active)) {
        int ret = lsm_memtable_freeze(manager->active);
        if (ret != KV_ERR_NONE) {
            pthread_rwlock_unlock(&manager->lock);
            return ret;
        }
        
        // 添加到不可变列表
        ret = add_immutable_memtable(manager, manager->active);
        if (ret != KV_ERR_NONE) {
            pthread_rwlock_unlock(&manager->lock);
            return ret;
        }
        
        // 创建新的活跃MemTable
        ret = create_new_active_memtable(manager);
        if (ret != KV_ERR_NONE) {
            pthread_rwlock_unlock(&manager->lock);
            return ret;
        }
        
        manager->stats.freeze_count++;
        manager->stats.switch_count++;
    }
    
    // 插入到活跃MemTable
    int ret = lsm_memtable_put(manager->active, key, key_len, value, value_len);
    if (ret == KV_ERR_NONE) {
        manager->stats.write_count++;
        update_manager_stats(manager);
    }
    
    pthread_rwlock_unlock(&manager->lock);
    return ret;
}

int lsm_memtable_manager_get(lsm_memtable_manager_t *manager,
                              const char *key, size_t key_len,
                              lsm_memtable_lookup_result_t *result) {
    if (!manager || !key || key_len == 0 || !result) return KV_ERR_PARAM;
    
    // 初始化结果
    lsm_memtable_lookup_result_init(result);
    
    pthread_rwlock_rdlock(&manager->lock);
    
    char *value = NULL;
    size_t value_len_found = 0;
    uint64_t seq_num = 0;
    bool deleted = false;
    uint64_t max_seq_num = 0;
    bool found_any = false;
    lsm_memtable_t *source = NULL;
    
    // 1. 先查找活跃MemTable
    int ret = lsm_memtable_get(manager->active, key, key_len, 
                               &value, &value_len_found, &seq_num, &deleted);
    if (ret == KV_ERR_NONE) {
        max_seq_num = seq_num;
        found_any = true;
        source = manager->active;
        
        result->value = value;
        result->value_len = value_len_found;
        result->seq_num = seq_num;
        result->deleted = deleted;
        result->source_memtable = source;
        result->found = true;
    }
    
    // 2. 按时间倒序查找不可变MemTable（寻找更新的版本）
    for (int i = manager->immutable_count - 1; i >= 0; i--) {
        char *imm_value = NULL;
        size_t imm_value_len = 0;
        uint64_t imm_seq_num = 0;
        bool imm_deleted = false;
        
        int imm_ret = lsm_memtable_get(manager->immutable[i], key, key_len,
                                       &imm_value, &imm_value_len, &imm_seq_num, &imm_deleted);
        
        if (imm_ret == KV_ERR_NONE) {
            // 如果序列号更大，说明这是更新的版本
            if (!found_any || imm_seq_num > max_seq_num) {
                // 清理之前的结果
                if (found_any && result->value) {
                    kv_store_free(result->value);
                }
                
                max_seq_num = imm_seq_num;
                found_any = true;
                source = manager->immutable[i];
                
                result->value = imm_value;
                result->value_len = imm_value_len;
                result->seq_num = imm_seq_num;
                result->deleted = imm_deleted;
                result->source_memtable = source;
                result->found = true;
            } else {
                // 释放不需要的值
                if (imm_value) {
                    kv_store_free(imm_value);
                }
            }
        }
    }
    
    manager->stats.read_count++;
    pthread_rwlock_unlock(&manager->lock);
    
    return found_any ? KV_ERR_NONE : KV_ERR_NOT_FOUND;
}

int lsm_memtable_manager_delete(lsm_memtable_manager_t *manager,
                                 const char *key, size_t key_len) {
    if (!manager || !key || key_len == 0) return KV_ERR_PARAM;
    
    pthread_rwlock_wrlock(&manager->lock);
    
    // 检查是否需要自动冻结
    if (manager->config.auto_freeze && lsm_memtable_is_full(manager->active)) {
        int ret = lsm_memtable_freeze(manager->active);
        if (ret != KV_ERR_NONE) {
            pthread_rwlock_unlock(&manager->lock);
            return ret;
        }
        
        ret = add_immutable_memtable(manager, manager->active);
        if (ret != KV_ERR_NONE) {
            pthread_rwlock_unlock(&manager->lock);
            return ret;
        }
        
        ret = create_new_active_memtable(manager);
        if (ret != KV_ERR_NONE) {
            pthread_rwlock_unlock(&manager->lock);
            return ret;
        }
        
        manager->stats.freeze_count++;
        manager->stats.switch_count++;
    }
    
    // 在活跃MemTable中设置墓碑标记
    int ret = lsm_memtable_delete(manager->active, key, key_len);
    if (ret == KV_ERR_NONE) {
        manager->stats.delete_count++;
        update_manager_stats(manager);
    }
    
    pthread_rwlock_unlock(&manager->lock);
    return ret;
}

// === MemTable管理操作 ===
int lsm_memtable_manager_freeze_active(lsm_memtable_manager_t *manager) {
    if (!manager) return KV_ERR_PARAM;
    
    pthread_rwlock_wrlock(&manager->lock);
    
    if (!manager->active) {
        pthread_rwlock_unlock(&manager->lock);
        return KV_ERR_PARAM;
    }
    
    // 冻结当前活跃MemTable
    int ret = lsm_memtable_freeze(manager->active);
    if (ret != KV_ERR_NONE) {
        pthread_rwlock_unlock(&manager->lock);
        return ret;
    }
    
    // 添加到不可变列表
    ret = add_immutable_memtable(manager, manager->active);
    if (ret != KV_ERR_NONE) {
        pthread_rwlock_unlock(&manager->lock);
        return ret;
    }
    
    // 创建新的活跃MemTable
    ret = create_new_active_memtable(manager);
    if (ret != KV_ERR_NONE) {
        pthread_rwlock_unlock(&manager->lock);
        return ret;
    }
    
    manager->stats.freeze_count++;
    manager->stats.switch_count++;
    update_manager_stats(manager);
    
    pthread_rwlock_unlock(&manager->lock);
    return KV_ERR_NONE;
}

lsm_memtable_t* lsm_memtable_manager_get_oldest_immutable(lsm_memtable_manager_t *manager) {
    if (!manager) return NULL;
    
    pthread_rwlock_rdlock(&manager->lock);
    
    lsm_memtable_t *oldest = NULL;
    if (manager->immutable_count > 0) {
        oldest = manager->immutable[0];  // 最老的在数组开头
    }
    
    pthread_rwlock_unlock(&manager->lock);
    return oldest;
}

int lsm_memtable_manager_remove_oldest_immutable(lsm_memtable_manager_t *manager) {
    if (!manager) return KV_ERR_PARAM;
    
    pthread_rwlock_wrlock(&manager->lock);
    
    if (manager->immutable_count <= 0) {
        pthread_rwlock_unlock(&manager->lock);
        return KV_ERR_NOT_FOUND;
    }
    
    // 最老的在索引0
    lsm_memtable_t *oldest = manager->immutable[0];
    int ret = remove_immutable_memtable_at(manager, 0);
    
    if (ret == KV_ERR_NONE) {
        lsm_memtable_destroy(oldest);
        manager->stats.flush_count++;
        update_manager_stats(manager);
    }
    
    pthread_rwlock_unlock(&manager->lock);
    return ret;
}

int lsm_memtable_manager_remove_specific_immutable(lsm_memtable_manager_t *manager,
                                                    lsm_memtable_t *memtable) {
    if (!manager || !memtable) return KV_ERR_PARAM;
    
    pthread_rwlock_wrlock(&manager->lock);
    
    // 查找指定的MemTable
    int found_index = -1;
    for (int i = 0; i < manager->immutable_count; i++) {
        if (manager->immutable[i] == memtable) {
            found_index = i;
            break;
        }
    }
    
    if (found_index < 0) {
        pthread_rwlock_unlock(&manager->lock);
        return KV_ERR_NOT_FOUND;  // MemTable可能已被其他线程移除
    }
    
    // 移除指定的MemTable
    int ret = remove_immutable_memtable_at(manager, found_index);
    
    if (ret == KV_ERR_NONE) {
        // 注意：这里不销毁MemTable，因为它可能仍被压缩任务使用
        // 压缩任务完成后会自行处理MemTable的生命周期
        manager->stats.flush_count++;
        update_manager_stats(manager);
    }
    
    pthread_rwlock_unlock(&manager->lock);
    return ret;
}

int lsm_memtable_manager_switch_active(lsm_memtable_manager_t *manager) {
    if (!manager) return KV_ERR_PARAM;
    
    pthread_rwlock_wrlock(&manager->lock);
    
    // 如果当前活跃MemTable不为空，先冻结它
    if (manager->active && !lsm_memtable_is_empty(manager->active)) {
        int ret = lsm_memtable_freeze(manager->active);
        if (ret != KV_ERR_NONE) {
            pthread_rwlock_unlock(&manager->lock);
            return ret;
        }
        
        ret = add_immutable_memtable(manager, manager->active);
        if (ret != KV_ERR_NONE) {
            pthread_rwlock_unlock(&manager->lock);
            return ret;
        }
        
        manager->stats.freeze_count++;
    } else if (manager->active) {
        // 如果MemTable为空，直接销毁
        lsm_memtable_destroy(manager->active);
    }
    
    // 创建新的活跃MemTable
    int ret = create_new_active_memtable(manager);
    
    manager->stats.switch_count++;
    update_manager_stats(manager);
    
    pthread_rwlock_unlock(&manager->lock);
    return ret;
}

// === 状态查询 ===
bool lsm_memtable_manager_should_freeze(lsm_memtable_manager_t *manager) {
    if (!manager || !manager->active) return false;
    
    pthread_rwlock_rdlock(&manager->lock);
    bool should_freeze = lsm_memtable_is_full(manager->active);
    pthread_rwlock_unlock(&manager->lock);
    
    return should_freeze;
}

bool lsm_memtable_manager_should_flush(lsm_memtable_manager_t *manager) {
    if (!manager) return false;
    
    pthread_rwlock_rdlock(&manager->lock);
    bool should_flush = (manager->immutable_count >= manager->max_immutable_count / 2);
    pthread_rwlock_unlock(&manager->lock);
    
    return should_flush;
}

int lsm_memtable_manager_get_stats(lsm_memtable_manager_t *manager,
                                    lsm_memtable_manager_stats_t *stats) {
    if (!manager || !stats) return KV_ERR_PARAM;
    
    pthread_rwlock_rdlock(&manager->lock);
    update_manager_stats(manager);
    *stats = manager->stats;
    pthread_rwlock_unlock(&manager->lock);
    
    return KV_ERR_NONE;
}

int lsm_memtable_manager_get_active_count(lsm_memtable_manager_t *manager) {
    if (!manager) return 0;
    
    pthread_rwlock_rdlock(&manager->lock);
    int count = manager->active ? 1 : 0;
    pthread_rwlock_unlock(&manager->lock);
    
    return count;
}

int lsm_memtable_manager_get_immutable_count(lsm_memtable_manager_t *manager) {
    if (!manager) return 0;
    
    pthread_rwlock_rdlock(&manager->lock);
    int count = manager->immutable_count;
    pthread_rwlock_unlock(&manager->lock);
    
    return count;
}

// === 辅助函数 ===
void lsm_memtable_lookup_result_cleanup(lsm_memtable_lookup_result_t *result) {
    if (!result) return;
    
    if (result->value) {
        kv_store_free(result->value);
        result->value = NULL;
    }
    
    result->value_len = 0;
    result->seq_num = 0;
    result->deleted = false;
    result->found = false;
    result->source_memtable = NULL;
}

void lsm_memtable_lookup_result_init(lsm_memtable_lookup_result_t *result) {
    if (!result) return;
    
    memset(result, 0, sizeof(lsm_memtable_lookup_result_t));
}

void lsm_memtable_manager_print_status(lsm_memtable_manager_t *manager) {
    if (!manager) return;
    
    pthread_rwlock_rdlock(&manager->lock);
    
    printf("=== MemTable管理器状态 [%s] ===\n", manager->name);
    printf("活跃MemTable: %s\n", manager->active ? "存在" : "无");
    if (manager->active) {
        printf("  - 大小: %zu 字节\n", lsm_memtable_size(manager->active));
        printf("  - 条目: %lu\n", lsm_memtable_count(manager->active));
        printf("  - 已满: %s\n", lsm_memtable_is_full(manager->active) ? "是" : "否");
    }
    
    printf("不可变MemTable: %d/%d\n", manager->immutable_count, manager->max_immutable_count);
    for (int i = 0; i < manager->immutable_count; i++) {
        printf("  [%d] 大小: %zu, 条目: %lu\n", i,
               lsm_memtable_size(manager->immutable[i]),
               lsm_memtable_count(manager->immutable[i]));
    }
    
    printf("全局序列号: %lu\n", manager->global_seq_num);
    printf("统计信息:\n");
    printf("  - 总MemTable: %lu\n", manager->stats.total_memtables);
    printf("  - 写操作: %lu\n", manager->stats.write_count);
    printf("  - 读操作: %lu\n", manager->stats.read_count);
    printf("  - 删除操作: %lu\n", manager->stats.delete_count);
    printf("  - 冻结次数: %lu\n", manager->stats.freeze_count);
    printf("  - 切换次数: %lu\n", manager->stats.switch_count);
    printf("  - 刷写次数: %lu\n", manager->stats.flush_count);
    
    pthread_rwlock_unlock(&manager->lock);
}

// === 内部辅助函数实现 ===
static uint64_t get_timestamp_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static int create_new_active_memtable(lsm_memtable_manager_t *manager) {
    char memtable_name[128];
    snprintf(memtable_name, sizeof(memtable_name), "%s_active_%lu",
             manager->config.name_prefix, get_timestamp_us());
    
    manager->active = lsm_memtable_create(manager->config.memtable_max_size, memtable_name);
    if (!manager->active) {
        return KV_ERR_MEM;
    }
    
    return KV_ERR_NONE;
}

static int add_immutable_memtable(lsm_memtable_manager_t *manager, lsm_memtable_t *memtable) {
    if (manager->immutable_count >= manager->max_immutable_count) {
        return KV_ERR_MEM;
    }
    
    // 添加到数组末尾（最新的）
    manager->immutable[manager->immutable_count] = memtable;
    manager->immutable_count++;
    
    return KV_ERR_NONE;
}

static int remove_immutable_memtable_at(lsm_memtable_manager_t *manager, int index) {
    if (index < 0 || index >= manager->immutable_count) {
        return KV_ERR_PARAM;
    }
    
    // 移动数组元素
    for (int i = index; i < manager->immutable_count - 1; i++) {
        manager->immutable[i] = manager->immutable[i + 1];
    }
    
    manager->immutable_count--;
    manager->immutable[manager->immutable_count] = NULL;
    
    return KV_ERR_NONE;
}

static void update_manager_stats(lsm_memtable_manager_t *manager) {
    manager->stats.active_memtables = manager->active ? 1 : 0;
    manager->stats.immutable_memtables = manager->immutable_count;
    manager->stats.total_memtables = manager->stats.active_memtables + manager->stats.immutable_memtables;
    
    // 统计总大小和条目数
    uint64_t total_entries = 0;
    uint64_t total_size = 0;
    uint64_t total_memory = 0;
    
    if (manager->active) {
        total_entries += lsm_memtable_count(manager->active);
        total_size += lsm_memtable_size(manager->active);
        total_memory += lsm_memtable_size(manager->active);  // 简化计算
    }
    
    for (int i = 0; i < manager->immutable_count; i++) {
        total_entries += lsm_memtable_count(manager->immutable[i]);
        total_size += lsm_memtable_size(manager->immutable[i]);
        total_memory += lsm_memtable_size(manager->immutable[i]);
    }
    
    manager->stats.total_entries = total_entries;
    manager->stats.total_size = total_size;
    manager->stats.total_memory_usage = total_memory;
}

// === 兼容性函数实现 ===

int lsm_memtable_manager_flush_all(lsm_memtable_manager_t *manager) {
    if (!manager) return KV_ERR_INVALID_PARAM;
    
    // 冻结当前活跃MemTable
    int ret = lsm_memtable_manager_freeze_active(manager);
    if (ret != KV_ERR_NONE) {
        return ret;
    }
    
    // 这里可以添加实际的刷写逻辑
    // 目前只是简单地冻结活跃MemTable
    
    return KV_ERR_NONE;
}

lsm_memtable_manager_t* lsm_memtable_manager_create_compat(size_t memtable_size, int max_immutable_count) {
    lsm_memtable_manager_config_t config = lsm_memtable_manager_default_config();
    config.memtable_max_size = memtable_size;
    config.max_immutable_count = max_immutable_count;
    
    return lsm_memtable_manager_create(&config, "compat");
}

int lsm_memtable_manager_get_compat(lsm_memtable_manager_t *manager,
                                    const char *key, size_t key_len,
                                    char **value, size_t *value_len,
                                    uint64_t *seq_num, bool *deleted) {
    if (!manager || !key || !value || !value_len || !seq_num || !deleted) {
        return KV_ERR_INVALID_PARAM;
    }
    
    lsm_memtable_lookup_result_t result;
    lsm_memtable_lookup_result_init(&result);
    
    int ret = lsm_memtable_manager_get(manager, key, key_len, &result);
    
    if (ret == KV_ERR_NONE && result.found) {
        *value = result.value;
        *value_len = result.value_len;
        *seq_num = result.seq_num;
        *deleted = result.deleted;
        
        // 不要清理result，因为value指针被传递给调用者
        result.value = NULL; // 防止被清理
    } else {
        *value = NULL;
        *value_len = 0;
        *seq_num = 0;
        *deleted = false;
    }
    
    lsm_memtable_lookup_result_cleanup(&result);
    return ret;
}

int lsm_memtable_manager_put_compat(lsm_memtable_manager_t *manager,
                                    const char *key, size_t key_len,
                                    const char *value, size_t value_len) {
    return lsm_memtable_manager_put(manager, key, key_len, value, value_len);
}

int lsm_memtable_manager_delete_compat(lsm_memtable_manager_t *manager,
                                       const char *key, size_t key_len) {
    return lsm_memtable_manager_delete(manager, key, key_len);
} 