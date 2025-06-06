/*
 * @Author: Lzww0608
 * @Date: 2025-6-6 18:30:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-6 18:30:00
 * @Description: ConcordKV 完整布隆过滤器演示程序
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include "lsm_bloom_filter_optimized.h"

// 演示辅助函数
void print_separator(const char *title) {
    printf("\n🎯 %s\n", title);
    printf("==========================================\n");
}

void print_config_info(const bloom_filter_config_t *config) {
    printf("配置信息:\n");
    printf("  预期条目数: %lu\n", config->expected_entries);
    printf("  误判率: %.2f%%\n", config->false_positive_rate / 100.0);
    printf("  过滤器类型: %d\n", config->type);
    printf("  哈希函数类型: %d\n", config->hash_type);
    printf("  优化标志: %u\n", config->optimization_flags);
    printf("  启用统计: %s\n", config->enable_statistics ? "是" : "否");
}

// 演示标准布隆过滤器
void demo_standard_bloom_filter(void) {
    print_separator("标准布隆过滤器演示");
    
    // 创建配置
    bloom_filter_config_t config = bloom_filter_create_default_config(10000, 100); // 1%误判率
    config.type = BLOOM_TYPE_STANDARD;
    config.hash_type = BLOOM_HASH_MURMUR3;
    
    print_config_info(&config);
    
    // 创建布隆过滤器
    optimized_bloom_filter_t *filter = bloom_filter_create_optimized(&config);
    if (!filter) {
        printf("❌ 创建布隆过滤器失败\n");
        return;
    }
    
    printf("✅ 标准布隆过滤器创建成功\n");
    printf("位数组大小: %zu bits\n", filter->bit_count);
    printf("哈希函数数量: %u\n", filter->hash_count);
    
    // 添加一些测试数据
    const char *test_keys[] = {
        "apple", "banana", "cherry", "date", "elderberry",
        "fig", "grape", "honeydew", "kiwi", "lemon"
    };
    size_t key_count = sizeof(test_keys) / sizeof(test_keys[0]);
    
    printf("\n🔧 添加测试数据...\n");
    for (size_t i = 0; i < key_count; i++) {
        int result = bloom_filter_add_optimized(filter, test_keys[i], strlen(test_keys[i]));
        if (result == 0) {
            printf("  ✓ 添加 '%s'\n", test_keys[i]);
        } else {
            printf("  ❌ 添加 '%s' 失败\n", test_keys[i]);
        }
    }
    
    // 测试查询
    printf("\n🔍 测试查询...\n");
    for (size_t i = 0; i < key_count; i++) {
        bool exists = bloom_filter_may_contain_optimized(filter, test_keys[i], strlen(test_keys[i]));
        printf("  '%s': %s\n", test_keys[i], exists ? "可能存在" : "肯定不存在");
    }
    
    // 测试不存在的键
    const char *false_keys[] = {"orange", "peach", "strawberry"};
    printf("\n测试不存在的键:\n");
    for (size_t i = 0; i < 3; i++) {
        bool exists = bloom_filter_may_contain_optimized(filter, false_keys[i], strlen(false_keys[i]));
        printf("  '%s': %s\n", false_keys[i], exists ? "误判(false positive)" : "正确(true negative)");
    }
    
    // 获取统计信息
    bloom_filter_stats_t stats;
    if (bloom_filter_get_stats_optimized(filter, &stats) == 0) {
        printf("\n📊 统计信息:\n");
        printf("  总插入: %lu\n", stats.total_insertions);
        printf("  总查询: %lu\n", stats.total_queries);
        printf("  命中数: %lu\n", stats.total_hits);
        printf("  填充率: %.2f%%\n", stats.fill_ratio * 100.0);
        printf("  内存使用: %lu bytes\n", stats.memory_usage);
    }
    
    bloom_filter_destroy_optimized(filter);
    printf("✅ 标准布隆过滤器演示完成\n");
}

// 演示块布隆过滤器
void demo_blocked_bloom_filter(void) {
    print_separator("块布隆过滤器演示");
    
    bloom_filter_config_t config = bloom_filter_create_default_config(50000, 100);
    config.type = BLOOM_TYPE_BLOCKED;
    config.hash_type = BLOOM_HASH_XXHASH;
    config.optimization_flags = BLOOM_OPT_CACHE_FRIENDLY;
    config.block_size = BLOOM_CACHE_LINE_BITS;
    
    print_config_info(&config);
    
    optimized_bloom_filter_t *filter = bloom_filter_create_optimized(&config);
    if (!filter) {
        printf("❌ 创建块布隆过滤器失败\n");
        return;
    }
    
    printf("✅ 块布隆过滤器创建成功\n");
    printf("块数量: %u\n", filter->blocked.block_count);
    printf("块大小: %u bits\n", filter->config.block_size);
    
    // 性能测试
    const int test_size = 10000;
    printf("\n⚡ 性能测试 (%d 条目)...\n", test_size);
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // 批量插入
    for (int i = 0; i < test_size; i++) {
        char key[32];
        snprintf(key, sizeof(key), "blocked_key_%d", i);
        bloom_filter_add_optimized(filter, key, strlen(key));
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double insert_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("插入时间: %.3f 秒 (%.0f ops/sec)\n", insert_time, test_size / insert_time);
    
    // 批量查询
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    int hits = 0;
    for (int i = 0; i < test_size; i++) {
        char key[32];
        snprintf(key, sizeof(key), "blocked_key_%d", i);
        if (bloom_filter_may_contain_optimized(filter, key, strlen(key))) {
            hits++;
        }
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double query_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("查询时间: %.3f 秒 (%.0f ops/sec)\n", query_time, test_size / query_time);
    printf("命中率: %.2f%%\n", (double)hits * 100.0 / test_size);
    
    bloom_filter_destroy_optimized(filter);
    printf("✅ 块布隆过滤器演示完成\n");
}

// 简化的演示程序
int main(void) {
    printf("🎊 ConcordKV 完整布隆过滤器演示程序\n");
    printf("============================================\n");
    printf("本程序将演示各种类型的布隆过滤器及其功能\n\n");
    
    // 运行基础演示
    demo_standard_bloom_filter();
    demo_blocked_bloom_filter();
    
    print_separator("演示程序完成");
    printf("🎉 布隆过滤器功能演示完成！\n");
    printf("🚀 ConcordKV LSM-Tree 布隆过滤器系统已就绪\n");
    
    return 0;
} 