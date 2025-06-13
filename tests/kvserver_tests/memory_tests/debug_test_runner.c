#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include "../../../kvserver/kv_memory.h"

// 测试统计
static int tests_run = 0;
static int tests_passed = 0;
static int failed_tests[100];  // 记录失败的测试行号
static int failed_count = 0;

// 增强的测试宏，记录失败信息
#define DEBUG_TEST_ASSERT(condition, message) \
    do { \
        tests_run++; \
        if (condition) { \
            tests_passed++; \
            printf("✅ PASS: %s\n", message); \
        } else { \
            printf("❌ FAIL: %s (line %d)\n", message, __LINE__); \
            printf("   条件: %s\n", #condition); \
            if (failed_count < 100) { \
                failed_tests[failed_count++] = __LINE__; \
            } \
            fflush(stdout); \
        } \
    } while(0)

// 辅助函数：获取当前时间戳（微秒）
static uint64_t get_timestamp_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

// 简化的性能测试
void debug_performance_test(void) {
    printf("\n🧪 调试性能测试\n");
    printf("=====================================\n");
    
    const int num_allocations = 10000;
    
    // 测试增强版arena性能
    kv_arena_config_t config = KV_ARENA_DEFAULT_CONFIG;
    config.block_size = 65536; // 使用64KB块大小，与quick_perf_test保持一致
    config.thread_mode = KV_ARENA_THREAD_UNSAFE;
    config.enable_block_cache = false;
    config.enable_detailed_stats = false;
    
    kv_arena_t *arena = kv_arena_create_ex(&config);
    DEBUG_TEST_ASSERT(arena != NULL, "创建arena成功");
    
    uint64_t start_time = get_timestamp_us();
    
    for (int i = 0; i < num_allocations; i++) {
        size_t size = 100; // 固定大小，避免碎片问题
        void *ptr = kv_arena_alloc(arena, size);
        //DEBUG_TEST_ASSERT(ptr != NULL, "分配内存成功");
        if (!ptr) break;
    }

    kv_arena_destroy(arena);
    
    uint64_t end_time = get_timestamp_us();
    uint64_t arena_time = end_time - start_time;
    
    printf("📊 Arena性能: %d次分配，耗时 %lu μs\n", num_allocations, arena_time);
    
    // 测试标准malloc性能
    start_time = get_timestamp_us();
    
    void **ptrs = malloc(num_allocations * sizeof(void*));
    for (int i = 0; i < num_allocations; i++) {
        size_t size = 100; // 固定大小，保持一致性
        ptrs[i] = malloc(size);
    }
    
    for (int i = 0; i < num_allocations; i++) {
        free(ptrs[i]);
    }
    free(ptrs);
    
    end_time = get_timestamp_us();
    uint64_t malloc_time = end_time - start_time;
    
    printf("📊 Malloc性能: %d次分配+释放，耗时 %lu μs\n", num_allocations, malloc_time);
    
    double speedup = (double)malloc_time / arena_time;
    printf("📊 性能对比: Arena比malloc快 %.2fx\n", speedup);

    //kv_arena_destroy(arena);
}

// 基础功能测试
void debug_basic_test(void) {
    printf("\n🧪 调试基础功能测试\n");
    printf("=====================================\n");
    
    // 测试创建和销毁
    kv_arena_t *arena = kv_arena_create_ex(NULL);
    DEBUG_TEST_ASSERT(arena != NULL, "创建arena");
    
    // 测试基本分配
    void *ptr1 = kv_arena_alloc(arena, 100);
    DEBUG_TEST_ASSERT(ptr1 != NULL, "分配100字节");
    
    void *ptr2 = kv_arena_alloc(arena, 1000);
    DEBUG_TEST_ASSERT(ptr2 != NULL, "分配1000字节");
    
    void *ptr3 = kv_arena_alloc(arena, 10000);
    DEBUG_TEST_ASSERT(ptr3 != NULL, "分配10000字节");
    
    // 测试内存使用统计
    size_t usage = kv_arena_memory_usage(arena);
    DEBUG_TEST_ASSERT(usage > 0, "内存使用统计");
    printf("📊 内存使用: %zu bytes\n", usage);
    
    // 测试对齐分配
    void *aligned = kv_arena_alloc_aligned(arena, 100, 16);
    DEBUG_TEST_ASSERT(aligned != NULL, "对齐分配");
    DEBUG_TEST_ASSERT(((uintptr_t)aligned % 16) == 0, "16字节对齐正确");
    
    kv_arena_destroy(arena);
}

// 错误处理测试
void debug_error_handling_test(void) {
    printf("\n🧪 调试错误处理测试\n");
    printf("=====================================\n");
    
    // 测试NULL参数处理
    DEBUG_TEST_ASSERT(kv_arena_alloc(NULL, 100) == NULL, "NULL arena处理");
    DEBUG_TEST_ASSERT(kv_arena_memory_usage(NULL) == 0, "NULL arena内存查询");
    
    kv_arena_t *arena = kv_arena_create_ex(NULL);
    DEBUG_TEST_ASSERT(arena != NULL, "创建测试arena");
    
    // 测试零大小分配
    DEBUG_TEST_ASSERT(kv_arena_alloc(arena, 0) == NULL, "零大小分配处理");
    
    // 测试无效对齐参数
    DEBUG_TEST_ASSERT(kv_arena_alloc_aligned(arena, 100, 0) == NULL, "零对齐参数");
    DEBUG_TEST_ASSERT(kv_arena_alloc_aligned(arena, 100, 3) == NULL, "非2幂对齐参数");
    
    kv_arena_destroy(arena);
}

// 统计功能测试
void debug_statistics_test(void) {
    printf("\n🧪 调试统计功能测试\n");
    printf("=====================================\n");
    
    kv_arena_config_t config = KV_ARENA_DEFAULT_CONFIG;
    config.enable_detailed_stats = true;
    
    kv_arena_t *arena = kv_arena_create_ex(&config);
    DEBUG_TEST_ASSERT(arena != NULL, "创建统计arena");
    
    // 进行一些分配
    for (int i = 0; i < 10; i++) {
        void *ptr = kv_arena_alloc(arena, (i + 1) * 100);
        DEBUG_TEST_ASSERT(ptr != NULL, "统计测试分配");
    }
    
    // 获取统计信息
    kv_arena_stats_t stats;
    int ret = kv_arena_get_stats(arena, &stats);
    DEBUG_TEST_ASSERT(ret == 0, "获取统计信息");
    DEBUG_TEST_ASSERT(stats.total_allocations == 10, "分配次数统计");
    DEBUG_TEST_ASSERT(stats.total_bytes_allocated > 0, "分配字节统计");
    
    printf("📊 统计信息: 分配%lu次, 字节%lu, 内存%lu\n", 
           stats.total_allocations, stats.total_bytes_allocated, stats.current_memory_usage);
    
    kv_arena_destroy(arena);
}

int main(void) {
    printf("🚀 ConcordKV Arena内存池调试测试\n");
    printf("=========================================\n");
    printf("目标: 识别失败的具体测试项目\n\n");
    
    // 运行各项测试
    debug_basic_test();
    debug_error_handling_test();
    debug_statistics_test();
    debug_performance_test();
    
    // 输出详细的测试结果
    printf("\n📊 详细测试结果\n");
    printf("=====================================\n");
    printf("总测试数: %d\n", tests_run);
    printf("通过测试: %d\n", tests_passed);
    printf("失败测试: %d\n", tests_run - tests_passed);
    printf("成功率: %.2f%%\n", (double)tests_passed / tests_run * 100);
    
    if (failed_count > 0) {
        printf("\n❌ 失败的测试行号:\n");
        for (int i = 0; i < failed_count; i++) {
            printf("   Line %d\n", failed_tests[i]);
        }
    }
    
    if (tests_passed == tests_run) {
        printf("\n🎉 所有调试测试通过！\n");
        return 0;
    } else {
        printf("\n❌ 发现 %d 个失败的测试\n", tests_run - tests_passed);
        return 1;
    }
} 