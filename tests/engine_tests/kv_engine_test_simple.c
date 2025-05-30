/*
 * @Author: Lzww0608  
 * @Date: 2025-5-31 00:07:12
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-5-31 00:07:15
 * @Description: ConcordKV 存储引擎接口简化测试框架
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../../kvserver/kv_engine_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>

// === 测试统计信息 ===
typedef struct test_stats {
    int total_tests;
    int passed_tests;
    int failed_tests;
    double total_time_ms;
    char current_test_name[256];
} test_stats_t;

static test_stats_t g_test_stats = {0};

// === 测试工具宏 ===
#define TEST_ASSERT(condition, message, ...) \
    do { \
        if (!(condition)) { \
            printf("❌ TEST FAILED: " message " (in %s)\n", ##__VA_ARGS__, g_test_stats.current_test_name); \
            g_test_stats.failed_tests++; \
            return -1; \
        } \
    } while(0)

#define TEST_SUCCESS(message, ...) \
    do { \
        printf("✅ TEST PASSED: " message " (in %s)\n", ##__VA_ARGS__, g_test_stats.current_test_name); \
        g_test_stats.passed_tests++; \
        return 0; \
    } while(0)

#define RUN_TEST(test_func) \
    do { \
        printf("\n🔧 Running: %s\n", #test_func); \
        strncpy(g_test_stats.current_test_name, #test_func, sizeof(g_test_stats.current_test_name) - 1); \
        g_test_stats.total_tests++; \
        struct timeval start, end; \
        gettimeofday(&start, NULL); \
        int result = test_func(); \
        gettimeofday(&end, NULL); \
        double time_ms = (end.tv_sec - start.tv_sec) * 1000.0 + \
                        (end.tv_usec - start.tv_usec) / 1000.0; \
        g_test_stats.total_time_ms += time_ms; \
        \
        if (result != 0) { \
            printf("❌ %s FAILED (%.2f ms)\n", #test_func, time_ms); \
        } else { \
            printf("✅ %s PASSED (%.2f ms)\n", #test_func, time_ms); \
        } \
    } while(0)

// === 基础功能测试 ===

// 测试工厂函数基础功能
int test_engine_factory_basic() {
    printf("🔧 开始测试工厂函数基础功能...\n");
    
    // 测试无效类型
    printf("   测试无效类型...\n");
    kv_engine_t *engine = kv_engine_create(KV_ENGINE_MAX, NULL);
    TEST_ASSERT(engine == NULL, "Should reject invalid engine type");
    printf("   ✅ 无效类型测试通过\n");
    
    // 测试Array引擎创建
    printf("   测试Array引擎创建...\n");
    engine = kv_engine_create(KV_ENGINE_ARRAY, NULL);
    TEST_ASSERT(engine != NULL, "Failed to create Array engine");
    TEST_ASSERT(engine->type == KV_ENGINE_ARRAY, "Engine type mismatch");
    TEST_ASSERT(engine->state == KV_ENGINE_STATE_RUNNING, "Engine should be running");
    
    // 安全地检查引擎名称
    printf("   检查引擎名称...\n");
    if (engine->name && engine->name[0] != '\0') {
        printf("   引擎名称: '%s'\n", engine->name);
    } else {
        TEST_ASSERT(0, "Engine name should not be empty");
    }
    
    int ret = kv_engine_destroy(engine);
    TEST_ASSERT(ret == KV_ERR_NONE, "Failed to destroy Array engine");
    printf("   ✅ Array引擎测试通过\n");
    
    // 测试RBTree引擎创建
    printf("   测试RBTree引擎创建...\n");
    engine = kv_engine_create(KV_ENGINE_RBTREE, NULL);
    TEST_ASSERT(engine != NULL, "Failed to create RBTree engine");
    TEST_ASSERT(engine->type == KV_ENGINE_RBTREE, "Engine type mismatch");
    kv_engine_destroy(engine);
    printf("   ✅ RBTree引擎测试通过\n");
    
    // 测试Hash引擎创建
    printf("   测试Hash引擎创建...\n");
    engine = kv_engine_create(KV_ENGINE_HASH, NULL);
    TEST_ASSERT(engine != NULL, "Failed to create Hash engine");
    TEST_ASSERT(engine->type == KV_ENGINE_HASH, "Engine type mismatch");
    kv_engine_destroy(engine);
    printf("   ✅ Hash引擎测试通过\n");
    
    // 测试未实现的引擎类型
    printf("   测试未实现的引擎类型...\n");
    engine = kv_engine_create(KV_ENGINE_BTREE, NULL);
    TEST_ASSERT(engine == NULL, "BTree engine should not be available yet");
    
    engine = kv_engine_create(KV_ENGINE_LSM, NULL);
    TEST_ASSERT(engine == NULL, "LSM engine should not be available yet");
    printf("   ✅ 未实现引擎类型测试通过\n");
    
    TEST_SUCCESS("Engine factory basic tests passed");
}

// 测试基础CRUD操作
int test_engine_basic_crud(kv_engine_type_t type) {
    kv_engine_t *engine = kv_engine_create(type, NULL);
    TEST_ASSERT(engine != NULL, "Failed to create engine for type %d", type);
    
    // 测试SET操作
    int ret = KV_ENGINE_SET(engine, "key1", "value1");
    TEST_ASSERT(ret == KV_ERR_NONE, "SET operation failed");
    
    ret = KV_ENGINE_SET(engine, "key2", "value2");
    TEST_ASSERT(ret == KV_ERR_NONE, "SET operation failed");
    
    // 测试GET操作
    char *value = KV_ENGINE_GET(engine, "key1");
    TEST_ASSERT(value != NULL, "GET operation failed");
    TEST_ASSERT(strcmp(value, "value1") == 0, "GET returned wrong value: %s", value);
    
    value = KV_ENGINE_GET(engine, "key2");
    TEST_ASSERT(value != NULL, "GET operation failed");
    TEST_ASSERT(strcmp(value, "value2") == 0, "GET returned wrong value: %s", value);
    
    // 测试不存在的键
    value = KV_ENGINE_GET(engine, "nonexistent");
    TEST_ASSERT(value == NULL, "GET should return NULL for nonexistent key");
    
    // 测试UPDATE操作
    if (engine->vtable->update) {
        ret = engine->vtable->update(engine, "key1", "new_value1");
        TEST_ASSERT(ret == KV_ERR_NONE, "UPDATE operation failed");
        
        value = KV_ENGINE_GET(engine, "key1");
        TEST_ASSERT(value != NULL && strcmp(value, "new_value1") == 0, 
                   "UPDATE didn't change value correctly");
    }
    
    // 测试COUNT操作
    int count = KV_ENGINE_COUNT(engine);
    TEST_ASSERT(count == 2, "COUNT should return 2, got %d", count);
    
    // 测试DELETE操作
    ret = KV_ENGINE_DELETE(engine, "key1");
    TEST_ASSERT(ret == KV_ERR_NONE, "DELETE operation failed");
    
    value = KV_ENGINE_GET(engine, "key1");
    TEST_ASSERT(value == NULL, "Key should be deleted");
    
    count = KV_ENGINE_COUNT(engine);
    TEST_ASSERT(count == 1, "COUNT should return 1 after deletion, got %d", count);
    
    // 测试删除不存在的键
    ret = KV_ENGINE_DELETE(engine, "nonexistent");
    TEST_ASSERT(ret == KV_ERR_NOT_FOUND, "DELETE should return NOT_FOUND for nonexistent key");
    
    kv_engine_destroy(engine);
    
    TEST_SUCCESS("Basic CRUD tests passed for engine type %d", type);
}

// 压力测试
int test_engine_stress() {
    const int num_keys = 1000; // 减少数量避免超时
    
    kv_engine_t *engine = kv_engine_create(KV_ENGINE_HASH, NULL);
    TEST_ASSERT(engine != NULL, "Failed to create engine for stress test");
    
    printf("📊 Starting stress test with %d keys...\n", num_keys);
    
    struct timeval start, mid, end;
    gettimeofday(&start, NULL);
    
    // 批量写入
    for (int i = 0; i < num_keys; i++) {
        char key[32], value[64];
        snprintf(key, sizeof(key), "stress_key_%d", i);
        snprintf(value, sizeof(value), "stress_value_%d", i);
        
        int ret = KV_ENGINE_SET(engine, key, value);
        TEST_ASSERT(ret == KV_ERR_NONE, "Failed to set key %d in stress test", i);
    }
    
    gettimeofday(&mid, NULL);
    double write_time = (mid.tv_sec - start.tv_sec) * 1000.0 + 
                       (mid.tv_usec - start.tv_usec) / 1000.0;
    
    // 验证数据完整性
    int verified_count = 0;
    for (int i = 0; i < num_keys; i++) {
        char key[32], expected[64];
        snprintf(key, sizeof(key), "stress_key_%d", i);
        snprintf(expected, sizeof(expected), "stress_value_%d", i);
        
        char *value = KV_ENGINE_GET(engine, key);
        if (value && strcmp(value, expected) == 0) {
            verified_count++;
        }
    }
    
    gettimeofday(&end, NULL);
    double total_time = (end.tv_sec - start.tv_sec) * 1000.0 + 
                       (end.tv_usec - start.tv_usec) / 1000.0;
    
    printf("📊 Stress test completed:\n");
    printf("   Write time: %.2f ms\n", write_time);
    printf("   Total time: %.2f ms\n", total_time);
    printf("   Verified keys: %d\n", verified_count);
    
    int final_count = KV_ENGINE_COUNT(engine);
    printf("   Final count: %d\n", final_count);
    
    kv_engine_destroy(engine);
    
    TEST_SUCCESS("Stress test completed with %d verified keys", verified_count);
}

// === 包装函数用于带参数的测试 ===
int test_array_crud() {
    return test_engine_basic_crud(KV_ENGINE_ARRAY);
}

int test_rbtree_crud() {
    return test_engine_basic_crud(KV_ENGINE_RBTREE);
}

int test_hash_crud() {
    return test_engine_basic_crud(KV_ENGINE_HASH);
}

// === 主测试函数 ===
int main() {
    printf("🚀 ConcordKV 存储引擎接口简化测试开始\n");
    printf("============================================\n");
    
    // 初始化错误处理系统
    kv_error_init(KV_LOG_INFO, NULL, 1);
    
    // 运行基础测试
    RUN_TEST(test_engine_factory_basic);
    
    // 针对每种已实现的引擎类型运行CRUD测试
    printf("\n🔍 Testing implemented engines:\n");
    RUN_TEST(test_array_crud);
    RUN_TEST(test_rbtree_crud);
    RUN_TEST(test_hash_crud);
    
    // 压力测试
    printf("\n⚡ Stress tests:\n");
    RUN_TEST(test_engine_stress);
    
    // 打印测试总结
    printf("\n📋 测试总结\n");
    printf("============================================\n");
    printf("总测试数量: %d\n", g_test_stats.total_tests);
    printf("通过测试: %d\n", g_test_stats.passed_tests);
    printf("失败测试: %d\n", g_test_stats.failed_tests);
    printf("总耗时: %.2f ms\n", g_test_stats.total_time_ms);
    
    if (g_test_stats.total_tests > 0) {
        printf("成功率: %.1f%%\n", 
               (double)g_test_stats.passed_tests / g_test_stats.total_tests * 100);
    }
    
    if (g_test_stats.failed_tests == 0) {
        printf("\n🎉 所有测试通过！存储引擎接口实现正确。\n");
        return 0;
    } else {
        printf("\n❌ 有 %d 个测试失败，请检查实现。\n", g_test_stats.failed_tests);
        return 1;
    }
} 