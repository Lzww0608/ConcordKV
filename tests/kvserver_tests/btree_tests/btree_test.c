/*
 * @Author: Lzww0608  
 * @Date: 2025-5-31 23:34:29
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-1 00:15:59
 * @Description: ConcordKV B+Tree存储引擎测试程序
 */
#define _GNU_SOURCE     // 启用扩展函数
#define _POSIX_C_SOURCE 200809L  // 启用POSIX扩展

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>

// 包含必要的头文件
#include "../../kvserver/kv_engine_interface.h"
#include "../../kvserver/kvstore_btree.h"
#include "../../kvserver/btree_adapter.h"
#include "../../kvserver/kv_error.h"

// 测试配置
#define TEST_TIMEOUT_SECONDS 30     // 每个测试的最大执行时间
#define TEST_KEY_COUNT 100          // 基础测试的键数量
#define TEST_STRESS_COUNT 1000      // 压力测试的键数量
#define TEST_KEY_MAX_LEN 64         // 测试键的最大长度
#define TEST_VALUE_MAX_LEN 256      // 测试值的最大长度

// 全局变量
static volatile int timeout_flag = 0;
static pthread_t timeout_thread;

// === 超时控制机制 ===

/**
 * 超时处理函数
 */
static void* timeout_handler(void* arg) {
    int timeout_seconds = *(int*)arg;
    sleep(timeout_seconds);
    
    if (!timeout_flag) {
        timeout_flag = 1;
        printf("\n[TIMEOUT] Test execution exceeded %d seconds - forcing exit\n", timeout_seconds);
        printf("[TIMEOUT] This may indicate a deadlock or infinite loop\n");
        exit(EXIT_FAILURE);
    }
    
    return NULL;
}

/**
 * 启动超时保护
 */
static void start_timeout_protection(int seconds) {
    timeout_flag = 0;
    static int timeout_seconds;
    timeout_seconds = seconds;
    
    if (pthread_create(&timeout_thread, NULL, timeout_handler, &timeout_seconds) != 0) {
        printf("[WARNING] Failed to create timeout thread\n");
    }
}

/**
 * 停止超时保护
 */
static void stop_timeout_protection(void) {
    timeout_flag = 1;
    pthread_cancel(timeout_thread);
    pthread_join(timeout_thread, NULL);
}

// === 测试工具函数 ===

/**
 * 生成测试键
 */
static void generate_test_key(char *buffer, int index, size_t buffer_size) {
    snprintf(buffer, buffer_size, "test_key_%06d", index);
}

/**
 * 生成测试值
 */
static void generate_test_value(char *buffer, int index, size_t buffer_size) {
    snprintf(buffer, buffer_size, "test_value_%06d_data_content_for_testing", index);
}

/**
 * 测试结果记录
 */
typedef struct {
    int total_tests;
    int passed_tests;
    int failed_tests;
    double total_time_ms;
} test_result_t;

static test_result_t g_test_result = {0, 0, 0, 0.0};

/**
 * 获取当前时间戳(毫秒)
 */
static double get_current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/**
 * 运行单个测试
 */
static void run_test(const char *test_name, int (*test_func)(void)) {
    printf("\n=== Running Test: %s ===\n", test_name);
    
    double start_time = get_current_time_ms();
    
    // 启动超时保护
    start_timeout_protection(TEST_TIMEOUT_SECONDS);
    
    int result = test_func();
    
    // 停止超时保护
    stop_timeout_protection();
    
    double end_time = get_current_time_ms();
    double duration = end_time - start_time;
    
    g_test_result.total_tests++;
    g_test_result.total_time_ms += duration;
    
    if (result == 0) {
        g_test_result.passed_tests++;
        printf("[PASS] %s (%.2f ms)\n", test_name, duration);
    } else {
        g_test_result.failed_tests++;
        printf("[FAIL] %s (%.2f ms) - Error code: %d\n", test_name, duration, result);
    }
}

// === B+Tree基础功能测试 ===

/**
 * 测试B+Tree创建和销毁
 */
static int test_btree_create_destroy(void) {
    printf("Testing B+Tree creation and destruction...\n");
    
    // 测试正常创建
    btree_t *tree = btree_create(BTREE_DEFAULT_ORDER);
    if (!tree) {
        printf("ERROR: Failed to create B+Tree\n");
        return -1;
    }
    
    // 验证初始状态
    if (btree_count(tree) != 0) {
        printf("ERROR: New B+Tree should have 0 keys, got %zu\n", btree_count(tree));
        btree_destroy(tree);
        return -1;
    }
    
    if (!tree->root || !tree->root->is_leaf) {
        printf("ERROR: New B+Tree should have a leaf root\n");
        btree_destroy(tree);
        return -1;
    }
    
    // 测试销毁
    btree_destroy(tree);
    
    // 测试无效参数
    tree = btree_create(1); // 太小的阶数
    if (tree) {
        printf("ERROR: Should reject invalid order\n");
        btree_destroy(tree);
        return -1;
    }
    
    tree = btree_create(BTREE_MAX_ORDER + 1); // 太大的阶数
    if (tree) {
        printf("ERROR: Should reject invalid order\n");
        btree_destroy(tree);
        return -1;
    }
    
    printf("B+Tree creation and destruction test passed\n");
    return 0;
}

/**
 * 测试节点基础操作
 */
static int test_btree_node_operations(void) {
    printf("Testing B+Tree node operations...\n");
    
    // 创建叶子节点
    btree_node_t *leaf = btree_node_create(BTREE_NODE_LEAF, 5);
    if (!leaf) {
        printf("ERROR: Failed to create leaf node\n");
        return -1;
    }
    
    // 测试节点状态检查
    if (!btree_node_is_full(leaf) == false) {
        printf("ERROR: Empty node should not be full\n");
        btree_node_destroy(leaf);
        return -1;
    }
    
    // 测试键查找
    int pos = btree_node_find_key(leaf, "test", 4, false);
    if (pos != 0) {
        printf("ERROR: Insert position should be 0 for empty node\n");
        btree_node_destroy(leaf);
        return -1;
    }
    
    // 测试插入
    int ret = btree_node_insert_at(leaf, 0, "key1", 4, "value1", 6);
    if (ret != KV_ERR_NONE) {
        printf("ERROR: Failed to insert into node\n");
        btree_node_destroy(leaf);
        return -1;
    }
    
    if (leaf->key_count != 1) {
        printf("ERROR: Node should have 1 key after insert\n");
        btree_node_destroy(leaf);
        return -1;
    }
    
    // 测试验证
    if (!btree_node_validate(leaf)) {
        printf("ERROR: Node validation failed\n");
        btree_node_destroy(leaf);
        return -1;
    }
    
    // 测试删除
    ret = btree_node_remove_at(leaf, 0);
    if (ret != KV_ERR_NONE) {
        printf("ERROR: Failed to remove from node\n");
        btree_node_destroy(leaf);
        return -1;
    }
    
    if (leaf->key_count != 0) {
        printf("ERROR: Node should have 0 keys after remove\n");
        btree_node_destroy(leaf);
        return -1;
    }
    
    btree_node_destroy(leaf);
    printf("B+Tree node operations test passed\n");
    return 0;
}

/**
 * 测试基础CRUD操作
 */
static int test_btree_basic_crud(void) {
    printf("Testing B+Tree basic CRUD operations...\n");
    
    btree_t *tree = kv_store_btree_create(10);
    if (!tree) {
        printf("ERROR: Failed to create B+Tree\n");
        return -1;
    }
    
    char key[TEST_KEY_MAX_LEN];
    char value[TEST_VALUE_MAX_LEN];
    char *retrieved_value;
    
    // 测试单个插入和查找
    strcpy(key, "test_key");
    strcpy(value, "test_value");
    
    int ret = kvs_btree_set(tree, key, value);
    if (ret != 0) {
        printf("ERROR: Failed to set key-value pair\n");
        kv_store_btree_destroy(tree);
        return -1;
    }
    
    retrieved_value = kvs_btree_get(tree, key);
    if (!retrieved_value || strcmp(retrieved_value, value) != 0) {
        printf("ERROR: Retrieved value doesn't match\n");
        if (retrieved_value) free(retrieved_value);
        kv_store_btree_destroy(tree);
        return -1;
    }
    free(retrieved_value);
    
    // 测试计数
    if (kvs_btree_count(tree) != 1) {
        printf("ERROR: Count should be 1 after inserting one key\n");
        kv_store_btree_destroy(tree);
        return -1;
    }
    
    // 测试更新
    strcpy(value, "updated_value");
    ret = kvs_btree_modify(tree, key, value);
    if (ret != 0) {
        printf("ERROR: Failed to modify key-value pair\n");
        kv_store_btree_destroy(tree);
        return -1;
    }
    
    retrieved_value = kvs_btree_get(tree, key);
    if (!retrieved_value || strcmp(retrieved_value, value) != 0) {
        printf("ERROR: Updated value doesn't match\n");
        if (retrieved_value) free(retrieved_value);
        kv_store_btree_destroy(tree);
        return -1;
    }
    free(retrieved_value);
    
    // 测试删除
    ret = kvs_btree_delete(tree, key);
    if (ret != 0) {
        printf("ERROR: Failed to delete key\n");
        kv_store_btree_destroy(tree);
        return -1;
    }
    
    retrieved_value = kvs_btree_get(tree, key);
    if (retrieved_value) {
        printf("ERROR: Key should not exist after deletion\n");
        free(retrieved_value);
        kv_store_btree_destroy(tree);
        return -1;
    }
    
    if (kvs_btree_count(tree) != 0) {
        printf("ERROR: Count should be 0 after deleting all keys\n");
        kv_store_btree_destroy(tree);
        return -1;
    }
    
    kv_store_btree_destroy(tree);
    printf("B+Tree basic CRUD operations test passed\n");
    return 0;
}

/**
 * 测试多键操作 - 现在可以测试节点分裂功能
 */
static int test_btree_multiple_keys(void) {
    printf("Testing B+Tree multiple keys operations with node splitting...\n");
    
    btree_t *tree = kv_store_btree_create(5);  // 使用较小的阶数以测试分裂
    if (!tree) {
        printf("ERROR: Failed to create B+Tree\n");
        return -1;
    }
    
    char key[TEST_KEY_MAX_LEN];
    char value[TEST_VALUE_MAX_LEN];
    char *retrieved_value;
    
    // 插入足够的键值对以触发节点分裂
    int test_count = 20;  // 插入20个键，阶数为5，必然触发多次分裂
    for (int i = 0; i < test_count; i++) {
        generate_test_key(key, i, sizeof(key));
        generate_test_value(value, i, sizeof(value));
        
        int ret = kvs_btree_set(tree, key, value);
        if (ret != 0) {
            printf("ERROR: Failed to set key %s (iteration %d)\n", key, i);
            kv_store_btree_destroy(tree);
            return -1;
        }
        
        // 验证插入后立即能查找到
        retrieved_value = kvs_btree_get(tree, key);
        if (!retrieved_value || strcmp(retrieved_value, value) != 0) {
            printf("ERROR: Failed to retrieve key %s immediately after insertion\n", key);
            if (retrieved_value) free(retrieved_value);
            kv_store_btree_destroy(tree);
            return -1;
        }
        free(retrieved_value);
    }
    
    // 验证所有键都可以查找到
    for (int i = 0; i < test_count; i++) {
        generate_test_key(key, i, sizeof(key));
        generate_test_value(value, i, sizeof(value));
        
        retrieved_value = kvs_btree_get(tree, key);
        if (!retrieved_value || strcmp(retrieved_value, value) != 0) {
            printf("ERROR: Retrieved value for key %s doesn't match\n", key);
            if (retrieved_value) free(retrieved_value);
            kv_store_btree_destroy(tree);
            return -1;
        }
        free(retrieved_value);
    }
    
    // 检查计数
    if (kvs_btree_count(tree) != test_count) {
        printf("ERROR: Count should be %d, got %d\n", test_count, kvs_btree_count(tree));
        kv_store_btree_destroy(tree);
        return -1;
    }
    
    // 测试删除一些键
    for (int i = 0; i < test_count / 2; i++) {
        generate_test_key(key, i, sizeof(key));
        
        int ret = kvs_btree_delete(tree, key);
        if (ret != 0) {
            printf("ERROR: Failed to delete key %s\n", key);
            kv_store_btree_destroy(tree);
            return -1;
        }
        
        // 验证删除后不能找到
        retrieved_value = kvs_btree_get(tree, key);
        if (retrieved_value) {
            printf("ERROR: Key %s should not exist after deletion\n", key);
            free(retrieved_value);
            kv_store_btree_destroy(tree);
            return -1;
        }
    }
    
    // 验证剩余的键仍然存在
    for (int i = test_count / 2; i < test_count; i++) {
        generate_test_key(key, i, sizeof(key));
        generate_test_value(value, i, sizeof(value));
        
        retrieved_value = kvs_btree_get(tree, key);
        if (!retrieved_value || strcmp(retrieved_value, value) != 0) {
            printf("ERROR: Remaining key %s not found or value mismatch\n", key);
            if (retrieved_value) free(retrieved_value);
            kv_store_btree_destroy(tree);
            return -1;
        }
        free(retrieved_value);
    }
    
    kv_store_btree_destroy(tree);
    printf("B+Tree multiple keys operations with splitting test passed\n");
    return 0;
}

/**
 * 测试节点分裂功能
 */
static int test_btree_node_splitting(void) {
    printf("Testing B+Tree node splitting functionality...\n");
    
    btree_t *tree = kv_store_btree_create(3);  // 非常小的阶数，快速触发分裂
    if (!tree) {
        printf("ERROR: Failed to create B+Tree\n");
        return -1;
    }
    
    char key[TEST_KEY_MAX_LEN];
    char value[TEST_VALUE_MAX_LEN];
    
    // 连续插入键，应该触发多次分裂
    for (int i = 0; i < 15; i++) {
        snprintf(key, sizeof(key), "key_%02d", i);
        snprintf(value, sizeof(value), "value_%02d", i);
        
        int ret = kvs_btree_set(tree, key, value);
        if (ret != 0) {
            printf("ERROR: Failed to insert key %s\n", key);
            kv_store_btree_destroy(tree);
            return -1;
        }
    }
    
    // 验证树的高度增加了（表明发生了分裂）
    if (tree->height <= 1) {
        printf("ERROR: Tree height should be > 1 after multiple splits, got %u\n", tree->height);
        kv_store_btree_destroy(tree);
        return -1;
    }
    
    // 验证所有键都能找到
    for (int i = 0; i < 15; i++) {
        snprintf(key, sizeof(key), "key_%02d", i);
        snprintf(value, sizeof(value), "value_%02d", i);
        
        char *retrieved_value = kvs_btree_get(tree, key);
        if (!retrieved_value || strcmp(retrieved_value, value) != 0) {
            printf("ERROR: Key %s not found or value mismatch after splitting\n", key);
            if (retrieved_value) free(retrieved_value);
            kv_store_btree_destroy(tree);
            return -1;
        }
        free(retrieved_value);
    }
    
    printf("Tree height after splits: %u\n", tree->height);
    printf("Total leaf nodes: %u\n", tree->leaf_nodes);
    printf("Total internal nodes: %u\n", tree->internal_nodes);
    
    kv_store_btree_destroy(tree);
    printf("B+Tree node splitting test passed\n");
    return 0;
}

/**
 * 测试大量数据插入删除
 */
static int test_btree_stress_operations(void) {
    printf("Testing B+Tree stress operations...\n");
    
    btree_t *tree = kv_store_btree_create(10);
    if (!tree) {
        printf("ERROR: Failed to create B+Tree\n");
        return -1;
    }
    
    const int stress_count = 100;  // 插入100个键值对
    char key[TEST_KEY_MAX_LEN];
    char value[TEST_VALUE_MAX_LEN];
    
    // 大量插入
    for (int i = 0; i < stress_count; i++) {
        generate_test_key(key, i, sizeof(key));
        generate_test_value(value, i, sizeof(value));
        
        int ret = kvs_btree_set(tree, key, value);
        if (ret != 0) {
            printf("ERROR: Failed to insert key %s in stress test\n", key);
            kv_store_btree_destroy(tree);
            return -1;
        }
    }
    
    // 验证计数
    if (kvs_btree_count(tree) != stress_count) {
        printf("ERROR: Count should be %d, got %d\n", stress_count, kvs_btree_count(tree));
        kv_store_btree_destroy(tree);
        return -1;
    }
    
    // 随机删除一半
    for (int i = 0; i < stress_count; i += 2) {
        generate_test_key(key, i, sizeof(key));
        
        int ret = kvs_btree_delete(tree, key);
        if (ret != 0) {
            printf("ERROR: Failed to delete key %s in stress test\n", key);
            kv_store_btree_destroy(tree);
            return -1;
        }
    }
    
    // 验证剩余数量
    int expected_remaining = stress_count - stress_count / 2;
    if (kvs_btree_count(tree) != expected_remaining) {
        printf("ERROR: After deletion, count should be %d, got %d\n", 
               expected_remaining, kvs_btree_count(tree));
        kv_store_btree_destroy(tree);
        return -1;
    }
    
    // 验证剩余的键仍然能找到
    for (int i = 1; i < stress_count; i += 2) {
        generate_test_key(key, i, sizeof(key));
        generate_test_value(value, i, sizeof(value));
        
        char *retrieved_value = kvs_btree_get(tree, key);
        if (!retrieved_value || strcmp(retrieved_value, value) != 0) {
            printf("ERROR: Remaining key %s not found or value mismatch\n", key);
            if (retrieved_value) free(retrieved_value);
            kv_store_btree_destroy(tree);
            return -1;
        }
        free(retrieved_value);
    }
    
    printf("Stress test completed: %d insertions, %d deletions\n", 
           stress_count, stress_count / 2);
    printf("Final tree height: %u\n", tree->height);
    
    kv_store_btree_destroy(tree);
    printf("B+Tree stress operations test passed\n");
    return 0;
}

/**
 * 测试边界条件
 */
static int test_btree_edge_cases(void) {
    printf("Testing B+Tree edge cases...\n");
    
    btree_t *tree = kv_store_btree_create(5);
    if (!tree) {
        printf("ERROR: Failed to create B+Tree\n");
        return -1;
    }
    
    // 测试空键查找
    char *result = kvs_btree_get(tree, "nonexistent");
    if (result) {
        printf("ERROR: Should return NULL for nonexistent key\n");
        free(result);
        kv_store_btree_destroy(tree);
        return -1;
    }
    
    // 测试删除不存在的键
    int ret = kvs_btree_delete(tree, "nonexistent");
    if (ret == 0) {
        printf("ERROR: Should fail when deleting nonexistent key\n");
        kv_store_btree_destroy(tree);
        return -1;
    }
    
    // 测试修改不存在的键
    ret = kvs_btree_modify(tree, "nonexistent", "value");
    if (ret <= 0) {
        printf("ERROR: Should return positive value when modifying nonexistent key\n");
        kv_store_btree_destroy(tree);
        return -1;
    }
    
    // 测试NULL参数
    ret = kvs_btree_set(NULL, "key", "value");
    if (ret == 0) {
        printf("ERROR: Should fail with NULL tree parameter\n");
        kv_store_btree_destroy(tree);
        return -1;
    }
    
    ret = kvs_btree_set(tree, NULL, "value");
    if (ret == 0) {
        printf("ERROR: Should fail with NULL key parameter\n");
        kv_store_btree_destroy(tree);
        return -1;
    }
    
    kv_store_btree_destroy(tree);
    printf("B+Tree edge cases test passed\n");
    return 0;
}

/**
 * 测试统一存储引擎接口
 */
static int test_unified_engine_interface(void) {
    printf("Testing unified storage engine interface...\n");
    
    // 创建B+Tree引擎配置
    kv_engine_config_t *config = kv_engine_config_create(KV_ENGINE_BTREE);
    if (!config) {
        printf("ERROR: Failed to create engine config\n");
        return -1;
    }
    
    config->max_keys_per_node = 10;
    
    // 创建引擎
    kv_engine_t *engine = kv_engine_create(KV_ENGINE_BTREE, config);
    if (!engine) {
        printf("ERROR: Failed to create B+Tree engine\n");
        kv_engine_config_destroy(config);
        return -1;
    }
    
    // 测试统一接口
    int ret = KV_ENGINE_SET(engine, "test_key", "test_value");
    if (ret != KV_ERR_NONE) {
        printf("ERROR: Failed to set via unified interface\n");
        kv_engine_destroy(engine);
        kv_engine_config_destroy(config);
        return -1;
    }
    
    char *value = KV_ENGINE_GET(engine, "test_key");
    if (!value || strcmp(value, "test_value") != 0) {
        printf("ERROR: Failed to get via unified interface\n");
        if (value) free(value);
        kv_engine_destroy(engine);
        kv_engine_config_destroy(config);
        return -1;
    }
    free(value);
    
    int count = KV_ENGINE_COUNT(engine);
    if (count != 1) {
        printf("ERROR: Count should be 1, got %d\n", count);
        kv_engine_destroy(engine);
        kv_engine_config_destroy(config);
        return -1;
    }
    
    ret = KV_ENGINE_DELETE(engine, "test_key");
    if (ret != KV_ERR_NONE) {
        printf("ERROR: Failed to delete via unified interface\n");
        kv_engine_destroy(engine);
        kv_engine_config_destroy(config);
        return -1;
    }
    
    // 清理
    kv_engine_destroy(engine);
    kv_engine_config_destroy(config);
    
    printf("Unified storage engine interface test passed\n");
    return 0;
}

// === 主测试函数 ===

int main(int argc, char *argv[]) {
    printf("=== ConcordKV B+Tree Storage Engine Test ===\n");
    printf("Test configuration:\n");
    printf("- Timeout per test: %d seconds\n", TEST_TIMEOUT_SECONDS);
    printf("- Basic test key count: %d\n", TEST_KEY_COUNT);
    printf("- Stress test key count: %d\n", TEST_STRESS_COUNT);
    printf("\n");
    
    double start_time = get_current_time_ms();
    
    // 运行所有测试
    run_test("B+Tree Create/Destroy", test_btree_create_destroy);
    run_test("B+Tree Node Operations", test_btree_node_operations);
    run_test("B+Tree Basic CRUD", test_btree_basic_crud);
    run_test("B+Tree Multiple Keys", test_btree_multiple_keys);
    run_test("B+Tree Node Splitting", test_btree_node_splitting);
    run_test("B+Tree Stress Operations", test_btree_stress_operations);
    run_test("B+Tree Edge Cases", test_btree_edge_cases);
    run_test("Unified Engine Interface", test_unified_engine_interface);
    
    double end_time = get_current_time_ms();
    double total_duration = end_time - start_time;
    
    // 输出测试结果
    printf("\n=== Test Summary ===\n");
    printf("Total tests: %d\n", g_test_result.total_tests);
    printf("Passed: %d\n", g_test_result.passed_tests);
    printf("Failed: %d\n", g_test_result.failed_tests);
    printf("Success rate: %.1f%%\n", 
           g_test_result.total_tests > 0 ? 
           (double)g_test_result.passed_tests / g_test_result.total_tests * 100.0 : 0.0);
    printf("Total execution time: %.2f ms\n", total_duration);
    printf("Average test time: %.2f ms\n", 
           g_test_result.total_tests > 0 ? 
           g_test_result.total_time_ms / g_test_result.total_tests : 0.0);
    
    if (g_test_result.failed_tests == 0) {
        printf("\n🎉 ALL TESTS PASSED! B+Tree engine is working correctly.\n");
        return EXIT_SUCCESS;
    } else {
        printf("\n❌ %d TESTS FAILED! Please check the implementation.\n", 
               g_test_result.failed_tests);
        return EXIT_FAILURE;
    }
} 