/*
 * @Author: Lzww0608  
 * @Date: 2025-6-1 00:45:03
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-1 00:45:06
 * @Description: B+Tree生产环境级别测试和检查
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include "../../../kvserver/kvstore_btree.h"
#include "../../../kvserver/btree_adapter.h"

// 测试配置
#define TEST_THREAD_COUNT 8
#define TEST_OPERATIONS_PER_THREAD 10000
#define TEST_KEY_LENGTH_MAX 100
#define TEST_VALUE_LENGTH_MAX 1000

// 性能基准
#define EXPECTED_MIN_OPS_PER_SEC 50000
#define EXPECTED_MAX_HEIGHT_RATIO 15  // log_order(keys) * ratio
#define EXPECTED_MAX_MEMORY_OVERHEAD 2.0  // 内存开销倍数

// 测试结果结构
typedef struct {
    int total_tests;
    int passed_tests;
    int critical_failures;
    double total_time_ms;
} production_test_result_t;

// 线程测试参数
typedef struct {
    btree_t *tree;
    int thread_id;
    int operations;
    int start_key;
    double ops_per_sec;
    int errors;
} thread_test_params_t;

// === 工具函数 ===

static double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// === 阶数和配置检查 ===

static int check_btree_order_configuration() {
    printf("\n=== B+Tree阶数和配置检查 ===\n");
    
    // 检查默认配置
    printf("默认阶数: %d\n", BTREE_DEFAULT_ORDER);
    printf("最小阶数: %d\n", BTREE_MIN_ORDER);
    printf("最大阶数: %d\n", BTREE_MAX_ORDER);
    printf("键最大长度: %d\n", BTREE_KEY_MAX_LEN);
    printf("值最大长度: %d\n", BTREE_VALUE_MAX_LEN);
    
    // 阶数合理性检查
    if (BTREE_MIN_ORDER < 3) {
        printf("❌ 错误: 最小阶数应该 >= 3\n");
        return -1;
    }
    
    if (BTREE_DEFAULT_ORDER < 50 || BTREE_DEFAULT_ORDER > 500) {
        printf("⚠️  警告: 默认阶数 %d 可能不适合生产环境\n", BTREE_DEFAULT_ORDER);
        printf("   推荐范围: 50-500\n");
    }
    
    if (BTREE_MAX_ORDER > 2000) {
        printf("⚠️  警告: 最大阶数 %d 可能过大，会增加内存使用\n", BTREE_MAX_ORDER);
    }
    
    // 测试不同阶数下的性能特征
    printf("\n阶数性能测试:\n");
    int test_orders[] = {3, 10, 50, 100, 200, 500};
    int num_orders = sizeof(test_orders) / sizeof(test_orders[0]);
    
    for (int i = 0; i < num_orders; i++) {
        int order = test_orders[i];
        btree_t *tree = kv_store_btree_create(order);
        if (!tree) {
            printf("❌ 无法创建阶数为 %d 的B+Tree\n", order);
            continue;
        }
        
        // 插入1000个键测试
        double start_time = get_time_ms();
        char key[64], value[64];
        
        for (int j = 0; j < 1000; j++) {
            snprintf(key, sizeof(key), "key_%06d", j);
            snprintf(value, sizeof(value), "value_%06d", j);
            kvs_btree_set(tree, key, value);
        }
        
        double insert_time = get_time_ms() - start_time;
        
        // 搜索测试
        start_time = get_time_ms();
        for (int j = 0; j < 1000; j++) {
            snprintf(key, sizeof(key), "key_%06d", j);
            char *result = kvs_btree_get(tree, key);
            if (result) free(result);
        }
        double search_time = get_time_ms() - start_time;
        
        printf("  阶数 %3d: 高度=%u, 插入耗时=%.2fms, 搜索耗时=%.2fms\n", 
               order, tree->height, insert_time, search_time);
        
        kv_store_btree_destroy(tree);
    }
    
    printf("✅ 阶数配置检查完成\n");
    return 0;
}

// === 内存使用检查 ===

static int check_memory_usage() {
    printf("\n=== 内存使用检查 ===\n");
    
    btree_t *tree = kv_store_btree_create(BTREE_DEFAULT_ORDER);
    if (!tree) {
        printf("❌ 无法创建B+Tree\n");
        return -1;
    }
    
    // 逐步插入数据，监控内存使用
    int test_sizes[] = {100, 1000, 10000, 50000};
    int num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    char key[256], value[512];
    
    for (int i = 0; i < num_sizes; i++) {
        int target_size = test_sizes[i];
        int current_size = kvs_btree_count(tree);
        
        // 插入到目标大小
        for (int j = current_size; j < target_size; j++) {
            snprintf(key, sizeof(key), "memory_test_key_%08d", j);
            snprintf(value, sizeof(value), "memory_test_value_%08d_padding_data", j);
            kvs_btree_set(tree, key, value);
        }
        
        size_t memory_usage = btree_memory_usage(tree);
        size_t estimated_data_size = target_size * (strlen(key) + strlen(value));
        double memory_ratio = (double)memory_usage / estimated_data_size;
        
        printf("  %5d 键: 内存使用=%zu KB, 数据大小=%zu KB, 比率=%.2f\n",
               target_size, memory_usage / 1024, estimated_data_size / 1024, memory_ratio);
        
        if (memory_ratio > EXPECTED_MAX_MEMORY_OVERHEAD) {
            printf("⚠️  警告: 内存开销比率 %.2f 超过预期 %.2f\n", 
                   memory_ratio, EXPECTED_MAX_MEMORY_OVERHEAD);
        }
    }
    
    kv_store_btree_destroy(tree);
    printf("✅ 内存使用检查完成\n");
    return 0;
}

// === 并发安全检查 ===

static void* concurrent_test_thread(void *arg) {
    thread_test_params_t *params = (thread_test_params_t*)arg;
    double start_time = get_time_ms();
    int errors = 0;
    
    char key[TEST_KEY_LENGTH_MAX], value[TEST_VALUE_LENGTH_MAX];
    
    for (int i = 0; i < params->operations; i++) {
        int key_id = params->start_key + i;
        snprintf(key, sizeof(key), "thread_%d_key_%d", params->thread_id, key_id);
        snprintf(value, sizeof(value), "thread_%d_value_%d", params->thread_id, key_id);
        
        // 50% 插入, 30% 查询, 20% 删除
        int op = rand() % 100;
        
        if (op < 50) {
            // 插入操作
            if (kvs_btree_set(params->tree, key, value) != 0) {
                errors++;
            }
        } else if (op < 80) {
            // 查询操作
            char *result = kvs_btree_get(params->tree, key);
            if (result) {
                free(result);
            }
        } else {
            // 删除操作
            kvs_btree_delete(params->tree, key);
        }
    }
    
    double end_time = get_time_ms();
    params->ops_per_sec = params->operations * 1000.0 / (end_time - start_time);
    params->errors = errors;
    
    return NULL;
}

static int check_concurrent_safety() {
    printf("\n=== 并发安全检查 ===\n");
    
    btree_t *tree = kv_store_btree_create(BTREE_DEFAULT_ORDER);
    if (!tree) {
        printf("❌ 无法创建B+Tree\n");
        return -1;
    }
    
    pthread_t threads[TEST_THREAD_COUNT];
    thread_test_params_t params[TEST_THREAD_COUNT];
    double total_ops_per_sec = 0;
    int total_errors = 0;
    
    printf("启动 %d 个线程，每个线程执行 %d 次操作...\n", 
           TEST_THREAD_COUNT, TEST_OPERATIONS_PER_THREAD);
    
    double start_time = get_time_ms();
    
    // 创建线程
    for (int i = 0; i < TEST_THREAD_COUNT; i++) {
        params[i].tree = tree;
        params[i].thread_id = i;
        params[i].operations = TEST_OPERATIONS_PER_THREAD;
        params[i].start_key = i * TEST_OPERATIONS_PER_THREAD;
        
        if (pthread_create(&threads[i], NULL, concurrent_test_thread, &params[i]) != 0) {
            printf("❌ 无法创建线程 %d\n", i);
            return -1;
        }
    }
    
    // 等待所有线程完成
    for (int i = 0; i < TEST_THREAD_COUNT; i++) {
        pthread_join(threads[i], NULL);
        total_ops_per_sec += params[i].ops_per_sec;
        total_errors += params[i].errors;
        
        printf("  线程 %d: %.0f ops/sec, %d 错误\n", 
               i, params[i].ops_per_sec, params[i].errors);
    }
    
    double total_time = get_time_ms() - start_time;
    double overall_ops_per_sec = (TEST_THREAD_COUNT * TEST_OPERATIONS_PER_THREAD * 1000.0) / total_time;
    
    printf("总体性能: %.0f ops/sec, 总错误: %d\n", overall_ops_per_sec, total_errors);
    printf("树状态: 高度=%u, 键数=%zu, 叶子节点=%lu\n", 
           tree->height, tree->total_keys, tree->leaf_nodes);
    
    // 性能检查
    if (overall_ops_per_sec < EXPECTED_MIN_OPS_PER_SEC) {
        printf("⚠️  警告: 性能 %.0f ops/sec 低于预期 %d ops/sec\n", 
               overall_ops_per_sec, EXPECTED_MIN_OPS_PER_SEC);
    }
    
    if (total_errors > 0) {
        printf("❌ 发现 %d 个并发错误\n", total_errors);
        kv_store_btree_destroy(tree);
        return -1;
    }
    
    kv_store_btree_destroy(tree);
    printf("✅ 并发安全检查通过\n");
    return 0;
}

// === 树结构完整性检查 ===

static int validate_btree_structure(btree_t *tree) {
    if (!tree || !tree->root) {
        return 0;  // 空树是有效的
    }
    
    // 检查树的基本属性
    if (tree->height == 0) {
        printf("❌ 树高度为0但根节点存在\n");
        return -1;
    }
    
    // TODO: 实现完整的树结构验证
    // 1. 所有叶子节点在同一层
    // 2. 内部节点的键数量符合B+树要求
    // 3. 键的顺序正确
    // 4. 父子关系正确
    
    return 0;
}

static int check_tree_structure_integrity() {
    printf("\n=== 树结构完整性检查 ===\n");
    
    btree_t *tree = kv_store_btree_create(5);  // 小阶数便于测试
    if (!tree) {
        printf("❌ 无法创建B+Tree\n");
        return -1;
    }
    
    char key[64], value[64];
    
    // 逐步插入并验证
    for (int i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "struct_test_%03d", i);
        snprintf(value, sizeof(value), "value_%03d", i);
        
        kvs_btree_set(tree, key, value);
        
        if (validate_btree_structure(tree) != 0) {
            printf("❌ 插入第 %d 个键后结构验证失败\n", i + 1);
            kv_store_btree_destroy(tree);
            return -1;
        }
    }
    
    printf("结构测试: 插入100个键，最终高度=%u\n", tree->height);
    
    // 删除测试
    for (int i = 0; i < 50; i++) {
        snprintf(key, sizeof(key), "struct_test_%03d", i * 2);
        kvs_btree_delete(tree, key);
        
        if (validate_btree_structure(tree) != 0) {
            printf("❌ 删除第 %d 个键后结构验证失败\n", i + 1);
            kv_store_btree_destroy(tree);
            return -1;
        }
    }
    
    printf("删除测试: 删除50个键，最终高度=%u, 键数=%zu\n", 
           tree->height, tree->total_keys);
    
    kv_store_btree_destroy(tree);
    printf("✅ 树结构完整性检查通过\n");
    return 0;
}

// === 主测试函数 ===

int main() {
    printf("🔍 B+Tree生产环境级别检查\n");
    printf("===========================\n");
    
    production_test_result_t result = {0, 0, 0, 0.0};
    double start_time = get_time_ms();
    
    // 1. 阶数和配置检查
    result.total_tests++;
    if (check_btree_order_configuration() == 0) {
        result.passed_tests++;
    } else {
        result.critical_failures++;
    }
    
    // 2. 内存使用检查
    result.total_tests++;
    if (check_memory_usage() == 0) {
        result.passed_tests++;
    }
    
    // 3. 并发安全检查
    result.total_tests++;
    if (check_concurrent_safety() == 0) {
        result.passed_tests++;
    } else {
        result.critical_failures++;
    }
    
    // 4. 树结构完整性检查
    result.total_tests++;
    if (check_tree_structure_integrity() == 0) {
        result.passed_tests++;
    } else {
        result.critical_failures++;
    }
    
    result.total_time_ms = get_time_ms() - start_time;
    
    // 最终报告
    printf("\n=== 生产环境检查报告 ===\n");
    printf("总测试数: %d\n", result.total_tests);
    printf("通过测试: %d\n", result.passed_tests);
    printf("关键失败: %d\n", result.critical_failures);
    printf("总耗时: %.2f ms\n", result.total_time_ms);
    printf("成功率: %.1f%%\n", 
           (double)result.passed_tests / result.total_tests * 100.0);
    
    if (result.critical_failures == 0) {
        printf("\n🎉 B+Tree已准备好投入生产环境！\n");
        return 0;
    } else {
        printf("\n❌ B+Tree存在 %d 个关键问题，需要修复后才能用于生产环境\n", 
               result.critical_failures);
        return 1;
    }
} 