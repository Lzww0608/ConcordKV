#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../../kvserver/kv_memory.h"

// 获取时间戳（微秒）
static uint64_t get_timestamp_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

int main(void) {
    printf("🚀 Arena性能快速测试\n");
    printf("=====================================\n");
    
    const int num_allocations = 10000;
    
    // 测试1: 最简配置的Arena
    printf("\n📊 测试1: 最简配置Arena\n");
    kv_arena_config_t config = KV_ARENA_DEFAULT_CONFIG;
    config.thread_mode = KV_ARENA_THREAD_UNSAFE;
    config.enable_block_cache = false;
    config.enable_detailed_stats = false;
    config.block_size = 65536; // 64KB
    
    kv_arena_t *arena = kv_arena_create_ex(&config);
    if (!arena) {
        printf("❌ 创建Arena失败\n");
        return 1;
    }
    
    uint64_t start_time = get_timestamp_us();
    
    for (int i = 0; i < num_allocations; i++) {
        void *ptr = kv_arena_alloc(arena, 100);
        if (!ptr) {
            printf("❌ 分配失败 at %d\n", i);
            break;
        }
    }
    
    uint64_t end_time = get_timestamp_us();
    uint64_t arena_time = end_time - start_time;
    
    printf("Arena性能: %d次分配，耗时 %lu μs\n", num_allocations, arena_time);
    
    kv_arena_destroy(arena);
    
    // 测试2: 标准malloc
    printf("\n📊 测试2: 标准malloc\n");
    start_time = get_timestamp_us();
    
    void **ptrs = malloc(num_allocations * sizeof(void*));
    for (int i = 0; i < num_allocations; i++) {
        ptrs[i] = malloc(100);
    }
    
    for (int i = 0; i < num_allocations; i++) {
        free(ptrs[i]);
    }
    free(ptrs);
    
    end_time = get_timestamp_us();
    uint64_t malloc_time = end_time - start_time;
    
    printf("Malloc性能: %d次分配+释放，耗时 %lu μs\n", num_allocations, malloc_time);
    
    // 性能对比
    printf("\n📊 性能对比\n");
    printf("Arena时间: %lu μs\n", arena_time);
    printf("Malloc时间: %lu μs\n", malloc_time);
    
    if (arena_time > 0 && malloc_time > 0) {
        double ratio = (double)arena_time / malloc_time;
        printf("Arena/Malloc比值: %.2f\n", ratio);
        
        if (ratio < 1.0) {
            printf("✅ Arena比malloc快 %.2fx\n", 1.0/ratio);
        } else {
            printf("❌ Arena比malloc慢 %.2fx\n", ratio);
        }
    }
    
    return 0;
} 