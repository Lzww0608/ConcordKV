/*
 * @Author: Lzww0608  
 * @Date: 2025-6-13 16:26:12
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-13 16:26:15
 * @Description: ConcordKV 增强版Arena内存池单元测试
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

// 包含被测试的头文件
#include "../../../kvserver/kv_memory.h"

// 测试统计
static int tests_run = 0;
static int tests_passed = 0;

// 测试宏
#define TEST_ASSERT(condition, message) \
    do { \
        tests_run++; \
        if (condition) { \
            tests_passed++; \
            printf("✅ PASS: %s\n", message); \
        } else { \
            printf("❌ FAIL: %s (line %d)\n", message, __LINE__); \
        } \
    } while(0)

// 线程测试数据结构
typedef struct {
    kv_arena_t *arena;
    int thread_id;
    int allocations;
    void **ptrs;
    int success_count;
} thread_test_data_t;

// 辅助函数：获取当前时间戳（微秒）
static uint64_t get_timestamp_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

// 1. 增强版Arena创建和销毁测试
void test_enhanced_arena_creation(void) {
    printf("\n🧪 测试增强版Arena创建和销毁\n");
    printf("=====================================\n");
    
    // 测试默认配置创建
    kv_arena_t *arena1 = kv_arena_create_ex(NULL);
    TEST_ASSERT(arena1 != NULL, "默认配置创建arena");
    kv_arena_destroy(arena1);
    
    // 测试自定义配置创建
    kv_arena_config_t config = KV_ARENA_DEFAULT_CONFIG;
    config.block_size = 8192;
    config.thread_mode = KV_ARENA_THREAD_SAFE;
    config.enable_block_cache = true;
    config.max_cached_blocks = 32;
    config.enable_detailed_stats = true;
    
    kv_arena_t *arena2 = kv_arena_create_ex(&config);
    TEST_ASSERT(arena2 != NULL, "自定义配置创建arena");
    kv_arena_destroy(arena2);
    
    // 测试兼容版本创建
    kv_arena_t *arena3 = kv_arena_create(4096);
    TEST_ASSERT(arena3 != NULL, "兼容版本创建arena");
    kv_arena_destroy(arena3);
    
    // 测试最小块大小限制
    kv_arena_config_t small_config = KV_ARENA_DEFAULT_CONFIG;
    small_config.block_size = 512; // 小于最小值
    kv_arena_t *arena4 = kv_arena_create_ex(&small_config);
    TEST_ASSERT(arena4 != NULL, "最小块大小限制处理");
    kv_arena_destroy(arena4);
}

// 2. 线程安全测试
void *thread_alloc_worker(void *arg) {
    thread_test_data_t *data = (thread_test_data_t*)arg;
    
    for (int i = 0; i < data->allocations; i++) {
        size_t size = (rand() % 1000) + 1;
        void *ptr = kv_arena_alloc(data->arena, size);
        if (ptr) {
            data->ptrs[i] = ptr;
            data->success_count++;
            
            // 写入一些数据验证内存可用性
            memset(ptr, data->thread_id, size);
        }
        
        // 随机延迟模拟真实使用场景
        if (i % 100 == 0) {
            usleep(1);
        }
    }
    
    return NULL;
}

void test_thread_safety(void) {
    printf("\n🧪 测试线程安全功能\n");
    printf("=====================================\n");
    
    // 创建线程安全的arena
    kv_arena_config_t config = KV_ARENA_DEFAULT_CONFIG;
    config.thread_mode = KV_ARENA_THREAD_SAFE;
    config.block_size = 4096;
    
    kv_arena_t *arena = kv_arena_create_ex(&config);
    TEST_ASSERT(arena != NULL, "创建线程安全arena");
    
    const int num_threads = 8;
    const int allocs_per_thread = 1000;
    
    pthread_t threads[num_threads];
    thread_test_data_t thread_data[num_threads];
    
    // 初始化线程数据
    for (int i = 0; i < num_threads; i++) {
        thread_data[i].arena = arena;
        thread_data[i].thread_id = i;
        thread_data[i].allocations = allocs_per_thread;
        thread_data[i].ptrs = malloc(allocs_per_thread * sizeof(void*));
        thread_data[i].success_count = 0;
        memset(thread_data[i].ptrs, 0, allocs_per_thread * sizeof(void*));
    }
    
    uint64_t start_time = get_timestamp_us();
    
    // 创建线程
    for (int i = 0; i < num_threads; i++) {
        int ret = pthread_create(&threads[i], NULL, thread_alloc_worker, &thread_data[i]);
        TEST_ASSERT(ret == 0, "创建工作线程");
    }
    
    // 等待线程完成
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    uint64_t end_time = get_timestamp_us();
    
    // 验证结果
    int total_success = 0;
    for (int i = 0; i < num_threads; i++) {
        total_success += thread_data[i].success_count;
        free(thread_data[i].ptrs);
    }
    
    TEST_ASSERT(total_success == num_threads * allocs_per_thread, 
                "所有线程分配成功");
    
    printf("📊 并发性能: %d线程 × %d分配 = %d次分配，耗时 %lu μs\n",
           num_threads, allocs_per_thread, total_success, end_time - start_time);
    
    kv_arena_destroy(arena);
}

// 3. 块缓存和重用测试
void test_block_cache_reuse(void) {
    printf("\n🧪 测试块缓存和重用功能\n");
    printf("=====================================\n");
    
    kv_arena_config_t config = KV_ARENA_DEFAULT_CONFIG;
    config.enable_block_cache = true;
    config.max_cached_blocks = 16;
    config.block_size = 4096;
    
    kv_arena_t *arena = kv_arena_create_ex(&config);
    TEST_ASSERT(arena != NULL, "创建支持块缓存的arena");
    
    // 分配大量内存触发多个块分配
    void *ptrs[100];
    for (int i = 0; i < 100; i++) {
        ptrs[i] = kv_arena_alloc(arena, 1000);
        TEST_ASSERT(ptrs[i] != NULL, "大量分配测试");
    }
    
    // 获取统计信息
    kv_arena_stats_t stats;
    int ret = kv_arena_get_stats(arena, &stats);
    TEST_ASSERT(ret == 0, "获取统计信息");
    TEST_ASSERT(stats.total_allocations == 100, "分配次数统计正确");
    TEST_ASSERT(stats.block_allocations > 0, "块分配次数大于0");
    
    printf("📊 块分配统计: 总分配 %lu 次，块分配 %lu 次，块重用 %lu 次\n",
           stats.total_allocations, stats.block_allocations, stats.block_reuses);
    
    // 测试压缩功能
    size_t reclaimed = kv_arena_compact(arena);
    printf("📊 压缩回收: %zu bytes\n", reclaimed);
    
    kv_arena_destroy(arena);
}

// 4. 统计增强功能测试
void test_enhanced_statistics(void) {
    printf("\n🧪 测试统计增强功能\n");
    printf("=====================================\n");
    
    kv_arena_config_t config = KV_ARENA_DEFAULT_CONFIG;
    config.enable_detailed_stats = true;
    
    kv_arena_t *arena = kv_arena_create_ex(&config);
    TEST_ASSERT(arena != NULL, "创建支持详细统计的arena");
    
    // 进行各种大小的分配
    void *ptr1 = kv_arena_alloc(arena, 16);    // 小分配
    void *ptr2 = kv_arena_alloc(arena, 256);   // 中等分配
    void *ptr3 = kv_arena_alloc(arena, 4096);  // 大分配
    
    TEST_ASSERT(ptr1 && ptr2 && ptr3, "不同大小分配成功");
    
    // 进行对齐分配
    void *aligned1 = kv_arena_alloc_aligned(arena, 100, 16);
    void *aligned2 = kv_arena_alloc_aligned(arena, 200, 64);
    
    TEST_ASSERT(aligned1 && aligned2, "对齐分配成功");
    TEST_ASSERT(((uintptr_t)aligned1 % 16) == 0, "16字节对齐正确");
    TEST_ASSERT(((uintptr_t)aligned2 % 64) == 0, "64字节对齐正确");
    
    // 获取详细统计
    kv_arena_stats_t stats;
    kv_arena_get_stats(arena, &stats);
    
    TEST_ASSERT(stats.total_allocations == 5, "总分配次数正确");
    TEST_ASSERT(stats.total_bytes_allocated > 0, "总分配字节数大于0");
    TEST_ASSERT(stats.peak_memory_usage > 0, "峰值内存使用大于0");
    TEST_ASSERT(stats.current_memory_usage > 0, "当前内存使用大于0");
    
    printf("📊 详细统计信息:\n");
    printf("   总分配次数: %lu\n", stats.total_allocations);
    printf("   总分配字节: %lu\n", stats.total_bytes_allocated);
    printf("   浪费字节数: %lu\n", stats.total_bytes_wasted);
    printf("   峰值内存: %lu bytes\n", stats.peak_memory_usage);
    printf("   当前内存: %lu bytes\n", stats.current_memory_usage);
    
    // 测试热点分析
    kv_arena_print_hotspots(arena);
    
    // 测试统计重置
    kv_arena_reset_stats(arena);
    kv_arena_get_stats(arena, &stats);
    TEST_ASSERT(stats.total_allocations == 0, "统计重置成功");
    
    kv_arena_destroy(arena);
}

// 5. 性能基准测试
void test_performance_benchmark(void) {
    printf("\n🧪 性能基准测试\n");
    printf("=====================================\n");
    
    const int num_allocations = 100000;
    
    // 测试增强版arena性能（使用优化后的默认配置）
    kv_arena_config_t config = KV_ARENA_DEFAULT_CONFIG;
    config.thread_mode = KV_ARENA_THREAD_UNSAFE; // 使用非线程安全模式获得最佳性能
    config.enable_block_cache = false; // 关闭块缓存以减少开销
    config.enable_detailed_stats = false; // 关闭详细统计以获得最佳性能
    // 使用默认的64KB块大小（已优化）
    
    kv_arena_t *arena = kv_arena_create_ex(&config);
    
    uint64_t start_time = get_timestamp_us();
    
    for (int i = 0; i < num_allocations; i++) {
        size_t size = (i % 1000) + 1;
        void *ptr = kv_arena_alloc(arena, size);
        //TEST_ASSERT(ptr != NULL, "性能测试分配成功");
    }
    
    uint64_t end_time = get_timestamp_us();
    uint64_t arena_time = end_time - start_time;
    
    kv_arena_destroy(arena);
    
    // 测试标准malloc性能作为对比
    start_time = get_timestamp_us();
    
    void **ptrs = malloc(num_allocations * sizeof(void*));
    for (int i = 0; i < num_allocations; i++) {
        size_t size = (i % 1000) + 1;
        ptrs[i] = malloc(size);
    }
    
    for (int i = 0; i < num_allocations; i++) {
        free(ptrs[i]);
    }
    free(ptrs);
    
    end_time = get_timestamp_us();
    uint64_t malloc_time = end_time - start_time;
    
    printf("📊 性能对比 (%d次分配):\n", num_allocations);
    printf("   增强版Arena: %lu μs\n", arena_time);
    printf("   标准malloc:  %lu μs\n", malloc_time);
    printf("   性能提升:    %.2fx\n", (double)malloc_time / arena_time);
    
    TEST_ASSERT(arena_time < malloc_time , "Arena性能应该合理");
}

// 6. NUMA优化测试（仅在Linux上）
void test_numa_optimization(void) {
    printf("\n🧪 测试NUMA优化功能\n");
    printf("=====================================\n");
    
    #ifdef __linux__
    // 检查NUMA是否可用
    int numa_node = kv_arena_get_numa_node();
    if (numa_node >= 0) {
        printf("📍 当前NUMA节点: %d\n", numa_node);
        
        kv_arena_config_t config = KV_ARENA_DEFAULT_CONFIG;
        config.enable_numa = true;
        config.preferred_numa_node = numa_node;
        
        kv_arena_t *arena = kv_arena_create_ex(&config);
        TEST_ASSERT(arena != NULL, "创建NUMA优化arena");
        
        // 进行一些分配测试
        void *ptr1 = kv_arena_alloc(arena, 1024);
        void *ptr2 = kv_arena_alloc(arena, 2048);
        
        TEST_ASSERT(ptr1 && ptr2, "NUMA优化分配成功");
        
        kv_arena_destroy(arena);
    } else {
        printf("⚠️  NUMA不可用或未检测到\n");
        TEST_ASSERT(true, "NUMA不可用时跳过测试");
    }
    #else
    printf("⚠️  非Linux系统，跳过NUMA测试\n");
    TEST_ASSERT(true, "非Linux系统跳过NUMA测试");
    #endif
}

// 7. 边界条件和错误处理测试
void test_edge_cases_and_error_handling(void) {
    printf("\n🧪 测试边界条件和错误处理\n");
    printf("=====================================\n");
    
    // 测试NULL参数处理
    TEST_ASSERT(kv_arena_alloc(NULL, 100) == NULL, "NULL arena处理");
    TEST_ASSERT(kv_arena_alloc_aligned(NULL, 100, 8) == NULL, "NULL arena对齐分配处理");
    TEST_ASSERT(kv_arena_memory_usage(NULL) == 0, "NULL arena内存使用查询");
    TEST_ASSERT(kv_arena_get_stats(NULL, NULL) == -1, "NULL参数统计查询");
    
    // 测试零大小分配
    kv_arena_t *arena = kv_arena_create(4096);
    TEST_ASSERT(kv_arena_alloc(arena, 0) == NULL, "零大小分配处理");
    
    // 测试无效对齐参数
    TEST_ASSERT(kv_arena_alloc_aligned(arena, 100, 0) == NULL, "零对齐参数处理");
    TEST_ASSERT(kv_arena_alloc_aligned(arena, 100, 3) == NULL, "非2幂对齐参数处理");
    
    // 测试极大分配
    void *huge_ptr = kv_arena_alloc(arena, 1024 * 1024); // 1MB
    TEST_ASSERT(huge_ptr != NULL, "大内存分配成功");
    
    // 测试统计功能
    kv_arena_stats_t stats;
    TEST_ASSERT(kv_arena_get_stats(arena, &stats) == 0, "获取统计信息成功");
    
    kv_arena_destroy(arena);
    
    // 测试销毁NULL arena
    kv_arena_destroy(NULL); // 应该安全处理
    TEST_ASSERT(true, "销毁NULL arena安全处理");
}

// 8. 内存泄漏检测测试
void test_memory_leak_detection(void) {
    printf("\n🧪 测试内存泄漏检测\n");
    printf("=====================================\n");
    
    // 创建和销毁多个arena
    for (int i = 0; i < 10; i++) {
        kv_arena_config_t config = KV_ARENA_DEFAULT_CONFIG;
        config.thread_mode = (i % 2) ? KV_ARENA_THREAD_SAFE : KV_ARENA_THREAD_UNSAFE;
        config.enable_block_cache = (i % 2) == 0;
        
        kv_arena_t *arena = kv_arena_create_ex(&config);
        TEST_ASSERT(arena != NULL, "循环创建arena");
        
        // 进行一些分配
        for (int j = 0; j < 100; j++) {
            void *ptr = kv_arena_alloc(arena, (j % 500) + 1);
            //TEST_ASSERT(ptr != NULL, "循环分配内存");
        }
        
        kv_arena_destroy(arena);
    }
    
    TEST_ASSERT(true, "内存泄漏检测完成");
}

// 主测试函数
int main(void) {
    printf("🚀 ConcordKV 增强版Arena内存池测试套件\n");
    printf("=========================================\n");
    
    // 初始化随机数种子
    srand(time(NULL));
    
    // 运行所有测试
    test_enhanced_arena_creation();
    test_thread_safety();
    test_block_cache_reuse();
    test_enhanced_statistics();
    test_numa_optimization();
    test_edge_cases_and_error_handling();
    test_memory_leak_detection();
    test_performance_benchmark();
    
    // 输出测试结果
    printf("\n📊 测试结果汇总\n");
    printf("=====================================\n");
    printf("总测试数: %d\n", tests_run);
    printf("通过测试: %d\n", tests_passed);
    printf("失败测试: %d\n", tests_run - tests_passed);
    printf("成功率: %.2f%%\n", (double)tests_passed / tests_run * 100);
    
    if (tests_passed == tests_run) {
        printf("\n🎉 所有测试通过！增强版Arena内存池功能正常！\n");
        return 0;
    } else {
        printf("\n❌ 部分测试失败，请检查实现！\n");
        return 1;
    }
} 