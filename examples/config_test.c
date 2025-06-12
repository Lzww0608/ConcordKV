#include <stdio.h>
#include <stdlib.h>
#include "kv_cache.h"

int main() {
    printf("=== 配置测试 ===\n");
    
    // 创建LFU缓存配置
    kv_cache_config_t* config = kv_cache_config_create_default(KV_CACHE_POLICY_LFU);
    if (!config) {
        printf("创建配置失败\n");
        return 1;
    }
    
    printf("默认配置值:\n");
    printf("  策略: %s\n", kv_cache_policy_name(config->policy));
    printf("  最大条目: %zu\n", config->max_entries);
    printf("  淘汰因子: %.2f\n", config->eviction_factor);
    printf("  最小淘汰数: %u\n", config->min_eviction_count);
    printf("  最大淘汰数: %u\n", config->max_eviction_count);
    
    // 修改配置
    config->max_entries = 3;
    printf("\n修改后的配置值:\n");
    printf("  最大条目: %zu\n", config->max_entries);
    printf("  淘汰因子: %.2f\n", config->eviction_factor);
    printf("  最小淘汰数: %u\n", config->min_eviction_count);
    printf("  最大淘汰数: %u\n", config->max_eviction_count);
    
    // 计算淘汰数量
    size_t evict_count = (size_t)(config->max_entries * config->eviction_factor);
    printf("\n淘汰计算:\n");
    printf("  计算的淘汰数: %zu\n", evict_count);
    if (evict_count < config->min_eviction_count) {
        evict_count = config->min_eviction_count;
        printf("  调整后淘汰数: %zu (使用最小值)\n", evict_count);
    }
    
    // 清理
    kv_cache_config_destroy(config);
    
    printf("\n测试完成\n");
    return 0;
} 