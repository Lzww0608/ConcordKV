#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kv_cache.h"

int main() {
    printf("=== LFU测试场景调试 ===\n");
    
    // 创建容量为3的LFU缓存
    kv_cache_config_t* config = kv_cache_config_create_default(KV_CACHE_POLICY_LFU);
    if (!config) {
        printf("创建配置失败\n");
        return 1;
    }
    
    config->max_entries = 3;
    printf("缓存配置: 容量=%zu, 策略=%s\n", 
           config->max_entries, kv_cache_policy_name(config->policy));
    
    kv_cache_t* cache = kv_cache_create(config);
    if (!cache) {
        printf("创建缓存失败\n");
        kv_cache_config_destroy(config);
        return 1;
    }
    
    printf("\n1. 添加三个条目\n");
    kv_cache_set(cache, "key1", 4, "value1", 6, 0);
    kv_cache_set(cache, "key2", 4, "value2", 6, 0);
    kv_cache_set(cache, "key3", 4, "value3", 6, 0);
    
    // 验证所有条目都存在
    printf("初始状态:\n");
    printf("  key1: %s\n", kv_cache_exists(cache, "key1", 4) ? "存在" : "不存在");
    printf("  key2: %s\n", kv_cache_exists(cache, "key2", 4) ? "存在" : "不存在");
    printf("  key3: %s\n", kv_cache_exists(cache, "key3", 4) ? "存在" : "不存在");
    
    printf("\n2. 模拟不同的访问频率\n");
    
    // key1: 访问5次（高频）
    printf("访问key1 5次...\n");
    for (int i = 0; i < 5; i++) {
        char *value;
        size_t value_len;
        int result = kv_cache_get(cache, "key1", 4, &value, &value_len);
        if (result == KV_SUCCESS && value) {
            printf("  第%d次访问key1成功\n", i+1);
            free(value);
        } else {
            printf("  第%d次访问key1失败\n", i+1);
        }
    }
    
    // key2: 访问2次（中频）
    printf("访问key2 2次...\n");
    for (int i = 0; i < 2; i++) {
        char *value;
        size_t value_len;
        int result = kv_cache_get(cache, "key2", 4, &value, &value_len);
        if (result == KV_SUCCESS && value) {
            printf("  第%d次访问key2成功\n", i+1);
            free(value);
        } else {
            printf("  第%d次访问key2失败\n", i+1);
        }
    }
    
    printf("访问后状态:\n");
    printf("  key1: %s\n", kv_cache_exists(cache, "key1", 4) ? "存在" : "不存在");
    printf("  key2: %s\n", kv_cache_exists(cache, "key2", 4) ? "存在" : "不存在");
    printf("  key3: %s\n", kv_cache_exists(cache, "key3", 4) ? "存在" : "不存在");
    
    // 获取统计信息
    kv_cache_stats_t stats;
    kv_cache_get_stats(cache, &stats);
    printf("当前条目数: %zu, 淘汰次数: %llu\n", 
           stats.current_entries, (unsigned long long)stats.evictions);
    
    printf("\n3. 添加第四个条目（应该触发淘汰）\n");
    kv_cache_set(cache, "key4", 4, "value4", 6, 0);
    
    printf("添加key4后状态:\n");
    printf("  key1: %s\n", kv_cache_exists(cache, "key1", 4) ? "存在" : "不存在");
    printf("  key2: %s\n", kv_cache_exists(cache, "key2", 4) ? "存在" : "不存在");
    printf("  key3: %s\n", kv_cache_exists(cache, "key3", 4) ? "存在" : "不存在");
    printf("  key4: %s\n", kv_cache_exists(cache, "key4", 4) ? "存在" : "不存在");
    
    // 获取最终统计信息
    kv_cache_get_stats(cache, &stats);
    printf("\n最终统计:\n");
    printf("  当前条目数: %zu\n", stats.current_entries);
    printf("  淘汰次数: %llu\n", (unsigned long long)stats.evictions);
    printf("  总请求数: %llu\n", (unsigned long long)stats.total_requests);
    printf("  缓存命中: %llu\n", (unsigned long long)stats.cache_hits);
    printf("  缓存未命中: %llu\n", (unsigned long long)stats.cache_misses);
    
    // 清理
    kv_cache_destroy(cache);
    kv_cache_config_destroy(config);
    
    printf("\n测试完成\n");
    return 0;
} 