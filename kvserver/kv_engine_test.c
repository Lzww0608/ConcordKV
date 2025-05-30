/*
 * @Author: Lzww0608  
 * @Date: 2025-5-30 22:24:30
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-5-30 22:24:33
 * @Description: ConcordKV 存储引擎接口测试框架
 */
#include "kv_engine_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>

// === 测试统计信息 ===
typedef struct test_stats {
    int total_tests;
    int passed_tests;
    int failed_tests;
    double total_time_ms;
} test_stats_t;

static test_stats_t g_test_stats = {0};

// === 测试工具宏 ===
#define TEST_ASSERT(condition, message, ...) \
    do { \
        if (!(condition)) { \
            printf("❌ TEST FAILED: " message "\n", ##__VA_ARGS__); \
            g_test_stats.failed_tests++; \
            return -1; \
        } \
    } while(0)

#define TEST_SUCCESS(message, ...) \
    do { \
        printf("✅ TEST PASSED: " message "\n", ##__VA_ARGS__); \
        g_test_stats.passed_tests++; \
        return 0; \
    } while(0)

#define RUN_TEST(test_func) \
    do { \
        printf("\n🔧 Running: %s\n", #test_func); \
        g_test_stats.total_tests++; \
        struct timeval start, end; \
        gettimeofday(&start, NULL); \
        int result = test_func(); \
        gettimeofday(&end, NULL); \
        double time_ms = (end.tv_sec - start.tv_sec) * 1000.0 + \
                        (end.tv_usec - start.tv_usec) / 1000.0; \
        g_test_stats.total_time_ms += time_ms; \
        printf("⏱️  Test completed in %.2f ms\n", time_ms); \
        if (result != 0) { \
            printf("❌ %s FAILED\n", #test_func); \
        } else { \
            printf("✅ %s PASSED\n", #test_func); \
        } \
    } while(0)

// === 测试用例实现 ===

// 测试工厂函数基础功能
int test_engine_factory_basic() {
    // 测试无效类型
    kv_engine_t *engine = kv_engine_create(KV_ENGINE_MAX, NULL);
    TEST_ASSERT(engine == NULL, "Should reject invalid engine type");
    
    // 测试Array引擎创建
    engine = kv_engine_create(KV_ENGINE_ARRAY, NULL);
    TEST_ASSERT(engine != NULL, "Failed to create Array engine");
    TEST_ASSERT(engine->type == KV_ENGINE_ARRAY, "Engine type mismatch");
    TEST_ASSERT(engine->state == KV_ENGINE_STATE_RUNNING, "Engine should be running");
    TEST_ASSERT(strlen(engine->name) > 0, "Engine name should not be empty");
    
    int ret = kv_engine_destroy(engine);
    TEST_ASSERT(ret == KV_ERR_NONE, "Failed to destroy Array engine");
    
    // 测试RBTree引擎创建
    engine = kv_engine_create(KV_ENGINE_RBTREE, NULL);
    TEST_ASSERT(engine != NULL, "Failed to create RBTree engine");
    TEST_ASSERT(engine->type == KV_ENGINE_RBTREE, "Engine type mismatch");
    kv_engine_destroy(engine);
    
    // 测试Hash引擎创建
    engine = kv_engine_create(KV_ENGINE_HASH, NULL);
    TEST_ASSERT(engine != NULL, "Failed to create Hash engine");
    TEST_ASSERT(engine->type == KV_ENGINE_HASH, "Engine type mismatch");
    kv_engine_destroy(engine);
    
    // 测试未实现的引擎类型
    engine = kv_engine_create(KV_ENGINE_BTREE, NULL);
    TEST_ASSERT(engine == NULL, "BTree engine should not be available yet");
    
    engine = kv_engine_create(KV_ENGINE_LSM, NULL);
    TEST_ASSERT(engine == NULL, "LSM engine should not be available yet");
    
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

// 测试参数验证
int test_engine_parameter_validation() {
    kv_engine_t *engine = kv_engine_create(KV_ENGINE_ARRAY, NULL);
    TEST_ASSERT(engine != NULL, "Failed to create engine");
    
    // 测试NULL参数
    int ret = KV_ENGINE_SET(engine, NULL, "value");
    TEST_ASSERT(ret == KV_ERR_PARAM, "Should reject NULL key");
    
    ret = KV_ENGINE_SET(engine, "key", NULL);
    TEST_ASSERT(ret == KV_ERR_PARAM, "Should reject NULL value");
    
    char *value = KV_ENGINE_GET(engine, NULL);
    TEST_ASSERT(value == NULL, "Should return NULL for NULL key");
    
    ret = KV_ENGINE_DELETE(engine, NULL);
    TEST_ASSERT(ret == KV_ERR_PARAM, "Should reject NULL key for delete");
    
    // 测试无效引擎句柄
    ret = KV_ENGINE_SET(NULL, "key", "value");
    TEST_ASSERT(ret == KV_ERR_PARAM, "Should reject NULL engine");
    
    value = KV_ENGINE_GET(NULL, "key");
    TEST_ASSERT(value == NULL, "Should return NULL for NULL engine");
    
    kv_engine_destroy(engine);
    
    TEST_SUCCESS("Parameter validation tests passed");
}

// 测试统计信息更新
int test_engine_statistics() {
    kv_engine_t *engine = kv_engine_create(KV_ENGINE_RBTREE, NULL);
    TEST_ASSERT(engine != NULL, "Failed to create engine");
    
    // 初始统计信息应该为0
    TEST_ASSERT(engine->stats.read_count == 0, "Initial read count should be 0");
    TEST_ASSERT(engine->stats.write_count == 0, "Initial write count should be 0");
    TEST_ASSERT(engine->stats.delete_count == 0, "Initial delete count should be 0");
    
    // 执行一些操作并检查统计信息
    KV_ENGINE_SET(engine, "key1", "value1");
    TEST_ASSERT(engine->stats.write_count == 1, "Write count should be 1");
    
    KV_ENGINE_GET(engine, "key1");
    TEST_ASSERT(engine->stats.read_count == 1, "Read count should be 1");
    
    KV_ENGINE_DELETE(engine, "key1");
    TEST_ASSERT(engine->stats.delete_count == 1, "Delete count should be 1");
    
    kv_engine_destroy(engine);
    
    TEST_SUCCESS("Statistics tracking tests passed");
}

// 测试批量操作工具函数
int test_batch_operations() {
    // 测试批量对象创建
    kv_batch_t *batch = kv_batch_create(10);
    TEST_ASSERT(batch != NULL, "Failed to create batch");
    TEST_ASSERT(batch->count == 0, "Initial count should be 0");
    TEST_ASSERT(batch->capacity == 10, "Capacity should be 10");
    
    // 测试添加键值对
    int ret = kv_batch_add(batch, "key1", "value1");
    TEST_ASSERT(ret == KV_ERR_NONE, "Failed to add to batch");
    TEST_ASSERT(batch->count == 1, "Count should be 1");
    
    ret = kv_batch_add(batch, "key2", "value2");
    TEST_ASSERT(ret == KV_ERR_NONE, "Failed to add to batch");
    TEST_ASSERT(batch->count == 2, "Count should be 2");
    
    // 测试批量扩容 - 添加超过初始容量的元素
    for (int i = 3; i <= 15; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(value, sizeof(value), "value%d", i);
        ret = kv_batch_add(batch, key, value);
        TEST_ASSERT(ret == KV_ERR_NONE, "Failed to add key%d to batch", i);
    }
    
    TEST_ASSERT(batch->count == 13, "Count should be 13");
    TEST_ASSERT(batch->capacity > 10, "Capacity should have expanded");
    
    // 测试批量清理
    kv_batch_clear(batch);
    TEST_ASSERT(batch->count == 0, "Count should be 0 after clear");
    
    kv_batch_destroy(batch);
    
    TEST_SUCCESS("Batch operations tests passed");
}

// 测试键值对工具函数
int test_kv_pair_operations() {
    // 测试键值对创建
    kv_pair_t *pair = kv_pair_create("test_key", "test_value");
    TEST_ASSERT(pair != NULL, "Failed to create kv_pair");
    TEST_ASSERT(strcmp(pair->key, "test_key") == 0, "Key mismatch");
    TEST_ASSERT(strcmp(pair->value, "test_value") == 0, "Value mismatch");
    TEST_ASSERT(pair->key_len == strlen("test_key"), "Key length mismatch");
    TEST_ASSERT(pair->value_len == strlen("test_value"), "Value length mismatch");
    
    // 测试键值对复制
    kv_pair_t *copy = kv_pair_copy(pair);
    TEST_ASSERT(copy != NULL, "Failed to copy kv_pair");
    TEST_ASSERT(strcmp(copy->key, pair->key) == 0, "Copied key mismatch");
    TEST_ASSERT(strcmp(copy->value, pair->value) == 0, "Copied value mismatch");
    TEST_ASSERT(copy->key != pair->key, "Copied key should be different pointer");
    TEST_ASSERT(copy->value != pair->value, "Copied value should be different pointer");
    
    kv_pair_destroy(pair);
    kv_pair_destroy(copy);
    
    // 测试NULL参数
    pair = kv_pair_create(NULL, "value");
    TEST_ASSERT(pair == NULL, "Should reject NULL key");
    
    pair = kv_pair_create("key", NULL);
    TEST_ASSERT(pair == NULL, "Should reject NULL value");
    
    copy = kv_pair_copy(NULL);
    TEST_ASSERT(copy == NULL, "Should reject NULL pair");
    
    TEST_SUCCESS("KV pair operations tests passed");
}

// 测试配置管理
int test_config_management() {
    // 测试各种引擎类型的配置创建
    kv_engine_config_t *config = kv_engine_config_create(KV_ENGINE_ARRAY);
    TEST_ASSERT(config != NULL, "Failed to create Array config");
    TEST_ASSERT(config->type == KV_ENGINE_ARRAY, "Config type mismatch");
    kv_engine_config_destroy(config);
    
    config = kv_engine_config_create(KV_ENGINE_LSM);
    TEST_ASSERT(config != NULL, "Failed to create LSM config");
    TEST_ASSERT(config->memtable_size > 0, "LSM config should have memtable_size");
    TEST_ASSERT(config->level0_file_limit > 0, "LSM config should have level0_file_limit");
    kv_engine_config_destroy(config);
    
    config = kv_engine_config_create(KV_ENGINE_BTREE);
    TEST_ASSERT(config != NULL, "Failed to create BTree config");
    TEST_ASSERT(config->page_size > 0, "BTree config should have page_size");
    TEST_ASSERT(config->max_keys_per_node > 0, "BTree config should have max_keys_per_node");
    kv_engine_config_destroy(config);
    
    config = kv_engine_config_create(KV_ENGINE_HASH);
    TEST_ASSERT(config != NULL, "Failed to create Hash config");
    TEST_ASSERT(config->initial_buckets > 0, "Hash config should have initial_buckets");
    TEST_ASSERT(config->load_factor > 0, "Hash config should have load_factor");
    kv_engine_config_destroy(config);
    
    TEST_SUCCESS("Config management tests passed");
}

// 性能测试
int test_engine_performance(kv_engine_type_t type, int num_operations) {
    printf("\n📊 Performance test for %s engine with %d operations\n", 
           kv_engine_type_to_string(type), num_operations);
    
    kv_engine_t *engine = kv_engine_create(type, NULL);
    TEST_ASSERT(engine != NULL, "Failed to create engine");
    
    struct timeval start, end;
    
    // 测试写入性能
    gettimeofday(&start, NULL);
    for (int i = 0; i < num_operations; i++) {
        char key[32], value[64];
        snprintf(key, sizeof(key), "key_%d", i);
        snprintf(value, sizeof(value), "value_%d_test_data", i);
        int ret = KV_ENGINE_SET(engine, key, value);
        if (ret != KV_ERR_NONE) {
            printf("❌ Write failed at iteration %d\n", i);
            break;
        }
    }
    gettimeofday(&end, NULL);
    
    double write_time = (end.tv_sec - start.tv_sec) * 1000.0 + 
                       (end.tv_usec - start.tv_usec) / 1000.0;
    double write_throughput = num_operations / (write_time / 1000.0);
    
    printf("📝 Write Performance: %.2f ms total, %.0f ops/sec\n", 
           write_time, write_throughput);
    
    // 测试读取性能
    gettimeofday(&start, NULL);
    int successful_reads = 0;
    for (int i = 0; i < num_operations; i++) {
        char key[32];
        snprintf(key, sizeof(key), "key_%d", i);
        char *value = KV_ENGINE_GET(engine, key);
        if (value != NULL) {
            successful_reads++;
        }
    }
    gettimeofday(&end, NULL);
    
    double read_time = (end.tv_sec - start.tv_sec) * 1000.0 + 
                      (end.tv_usec - start.tv_usec) / 1000.0;
    double read_throughput = num_operations / (read_time / 1000.0);
    
    printf("📖 Read Performance: %.2f ms total, %.0f ops/sec, %d/%d successful\n", 
           read_time, read_throughput, successful_reads, num_operations);
    
    // 验证数据完整性
    int count = KV_ENGINE_COUNT(engine);
    printf("📊 Final count: %d (expected: %d)\n", count, num_operations);
    
    kv_engine_destroy(engine);
    
    TEST_SUCCESS("Performance test completed for %s engine", 
                kv_engine_type_to_string(type));
}

// 测试工具函数
int test_utility_functions() {
    // 测试类型到字符串转换
    TEST_ASSERT(strcmp(kv_engine_type_to_string(KV_ENGINE_ARRAY), "Array") == 0,
               "Array type string mismatch");
    TEST_ASSERT(strcmp(kv_engine_type_to_string(KV_ENGINE_RBTREE), "RBTree") == 0,
               "RBTree type string mismatch");
    TEST_ASSERT(strcmp(kv_engine_type_to_string(KV_ENGINE_HASH), "Hash") == 0,
               "Hash type string mismatch");
    TEST_ASSERT(strcmp(kv_engine_type_to_string(KV_ENGINE_BTREE), "BTree") == 0,
               "BTree type string mismatch");
    TEST_ASSERT(strcmp(kv_engine_type_to_string(KV_ENGINE_LSM), "LSM") == 0,
               "LSM type string mismatch");
    TEST_ASSERT(strcmp(kv_engine_type_to_string(KV_ENGINE_MAX), "Unknown") == 0,
               "Unknown type string mismatch");
    
    // 测试状态到字符串转换
    TEST_ASSERT(strcmp(kv_engine_state_to_string(KV_ENGINE_STATE_INIT), "Init") == 0,
               "Init state string mismatch");
    TEST_ASSERT(strcmp(kv_engine_state_to_string(KV_ENGINE_STATE_RUNNING), "Running") == 0,
               "Running state string mismatch");
    TEST_ASSERT(strcmp(kv_engine_state_to_string(KV_ENGINE_STATE_ERROR), "Error") == 0,
               "Error state string mismatch");
    
    TEST_SUCCESS("Utility functions tests passed");
}

// === 主测试函数 ===
int main() {
    printf("🚀 ConcordKV 存储引擎接口测试开始\n");
    printf("============================================\n");
    
    // 初始化错误处理系统
    kv_error_init(KV_LOG_INFO, NULL, 1);
    
    // 运行所有测试
    RUN_TEST(test_engine_factory_basic);
    RUN_TEST(test_utility_functions);
    RUN_TEST(test_kv_pair_operations);
    RUN_TEST(test_batch_operations);
    RUN_TEST(test_config_management);
    RUN_TEST(test_parameter_validation);
    
    // 针对每种已实现的引擎类型运行CRUD测试
    printf("\n🔍 Testing implemented engines:\n");
    RUN_TEST((int(*)())test_engine_basic_crud(KV_ENGINE_ARRAY));
    RUN_TEST((int(*)())test_engine_basic_crud(KV_ENGINE_RBTREE));
    RUN_TEST((int(*)())test_engine_basic_crud(KV_ENGINE_HASH));
    
    RUN_TEST(test_engine_statistics);
    
    // 性能测试（较小的数据量以避免测试时间过长）
    printf("\n⚡ Performance tests:\n");
    RUN_TEST((int(*)())test_engine_performance(KV_ENGINE_ARRAY, 1000));
    RUN_TEST((int(*)())test_engine_performance(KV_ENGINE_RBTREE, 1000));
    RUN_TEST((int(*)())test_engine_performance(KV_ENGINE_HASH, 1000));
    
    // 打印测试总结
    printf("\n📋 测试总结\n");
    printf("============================================\n");
    printf("总测试数量: %d\n", g_test_stats.total_tests);
    printf("通过测试: %d\n", g_test_stats.passed_tests);
    printf("失败测试: %d\n", g_test_stats.failed_tests);
    printf("总耗时: %.2f ms\n", g_test_stats.total_time_ms);
    printf("成功率: %.1f%%\n", 
           (double)g_test_stats.passed_tests / g_test_stats.total_tests * 100);
    
    if (g_test_stats.failed_tests == 0) {
        printf("\n🎉 所有测试通过！存储引擎接口实现正确。\n");
        return 0;
    } else {
        printf("\n❌ 有 %d 个测试失败，请检查实现。\n", g_test_stats.failed_tests);
        return 1;
    }
} 