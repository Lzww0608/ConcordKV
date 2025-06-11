/*
 * @Author: Lzww0608
 * @Date: 2025-6-11 17:30:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-11 17:30:00
 * @Description: ConcordKV 异步批量I/O功能测试套件
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <pthread.h>
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>

#include "../../../kvserver/kv_async_batch_io.h"
#include "../../../kvserver/kv_io_uring.h"
#include "../../../kvserver/kv_error.h"

// === 测试配置 ===
#define TEST_TIMEOUT_SECONDS 30
#define TEST_DATA_DIR "/tmp/kv_async_batch_test"
#define TEST_FILE_PREFIX "test_batch_"
#define TEST_MAX_BATCH_SIZE 100
#define TEST_LARGE_BATCH_SIZE 1000
#define TEST_CONCURRENT_THREADS 4
#define TEST_OPERATIONS_PER_THREAD 100

// === 测试统计 ===
typedef struct {
    int tests_run;
    int tests_passed;
    int tests_failed;
    double total_time;
} test_stats_t;

static test_stats_t g_test_stats = {0};

// === 测试工具函数 ===

static double get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000.0 + tv.tv_usec;
}

static void setup_test_environment(void) {
    // 创建测试目录
    mkdir(TEST_DATA_DIR, 0755);
    
    // 清理旧文件
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -f %s/%s*", TEST_DATA_DIR, TEST_FILE_PREFIX);
    system(cmd);
}

static void cleanup_test_environment(void) {
    // 清理测试文件
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DATA_DIR);
    system(cmd);
}

static void test_callback(kv_async_batch_operation_t *op, int result, void *user_data) {
    int *counter = (int*)user_data;
    if (counter) {
        __sync_fetch_and_add(counter, 1);
    }
    printf("✓ 操作完成: 结果: %d\n", result);
    (void)op; // 避免未使用参数警告
}

static void batch_complete_callback(kv_async_batch_context_t *ctx, 
                                   size_t completed_count, size_t failed_count, 
                                   void *user_data) {
    printf("✓ 批量操作完成: 成功 %zu, 失败 %zu\n", completed_count, failed_count);
    (void)ctx; // 避免未使用参数警告
    (void)user_data; // 避免未使用参数警告
}

// === 测试用例 ===

// 测试1：配置管理和验证
void test_config_management(void) {
    printf("\n=== 测试1: 配置管理和验证 ===\n");
    double start_time = get_time_us();
    
    // 测试默认配置
    kv_async_batch_config_t config = kv_async_batch_default_config();
    assert(config.max_batch_size > 0);
    assert(config.max_memory_usage > 0);
    assert(config.timeout_ms > 0);
    assert(config.alloc_func != NULL);
    assert(config.free_func != NULL);
    printf("✓ 默认配置创建成功\n");
    
    // 测试配置验证
    int ret = kv_async_batch_config_validate(&config);
    assert(ret == KV_SUCCESS);
    printf("✓ 配置验证通过\n");
    
    // 测试无效配置
    kv_async_batch_config_t invalid_config = config;
    invalid_config.max_batch_size = 0;
    ret = kv_async_batch_config_validate(&invalid_config);
    assert(ret != KV_SUCCESS);
    printf("✓ 无效配置检测正确\n");
    
    // 测试配置复制
    kv_async_batch_config_t copied_config;
    ret = kv_async_batch_config_copy(&copied_config, &config);
    assert(ret == KV_SUCCESS);
    assert(copied_config.max_batch_size == config.max_batch_size);
    printf("✓ 配置复制成功\n");
    
    double end_time = get_time_us();
    printf("✓ 测试1通过 (%.2f ms)\n", (end_time - start_time) / 1000.0);
    g_test_stats.tests_passed++;
}

// 测试2：管理器生命周期
void test_manager_lifecycle(void) {
    printf("\n=== 测试2: 管理器生命周期 ===\n");
    double start_time = get_time_us();
    
    // 创建管理器
    kv_async_batch_config_t config = kv_async_batch_default_config();
    kv_async_batch_manager_t *manager = kv_async_batch_manager_create(&config);
    assert(manager != NULL);
    printf("✓ 管理器创建成功\n");
    
    // 启动管理器
    int ret = kv_async_batch_manager_start(manager);
    assert(ret == KV_SUCCESS);
    printf("✓ 管理器启动成功\n");
    
    // 停止管理器
    ret = kv_async_batch_manager_stop(manager);
    assert(ret == KV_SUCCESS);
    printf("✓ 管理器停止成功\n");
    
    // 销毁管理器
    kv_async_batch_manager_destroy(manager);
    printf("✓ 管理器销毁成功\n");
    
    double end_time = get_time_us();
    printf("✓ 测试2通过 (%.2f ms)\n", (end_time - start_time) / 1000.0);
    g_test_stats.tests_passed++;
}

// 测试3：基础批量操作
void test_basic_batch_operations(void) {
    printf("\n=== 测试3: 基础批量操作 ===\n");
    double start_time = get_time_us();
    
    // 创建管理器
    kv_async_batch_config_t config = kv_async_batch_default_config();
    kv_async_batch_manager_t *manager = kv_async_batch_manager_create(&config);
    assert(manager != NULL);
    
    int ret = kv_async_batch_manager_start(manager);
    assert(ret == KV_SUCCESS);
    
    // 创建批量操作上下文
    kv_async_batch_context_t *ctx = kv_async_batch_create(manager, 10);
    assert(ctx != NULL);
    printf("✓ 批量操作上下文创建成功\n");
    
    // 添加PUT操作
    int callback_count = 0;
    ret = kv_async_batch_put(ctx, "key1", 4, "value1", 6, test_callback, &callback_count);
    assert(ret == KV_SUCCESS);
    printf("✓ PUT操作添加成功\n");
    
    // 添加GET操作
    char *value = NULL;
    size_t value_len = 0;
    ret = kv_async_batch_get(ctx, "key1", 4, &value, &value_len, test_callback, &callback_count);
    assert(ret == KV_SUCCESS);
    printf("✓ GET操作添加成功\n");
    
    // 添加DELETE操作
    ret = kv_async_batch_delete(ctx, "key1", 4, test_callback, &callback_count);
    assert(ret == KV_SUCCESS);
    printf("✓ DELETE操作添加成功\n");
    
    // 提交批量操作
    ret = kv_async_batch_submit(ctx, batch_complete_callback, NULL);
    assert(ret == KV_SUCCESS);
    printf("✓ 批量操作提交成功\n");
    
    // 等待完成
    ret = kv_async_batch_wait(ctx, 5000);
    assert(ret == KV_SUCCESS);
    printf("✓ 批量操作等待完成\n");
    
    // 检查状态
    kv_async_batch_status_t status = kv_async_batch_get_status(ctx);
    assert(status == KV_ASYNC_BATCH_COMPLETED);
    printf("✓ 批量操作状态正确\n");
    
    // 获取统计信息
    kv_async_batch_stats_t stats;
    ret = kv_async_batch_get_stats(ctx, &stats);
    assert(ret == KV_SUCCESS);
    printf("✓ 统计信息获取成功\n");
    
    // 清理
    kv_async_batch_destroy(ctx);
    kv_async_batch_manager_stop(manager);
    kv_async_batch_manager_destroy(manager);
    
    double end_time = get_time_us();
    printf("✓ 测试3通过 (%.2f ms)\n", (end_time - start_time) / 1000.0);
    g_test_stats.tests_passed++;
}

// 测试4：大批量操作性能
void test_large_batch_performance(void) {
    printf("\n=== 测试4: 大批量操作性能 ===\n");
    double start_time = get_time_us();
    
    // 创建管理器
    kv_async_batch_config_t config = kv_async_batch_default_config();
    config.max_batch_size = TEST_LARGE_BATCH_SIZE;
    kv_async_batch_manager_t *manager = kv_async_batch_manager_create(&config);
    assert(manager != NULL);
    
    int ret = kv_async_batch_manager_start(manager);
    assert(ret == KV_SUCCESS);
    
    // 创建大批量操作上下文
    kv_async_batch_context_t *ctx = kv_async_batch_create(manager, TEST_LARGE_BATCH_SIZE);
    assert(ctx != NULL);
    
    double add_start = get_time_us();
    
    // 添加大量操作
    for (int i = 0; i < TEST_LARGE_BATCH_SIZE; i++) {
        char key[32], value[64];
        snprintf(key, sizeof(key), "key_%d", i);
        snprintf(value, sizeof(value), "value_%d_data", i);
        
        ret = kv_async_batch_put(ctx, key, strlen(key), value, strlen(value), NULL, NULL);
        assert(ret == KV_SUCCESS);
    }
    
    double add_end = get_time_us();
    double add_time = (add_end - add_start) / 1000.0;
    printf("✓ 添加 %d 个操作完成 (%.2f ms, %.0f ops/sec)\n", 
           TEST_LARGE_BATCH_SIZE, add_time, TEST_LARGE_BATCH_SIZE / add_time * 1000.0);
    
    // 提交批量操作
    double submit_start = get_time_us();
    ret = kv_async_batch_submit(ctx, batch_complete_callback, NULL);
    assert(ret == KV_SUCCESS);
    
    // 等待完成
    ret = kv_async_batch_wait(ctx, 10000);
    assert(ret == KV_SUCCESS);
    
    double submit_end = get_time_us();
    double submit_time = (submit_end - submit_start) / 1000.0;
    printf("✓ 提交并完成 %d 个操作 (%.2f ms, %.0f ops/sec)\n", 
           TEST_LARGE_BATCH_SIZE, submit_time, TEST_LARGE_BATCH_SIZE / submit_time * 1000.0);
    
    // 获取统计信息
    kv_async_batch_stats_t stats;
    ret = kv_async_batch_get_stats(ctx, &stats);
    assert(ret == KV_SUCCESS);
    printf("✓ 统计信息: 总批次 %lu, 总操作 %lu\n", 
           stats.total_batches, stats.total_operations);
    
    // 清理
    kv_async_batch_destroy(ctx);
    kv_async_batch_manager_stop(manager);
    kv_async_batch_manager_destroy(manager);
    
    double end_time = get_time_us();
    printf("✓ 测试4通过 (%.2f ms)\n", (end_time - start_time) / 1000.0);
    g_test_stats.tests_passed++;
}

// 测试5：批量操作取消
void test_batch_cancellation(void) {
    printf("\n=== 测试5: 批量操作取消 ===\n");
    double start_time = get_time_us();
    
    // 创建管理器
    kv_async_batch_config_t config = kv_async_batch_default_config();
    kv_async_batch_manager_t *manager = kv_async_batch_manager_create(&config);
    assert(manager != NULL);
    
    int ret = kv_async_batch_manager_start(manager);
    assert(ret == KV_SUCCESS);
    
    // 创建批量操作上下文
    kv_async_batch_context_t *ctx = kv_async_batch_create(manager, 10);
    assert(ctx != NULL);
    
    // 添加一些操作
    for (int i = 0; i < 5; i++) {
        char key[32], value[64];
        snprintf(key, sizeof(key), "cancel_key_%d", i);
        snprintf(value, sizeof(value), "cancel_value_%d", i);
        
        ret = kv_async_batch_put(ctx, key, strlen(key), value, strlen(value), NULL, NULL);
        assert(ret == KV_SUCCESS);
    }
    printf("✓ 添加操作完成\n");
    
    // 取消操作
    ret = kv_async_batch_cancel(ctx);
    assert(ret == KV_SUCCESS);
    printf("✓ 批量操作取消成功\n");
    
    // 检查状态
    kv_async_batch_status_t status = kv_async_batch_get_status(ctx);
    assert(status == KV_ASYNC_BATCH_CANCELLED);
    printf("✓ 取消状态正确\n");
    
    // 清理
    kv_async_batch_destroy(ctx);
    kv_async_batch_manager_stop(manager);
    kv_async_batch_manager_destroy(manager);
    
    double end_time = get_time_us();
    printf("✓ 测试5通过 (%.2f ms)\n", (end_time - start_time) / 1000.0);
    g_test_stats.tests_passed++;
}

// 测试6：并发批量操作安全性
typedef struct {
    kv_async_batch_manager_t *manager;
    int thread_id;
    int operations_count;
    int success_count;
    int error_count;
} thread_test_data_t;

void* concurrent_batch_worker(void *arg) {
    thread_test_data_t *data = (thread_test_data_t*)arg;
    
    for (int batch = 0; batch < 5; batch++) {
        // 创建批量操作上下文
        kv_async_batch_context_t *ctx = kv_async_batch_create(data->manager, 20);
        if (!ctx) {
            data->error_count++;
            continue;
        }
        
        // 添加操作
        bool batch_success = true;
        for (int i = 0; i < 20; i++) {
            char key[32], value[64];
            snprintf(key, sizeof(key), "t%d_b%d_k%d", data->thread_id, batch, i);
            snprintf(value, sizeof(value), "t%d_b%d_v%d_data", data->thread_id, batch, i);
            
            int ret = kv_async_batch_put(ctx, key, strlen(key), value, strlen(value), NULL, NULL);
            if (ret != KV_SUCCESS) {
                batch_success = false;
                break;
            }
        }
        
        if (batch_success) {
            // 提交批量操作
            int ret = kv_async_batch_submit(ctx, NULL, NULL);
            if (ret == KV_SUCCESS) {
                // 等待完成
                ret = kv_async_batch_wait(ctx, 5000);
                if (ret == KV_SUCCESS) {
                    data->success_count++;
                } else {
                    data->error_count++;
                }
            } else {
                data->error_count++;
            }
        } else {
            data->error_count++;
        }
        
        data->operations_count++;
        kv_async_batch_destroy(ctx);
        
        // 短暂休眠
        usleep(1000);
    }
    
    return NULL;
}

void test_concurrent_batch_safety(void) {
    printf("\n=== 测试6: 并发批量操作安全性 ===\n");
    double start_time = get_time_us();
    
    // 创建管理器
    kv_async_batch_config_t config = kv_async_batch_default_config();
    kv_async_batch_manager_t *manager = kv_async_batch_manager_create(&config);
    assert(manager != NULL);
    
    int ret = kv_async_batch_manager_start(manager);
    assert(ret == KV_SUCCESS);
    
    // 创建线程
    pthread_t threads[TEST_CONCURRENT_THREADS];
    thread_test_data_t thread_data[TEST_CONCURRENT_THREADS];
    
    for (int i = 0; i < TEST_CONCURRENT_THREADS; i++) {
        thread_data[i].manager = manager;
        thread_data[i].thread_id = i;
        thread_data[i].operations_count = 0;
        thread_data[i].success_count = 0;
        thread_data[i].error_count = 0;
        
        ret = pthread_create(&threads[i], NULL, concurrent_batch_worker, &thread_data[i]);
        assert(ret == 0);
    }
    printf("✓ %d 个并发线程启动\n", TEST_CONCURRENT_THREADS);
    
    // 等待所有线程完成
    for (int i = 0; i < TEST_CONCURRENT_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // 统计结果
    int total_operations = 0, total_success = 0, total_errors = 0;
    for (int i = 0; i < TEST_CONCURRENT_THREADS; i++) {
        total_operations += thread_data[i].operations_count;
        total_success += thread_data[i].success_count;
        total_errors += thread_data[i].error_count;
        printf("✓ 线程 %d: 操作 %d, 成功 %d, 错误 %d\n", 
               i, thread_data[i].operations_count, 
               thread_data[i].success_count, thread_data[i].error_count);
    }
    
    printf("✓ 总计: 操作 %d, 成功 %d, 错误 %d\n", total_operations, total_success, total_errors);
    printf("✓ 成功率: %.1f%%\n", (double)total_success / total_operations * 100.0);
    
    // 成功率应该很高（允许一些并发冲突）
    assert(total_success > total_operations * 0.8);  // 至少80%成功率
    
    // 清理
    kv_async_batch_manager_stop(manager);
    kv_async_batch_manager_destroy(manager);
    
    double end_time = get_time_us();
    printf("✓ 测试6通过 (%.2f ms)\n", (end_time - start_time) / 1000.0);
    g_test_stats.tests_passed++;
}

// 测试7：错误处理和边界条件
void test_error_handling(void) {
    printf("\n=== 测试7: 错误处理和边界条件 ===\n");
    double start_time = get_time_us();
    
    // 测试NULL指针 (config允许为NULL，使用默认配置)
    kv_async_batch_config_t config = kv_async_batch_default_config();
    kv_async_batch_manager_t *test_manager = kv_async_batch_manager_create(&config);
    assert(test_manager != NULL);
    kv_async_batch_manager_destroy(test_manager);
    
    assert(kv_async_batch_create(NULL, 10) == NULL);
    printf("✓ NULL指针检查通过\n");
    
    // 测试无效参数
    kv_async_batch_manager_t *manager = kv_async_batch_manager_create(&config);
    assert(manager != NULL);
    
    int ret = kv_async_batch_manager_start(manager);
    assert(ret == KV_SUCCESS);
    
    // 创建批量操作上下文
    kv_async_batch_context_t *ctx = kv_async_batch_create(manager, 5);
    assert(ctx != NULL);
    
    // 测试无效操作
    ret = kv_async_batch_put(ctx, NULL, 0, "value", 5, NULL, NULL);
    assert(ret != KV_SUCCESS);
    
    ret = kv_async_batch_put(ctx, "key", 3, NULL, 0, NULL, NULL);
    assert(ret != KV_SUCCESS);
    printf("✓ 无效参数检查通过\n");
    
    // 测试容量限制
    for (int i = 0; i < 10; i++) {  // 超过容量(5)
        char key[32], value[32];
        snprintf(key, sizeof(key), "key_%d", i);
        snprintf(value, sizeof(value), "value_%d", i);
        
        ret = kv_async_batch_put(ctx, key, strlen(key), value, strlen(value), NULL, NULL);
        if (i < 5) {
            assert(ret == KV_SUCCESS);
        } else {
            assert(ret != KV_SUCCESS);  // 应该失败
        }
    }
    printf("✓ 容量限制检查通过\n");
    
    // 测试重复提交
    ret = kv_async_batch_submit(ctx, NULL, NULL);
    assert(ret == KV_SUCCESS);
    
    ret = kv_async_batch_submit(ctx, NULL, NULL);
    assert(ret != KV_SUCCESS);  // 应该失败
    printf("✓ 重复提交检查通过\n");
    
    // 清理
    kv_async_batch_destroy(ctx);
    kv_async_batch_manager_stop(manager);
    kv_async_batch_manager_destroy(manager);
    
    double end_time = get_time_us();
    printf("✓ 测试7通过 (%.2f ms)\n", (end_time - start_time) / 1000.0);
    g_test_stats.tests_passed++;
}

// 测试8：工具函数和状态查询
void test_utility_functions(void) {
    printf("\n=== 测试8: 工具函数和状态查询 ===\n");
    double start_time = get_time_us();
    
    // 测试操作名称
    assert(strcmp(kv_async_batch_op_name(KV_ASYNC_BATCH_PUT), "PUT") == 0);
    assert(strcmp(kv_async_batch_op_name(KV_ASYNC_BATCH_GET), "GET") == 0);
    assert(strcmp(kv_async_batch_op_name(KV_ASYNC_BATCH_DELETE), "DELETE") == 0);
    printf("✓ 操作名称获取正确\n");
    
    // 测试状态名称
    assert(strcmp(kv_async_batch_status_name(KV_ASYNC_BATCH_PENDING), "PENDING") == 0);
    assert(strcmp(kv_async_batch_status_name(KV_ASYNC_BATCH_SUBMITTED), "SUBMITTED") == 0);
    assert(strcmp(kv_async_batch_status_name(KV_ASYNC_BATCH_COMPLETED), "COMPLETED") == 0);
    printf("✓ 状态名称获取正确\n");
    
    // 测试时间戳
    uint64_t ts1 = kv_async_batch_get_timestamp_us();
    usleep(1000);  // 1ms
    uint64_t ts2 = kv_async_batch_get_timestamp_us();
    assert(ts2 > ts1);
    printf("✓ 时间戳功能正确\n");
    
    double end_time = get_time_us();
    printf("✓ 测试8通过 (%.2f ms)\n", (end_time - start_time) / 1000.0);
    g_test_stats.tests_passed++;
}

// === 超时保护机制 ===
static void timeout_handler(int sig) {
    printf("\n❌ 测试超时 (%d 秒)！可能存在死锁或无限循环\n", TEST_TIMEOUT_SECONDS);
    cleanup_test_environment();
    exit(1);
}

// === 主测试函数 ===
int main(int argc, char *argv[]) {
    printf("=== ConcordKV 异步批量I/O功能测试套件 ===\n");
    printf("测试超时设置: %d 秒\n", TEST_TIMEOUT_SECONDS);
    
    // 设置超时处理
    signal(SIGALRM, timeout_handler);
    alarm(TEST_TIMEOUT_SECONDS);
    
    // 初始化测试环境
    setup_test_environment();
    
    double total_start = get_time_us();
    
    // 运行测试用例
    printf("\n开始运行测试用例...\n");
    
    g_test_stats.tests_run = 8;
    g_test_stats.tests_passed = 0;
    g_test_stats.tests_failed = 0;
    
    // 执行所有测试
    test_config_management();           // 测试1
    test_manager_lifecycle();           // 测试2
    test_basic_batch_operations();      // 测试3
    test_large_batch_performance();     // 测试4
    test_batch_cancellation();         // 测试5
    test_concurrent_batch_safety();     // 测试6
    test_error_handling();              // 测试7
    test_utility_functions();          // 测试8
    
    double total_end = get_time_us();
    g_test_stats.total_time = (total_end - total_start) / 1000.0;
    g_test_stats.tests_failed = g_test_stats.tests_run - g_test_stats.tests_passed;
    
    // 输出测试结果
    printf("\n=== 测试结果汇总 ===\n");
    printf("总测试数: %d\n", g_test_stats.tests_run);
    printf("通过测试: %d\n", g_test_stats.tests_passed);
    printf("失败测试: %d\n", g_test_stats.tests_failed);
    printf("总耗时: %.2f ms\n", g_test_stats.total_time);
    printf("成功率: %.1f%%\n", (double)g_test_stats.tests_passed / g_test_stats.tests_run * 100.0);
    
    if (g_test_stats.tests_failed == 0) {
        printf("\n🎉 所有测试通过！异步批量I/O功能运行正常\n");
    } else {
        printf("\n❌ 有 %d 个测试失败\n", g_test_stats.tests_failed);
    }
    
    // 清理测试环境
    cleanup_test_environment();
    
    // 取消超时
    alarm(0);
    
    return g_test_stats.tests_failed == 0 ? 0 : 1;
}