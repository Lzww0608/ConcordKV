#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "kvstore_btree.h"
#include "btree_adapter.h"

int main() {
    printf("=== B+Tree Debug Test ===\n");
    
    // 创建一个小阶数的B+Tree
    btree_t *tree = kv_store_btree_create(3);
    if (!tree) {
        printf("ERROR: Failed to create B+Tree\n");
        return -1;
    }
    
    printf("Created B+Tree with order 3\n");
    printf("Initial state: keys=%zu, height=%u\n", tree->total_keys, tree->height);
    
    // 插入几个键
    char *keys[] = {"key1", "key2", "key3", "key4", "key5"};
    char *values[] = {"val1", "val2", "val3", "val4", "val5"};
    
    for (int i = 0; i < 5; i++) {
        printf("\nInserting %s -> %s\n", keys[i], values[i]);
        int ret = kvs_btree_set(tree, keys[i], values[i]);
        printf("Insert result: %d\n", ret);
        printf("Tree state: keys=%zu, height=%u\n", tree->total_keys, tree->height);
        
        // 立即验证插入的键
        char *retrieved = kvs_btree_get(tree, keys[i]);
        if (retrieved) {
            printf("Retrieved: %s -> %s\n", keys[i], retrieved);
            free(retrieved);
        } else {
            printf("ERROR: Failed to retrieve %s\n", keys[i]);
        }
    }
    
    printf("\n=== Final verification ===\n");
    for (int i = 0; i < 5; i++) {
        char *retrieved = kvs_btree_get(tree, keys[i]);
        if (retrieved) {
            printf("Final check: %s -> %s (expected: %s) %s\n", 
                   keys[i], retrieved, values[i], 
                   strcmp(retrieved, values[i]) == 0 ? "OK" : "MISMATCH");
            free(retrieved);
        } else {
            printf("Final check: %s -> NOT FOUND\n", keys[i]);
        }
    }
    
    kv_store_btree_destroy(tree);
    printf("\nTest completed\n");
    return 0;
} 