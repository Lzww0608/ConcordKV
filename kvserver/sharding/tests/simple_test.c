

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../shard_hash.h"
#include "../shard_config.h"

int main(void) {
    printf("🚀 ConcordKV Shard Management Simple Test\n");
    printf("==========================================\n\n");
    
    // 测试1：哈希管理器
    printf("🧪 Testing Shard Hash Manager...\n");
    shard_hash_manager_t *hash_manager = shard_hash_manager_create(16, 1000, 300);
    if (hash_manager) {
        printf("   ✓ Hash manager created successfully\n");
        
        const char *test_key = "test_key_123";
        uint32_t shard_id = shard_hash_get_shard_id(hash_manager, test_key, strlen(test_key));
        printf("   ✓ Key '%s' -> Shard %u\n", test_key, shard_id);
        
        shard_hash_manager_destroy(hash_manager);
        printf("   ✓ Hash manager destroyed successfully\n");
    } else {
        printf("   ❌ Failed to create hash manager\n");
        return -1;
    }
    
    // 测试2：配置管理器
    printf("\n🧪 Testing Shard Config Manager...\n");
    shard_config_manager_t *config_manager = shard_config_manager_create(NULL);
    if (config_manager) {
        printf("   ✓ Config manager created successfully\n");
        
        const shard_config_t *config = shard_config_get_current(config_manager);
        if (config) {
            printf("   ✓ Config retrieved: %u shards, %s strategy\n", 
                   config->shard_count, 
                   config->strategy == SHARD_STRATEGY_HASH ? "hash" : "other");
        }
        
        shard_config_manager_destroy(config_manager);
        printf("   ✓ Config manager destroyed successfully\n");
    } else {
        printf("   ❌ Failed to create config manager\n");
        return -1;
    }
    
    printf("\n🎉 All simple tests passed!\n");
    return 0;
} 