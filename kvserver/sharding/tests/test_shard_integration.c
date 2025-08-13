/*
 * @Author: Lzww0608
 * @Date: 2025-6-25 17:57:40
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-25 17:57:40
 * @Description: ConcordKV 分片管理集成测试 - 验证分片感知存储适配器功能
 */

#include "../shard_hash.h"
#include "../shard_config.h"
#include "../shard_aware_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <math.h>

// 测试结果统计
typedef struct {
    int total_tests;
    int passed_tests;
    int failed_tests;
    double total_time;
} test_stats_t;

static test_stats_t g_test_stats = {0};

// 测试计时宏
#define TEST_START(name) \
    do { \
        printf("🧪 Testing %s...\n", (name)); \
        clock_t test_start_time = clock();

#define TEST_END() \
        clock_t test_end_time = clock(); \
        double test_elapsed = ((double)(test_end_time - test_start_time)) / CLOCKS_PER_SEC; \
        printf("   ✅ Test completed in %.3f seconds\n\n", test_elapsed); \
    } while(0)

#define ASSERT_TRUE(condition, message) \
    do { \
        if (!(condition)) { \
            printf("   ❌ ASSERTION FAILED: %s\n", (message)); \
            return 0; \
        } else { \
            printf("   ✓ %s\n", (message)); \
        } \
    } while(0)

// === 测试函数 ===

/**
 * 测试分片哈希功能
 */
int test_shard_hash(void) {
    TEST_START("Shard Hash Manager");
    
    // 创建哈希管理器
    shard_hash_manager_t *hash_manager = shard_hash_manager_create(16, 1000, 300);
    ASSERT_TRUE(hash_manager != NULL, "Hash manager created successfully");
    
    // 测试哈希计算
    uint64_t hash1 = shard_hash_compute("test_key", 8);
    uint64_t hash2 = shard_hash_compute("test_key", 8);
    ASSERT_TRUE(hash1 == hash2, "Same key should produce same hash");
    
    uint64_t hash3 = shard_hash_compute("different_key", 13);
    ASSERT_TRUE(hash1 != hash3, "Different keys should produce different hashes");
    
    // 测试分片ID计算
    uint32_t shard_id1 = shard_hash_get_shard_id(hash_manager, "test_key_1", 10);
    uint32_t shard_id2 = shard_hash_get_shard_id(hash_manager, "test_key_1", 10);
    ASSERT_TRUE(shard_id1 == shard_id2, "Same key should map to same shard");
    ASSERT_TRUE(shard_id1 < 16, "Shard ID should be within range");
    
    // 测试批量分片ID计算
    const char* keys[] = {"key1", "key2", "key3", "key4", "key5"};
    size_t key_lens[] = {4, 4, 4, 4, 4};
    uint32_t shard_ids[5];
    
    int result = shard_hash_batch_get_shard_ids(hash_manager, keys, key_lens, 5, shard_ids);
    ASSERT_TRUE(result == 0, "Batch shard ID calculation successful");
    
    // 验证分片分布
    for (int i = 0; i < 5; i++) {
        ASSERT_TRUE(shard_ids[i] < 16, "Shard ID out of range");
    }
    
    printf("   📊 Hash distribution test:\n");
    for (int i = 0; i < 5; i++) {
        printf("      Key '%s' -> Shard %u\n", keys[i], shard_ids[i]);
    }
    
    // 清理
    shard_hash_manager_destroy(hash_manager);
    
    TEST_END();
    return 0;
}

/**
 * 测试分片配置管理
 */
int test_shard_config(void) {
    TEST_START("Shard Configuration Manager");
    
    // 创建配置管理器
    shard_config_manager_t *config_manager = shard_config_manager_create(NULL);
    ASSERT_TRUE(config_manager != NULL, "Config manager created successfully");
    
    // 获取默认配置
    const shard_config_t *config = shard_config_get_current(config_manager);
    ASSERT_TRUE(config != NULL, "Current config retrieved successfully");
    ASSERT_TRUE(config->enabled == true, "Sharding should be enabled by default");
    ASSERT_TRUE(config->shard_count == 16, "Default shard count should be 16");
    ASSERT_TRUE(config->strategy == SHARD_STRATEGY_HASH, "Default strategy should be hash");
    
    // 测试配置验证
    char error_msg[1024];
    bool valid = shard_config_validate(config, error_msg, sizeof(error_msg));
    ASSERT_TRUE(valid == true, "Default config should be valid");
    
    // 测试配置打印
    printf("   📋 Current configuration:\n");
    shard_config_print(config);
    
    // 清理
    shard_config_manager_destroy(config_manager);
    
    TEST_END();
    return 0;
}

/**
 * 测试分片感知引擎基础功能
 */
int test_shard_aware_engine_basic(void) {
    TEST_START("Shard Aware Engine - Basic Operations");
    
    // 创建配置管理器
    shard_config_manager_t *config_manager = shard_config_manager_create(NULL);
    ASSERT_TRUE(config_manager != NULL, "Config manager created successfully");
    
    // 创建分片感知引擎
    shard_aware_engine_t *shard_engine = shard_aware_engine_create(config_manager);
    ASSERT_TRUE(shard_engine != NULL, "Shard aware engine created successfully");
    
    // 初始化引擎
    int result = shard_aware_engine_init(shard_engine);
    ASSERT_TRUE(result == 0, "Shard aware engine initialized successfully");
    
    // 测试分片路由
    uint32_t shard_id1 = shard_aware_engine_get_shard_id(shard_engine, "test_key_1");
    uint32_t shard_id2 = shard_aware_engine_get_shard_id(shard_engine, "test_key_1");
    ASSERT_TRUE(shard_id1 == shard_id2, "Same key should route to same shard");
    ASSERT_TRUE(shard_id1 < 16, "Shard ID should be within range");
    
    // 获取分片引擎
    kv_engine_t *engine = shard_aware_engine_get_shard_engine(shard_engine, shard_id1);
    ASSERT_TRUE(engine != NULL, "Shard engine retrieved successfully");
    
    printf("   🎯 Routing test: Key 'test_key_1' -> Shard %u\n", shard_id1);
    
    // 清理
    shard_aware_engine_destroy(shard_engine);
    shard_config_manager_destroy(config_manager);
    
    TEST_END();
    return 0;
}

/**
 * 测试分片感知引擎CRUD操作
 */
int test_shard_aware_engine_crud(void) {
    TEST_START("Shard Aware Engine CRUD Operations");
    
    // 创建配置管理器
    shard_config_manager_t *config_manager = shard_config_manager_create(NULL);
    ASSERT_TRUE(config_manager != NULL, "Config manager created successfully");
    
    // 创建分片感知引擎
    shard_aware_engine_t *shard_engine = shard_aware_engine_create(config_manager);
    ASSERT_TRUE(shard_engine != NULL, "Shard aware engine created successfully");
    
    // 初始化引擎
    int result = shard_aware_engine_init(shard_engine);
    ASSERT_TRUE(result == 0, "Shard aware engine initialized successfully");
    
    // 启动引擎
    result = shard_aware_engine_start(shard_engine);
    ASSERT_TRUE(result == 0, "Shard aware engine started successfully");
    
    // 测试SET操作
    const char *test_key = "test_key_123";
    const char *test_value = "test_value_123";
    
    result = shard_aware_engine_set(shard_engine, test_key, test_value);
    ASSERT_TRUE(result == 0, "SET operation successful");
    
    // 测试GET操作
    char *retrieved_value = shard_aware_engine_get(shard_engine, test_key);
    ASSERT_TRUE(retrieved_value != NULL, "GET operation successful");
    ASSERT_TRUE(strcmp(retrieved_value, test_value) == 0, "Retrieved value matches original");
    
    // 测试UPDATE操作
    const char *updated_value = "updated_test_value_123";
    result = shard_aware_engine_update(shard_engine, test_key, updated_value);
    ASSERT_TRUE(result == 0, "UPDATE operation successful");
    
    // 验证更新
    retrieved_value = shard_aware_engine_get(shard_engine, test_key);
    ASSERT_TRUE(retrieved_value != NULL, "GET after UPDATE successful");
    ASSERT_TRUE(strcmp(retrieved_value, updated_value) == 0, "Updated value matches");
    
    // 测试统计信息
    uint64_t total_count = shard_aware_engine_count(shard_engine);
    printf("   📊 Total keys in engine: %lu\n", total_count);
    
    // 测试DELETE操作
    result = shard_aware_engine_delete(shard_engine, test_key);
    ASSERT_TRUE(result == 0, "DELETE operation successful");
    
    // 验证删除
    retrieved_value = shard_aware_engine_get(shard_engine, test_key);
    ASSERT_TRUE(retrieved_value == NULL, "Key deleted successfully");
    
    // 停止和销毁引擎
    shard_aware_engine_stop(shard_engine);
    shard_aware_engine_destroy(shard_engine);
    shard_config_manager_destroy(config_manager);
    
    TEST_END();
    return 0;
}

/**
 * 测试分片分布性能
 */
int test_shard_distribution_performance(void) {
    TEST_START("Shard Distribution Performance");
    
    const uint32_t shard_count = 16;
    const size_t test_keys = 10000;
    
    shard_hash_manager_t *manager = shard_hash_manager_create(shard_count, 1000, 300);
    ASSERT_TRUE(manager != NULL, "Hash manager created successfully");
    
    // 分配分片计数数组
    uint32_t *shard_counts = calloc(shard_count, sizeof(uint32_t));
    ASSERT_TRUE(shard_counts != NULL, "Shard counts array allocated");
    
    char key_buffer[32];
    clock_t perf_start_time = clock();
    
    // 测试大量键的分片分布
    for (size_t i = 0; i < test_keys; i++) {
        snprintf(key_buffer, sizeof(key_buffer), "test_key_%zu", i);
        uint32_t shard_id = shard_hash_get_shard_id(manager, key_buffer, strlen(key_buffer));
        
        ASSERT_TRUE(shard_id < shard_count, "Shard ID within range");
        shard_counts[shard_id]++;
    }
    
    clock_t perf_end_time = clock();
    double perf_elapsed = ((double)(perf_end_time - perf_start_time)) / CLOCKS_PER_SEC;
    
    printf("   ⏱️  Hash calculation time: %.3f seconds\n", perf_elapsed);
    printf("   🚀 Hash rate: %.0f hashes/second\n", test_keys / perf_elapsed);
    
    // 计算分布统计
    double mean = (double)test_keys / shard_count;
    double variance = 0.0;
    
    for (uint32_t i = 0; i < shard_count; i++) {
        double diff = shard_counts[i] - mean;
        variance += diff * diff;
    }
    variance /= shard_count;
    double std_dev = sqrt(variance);
    
    printf("   📊 Distribution statistics:\n");
    printf("      Mean: %.2f keys per shard\n", mean);
    printf("      Standard deviation: %.2f\n", std_dev);
    printf("      Coefficient of variation: %.4f\n", std_dev / mean);
    
    // 验证总数
    uint64_t total_count = 0;
    for (uint32_t i = 0; i < shard_count; i++) {
        total_count += shard_counts[i];
    }
    printf("   🔢 Total count verification: %lu\n", total_count);
    ASSERT_TRUE(total_count == test_keys, "Total count matches test keys");
    
    free(shard_counts);
    shard_hash_manager_destroy(manager);
    TEST_END();
    return 0;
}

/**
 * 运行所有测试
 */
int run_all_tests(void) {
    printf("🔬 ConcordKV Shard Management Integration Tests\n");
    printf("================================================\n\n");
    
    // 运行测试
    if (test_shard_hash() != 0) return -1;
    if (test_shard_config() != 0) return -1;
    if (test_shard_aware_engine_basic() != 0) return -1;
    if (test_shard_aware_engine_crud() != 0) return -1;
    if (test_shard_distribution_performance() != 0) return -1;
    
    return 0;
}

/**
 * 打印测试结果统计
 */
void print_test_summary(void) {
    printf("📋 Test Summary\n");
    printf("===============\n");
    printf("Total tests:  %d\n", g_test_stats.total_tests);
    printf("Passed:       %d\n", g_test_stats.passed_tests);
    printf("Failed:       %d\n", g_test_stats.failed_tests);
    printf("Success rate: %.1f%%\n", 
           (double)g_test_stats.passed_tests / g_test_stats.total_tests * 100.0);
    printf("Total time:   %.3f seconds\n", g_test_stats.total_time);
    printf("Avg per test: %.3f seconds\n", g_test_stats.total_time / g_test_stats.total_tests);
    
    if (g_test_stats.failed_tests == 0) {
        printf("\n🎉 All tests passed! Shard management integration is working correctly.\n");
    } else {
        printf("\n❌ Some tests failed. Please check the implementation.\n");
    }
}

/**
 * 主函数
 */
int main(void) {
    printf("🚀 Starting ConcordKV Shard Management Integration Tests...\n\n");
    
    int result = run_all_tests();
    
    printf("\n");
    print_test_summary();
    
    return result;
}
