#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include "../../../kvserver/kv_memory.h"

int main() {
    printf("=== ConcordKV 内存池集成测试 ===\n");
    
    // 测试 kv_store_malloc
    printf("1. 测试 kv_store_malloc...\n");
    void *ptr1 = kv_store_malloc(1024);
    if (ptr1) {
        printf("   ✓ kv_store_malloc(1024) 成功\n");
        memset(ptr1, 0xAA, 1024);  // 填充测试数据
        printf("   ✓ 内存写入测试通过\n");
    } else {
        printf("   ✗ kv_store_malloc(1024) 失败\n");
        return 1;
    }
    
    // 测试 kv_store_calloc
    printf("2. 测试 kv_store_calloc...\n");
    void *ptr2 = kv_store_calloc(10, sizeof(int));
    if (ptr2) {
        printf("   ✓ kv_store_calloc(10, sizeof(int)) 成功\n");
        // 检查内存是否被清零
        int *arr = (int*)ptr2;
        int all_zero = 1;
        for (int i = 0; i < 10; i++) {
            if (arr[i] != 0) {
                all_zero = 0;
                break;
            }
        }
        if (all_zero) {
            printf("   ✓ calloc内存清零测试通过\n");
        } else {
            printf("   ✗ calloc内存清零测试失败\n");
            return 1;
        }
    } else {
        printf("   ✗ kv_store_calloc(10, sizeof(int)) 失败\n");
        return 1;
    }
    
    // 测试 kv_store_realloc
    printf("3. 测试 kv_store_realloc...\n");
    void *ptr3 = kv_store_realloc(ptr1, 2048);
    if (ptr3) {
        printf("   ✓ kv_store_realloc(ptr, 2048) 成功\n");
        ptr1 = ptr3;  // 更新指针
    } else {
        printf("   ✗ kv_store_realloc(ptr, 2048) 失败\n");
        return 1;
    }
    
    // 测试大量分配和释放
    printf("4. 测试大量内存分配和释放...\n");
    const int test_count = 1000;
    void **ptrs = malloc(test_count * sizeof(void*));
    
    // 分配测试
    for (int i = 0; i < test_count; i++) {
        ptrs[i] = kv_store_malloc(64 + i);
        if (!ptrs[i]) {
            printf("   ✗ 第%d次分配失败\n", i+1);
            return 1;
        }
    }
    printf("   ✓ %d次内存分配成功\n", test_count);
    
    // 释放测试
    for (int i = 0; i < test_count; i++) {
        kv_store_free(ptrs[i]);
    }
    printf("   ✓ %d次内存释放成功\n", test_count);
    
    // 释放测试内存
    kv_store_free(ptr1);
    kv_store_free(ptr2);
    free(ptrs);
    
    printf("5. 测试 kv_store_free...\n");
    printf("   ✓ 所有内存释放完成\n");
    
    printf("\n=== 所有测试通过！内存池集成成功！ ===\n");
    return 0;
} 