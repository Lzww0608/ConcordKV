/*
 * @Author: Lzww0608  
 * @Date: 2025-6-13 15:58:58
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-13 15:59:01
 * @Description: ConcordKV Arena内存池单元测试
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>

// 包含内存模块头文件
#include "../../../kvserver/kv_memory.h"

// 测试结果统计
typedef struct {
    int total_tests;
    int passed_tests;
    int failed_tests;
} test_stats_t;

static test_stats_t g_stats = {0, 0, 0};

// 测试宏定义
#define TEST_ASSERT(condition, message) \
    do { \
        g_stats.total_tests++; \
        if (condition) { \
            printf("✅ PASS: %s\n", message); \
            g_stats.passed_tests++; \
        } else { \
            printf("❌ FAIL: %s\n", message); \
            g_stats.failed_tests++; \
        } \
    } while(0)

#define TEST_SECTION(name) \
    printf("\n🧪 测试组: %s\n", name); \
    printf("==========================================\n")

// 获取当前时间（微秒）
static uint64_t get_time_microseconds(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

// 测试1: Arena创建和销毁
void test_arena_create_destroy(void) {
    TEST_SECTION("Arena创建和销毁测试");
    
    // 测试默认块大小创建
    kv_arena_t *arena = kv_arena_create(0);
    TEST_ASSERT(arena != NULL, "默认块大小创建arena");
    TEST_ASSERT(kv_arena_memory_usage(arena) == 0, "初始内存使用为0");
    TEST_ASSERT(kv_arena_total_allocated(arena) == 0, "初始分配为0");
    kv_arena_destroy(arena);
    
    // 测试自定义块大小创建
    arena = kv_arena_create(8192);
    TEST_ASSERT(arena != NULL, "自定义块大小创建arena");
    kv_arena_destroy(arena);
    
    // 测试最小块大小限制
    arena = kv_arena_create(512);  // 小于最小值1024
    TEST_ASSERT(arena != NULL, "小于最小值的块大小创建arena");
    kv_arena_destroy(arena);
    
    // 测试NULL销毁
    kv_arena_destroy(NULL);  // 不应该崩溃
    TEST_ASSERT(1, "NULL销毁不崩溃");
}

// 测试2: 基础内存分配
void test_basic_allocation(void) {
    TEST_SECTION("基础内存分配测试");
    
    kv_arena_t *arena = kv_arena_create(4096);
    TEST_ASSERT(arena != NULL, "创建测试arena");
    
    // 分配小块内存
    void *ptr1 = kv_arena_alloc(arena, 64);
    TEST_ASSERT(ptr1 != NULL, "分配64字节");
    TEST_ASSERT(kv_arena_total_allocated(arena) >= 64, "分配统计正确");
    
    // 分配多个小块
    void *ptr2 = kv_arena_alloc(arena, 128);
    void *ptr3 = kv_arena_alloc(arena, 256);
    TEST_ASSERT(ptr2 != NULL && ptr3 != NULL, "分配多个小块");
    TEST_ASSERT(ptr1 != ptr2 && ptr2 != ptr3, "返回不同指针");
    
    // 测试内存可写
    memset(ptr1, 0xAA, 64);
    memset(ptr2, 0xBB, 128);
    memset(ptr3, 0xCC, 256);
    TEST_ASSERT(((char*)ptr1)[0] == (char)0xAA, "内存可写入");
    
    // 分配0字节应该返回NULL
    void *ptr_zero = kv_arena_alloc(arena, 0);
    TEST_ASSERT(ptr_zero == NULL, "分配0字节返回NULL");
    
    kv_arena_destroy(arena);
}

// 测试3: 大块内存分配
void test_large_allocation(void) {
    TEST_SECTION("大块内存分配测试");
    
    kv_arena_t *arena = kv_arena_create(4096);
    TEST_ASSERT(arena != NULL, "创建测试arena");
    
    // 分配超过块大小的内存
    void *large_ptr = kv_arena_alloc(arena, 8192);
    TEST_ASSERT(large_ptr != NULL, "分配超过块大小的内存");
    
    // 验证内存可用
    memset(large_ptr, 0x55, 8192);
    TEST_ASSERT(((char*)large_ptr)[0] == 0x55, "大块内存可写入");
    TEST_ASSERT(((char*)large_ptr)[8191] == 0x55, "大块内存末尾可写入");
    
    // 分配多个大块
    void *large_ptr2 = kv_arena_alloc(arena, 6144);
    TEST_ASSERT(large_ptr2 != NULL, "分配第二个大块");
    TEST_ASSERT(large_ptr != large_ptr2, "大块返回不同指针");
    
    kv_arena_destroy(arena);
}

// 测试4: 内存对齐分配
void test_aligned_allocation(void) {
    TEST_SECTION("内存对齐分配测试");
    
    kv_arena_t *arena = kv_arena_create(4096);
    TEST_ASSERT(arena != NULL, "创建测试arena");
    
    // 测试8字节对齐
    void *ptr8 = kv_arena_alloc_aligned(arena, 100, 8);
    TEST_ASSERT(ptr8 != NULL, "8字节对齐分配");
    TEST_ASSERT(((uintptr_t)ptr8 % 8) == 0, "8字节对齐正确");
    
    // 测试16字节对齐
    void *ptr16 = kv_arena_alloc_aligned(arena, 100, 16);
    TEST_ASSERT(ptr16 != NULL, "16字节对齐分配");
    TEST_ASSERT(((uintptr_t)ptr16 % 16) == 0, "16字节对齐正确");
    
    // 测试64字节对齐
    void *ptr64 = kv_arena_alloc_aligned(arena, 100, 64);
    TEST_ASSERT(ptr64 != NULL, "64字节对齐分配");
    TEST_ASSERT(((uintptr_t)ptr64 % 64) == 0, "64字节对齐正确");
    
    // 测试无效对齐参数
    void *ptr_invalid = kv_arena_alloc_aligned(arena, 100, 7);  // 非2的幂
    TEST_ASSERT(ptr_invalid == NULL, "无效对齐参数返回NULL");
    
    // 测试0对齐参数
    void *ptr_zero = kv_arena_alloc_aligned(arena, 100, 0);
    TEST_ASSERT(ptr_zero == NULL, "0对齐参数返回NULL");
    
    kv_arena_destroy(arena);
}

// 测试5: 内存统计功能
void test_memory_statistics(void) {
    TEST_SECTION("内存统计功能测试");
    
    kv_arena_t *arena = kv_arena_create(4096);
    TEST_ASSERT(arena != NULL, "创建测试arena");
    
    size_t initial_usage = kv_arena_memory_usage(arena);
    size_t initial_allocated = kv_arena_total_allocated(arena);
    
    // 分配一些内存
    void *ptr1 = kv_arena_alloc(arena, 1000);
    void *ptr2 = kv_arena_alloc(arena, 2000);
    TEST_ASSERT(ptr1 != NULL && ptr2 != NULL, "分配测试内存");
    
    // 检查统计信息
    size_t after_usage = kv_arena_memory_usage(arena);
    size_t after_allocated = kv_arena_total_allocated(arena);
    
    TEST_ASSERT(after_usage > initial_usage, "内存使用量增加");
    TEST_ASSERT(after_allocated >= 3000, "分配统计正确");
    
    printf("   内存使用: %zu bytes, 总分配: %zu bytes\n", 
           after_usage, after_allocated);
    
    // 测试NULL指针统计
    TEST_ASSERT(kv_arena_memory_usage(NULL) == 0, "NULL指针内存使用为0");
    TEST_ASSERT(kv_arena_total_allocated(NULL) == 0, "NULL指针总分配为0");
    
    kv_arena_destroy(arena);
}

// 测试6: 多块分配测试
void test_multiple_blocks(void) {
    TEST_SECTION("多块分配测试");
    
    kv_arena_t *arena = kv_arena_create(1024);  // 小块大小强制多块
    TEST_ASSERT(arena != NULL, "创建小块arena");
    
    void *ptrs[10];
    int valid_count = 0;
    
    // 分配多个小块，总大小超过单个块
    for (int i = 0; i < 10; i++) {
        ptrs[i] = kv_arena_alloc(arena, 200);
        if (ptrs[i] != NULL) {
            valid_count++;
            // 写入标识数据
            memset(ptrs[i], i + 1, 200);
        }
    }
    
    TEST_ASSERT(valid_count == 10, "所有小块分配成功");
    
    // 验证数据完整性
    int data_valid = 1;
    for (int i = 0; i < 10; i++) {
        if (ptrs[i] && ((char*)ptrs[i])[0] != (char)(i + 1)) {
            data_valid = 0;
            break;
        }
    }
    TEST_ASSERT(data_valid, "多块数据完整性");
    
    size_t total_usage = kv_arena_memory_usage(arena);
    printf("   多块总内存使用: %zu bytes\n", total_usage);
    TEST_ASSERT(total_usage > 1024, "使用了多个块");
    
    kv_arena_destroy(arena);
}

// 测试7: 边界条件测试
void test_edge_cases(void) {
    TEST_SECTION("边界条件测试");
    
    // 测试NULL arena
    void *ptr_null = kv_arena_alloc(NULL, 100);
    TEST_ASSERT(ptr_null == NULL, "NULL arena返回NULL");
    
    ptr_null = kv_arena_alloc_aligned(NULL, 100, 8);
    TEST_ASSERT(ptr_null == NULL, "NULL arena对齐分配返回NULL");
    
    // 测试极大分配请求
    kv_arena_t *arena = kv_arena_create(4096);
    TEST_ASSERT(arena != NULL, "创建测试arena");
    
    void *huge_ptr = kv_arena_alloc(arena, SIZE_MAX);
    TEST_ASSERT(huge_ptr == NULL, "极大分配请求返回NULL");
    
    // 测试连续小分配
    int small_alloc_count = 0;
    for (int i = 0; i < 1000; i++) {
        void *small_ptr = kv_arena_alloc(arena, 1);
        if (small_ptr != NULL) {
            small_alloc_count++;
        }
    }
    TEST_ASSERT(small_alloc_count == 1000, "连续小分配成功");
    
    kv_arena_destroy(arena);
}

// 测试8: 性能基准测试
void test_performance_benchmark(void) {
    TEST_SECTION("性能基准测试");
    
    const int num_allocations = 100000;
    kv_arena_t *arena = kv_arena_create(64 * 1024);  // 64KB块
    TEST_ASSERT(arena != NULL, "创建性能测试arena");
    
    // Arena分配性能测试
    uint64_t start_time = get_time_microseconds();
    
    void **ptrs = malloc(num_allocations * sizeof(void*));
    for (int i = 0; i < num_allocations; i++) {
        ptrs[i] = kv_arena_alloc(arena, 64);  // 分配64字节
    }
    
    uint64_t arena_time = get_time_microseconds() - start_time;
    
    // 验证分配成功
    int success_count = 0;
    for (int i = 0; i < num_allocations; i++) {
        if (ptrs[i] != NULL) {
            success_count++;
        }
    }
    TEST_ASSERT(success_count == num_allocations, "所有arena分配成功");
    
    // 标准malloc性能对比
    start_time = get_time_microseconds();
    
    void **malloc_ptrs = malloc(num_allocations * sizeof(void*));
    for (int i = 0; i < num_allocations; i++) {
        malloc_ptrs[i] = malloc(64);
    }
    
    uint64_t malloc_time = get_time_microseconds() - start_time;
    
    // 清理malloc分配的内存
    for (int i = 0; i < num_allocations; i++) {
        if (malloc_ptrs[i]) {
            free(malloc_ptrs[i]);
        }
    }
    free(malloc_ptrs);
    
    printf("   Arena分配时间: %lu μs\n", arena_time);
    printf("   标准malloc时间: %lu μs\n", malloc_time);
    printf("   性能比: %.2fx\n", (double)malloc_time / arena_time);
    
    TEST_ASSERT(arena_time > 0 && malloc_time > 0, "性能测试完成");
    
    // 内存使用统计
    size_t memory_usage = kv_arena_memory_usage(arena);
    size_t total_allocated = kv_arena_total_allocated(arena);
    printf("   内存使用: %zu bytes, 总分配: %zu bytes\n", 
           memory_usage, total_allocated);
    printf("   内存效率: %.2f%%\n", 
           (double)total_allocated / memory_usage * 100);
    
    free(ptrs);
    kv_arena_destroy(arena);
}

// 测试9: 兼容性API测试 
void test_compatibility_api(void) {
    TEST_SECTION("兼容性API测试");
    
    // 测试标准内存API（非内存池模式）
    void *ptr1 = kv_store_malloc(1024);
    TEST_ASSERT(ptr1 != NULL, "kv_store_malloc分配成功");
    
    memset(ptr1, 0x77, 1024);
    TEST_ASSERT(((char*)ptr1)[0] == 0x77, "内存可写入");
    
    void *ptr2 = kv_store_realloc(ptr1, 2048);
    TEST_ASSERT(ptr2 != NULL, "kv_store_realloc成功");
    TEST_ASSERT(((char*)ptr2)[0] == 0x77, "realloc保持数据");
    
    kv_store_free(ptr2);
    TEST_ASSERT(1, "kv_store_free不崩溃");
    
    // 测试宏定义
    void *macro_ptr = kv_malloc(512);
    TEST_ASSERT(macro_ptr != NULL, "kv_malloc宏工作");
    kv_free(macro_ptr);
    
    // 测试清理函数
    kv_memory_cleanup();
    TEST_ASSERT(1, "内存清理不崩溃");
}

// 主测试函数
int main(void) {
    printf("🚀 ConcordKV Arena内存池测试套件\n");
    printf("===========================================\n");
    
    // 执行所有测试
    test_arena_create_destroy();
    test_basic_allocation();
    test_large_allocation();
    test_aligned_allocation();
    test_memory_statistics();
    test_multiple_blocks();
    test_edge_cases();
    test_performance_benchmark();
    test_compatibility_api();
    
    // 输出测试结果
    printf("\n📊 测试结果统计\n");
    printf("===========================================\n");
    printf("总测试数: %d\n", g_stats.total_tests);
    printf("通过测试: %d\n", g_stats.passed_tests);
    printf("失败测试: %d\n", g_stats.failed_tests);
    printf("成功率: %.2f%%\n", 
           (double)g_stats.passed_tests / g_stats.total_tests * 100);
    
    if (g_stats.failed_tests == 0) {
        printf("\n🎉 所有测试通过！Arena内存池实现正确！\n");
        return 0;
    } else {
        printf("\n⚠️  发现 %d 个失败测试，请检查实现！\n", g_stats.failed_tests);
        return 1;
    }
} 