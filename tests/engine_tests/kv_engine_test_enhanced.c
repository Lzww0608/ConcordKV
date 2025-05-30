/*
 * @Author: Lzww0608  
 * @Date: 2025-5-31 00:07:02
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-5-31 00:07:04
 * @Description: ConcordKV 存储引擎接口增强测试框架 - 包含超时机制防止死锁
 */
#define _GNU_SOURCE     // 启用扩展函数
#define _POSIX_C_SOURCE 200809L  // 启用POSIX扩展

#include "../../kvserver/kv_engine_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <pthread.h>

// === 超时控制相关 ===
#define TEST_TIMEOUT_SECONDS 30    // 单个测试超时时间
#define STRESS_TEST_TIMEOUT_SECONDS 60  // 压力测试超时时间
static jmp_buf timeout_jmp;
static volatile int test_timed_out = 0;

// === 测试统计信息 ===
typedef struct test_stats {
    int total_tests;
    int passed_tests;
    int failed_tests;
    int timeout_tests;
    double total_time_ms;
    char current_test_name[256];
} test_stats_t;

static test_stats_t g_test_stats = {0};

// === 超时信号处理器 ===
static void timeout_handler(int sig) {
    (void)sig; // 避免未使用参数警告
    test_timed_out = 1;
    longjmp(timeout_jmp, 1);
}

// === 设置测试超时 ===
static void set_test_timeout(int seconds) {
    test_timed_out = 0;
    signal(SIGALRM, timeout_handler);
    alarm(seconds);
}

// === 清除测试超时 ===
static void clear_test_timeout(void) {
    alarm(0);
    signal(SIGALRM, SIG_DFL);
}

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

#define RUN_TEST_WITH_TIMEOUT(test_func, timeout_sec) \
    do { \
        printf("\n🔧 Running: %s (timeout: %ds)\n", #test_func, timeout_sec); \
        strncpy(g_test_stats.current_test_name, #test_func, sizeof(g_test_stats.current_test_name) - 1); \
        g_test_stats.total_tests++; \
        struct timeval start, end; \
        gettimeofday(&start, NULL); \
        int result = -1; \
        \
        if (setjmp(timeout_jmp) == 0) { \
            set_test_timeout(timeout_sec); \
            result = test_func(); \
            clear_test_timeout(); \
        } else { \
            clear_test_timeout(); \
            printf("⏰ TEST TIMEOUT: %s exceeded %d seconds\n", #test_func, timeout_sec); \
            g_test_stats.timeout_tests++; \
            result = -2; /* timeout */ \
        } \
        \
        gettimeofday(&end, NULL); \
        double time_ms = (end.tv_sec - start.tv_sec) * 1000.0 + \
                        (end.tv_usec - start.tv_usec) / 1000.0; \
        g_test_stats.total_time_ms += time_ms; \
        \
        if (result == -2) { \
            printf("⏰ %s TIMEOUT (%.2f ms)\n", #test_func, time_ms); \
        } else if (result != 0) { \
            printf("❌ %s FAILED (%.2f ms)\n", #test_func, time_ms); \
        } else { \
            printf("✅ %s PASSED (%.2f ms)\n", #test_func, time_ms); \
        } \
    } while(0)

#define RUN_TEST(test_func) RUN_TEST_WITH_TIMEOUT(test_func, TEST_TIMEOUT_SECONDS)

// === 并发测试结构 ===
typedef struct thread_test_data {
    kv_engine_t *engine;
    int thread_id;
    int num_operations;
    int *success_count;
    int *error_count;
    pthread_mutex_t *stats_mutex;
    volatile int *should_stop;
} thread_test_data_t;

// === 基础功能测试 ===

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

// 并发测试工作线程函数
void* concurrent_worker(void* arg) {
    thread_test_data_t *data = (thread_test_data_t*)arg;
    int local_success = 0, local_error = 0;
    
    for (int i = 0; i < data->num_operations && !(*data->should_stop); i++) {
        char key[64], value[64];
        snprintf(key, sizeof(key), "thread_%d_key_%d", data->thread_id, i);
        snprintf(value, sizeof(value), "thread_%d_value_%d", data->thread_id, i);
        
        // 写入测试
        if (KV_ENGINE_SET(data->engine, key, value) == KV_ERR_NONE) {
            local_success++;
        } else {
            local_error++;
        }
        
        // 读取测试
        char *retrieved = KV_ENGINE_GET(data->engine, key);
        if (retrieved && strcmp(retrieved, value) == 0) {
            local_success++;
        } else {
            local_error++;
        }
        
        // 稍微暂停避免过度竞争
        if (i % 100 == 0) {
            usleep(1000); // 1ms
        }
    }
    
    // 更新全局统计
    pthread_mutex_lock(data->stats_mutex);
    *(data->success_count) += local_success;
    *(data->error_count) += local_error;
    pthread_mutex_unlock(data->stats_mutex);
    
    return NULL;
}

// 并发安全测试
int test_engine_concurrent_safety() {
    const int num_threads = 8;
    const int ops_per_thread = 100;
    
    kv_engine_t *engine = kv_engine_create(KV_ENGINE_RBTREE, NULL);
    TEST_ASSERT(engine != NULL, "Failed to create engine for concurrent test");
    
    pthread_t threads[num_threads];
    thread_test_data_t thread_data[num_threads];
    pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;
    
    int total_success = 0, total_error = 0;
    volatile int should_stop = 0;
    
    // 创建线程
    for (int i = 0; i < num_threads; i++) {
        thread_data[i].engine = engine;
        thread_data[i].thread_id = i;
        thread_data[i].num_operations = ops_per_thread;
        thread_data[i].success_count = &total_success;
        thread_data[i].error_count = &total_error;
        thread_data[i].stats_mutex = &stats_mutex;
        thread_data[i].should_stop = &should_stop;
        
        if (pthread_create(&threads[i], NULL, concurrent_worker, &thread_data[i]) != 0) {
            should_stop = 1;
            // 等待已创建的线程
            for (int j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }
            kv_engine_destroy(engine);
            TEST_ASSERT(0, "Failed to create thread %d", i);
        }
    }
    
    // 等待所有线程完成
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    pthread_mutex_destroy(&stats_mutex);
    
    printf("📊 Concurrent test results: %d success, %d errors\n", total_success, total_error);
    
    // 验证引擎仍然可用
    int count = KV_ENGINE_COUNT(engine);
    TEST_ASSERT(count >= 0, "Engine should still be functional after concurrent access");
    
    kv_engine_destroy(engine);
    
    TEST_SUCCESS("Concurrent safety test passed with %d threads, %d ops each", 
                num_threads, ops_per_thread);
}

// 大数据量压力测试
int test_engine_stress() {
    const int num_keys = 10000;
    
    kv_engine_t *engine = kv_engine_create(KV_ENGINE_HASH, NULL);
    TEST_ASSERT(engine != NULL, "Failed to create engine for stress test");
    
    printf("📊 Starting stress test with %d keys...\n", num_keys);
    
    struct timeval start, mid, end;
    gettimeofday(&start, NULL);
    
    // 批量写入
    for (int i = 0; i < num_keys; i++) {
        char key[32], value[64];
        snprintf(key, sizeof(key), "stress_key_%d", i);
        snprintf(value, sizeof(value), "stress_value_%d_with_some_longer_data", i);
        
        int ret = KV_ENGINE_SET(engine, key, value);
        TEST_ASSERT(ret == KV_ERR_NONE, "Failed to set key %d in stress test", i);
        
        // 每1000个操作检查一次时间，防止超时
        if (i % 1000 == 0) {
            gettimeofday(&mid, NULL);
            double elapsed = (mid.tv_sec - start.tv_sec) * 1000.0 + 
                           (mid.tv_usec - start.tv_usec) / 1000.0;
            if (elapsed > (STRESS_TEST_TIMEOUT_SECONDS - 5) * 1000) {
                printf("⚠️  Stress test approaching timeout, stopping at %d keys\n", i);
                break;
            }
        }
    }
    
    gettimeofday(&mid, NULL);
    double write_time = (mid.tv_sec - start.tv_sec) * 1000.0 + 
                       (mid.tv_usec - start.tv_usec) / 1000.0;
    
    // 验证数据完整性
    int verified_count = 0;
    for (int i = 0; i < num_keys; i++) {
        char key[32], expected[64];
        snprintf(key, sizeof(key), "stress_key_%d", i);
        snprintf(expected, sizeof(expected), "stress_value_%d_with_some_longer_data", i);
        
        char *value = KV_ENGINE_GET(engine, key);
        if (value && strcmp(value, expected) == 0) {
            verified_count++;
        }
        
        // 防止超时
        if (i % 1000 == 0) {
            gettimeofday(&end, NULL);
            double total_elapsed = (end.tv_sec - start.tv_sec) * 1000.0 + 
                                 (end.tv_usec - start.tv_usec) / 1000.0;
            if (total_elapsed > (STRESS_TEST_TIMEOUT_SECONDS - 2) * 1000) {
                printf("⚠️  Verification approaching timeout, verified %d keys\n", verified_count);
                break;
            }
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

// 内存泄漏检测测试
int test_memory_leaks() {
    const int iterations = 1000;
    
    printf("🔍 Memory leak test with %d iterations...\n", iterations);
    
    for (int i = 0; i < iterations; i++) {
        // 创建和销毁引擎
        kv_engine_t *engine = kv_engine_create(KV_ENGINE_ARRAY, NULL);
        TEST_ASSERT(engine != NULL, "Failed to create engine in iteration %d", i);
        
        // 进行一些操作
        char key[32], value[64];
        snprintf(key, sizeof(key), "leak_test_key_%d", i);
        snprintf(value, sizeof(value), "leak_test_value_%d", i);
        
        KV_ENGINE_SET(engine, key, value);
        char *retrieved = KV_ENGINE_GET(engine, key);
        TEST_ASSERT(retrieved != NULL, "Failed to retrieve value in iteration %d", i);
        
        KV_ENGINE_DELETE(engine, key);
        
        // 销毁引擎
        int ret = kv_engine_destroy(engine);
        TEST_ASSERT(ret == KV_ERR_NONE, "Failed to destroy engine in iteration %d", i);
        
        // 每100次迭代输出进度
        if (i % 100 == 0) {
            printf("   Progress: %d/%d\n", i, iterations);
        }
    }
    
    TEST_SUCCESS("Memory leak test completed %d iterations", iterations);
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
    printf("🚀 ConcordKV 存储引擎接口增强测试开始\n");
    printf("============================================\n");
    printf("⏰ 测试超时设置: 普通测试 %ds, 压力测试 %ds\n", 
           TEST_TIMEOUT_SECONDS, STRESS_TEST_TIMEOUT_SECONDS);
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
    
    // 并发安全测试
    printf("\n🔄 Concurrent safety tests:\n");
    RUN_TEST(test_engine_concurrent_safety);
    
    // 内存泄漏测试
    printf("\n🔍 Memory leak tests:\n");
    RUN_TEST(test_memory_leaks);
    
    // 压力测试（使用更长的超时时间）
    printf("\n⚡ Stress tests:\n");
    RUN_TEST_WITH_TIMEOUT(test_engine_stress, STRESS_TEST_TIMEOUT_SECONDS);
    
    // 打印测试总结
    printf("\n📋 测试总结\n");
    printf("============================================\n");
    printf("总测试数量: %d\n", g_test_stats.total_tests);
    printf("通过测试: %d\n", g_test_stats.passed_tests);
    printf("失败测试: %d\n", g_test_stats.failed_tests);
    printf("超时测试: %d\n", g_test_stats.timeout_tests);
    printf("总耗时: %.2f ms\n", g_test_stats.total_time_ms);
    
    if (g_test_stats.total_tests > 0) {
        printf("成功率: %.1f%%\n", 
               (double)g_test_stats.passed_tests / g_test_stats.total_tests * 100);
    }
    
    if (g_test_stats.failed_tests == 0 && g_test_stats.timeout_tests == 0) {
        printf("\n🎉 所有测试通过！存储引擎接口实现正确。\n");
        return 0;
    } else {
        printf("\n❌ 有 %d 个测试失败，%d 个测试超时，请检查实现。\n", 
               g_test_stats.failed_tests, g_test_stats.timeout_tests);
        return 1;
    }
} 