#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kv_cache.h"

int main() {
    printf("=== 简单LFU测试 ===\n");
    
    // 创建LFU缓存，容量为3
    kv_cache_config_t* config = kv_cache_config_create_default(KV_CACHE_POLICY_LFU);
    if (!config) {
        printf("创建配置失败\n");
        return 1;
    }
    
    config->max_entries = 3;
    printf("缓存配置: 最大条目=%zu, 淘汰因子=%.2f, 最小淘汰数=%zu\n", 
           config->max_entries, config->eviction_factor, config->min_eviction_count);
    
    kv_cache_t* cache = kv_cache_create(config);
    if (!cache) {
        printf("创建缓存失败\n");
        kv_cache_config_destroy(config);
        return 1;
    }
    
    printf("\n1. 添加3个条目到缓存\n");
    kv_cache_set(cache, "key1", 4, "value1", 6, 0);
    kv_cache_set(cache, "key2", 4, "value2", 6, 0);
    kv_cache_set(cache, "key3", 4, "value3", 6, 0);
    
    // 获取统计信息
    kv_cache_stats_t stats;
    kv_cache_get_stats(cache, &stats);
    printf("当前条目数: %zu\n", stats.current_entries);
    
    printf("\n2. 访问key1和key2多次以增加频率\n");
    char *value;
    size_t value_len;
    for (int i = 0; i < 5; i++) {
        kv_cache_get(cache, "key1", 4, &value, &value_len);
        if (value) free(value);
        kv_cache_get(cache, "key2", 4, &value, &value_len);
        if (value) free(value);
    }
    
    printf("\n3. 添加第4个条目，应该触发淘汰\n");
    kv_cache_set(cache, "key4", 4, "value4", 6, 0);
    
    // 检查哪些键还存在
    printf("\n4. 检查剩余的键:\n");
    printf("key1: %s\n", kv_cache_exists(cache, "key1", 4) ? "存在" : "不存在");
    printf("key2: %s\n", kv_cache_exists(cache, "key2", 4) ? "存在" : "不存在");
    printf("key3: %s\n", kv_cache_exists(cache, "key3", 4) ? "存在" : "不存在");
    printf("key4: %s\n", kv_cache_exists(cache, "key4", 4) ? "存在" : "不存在");
    
    // 获取最终统计信息
    kv_cache_get_stats(cache, &stats);
    printf("\n5. 最终统计:\n");
    printf("当前条目数: %zu\n", stats.current_entries);
    printf("淘汰次数: %llu\n", (unsigned long long)stats.evictions);
    
    // 清理
    kv_cache_destroy(cache);
    kv_cache_config_destroy(config);
    
    printf("\n测试完成\n");
    return 0;
} 