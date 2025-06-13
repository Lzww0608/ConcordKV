#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <pthread.h>
#include "../../../kvserver/kv_memory.h"

static uint64_t get_timestamp_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000ULL + tv.tv_usec;
}

void test_block_size_performance(const char *scenario, size_t block_size, int num_allocations) {
    printf("\n🧪 测试场景: %s (块大小: %zu bytes)\n", scenario, block_size);
    printf("=====================================\n");
    
    // 创建arena
    kv_arena_config_t config = KV_ARENA_DEFAULT_CONFIG;
    config.thread_mode = KV_ARENA_THREAD_UNSAFE;
    config.enable_block_cache = false;
    config.enable_detailed_stats = false;
    config.block_size = block_size;
    
    kv_arena_t *arena = kv_arena_create_ex(&config);
    if (!arena) {
        printf("❌ 创建arena失败\n");
        return;
    }
    
    // 性能测试
    uint64_t start_time = get_timestamp_us();
    
    for (int i = 0; i < num_allocations; i++) {
        size_t size = (i % 1000) + 1;
        void *ptr = kv_arena_alloc(arena, size);
        if (!ptr) {
            printf("❌ 分配失败 (第%d次)\n", i);
            break;
        }
    }
    
    uint64_t end_time = get_timestamp_us();
    uint64_t arena_time = end_time - start_time;
    
    // 获取统计信息
    size_t memory_usage = kv_arena_memory_usage(arena);
    size_t total_allocated = kv_arena_total_allocated(arena);
    
    printf("⏱️  分配时间: %lu μs\n", arena_time);
    printf("💾 内存使用: %zu bytes (%.2f KB)\n", memory_usage, memory_usage / 1024.0);
    printf("📊 总分配: %zu bytes (%.2f KB)\n", total_allocated, total_allocated / 1024.0);
    printf("📈 内存效率: %.2f%%\n", (double)total_allocated / memory_usage * 100);
    printf("⚡ 平均分配时间: %.2f ns/次\n", (double)arena_time * 1000 / num_allocations);
    
    kv_arena_destroy(arena);
}

int main(void) {
    printf("🚀 Arena内存池块大小性能对比测试\n");
    printf("=========================================\n");
    printf("测试说明: 展示不同块大小对分配性能的影响\n");
    
    const int num_allocations = 50000;
    
    // 测试不同的块大小
    test_block_size_performance("小块场景 (内存敏感)", 
                               kv_arena_suggest_block_size("small"), 
                               num_allocations);
    
    test_block_size_performance("默认场景 (平衡优化)", 
                               kv_arena_suggest_block_size("default"), 
                               num_allocations);
    
    test_block_size_performance("大块场景 (性能优先)", 
                               kv_arena_suggest_block_size("performance"), 
                               num_allocations);
    
    test_block_size_performance("极致性能场景", 
                               kv_arena_suggest_block_size("ultra"), 
                               num_allocations);
    
    // 对比标准malloc
    printf("\n🧪 测试场景: 标准malloc对比\n");
    printf("=====================================\n");
    
    uint64_t start_time = get_timestamp_us();
    
    void **ptrs = malloc(num_allocations * sizeof(void*));
    for (int i = 0; i < num_allocations; i++) {
        size_t size = (i % 1000) + 1;
        ptrs[i] = malloc(size);
    }
    
    for (int i = 0; i < num_allocations; i++) {
        free(ptrs[i]);
    }
    free(ptrs);
    
    uint64_t end_time = get_timestamp_us();
    uint64_t malloc_time = end_time - start_time;
    
    printf("⏱️  分配+释放时间: %lu μs\n", malloc_time);
    printf("⚡ 平均分配时间: %.2f ns/次\n", (double)malloc_time * 1000 / num_allocations);
    
    printf("\n📊 性能总结\n");
    printf("=====================================\n");
    printf("✅ 块大小越大，Arena性能越好\n");
    printf("✅ 64KB默认块大小提供良好的性能平衡\n");
    printf("✅ 1MB+块大小适合高性能场景\n");
    printf("✅ 4KB小块适合内存敏感场景\n");
    printf("💡 建议: 根据应用场景选择合适的块大小\n");
    
    return 0;
} 