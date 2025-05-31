/*
 * @Author: Lzww0608  
 * @Date: 2025-6-1 00:33:54
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-1 00:33:56
 * @Description: 简单的B+Tree测试程序
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../kvserver/kvstore_btree.h"
#include "../../kvserver/btree_adapter.h"

int main() {
    printf("=== Simple B+Tree Test ===\n");
    
    btree_t *tree = kv_store_btree_create(3);
    if (!tree) {
        printf("Failed to create tree\n");
        return -1;
    }
    
    // 测试插入15个键（这会触发多次分裂）
    for (int i = 0; i < 15; i++) {
        char key[20], value[20];
        snprintf(key, sizeof(key), "key_%02d", i);
        snprintf(value, sizeof(value), "value_%02d", i);
        
        printf("Inserting %s\n", key);
        int ret = kvs_btree_set(tree, key, value);
        if (ret != 0) {
            printf("ERROR: Failed to insert %s\n", key);
            kv_store_btree_destroy(tree);
            return -1;
        }
    }
    
    printf("Tree height: %u\n", tree->height);
    printf("Total keys: %zu\n", tree->total_keys);
    
    // 验证所有键
    printf("Verifying all keys...\n");
    for (int i = 0; i < 15; i++) {
        char key[20], expected_value[20];
        snprintf(key, sizeof(key), "key_%02d", i);
        snprintf(expected_value, sizeof(expected_value), "value_%02d", i);
        
        char *value = kvs_btree_get(tree, key);
        if (!value) {
            printf("ERROR: Key %s not found\n", key);
            kv_store_btree_destroy(tree);
            return -1;
        }
        
        if (strcmp(value, expected_value) != 0) {
            printf("ERROR: Key %s value mismatch: got %s, expected %s\n", 
                   key, value, expected_value);
            free(value);
            kv_store_btree_destroy(tree);
            return -1;
        }
        
        printf("✓ %s -> %s\n", key, value);
        free(value);
    }
    
    printf("All insertion and search tests passed!\n");
    
    kv_store_btree_destroy(tree);
    printf("Test completed successfully\n");
    return 0;
} 