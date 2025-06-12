#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "kv_cache.h"

void print_cache_state(kv_cache_t *cache, const char *description) {
    printf("\n=== %s ===\n", description);
    
    // 检查每个键是否存在
    const char *keys[] = {"key1", "key2", "key3", "key4"};
    for (int i = 0; i < 4; i++) {
        bool exists = kv_cache_exists(cache, keys[i], strlen(keys[i]));
        printf("  %s: %s\n", keys[i], exists ? "存在" : "不存在");
    }
    
    // 获取统计信息
    kv_cache_stats_t stats;
    if (kv_cache_get_stats(cache, &stats) == KV_SUCCESS) {
        printf("  当前条目数: %zu\n", stats.current_entries);
        printf("  淘汰次数: %llu\n", (unsigned long long)stats.evictions);
    }
}

void print_entry_frequencies(kv_cache_t *cache) {
    printf("\n=== 条目频率信息 ===\n");
    
    // 遍历哈希表查看频率
    for (size_t i = 0; i < cache->hashtable->bucket_count; i++) {
        kv_cache_entry_t *entry = cache->hashtable->buckets[i];
        while (entry) {
            printf("  %s: 频率=%llu\n", entry->key, (unsigned long long)entry->frequency);
            entry = entry->hash_next;
        }
    }
}

int main() {
    printf("=== LFU策略调试程序 ===\n");
    
    // 创建LFU缓存，容量为3
    kv_cache_config_t *config = kv_cache_config_create_default(KV_CACHE_POLICY_LFU);
    config->max_entries = 3;
    config->default_ttl = 0; // 永不过期
    
    kv_cache_t *cache = kv_cache_create(config);
    if (!cache) {
        printf("创建缓存失败\n");
        return 1;
    }
    
    printf("创建容量为3的LFU缓存成功\n");
    
    // 添加三个条目
    printf("\n步骤1: 添加三个条目\n");
    kv_cache_set(cache, "key1", 4, "value1", 6, 0);
    kv_cache_set(cache, "key2", 4, "value2", 6, 0);
    kv_cache_set(cache, "key3", 4, "value3", 6, 0);
    
    print_cache_state(cache, "添加三个条目后");
    print_entry_frequencies(cache);
    
    // 模拟不同的访问模式
    printf("\n步骤2: 模拟访问模式\n");
    printf("key1访问5次...\n");
    for (int i = 0; i < 5; i++) {
        char *value;
        size_t value_len;
        kv_cache_get(cache, "key1", 4, &value, &value_len);
        if (value) free(value);
    }
    
    printf("key2访问2次...\n");
    for (int i = 0; i < 2; i++) {
        char *value;
        size_t value_len;
        kv_cache_get(cache, "key2", 4, &value, &value_len);
        if (value) free(value);
    }
    
    printf("key3不额外访问\n");
    
    print_entry_frequencies(cache);
    
    // 添加第四个条目，触发淘汰
    printf("\n步骤3: 添加第四个条目，触发淘汰\n");
    kv_cache_set(cache, "key4", 4, "value4", 6, 0);
    
    print_cache_state(cache, "添加key4后");
    print_entry_frequencies(cache);
    
    // 清理
    kv_cache_destroy(cache);
    kv_cache_config_destroy(config);
    
    return 0;
} 