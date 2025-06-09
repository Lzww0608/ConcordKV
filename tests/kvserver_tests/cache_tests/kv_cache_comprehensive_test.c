/*
 * @Author: Lzww0608  
 * @Date: 2025-6-9 20:15:22
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-9 20:15:22
 * @Description: ConcordKV 缓存层综合测试程序
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>

#include "../../../kvserver/kv_cache.h"
#include "../../../kvserver/kv_error.h"

// 测试配置
#define TEST_CACHE_SIZE 1000
#define TEST_KEY_COUNT 500
#define TEST_THREAD_COUNT 4
#define TEST_OPERATIONS_PER_THREAD 1000

// 辅助函数
static void test_print_result(const char *test_name, bool passed) {
    printf("[%s] %s: %s\n", 
           passed ? "PASS" : "FAIL", 
           test_name, 
           passed ? "✓" : "✗");
}

static uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

// === 基础功能测试 ===

bool test_cache_create_destroy(void) {
    printf("\n=== 测试缓存创建和销毁 ===\n");
    
    // 创建默认配置
    kv_cache_config_t *config = kv_cache_config_create_default(KV_CACHE_POLICY_LRU);
    assert(config != NULL);
    
    // 修改配置为测试值
    config->max_entries = TEST_CACHE_SIZE;
    config->hash_table_size = 128;
    
    // 验证配置
    bool config_valid = kv_cache_config_validate(config);
    test_print_result("配置验证", config_valid);
    
    // 创建缓存
    kv_cache_t *cache = kv_cache_create(config);
    test_print_result("缓存创建", cache != NULL);
    
    if (cache) {
        // 获取初始统计信息
        kv_cache_stats_t stats;
        int stats_result = kv_cache_get_stats(cache, &stats);
        test_print_result("获取统计信息", stats_result == KV_SUCCESS);
        test_print_result("初始条目数为零", stats.current_entries == 0);
        
        // 销毁缓存
        kv_cache_destroy(cache);
        printf("缓存销毁完成\n");
    }
    
    // 销毁配置
    kv_cache_config_destroy(config);
    
    return config_valid && (cache != NULL);
}

bool test_basic_operations(void) {
    printf("\n=== 测试基础操作 ===\n");
    
    kv_cache_config_t *config = kv_cache_config_create_default(KV_CACHE_POLICY_LRU);
    config->max_entries = TEST_CACHE_SIZE;
    
    kv_cache_t *cache = kv_cache_create(config);
    assert(cache != NULL);
    
    bool all_passed = true;
    
    // 测试SET操作
    const char *test_key = "test_key_1";
    const char *test_value = "test_value_1";
    
    int set_result = kv_cache_set(cache, test_key, strlen(test_key), 
                                  test_value, strlen(test_value), 0);
    test_print_result("SET操作", set_result == KV_SUCCESS);
    all_passed &= (set_result == KV_SUCCESS);
    
    // 测试EXISTS操作
    bool exists = kv_cache_exists(cache, test_key, strlen(test_key));
    test_print_result("EXISTS操作", exists);
    all_passed &= exists;
    
    // 测试GET操作
    char *retrieved_value = NULL;
    size_t retrieved_len = 0;
    int get_result = kv_cache_get(cache, test_key, strlen(test_key), 
                                  &retrieved_value, &retrieved_len);
    test_print_result("GET操作", get_result == KV_SUCCESS);
    
    if (get_result == KV_SUCCESS && retrieved_value) {
        bool value_match = (retrieved_len == strlen(test_value)) && 
                          (strcmp(retrieved_value, test_value) == 0);
        test_print_result("GET值匹配", value_match);
        all_passed &= value_match;
        free(retrieved_value);
    } else {
        all_passed = false;
    }
    
    // 测试更新操作
    const char *new_value = "updated_value_1";
    int update_result = kv_cache_set(cache, test_key, strlen(test_key), 
                                     new_value, strlen(new_value), 0);
    test_print_result("UPDATE操作", update_result == KV_SUCCESS);
    all_passed &= (update_result == KV_SUCCESS);
    
    // 验证更新后的值
    char *updated_value = NULL;
    size_t updated_len = 0;
    get_result = kv_cache_get(cache, test_key, strlen(test_key), 
                             &updated_value, &updated_len);
    if (get_result == KV_SUCCESS && updated_value) {
        bool update_match = (updated_len == strlen(new_value)) && 
                           (strcmp(updated_value, new_value) == 0);
        test_print_result("UPDATE值验证", update_match);
        all_passed &= update_match;
        free(updated_value);
    }
    
    // 测试DELETE操作
    int delete_result = kv_cache_delete(cache, test_key, strlen(test_key));
    test_print_result("DELETE操作", delete_result == KV_SUCCESS);
    all_passed &= (delete_result == KV_SUCCESS);
    
    // 验证删除后不存在
    bool exists_after_delete = kv_cache_exists(cache, test_key, strlen(test_key));
    test_print_result("DELETE后不存在", !exists_after_delete);
    all_passed &= !exists_after_delete;
    
    kv_cache_destroy(cache);
    kv_cache_config_destroy(config);
    
    return all_passed;
}

bool test_ttl_functionality(void) {
    printf("\n=== 测试TTL功能 ===\n");
    
    kv_cache_config_t *config = kv_cache_config_create_default(KV_CACHE_POLICY_LRU);
    config->max_entries = TEST_CACHE_SIZE;
    
    kv_cache_t *cache = kv_cache_create(config);
    assert(cache != NULL);
    
    bool all_passed = true;
    
    // 设置短TTL的键值对（1秒）
    const char *ttl_key = "ttl_test_key";
    const char *ttl_value = "ttl_test_value";
    uint64_t short_ttl = 1000000ULL; // 1秒（微秒）
    
    int set_result = kv_cache_set(cache, ttl_key, strlen(ttl_key), 
                                  ttl_value, strlen(ttl_value), short_ttl);
    test_print_result("SET TTL键", set_result == KV_SUCCESS);
    all_passed &= (set_result == KV_SUCCESS);
    
    // 立即检查存在性
    bool exists_immediately = kv_cache_exists(cache, ttl_key, strlen(ttl_key));
    test_print_result("TTL键立即存在", exists_immediately);
    all_passed &= exists_immediately;
    
    // 等待TTL过期
    printf("等待TTL过期...\n");
    sleep(2);
    
    // 检查过期后是否还存在
    bool exists_after_ttl = kv_cache_exists(cache, ttl_key, strlen(ttl_key));
    test_print_result("TTL过期后不存在", !exists_after_ttl);
    all_passed &= !exists_after_ttl;
    
    kv_cache_destroy(cache);
    kv_cache_config_destroy(config);
    
    return all_passed;
}

bool test_lru_eviction(void) {
    printf("\n=== 测试LRU淘汰机制 ===\n");
    
    kv_cache_config_t *config = kv_cache_config_create_default(KV_CACHE_POLICY_LRU);
    config->max_entries = 5; // 小缓存用于测试淘汰
    config->eviction_factor = 0.4f; // 40%淘汰率
    
    kv_cache_t *cache = kv_cache_create(config);
    assert(cache != NULL);
    
    bool all_passed = true;
    
    // 填满缓存
    for (int i = 0; i < 5; i++) {
        char key[64], value[64];
        snprintf(key, sizeof(key), "key_%d", i);
        snprintf(value, sizeof(value), "value_%d", i);
        
        int result = kv_cache_set(cache, key, strlen(key), value, strlen(value), 0);
        all_passed &= (result == KV_SUCCESS);
    }
    
    kv_cache_stats_t stats;
    kv_cache_get_stats(cache, &stats);
    test_print_result("缓存已满", stats.current_entries == 5);
    all_passed &= (stats.current_entries == 5);
    
    // 访问一些键以建立LRU顺序
    char *value = NULL;
    size_t value_len = 0;
    kv_cache_get(cache, "key_0", 5, &value, &value_len);
    if (value) free(value);
    kv_cache_get(cache, "key_1", 5, &value, &value_len);
    if (value) free(value);
    
    // 添加新键触发淘汰
    int new_result = kv_cache_set(cache, "new_key", 7, "new_value", 9, 0);
    test_print_result("触发淘汰的SET", new_result == KV_SUCCESS);
    all_passed &= (new_result == KV_SUCCESS);
    
    // 检查统计信息
    kv_cache_get_stats(cache, &stats);
    test_print_result("发生了淘汰", stats.evictions > 0);
    all_passed &= (stats.evictions > 0);
    
    printf("当前条目数: %zu, 淘汰次数: %llu\n", 
           stats.current_entries, (unsigned long long)stats.evictions);
    
    kv_cache_destroy(cache);
    kv_cache_config_destroy(config);
    
    return all_passed;
}

// === 性能测试 ===

bool test_performance(void) {
    printf("\n=== 性能测试 ===\n");
    
    kv_cache_config_t *config = kv_cache_config_create_default(KV_CACHE_POLICY_LRU);
    config->max_entries = TEST_KEY_COUNT * 2;
    
    kv_cache_t *cache = kv_cache_create(config);
    assert(cache != NULL);
    
    bool all_passed = true;
    
    // SET性能测试
    uint64_t start_time = get_time_us();
    
    for (int i = 0; i < TEST_KEY_COUNT; i++) {
        char key[64], value[256];
        snprintf(key, sizeof(key), "perf_key_%d", i);
        snprintf(value, sizeof(value), "performance_test_value_%d_with_more_data", i);
        
        int result = kv_cache_set(cache, key, strlen(key), value, strlen(value), 0);
        all_passed &= (result == KV_SUCCESS);
    }
    
    uint64_t set_time = get_time_us() - start_time;
    double set_ops_per_sec = (double)TEST_KEY_COUNT / ((double)set_time / 1000000.0);
    
    printf("SET性能: %d操作用时 %llu 微秒 (%.2f ops/sec)\n", 
           TEST_KEY_COUNT, (unsigned long long)set_time, set_ops_per_sec);
    
    // GET性能测试
    start_time = get_time_us();
    int successful_gets = 0;
    
    for (int i = 0; i < TEST_KEY_COUNT; i++) {
        char key[64];
        snprintf(key, sizeof(key), "perf_key_%d", i);
        
        char *value = NULL;
        size_t value_len = 0;
        int result = kv_cache_get(cache, key, strlen(key), &value, &value_len);
        
        if (result == KV_SUCCESS) {
            successful_gets++;
            free(value);
        }
    }
    
    uint64_t get_time = get_time_us() - start_time;
    double get_ops_per_sec = (double)TEST_KEY_COUNT / ((double)get_time / 1000000.0);
    
    printf("GET性能: %d操作用时 %llu 微秒 (%.2f ops/sec)\n", 
           TEST_KEY_COUNT, (unsigned long long)get_time, get_ops_per_sec);
    
    test_print_result("所有GET成功", successful_gets == TEST_KEY_COUNT);
    all_passed &= (successful_gets == TEST_KEY_COUNT);
    
    // 获取最终统计信息
    kv_cache_stats_t stats;
    kv_cache_get_stats(cache, &stats);
    
    double hit_ratio = kv_cache_hit_ratio(&stats);
    printf("缓存命中率: %.2f%%\n", hit_ratio * 100.0);
    printf("总请求数: %llu, 命中数: %llu, 未命中数: %llu\n",
           (unsigned long long)stats.total_requests,
           (unsigned long long)stats.cache_hits,
           (unsigned long long)stats.cache_misses);
    
    test_print_result("高命中率", hit_ratio > 0.95);
    all_passed &= (hit_ratio > 0.95);
    
    kv_cache_destroy(cache);
    kv_cache_config_destroy(config);
    
    return all_passed;
}

// === 并发测试 ===

typedef struct {
    kv_cache_t *cache;
    int thread_id;
    int operations;
    int success_count;
} thread_test_data_t;

void* thread_test_worker(void *arg) {
    thread_test_data_t *data = (thread_test_data_t*)arg;
    data->success_count = 0;
    
    for (int i = 0; i < data->operations; i++) {
        char key[64], value[128];
        snprintf(key, sizeof(key), "thread_%d_key_%d", data->thread_id, i);
        snprintf(value, sizeof(value), "thread_%d_value_%d", data->thread_id, i);
        
        // SET操作
        int set_result = kv_cache_set(data->cache, key, strlen(key), 
                                      value, strlen(value), 0);
        
        if (set_result == KV_SUCCESS) {
            // GET操作验证
            char *retrieved_value = NULL;
            size_t retrieved_len = 0;
            int get_result = kv_cache_get(data->cache, key, strlen(key), 
                                          &retrieved_value, &retrieved_len);
            
            if (get_result == KV_SUCCESS && retrieved_value) {
                if (strcmp(retrieved_value, value) == 0) {
                    data->success_count++;
                }
                free(retrieved_value);
            }
        }
        
        // 偶尔执行DELETE操作
        if (i % 10 == 0 && i > 0) {
            char delete_key[64];
            snprintf(delete_key, sizeof(delete_key), "thread_%d_key_%d", 
                     data->thread_id, i - 5);
            kv_cache_delete(data->cache, delete_key, strlen(delete_key));
        }
    }
    
    return NULL;
}

bool test_concurrency(void) {
    printf("\n=== 并发安全测试 ===\n");
    
    kv_cache_config_t *config = kv_cache_config_create_default(KV_CACHE_POLICY_LRU);
    config->max_entries = TEST_THREAD_COUNT * TEST_OPERATIONS_PER_THREAD;
    
    kv_cache_t *cache = kv_cache_create(config);
    assert(cache != NULL);
    
    pthread_t threads[TEST_THREAD_COUNT];
    thread_test_data_t thread_data[TEST_THREAD_COUNT];
    
    uint64_t start_time = get_time_us();
    
    // 启动线程
    for (int i = 0; i < TEST_THREAD_COUNT; i++) {
        thread_data[i].cache = cache;
        thread_data[i].thread_id = i;
        thread_data[i].operations = TEST_OPERATIONS_PER_THREAD;
        thread_data[i].success_count = 0;
        
        int create_result = pthread_create(&threads[i], NULL, 
                                           thread_test_worker, &thread_data[i]);
        assert(create_result == 0);
    }
    
    // 等待线程完成
    int total_success = 0;
    for (int i = 0; i < TEST_THREAD_COUNT; i++) {
        pthread_join(threads[i], NULL);
        total_success += thread_data[i].success_count;
        printf("线程 %d: %d/%d 操作成功\n", 
               i, thread_data[i].success_count, TEST_OPERATIONS_PER_THREAD);
    }
    
    uint64_t total_time = get_time_us() - start_time;
    int expected_operations = TEST_THREAD_COUNT * TEST_OPERATIONS_PER_THREAD;
    double success_rate = (double)total_success / (double)expected_operations;
    
    printf("并发测试完成: %d/%d 成功 (%.2f%%) 用时 %llu 微秒\n", 
           total_success, expected_operations, success_rate * 100.0,
           (unsigned long long)total_time);
    
    bool all_passed = (success_rate > 0.95);
    test_print_result("高成功率", all_passed);
    
    kv_cache_destroy(cache);
    kv_cache_config_destroy(config);
    
    return all_passed;
}

// === 主测试函数 ===

int main(void) {
    printf("=== ConcordKV 缓存层综合测试 ===\n");
    printf("测试配置: 缓存大小=%d, 键数量=%d, 线程数=%d\n", 
           TEST_CACHE_SIZE, TEST_KEY_COUNT, TEST_THREAD_COUNT);
    
    bool all_tests_passed = true;
    int test_count = 0;
    int passed_count = 0;
    
    // 执行测试套件
    struct {
        const char *name;
        bool (*test_func)(void);
    } tests[] = {
        {"基础创建销毁", test_cache_create_destroy},
        {"基础操作", test_basic_operations},
        {"TTL功能", test_ttl_functionality},
        {"LRU淘汰", test_lru_eviction},
        {"性能测试", test_performance},
        {"并发安全", test_concurrency}
    };
    
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        printf("\n" "==========================================\n");
        printf("运行测试: %s\n", tests[i].name);
        printf("==========================================\n");
        
        bool result = tests[i].test_func();
        test_count++;
        
        if (result) {
            passed_count++;
            printf("✓ %s 通过\n", tests[i].name);
        } else {
            printf("✗ %s 失败\n", tests[i].name);
            all_tests_passed = false;
        }
    }
    
    // 测试总结
    printf("\n" "==========================================\n");
    printf("测试总结\n");
    printf("==========================================\n");
    printf("总测试数: %d\n", test_count);
    printf("通过测试: %d\n", passed_count);
    printf("失败测试: %d\n", test_count - passed_count);
    printf("成功率: %.2f%%\n", (double)passed_count / test_count * 100.0);
    
    if (all_tests_passed) {
        printf("\n🎉 所有测试通过！缓存层实现正确！\n");
        return 0;
    } else {
        printf("\n❌ 部分测试失败，需要修复问题。\n");
        return 1;
    }
} 