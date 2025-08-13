/*
 * @Author: Lzww0608  
 * @Date: 2025-6-18 21:45:59
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-18 21:46:01
 * @Description: ConcordKV 统一范围查询和前缀扫描实现
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "kv_range_query.h"
#include "kv_error.h"
#include "kv_memory.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <ctype.h>

// === 内部工具函数 ===

/**
 * 获取当前时间戳(毫秒)
 */
static double get_current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)(tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0);
}

/**
 * 字符串复制(安全版本)
 */
static char* safe_strdup(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char *copy = kv_malloc(len + 1);
    if (copy) {
        memcpy(copy, str, len + 1);
    }
    return copy;
}

/**
 * 检查超时
 */
static bool check_timeout(double start_time, int32_t timeout_ms) {
    if (timeout_ms <= 0) return false;
    double elapsed = get_current_time_ms() - start_time;
    return elapsed >= timeout_ms;
}

// === 核心API实现 ===

/**
 * 创建范围查询管理器
 */
kv_range_manager_t* kv_range_manager_create(kv_engine_t *engine) {
    if (!engine) {
        return NULL;
    }
    
    kv_range_manager_t *manager = kv_malloc(sizeof(kv_range_manager_t));
    if (!manager) {
        return NULL;
    }
    
    // 初始化结构
    memset(manager, 0, sizeof(kv_range_manager_t));
    manager->engine = engine;
    
    // 设置默认前缀配置
    manager->prefix_config.case_sensitive = true;
    manager->prefix_config.max_prefix_len = 256;
    manager->prefix_config.use_bloom_filter = false;
    
    // 初始化锁
    if (pthread_rwlock_init(&manager->lock, NULL) != 0) {
        kv_free(manager);
        return NULL;
    }
    
    manager->initialized = true;
    
    return manager;
}

/**
 * 销毁范围查询管理器
 */
void kv_range_manager_destroy(kv_range_manager_t *manager) {
    if (!manager) return;
    
    pthread_rwlock_destroy(&manager->lock);
    kv_free(manager);
}

/**
 * 创建默认查询选项
 */
kv_range_options_t kv_range_options_default(void) {
    kv_range_options_t options = {0};
    options.limit = 1000;           // 默认返回1000个结果
    options.reverse = false;
    options.keys_only = false;
    options.count_only = false;
    options.offset = 0;
    options.timeout_ms = 5000;      // 默认5秒超时
    return options;
}

/**
 * 创建前缀配置
 */
kv_prefix_config_t kv_prefix_config_default(void) {
    kv_prefix_config_t config = {0};
    config.case_sensitive = true;
    config.max_prefix_len = 256;
    config.use_bloom_filter = false;
    return config;
}

/**
 * 释放查询结果
 */
void kv_range_result_destroy(kv_range_result_t *result) {
    if (!result) return;
    
    if (result->pairs) {
        for (size_t i = 0; i < result->count; i++) {
            kv_free(result->pairs[i].key);
            kv_free(result->pairs[i].value);
        }
        kv_free(result->pairs);
    }
    
    if (result->next_start_key) {
        kv_free(result->next_start_key);
    }
    
    kv_free(result);
}

/**
 * 检查键是否匹配前缀
 */
bool kv_key_match_prefix(const char *key, const char *prefix, bool case_sensitive) {
    if (!key || !prefix) return false;
    
    size_t prefix_len = strlen(prefix);
    size_t key_len = strlen(key);
    
    if (key_len < prefix_len) return false;
    
    if (case_sensitive) {
        return strncmp(key, prefix, prefix_len) == 0;
    } else {
        return strncasecmp(key, prefix, prefix_len) == 0;
    }
}

/**
 * 比较键的大小关系
 */
int kv_key_compare(const char *key1, const char *key2) {
    if (!key1 && !key2) return 0;
    if (!key1) return -1;
    if (!key2) return 1;
    return strcmp(key1, key2);
}

/**
 * 生成前缀的下一个键
 */
char* kv_prefix_next_key(const char *prefix) {
    if (!prefix) return NULL;
    
    size_t len = strlen(prefix);
    if (len == 0) return NULL;
    
    char *next_key = kv_malloc(len + 1);
    if (!next_key) return NULL;
    
    memcpy(next_key, prefix, len + 1);
    
    // 从后往前找到第一个可以递增的字符
    for (int i = len - 1; i >= 0; i--) {
        if ((unsigned char)next_key[i] < 255) {
            next_key[i]++;
            break;
        } else {
            next_key[i] = 0;
            if (i == 0) {
                // 所有字符都是255，无法生成下一个键
                kv_free(next_key);
                return NULL;
            }
        }
    }
    
    return next_key;
}

/**
 * 使用迭代器进行范围扫描的通用实现
 */
static kv_range_result_t* range_scan_with_iterator(kv_range_manager_t *manager,
                                                  const char *start_key,
                                                  const char *end_key,
                                                  bool include_start,
                                                  bool include_end,
                                                  const kv_range_options_t *options) {
    if (!manager || !manager->engine || !options) {
        return NULL;
    }
    
    double start_time = get_current_time_ms();
    
    kv_range_result_t *result = kv_malloc(sizeof(kv_range_result_t));
    if (!result) return NULL;
    
    memset(result, 0, sizeof(kv_range_result_t));
    
    // 分配结果数组
    size_t initial_capacity = options->limit > 0 ? options->limit : 1000;
    result->pairs = kv_malloc(sizeof(kv_pair_t) * initial_capacity);
    if (!result->pairs) {
        kv_free(result);
        return NULL;
    }
    
    // 创建迭代器
    kv_iterator_t *iter = NULL;
    if (manager->engine->vtable->create_iterator) {
        iter = manager->engine->vtable->create_iterator(manager->engine);
    }
    
    if (!iter) {
        // 迭代器不可用，尝试使用引擎自己的范围查询实现
        kv_free(result->pairs);
        kv_free(result);
        return NULL;
    }
    
    // 定位到起始位置
    if (start_key) {
        iter->seek(iter, start_key);
    }
    
    size_t scanned = 0;
    size_t offset_count = 0;
    
    while (iter->valid && result->count < initial_capacity) {
        // 检查超时
        if (check_timeout(start_time, options->timeout_ms)) {
            break;
        }
        
        const char *current_key = iter->current.key;
        const char *current_value = iter->current.value;
        
        if (!current_key) {
            iter->next(iter);
            continue;
        }
        
        scanned++;
        
        // 检查是否超出结束边界
        if (end_key) {
            int cmp = kv_key_compare(current_key, end_key);
            if (options->reverse) {
                if (cmp < 0) break;
                if (cmp == 0 && !include_end) break;
            } else {
                if (cmp > 0) break;
                if (cmp == 0 && !include_end) break;
            }
        }
        
        // 检查起始边界
        if (start_key) {
            int cmp = kv_key_compare(current_key, start_key);
            if (cmp == 0 && !include_start) {
                iter->next(iter);
                continue;
            }
        }
        
        // 处理偏移量
        if (offset_count < options->offset) {
            offset_count++;
            iter->next(iter);
            continue;
        }
        
        // 添加到结果中
        result->pairs[result->count].key = safe_strdup(current_key);
        result->pairs[result->count].key_len = strlen(current_key);
        
        if (!options->keys_only && current_value) {
            result->pairs[result->count].value = safe_strdup(current_value);
            result->pairs[result->count].value_len = strlen(current_value);
        } else {
            result->pairs[result->count].value = NULL;
            result->pairs[result->count].value_len = 0;
        }
        
        result->count++;
        
        // 移动到下一个元素
        if (options->reverse) {
            iter->prev(iter);
        } else {
            iter->next(iter);
        }
    }
    
    // 检查是否还有更多结果
    result->has_more = iter->valid;
    if (result->has_more && result->count > 0) {
        // 保存下一次查询的起始键
        const char *last_key = result->pairs[result->count - 1].key;
        result->next_start_key = safe_strdup(last_key);
    }
    
    result->total_scanned = scanned;
    result->elapsed_ms = get_current_time_ms() - start_time;
    
    // 清理迭代器
    if (iter->destroy) {
        iter->destroy(iter);
    }
    
    // 更新统计信息
    pthread_rwlock_wrlock(&manager->lock);
    manager->stats.total_range_queries++;
    manager->stats.total_results_returned += result->count;
    // 计算移动平均值
    double total_time = manager->stats.avg_query_time_ms * (manager->stats.total_range_queries - 1) + result->elapsed_ms;
    manager->stats.avg_query_time_ms = total_time / manager->stats.total_range_queries;
    pthread_rwlock_unlock(&manager->lock);
    
    return result;
}

/**
 * 范围查询实现
 */
kv_range_result_t* kv_range_scan(kv_range_manager_t *manager,
                                const char *start_key, 
                                const char *end_key,
                                bool include_start, 
                                bool include_end,
                                const kv_range_options_t *options) {
    if (!manager || !options) {
        return NULL;
    }
    
    // 如果引擎支持原生范围查询，优先使用
    if (manager->engine->vtable->range_scan) {
        kv_range_t range = {0};
        
        if (start_key) {
            range.start_key = (char*)start_key;
            range.start_len = strlen(start_key);
        }
        if (end_key) {
            range.end_key = (char*)end_key;
            range.end_len = strlen(end_key);
        }
        
        range.start_inclusive = include_start;
        range.end_inclusive = include_end;
        range.limit = options->limit;
        
        kv_batch_t batch = {0};
        int ret = manager->engine->vtable->range_scan(manager->engine, &range, &batch);
        if (ret == KV_ERR_NONE && batch.pairs) {
            // 转换为range_result格式
            kv_range_result_t *result = kv_malloc(sizeof(kv_range_result_t));
            if (result) {
                memset(result, 0, sizeof(kv_range_result_t));
                result->pairs = batch.pairs;
                result->count = batch.count;
                result->total_scanned = batch.count;
                result->has_more = false; // 原生实现通常不提供分页信息
                return result;
            }
        }
    }
    
    // 使用迭代器实现
    return range_scan_with_iterator(manager, start_key, end_key, 
                                   include_start, include_end, options);
}

/**
 * 前缀查询实现
 */
kv_range_result_t* kv_prefix_scan(kv_range_manager_t *manager,
                                 const char *prefix,
                                 const kv_range_options_t *options) {
    if (!manager || !prefix || !options) {
        return NULL;
    }
    
    double start_time = get_current_time_ms();
    
    // 如果引擎支持原生前缀查询，优先使用
    if (manager->engine->vtable->prefix_scan) {
        kv_batch_t batch = {0};
        int ret = manager->engine->vtable->prefix_scan(manager->engine, prefix, &batch);
        if (ret == KV_ERR_NONE && batch.pairs) {
            kv_range_result_t *result = kv_malloc(sizeof(kv_range_result_t));
            if (result) {
                memset(result, 0, sizeof(kv_range_result_t));
                result->pairs = batch.pairs;
                result->count = batch.count;
                result->total_scanned = batch.count;
                result->has_more = false;
                result->elapsed_ms = get_current_time_ms() - start_time;
                
                // 更新统计信息
                pthread_rwlock_wrlock(&manager->lock);
                manager->stats.total_prefix_queries++;
                manager->stats.total_results_returned += result->count;
                pthread_rwlock_unlock(&manager->lock);
                
                return result;
            }
        }
    }
    
    // 使用范围查询实现前缀扫描
    char *next_key = kv_prefix_next_key(prefix);
    kv_range_result_t *result = kv_range_scan(manager, prefix, next_key, 
                                             true, false, options);
    
    if (next_key) {
        kv_free(next_key);
    }
    
    if (result) {
        // 过滤结果，确保都匹配前缀
        size_t filtered_count = 0;
        for (size_t i = 0; i < result->count; i++) {
            if (kv_key_match_prefix(result->pairs[i].key, prefix, 
                                   manager->prefix_config.case_sensitive)) {
                if (filtered_count != i) {
                    // 移动到正确位置
                    result->pairs[filtered_count] = result->pairs[i];
                    memset(&result->pairs[i], 0, sizeof(kv_pair_t));
                }
                filtered_count++;
            } else {
                // 释放不匹配的项
                kv_free(result->pairs[i].key);
                kv_free(result->pairs[i].value);
                memset(&result->pairs[i], 0, sizeof(kv_pair_t));
            }
        }
        result->count = filtered_count;
        
        // 更新统计信息
        pthread_rwlock_wrlock(&manager->lock);
        manager->stats.total_prefix_queries++;
        pthread_rwlock_unlock(&manager->lock);
    }
    
    return result;
}

/**
 * 批量前缀查询
 */
kv_range_result_t* kv_multi_prefix_scan(kv_range_manager_t *manager,
                                       const char **prefixes,
                                       size_t prefix_count,
                                       const kv_range_options_t *options) {
    if (!manager || !prefixes || prefix_count == 0 || !options) {
        return NULL;
    }
    
    kv_range_result_t *combined_result = kv_malloc(sizeof(kv_range_result_t));
    if (!combined_result) return NULL;
    
    memset(combined_result, 0, sizeof(kv_range_result_t));
    
    size_t total_capacity = options->limit > 0 ? options->limit : 1000;
    combined_result->pairs = kv_malloc(sizeof(kv_pair_t) * total_capacity);
    if (!combined_result->pairs) {
        kv_free(combined_result);
        return NULL;
    }
    
    double start_time = get_current_time_ms();
    
    // 为每个前缀创建单独的查询选项
    kv_range_options_t single_options = *options;
    single_options.limit = total_capacity / prefix_count; // 平均分配限制
    
    for (size_t i = 0; i < prefix_count && combined_result->count < total_capacity; i++) {
        kv_range_result_t *single_result = kv_prefix_scan(manager, prefixes[i], &single_options);
        if (single_result) {
            // 合并结果
            size_t copy_count = single_result->count;
            if (combined_result->count + copy_count > total_capacity) {
                copy_count = total_capacity - combined_result->count;
            }
            
            for (size_t j = 0; j < copy_count; j++) {
                combined_result->pairs[combined_result->count + j] = single_result->pairs[j];
                // 清空原结果中的指针，避免重复释放
                memset(&single_result->pairs[j], 0, sizeof(kv_pair_t));
            }
            
            combined_result->count += copy_count;
            combined_result->total_scanned += single_result->total_scanned;
            
            kv_range_result_destroy(single_result);
        }
        
        // 检查超时
        if (check_timeout(start_time, options->timeout_ms)) {
            break;
        }
    }
    
    combined_result->elapsed_ms = get_current_time_ms() - start_time;
    combined_result->has_more = (combined_result->count >= total_capacity);
    
    return combined_result;
}

/**
 * 迭代器范围查询
 */
kv_iterator_t* kv_range_iterator_create(kv_range_manager_t *manager,
                                       const char *start_key,
                                       const char *end_key,
                                       bool include_start,
                                       bool include_end,
                                       const kv_range_options_t *options) {
    // 避免未使用参数警告
    (void)end_key;
    (void)include_start;
    (void)include_end;
    (void)options;
    
    if (!manager || !manager->engine) {
        return NULL;
    }
    
    // 直接使用引擎的迭代器创建功能
    if (manager->engine->vtable->create_iterator) {
        kv_iterator_t *iter = manager->engine->vtable->create_iterator(manager->engine);
        if (iter && start_key) {
            iter->seek(iter, start_key);
        }
        return iter;
    }
    
    return NULL;
}

/**
 * 获取范围查询统计信息
 */
int kv_range_get_stats(kv_range_manager_t *manager, kv_range_stats_t *stats) {
    if (!manager || !stats) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_rdlock(&manager->lock);
    *stats = manager->stats;
    pthread_rwlock_unlock(&manager->lock);
    
    return KV_ERR_NONE;
}

/**
 * 重置统计信息
 */
int kv_range_reset_stats(kv_range_manager_t *manager) {
    if (!manager) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_wrlock(&manager->lock);
    memset(&manager->stats, 0, sizeof(kv_range_stats_t));
    pthread_rwlock_unlock(&manager->lock);
    
    return KV_ERR_NONE;
}

/**
 * 设置前缀配置
 */
int kv_range_set_prefix_config(kv_range_manager_t *manager,
                              const kv_prefix_config_t *config) {
    if (!manager || !config) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_wrlock(&manager->lock);
    manager->prefix_config = *config;
    pthread_rwlock_unlock(&manager->lock);
    
    return KV_ERR_NONE;
}

/**
 * 预热范围查询缓存
 */
int kv_range_warmup_cache(kv_range_manager_t *manager, 
                         const char **prefixes, 
                         size_t count) {
    if (!manager || !prefixes || count == 0) {
        return KV_ERR_PARAM;
    }
    
    kv_range_options_t options = kv_range_options_default();
    options.limit = 100; // 预热时限制数量
    options.timeout_ms = 1000; // 短超时
    
    for (size_t i = 0; i < count; i++) {
        kv_range_result_t *result = kv_prefix_scan(manager, prefixes[i], &options);
        if (result) {
            kv_range_result_destroy(result);
        }
    }
    
    return KV_ERR_NONE;
}

/**
 * 优化范围查询性能
 */
int kv_range_optimize(kv_range_manager_t *manager) {
    if (!manager || !manager->engine) {
        return KV_ERR_PARAM;
    }
    
    // 如果引擎支持压缩或优化操作，执行它
    if (manager->engine->vtable->compact) {
        return manager->engine->vtable->compact(manager->engine);
    }
    
    return KV_ERR_NONE;
}
