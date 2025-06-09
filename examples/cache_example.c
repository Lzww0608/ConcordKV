/*
 * @Author: Lzww0608  
 * @Date: 2025-6-9 21:30:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-9 21:30:00
 * @Description: ConcordKV 缓存层使用示例
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "../kvserver/kv_cache.h"
#include "../kvserver/kv_error.h"

// 演示基础缓存操作
void demo_basic_operations(void) {
    printf("\n=== 基础缓存操作演示 ===\n");
    
    // 创建LRU缓存配置
    kv_cache_config_t *config = kv_cache_config_create_default(KV_CACHE_POLICY_LRU);
    config->max_entries = 100;
    config->hash_table_size = 64;
    config->default_ttl = 30 * 1000000ULL; // 30秒TTL
    
    // 创建缓存实例
    kv_cache_t *cache = kv_cache_create(config);
    if (!cache) {
        printf("❌ 缓存创建失败\n");
        kv_cache_config_destroy(config);
        return;
    }
    
    printf("✅ 缓存创建成功 (策略: %s)\n", kv_cache_policy_name(config->policy));
    
    // 存储一些数据
    const char *keys[] = {"user:1001", "user:1002", "user:1003"};
    const char *values[] = {
        "{\"name\":\"Alice\",\"age\":25}",
        "{\"name\":\"Bob\",\"age\":30}",
        "{\"name\":\"Charlie\",\"age\":35}"
    };
    
    for (int i = 0; i < 3; i++) {
        int result = kv_cache_set(cache, keys[i], strlen(keys[i]), 
                                  values[i], strlen(values[i]), 0);
        if (result == KV_SUCCESS) {
            printf("✅ 存储 %s: %s\n", keys[i], values[i]);
        } else {
            printf("❌ 存储失败 %s\n", keys[i]);
        }
    }
    
    // 读取数据
    printf("\n--- 读取缓存数据 ---\n");
    for (int i = 0; i < 3; i++) {
        char *value = NULL;
        size_t value_len = 0;
        int result = kv_cache_get(cache, keys[i], strlen(keys[i]), &value, &value_len);
        
        if (result == KV_SUCCESS && value) {
            printf("✅ 读取 %s: %s\n", keys[i], value);
            free(value);
        } else {
            printf("❌ 读取失败 %s\n", keys[i]);
        }
    }
    
    // 更新数据
    printf("\n--- 更新缓存数据 ---\n");
    const char *new_value = "{\"name\":\"Alice\",\"age\":26,\"updated\":true}";
    int result = kv_cache_set(cache, keys[0], strlen(keys[0]), 
                              new_value, strlen(new_value), 0);
    if (result == KV_SUCCESS) {
        printf("✅ 更新 %s: %s\n", keys[0], new_value);
    }
    
    // 删除数据
    printf("\n--- 删除缓存数据 ---\n");
    result = kv_cache_delete(cache, keys[2], strlen(keys[2]));
    if (result == KV_SUCCESS) {
        printf("✅ 删除 %s\n", keys[2]);
    }
    
    // 验证删除
    bool exists = kv_cache_exists(cache, keys[2], strlen(keys[2]));
    printf("验证删除: %s %s\n", keys[2], exists ? "仍存在" : "已删除");
    
    // 显示统计信息
    kv_cache_print_stats(cache, stdout);
    
    // 清理
    kv_cache_destroy(cache);
    kv_cache_config_destroy(config);
}

// 演示TTL功能
void demo_ttl_functionality(void) {
    printf("\n=== TTL功能演示 ===\n");
    
    kv_cache_config_t *config = kv_cache_config_create_default(KV_CACHE_POLICY_LRU);
    config->max_entries = 50;
    
    kv_cache_t *cache = kv_cache_create(config);
    if (!cache) {
        printf("❌ 缓存创建失败\n");
        kv_cache_config_destroy(config);
        return;
    }
    
    // 设置不同TTL的数据
    const char *session_key = "session:abc123";
    const char *session_data = "{\"user_id\":1001,\"login_time\":1234567890}";
    uint64_t session_ttl = 3 * 1000000ULL; // 3秒TTL
    
    const char *cache_key = "cache:data";
    const char *cache_data = "{\"result\":\"cached_computation\"}";
    // 无TTL（永久缓存）
    
    // 存储会话数据（短TTL）
    int result = kv_cache_set(cache, session_key, strlen(session_key),
                              session_data, strlen(session_data), session_ttl);
    printf("✅ 存储会话数据 (TTL: 3秒): %s\n", session_key);
    
    // 存储缓存数据（无TTL）
    result = kv_cache_set(cache, cache_key, strlen(cache_key),
                          cache_data, strlen(cache_data), 0);
    printf("✅ 存储缓存数据 (无TTL): %s\n", cache_key);
    
    // 立即检查
    printf("\n--- 立即检查 ---\n");
    printf("会话数据存在: %s\n", kv_cache_exists(cache, session_key, strlen(session_key)) ? "是" : "否");
    printf("缓存数据存在: %s\n", kv_cache_exists(cache, cache_key, strlen(cache_key)) ? "是" : "否");
    
    // 等待TTL过期
    printf("\n等待TTL过期...\n");
    sleep(4);
    
    // 再次检查
    printf("\n--- TTL过期后检查 ---\n");
    printf("会话数据存在: %s\n", kv_cache_exists(cache, session_key, strlen(session_key)) ? "是" : "否");
    printf("缓存数据存在: %s\n", kv_cache_exists(cache, cache_key, strlen(cache_key)) ? "是" : "否");
    
    kv_cache_destroy(cache);
    kv_cache_config_destroy(config);
}

// 演示LRU淘汰机制
void demo_lru_eviction(void) {
    printf("\n=== LRU淘汰机制演示 ===\n");
    
    kv_cache_config_t *config = kv_cache_config_create_default(KV_CACHE_POLICY_LRU);
    config->max_entries = 5; // 小容量用于演示淘汰
    config->eviction_factor = 0.4f; // 40%淘汰率
    
    kv_cache_t *cache = kv_cache_create(config);
    if (!cache) {
        printf("❌ 缓存创建失败\n");
        kv_cache_config_destroy(config);
        return;
    }
    
    printf("缓存容量: %zu 条目\n", config->max_entries);
    
    // 填满缓存
    printf("\n--- 填满缓存 ---\n");
    for (int i = 1; i <= 5; i++) {
        char key[32], value[64];
        snprintf(key, sizeof(key), "item_%d", i);
        snprintf(value, sizeof(value), "data_for_item_%d", i);
        
        int result = kv_cache_set(cache, key, strlen(key), value, strlen(value), 0);
        if (result == KV_SUCCESS) {
            printf("✅ 存储 %s\n", key);
        }
    }
    
    // 访问一些条目以建立LRU顺序
    printf("\n--- 访问部分条目 ---\n");
    char *value = NULL;
    size_t value_len = 0;
    
    kv_cache_get(cache, "item_1", 6, &value, &value_len);
    if (value) {
        printf("✅ 访问 item_1\n");
        free(value);
    }
    
    kv_cache_get(cache, "item_3", 6, &value, &value_len);
    if (value) {
        printf("✅ 访问 item_3\n");
        free(value);
    }
    
    // 显示当前统计
    kv_cache_stats_t stats;
    kv_cache_get_stats(cache, &stats);
    printf("\n当前条目数: %zu\n", stats.current_entries);
    
    // 添加新条目触发淘汰
    printf("\n--- 添加新条目触发淘汰 ---\n");
    int result = kv_cache_set(cache, "new_item", 8, "new_data", 8, 0);
    if (result == KV_SUCCESS) {
        printf("✅ 添加 new_item\n");
    }
    
    // 检查淘汰结果
    kv_cache_get_stats(cache, &stats);
    printf("淘汰后条目数: %zu\n", stats.current_entries);
    printf("总淘汰次数: %llu\n", (unsigned long long)stats.evictions);
    
    // 检查哪些条目还存在
    printf("\n--- 检查剩余条目 ---\n");
    const char *test_keys[] = {"item_1", "item_2", "item_3", "item_4", "item_5", "new_item"};
    for (int i = 0; i < 6; i++) {
        bool exists = kv_cache_exists(cache, test_keys[i], strlen(test_keys[i]));
        printf("%s: %s\n", test_keys[i], exists ? "存在" : "已淘汰");
    }
    
    kv_cache_destroy(cache);
    kv_cache_config_destroy(config);
}

// 演示性能测试
void demo_performance_test(void) {
    printf("\n=== 性能测试演示 ===\n");
    
    kv_cache_config_t *config = kv_cache_config_create_default(KV_CACHE_POLICY_LRU);
    config->max_entries = 10000;
    config->hash_table_size = 1024;
    
    kv_cache_t *cache = kv_cache_create(config);
    if (!cache) {
        printf("❌ 缓存创建失败\n");
        kv_cache_config_destroy(config);
        return;
    }
    
    const int test_count = 1000;
    struct timespec start, end;
    
    // 写入性能测试
    printf("写入性能测试 (%d 条目)...\n", test_count);
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < test_count; i++) {
        char key[32], value[128];
        snprintf(key, sizeof(key), "perf_key_%d", i);
        snprintf(value, sizeof(value), "performance_test_data_%d_with_some_content", i);
        
        kv_cache_set(cache, key, strlen(key), value, strlen(value), 0);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double write_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double write_ops_per_sec = test_count / write_time;
    
    printf("✅ 写入完成: %.2f 秒, %.0f ops/sec\n", write_time, write_ops_per_sec);
    
    // 读取性能测试
    printf("\n读取性能测试 (%d 条目)...\n", test_count);
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    int successful_reads = 0;
    for (int i = 0; i < test_count; i++) {
        char key[32];
        snprintf(key, sizeof(key), "perf_key_%d", i);
        
        char *value = NULL;
        size_t value_len = 0;
        int result = kv_cache_get(cache, key, strlen(key), &value, &value_len);
        
        if (result == KV_SUCCESS && value) {
            successful_reads++;
            free(value);
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double read_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    double read_ops_per_sec = test_count / read_time;
    
    printf("✅ 读取完成: %.2f 秒, %.0f ops/sec\n", read_time, read_ops_per_sec);
    printf("成功读取: %d/%d (%.1f%%)\n", successful_reads, test_count, 
           (double)successful_reads / test_count * 100.0);
    
    // 显示最终统计
    kv_cache_print_stats(cache, stdout);
    
    kv_cache_destroy(cache);
    kv_cache_config_destroy(config);
}

int main(void) {
    printf("🚀 ConcordKV 缓存层功能演示\n");
    printf("========================================\n");
    
    // 运行各种演示
    demo_basic_operations();
    demo_ttl_functionality();
    demo_lru_eviction();
    demo_performance_test();
    
    printf("\n========================================\n");
    printf("✅ 所有演示完成！\n");
    printf("缓存层功能包括:\n");
    printf("  • LRU/LFU/FIFO/RANDOM 淘汰策略\n");
    printf("  • TTL 过期管理\n");
    printf("  • 线程安全操作\n");
    printf("  • 高性能哈希表\n");
    printf("  • 完整的统计信息\n");
    printf("  • 内存使用监控\n");
    printf("  • 自动清理机制\n");
    
    return 0;
} 