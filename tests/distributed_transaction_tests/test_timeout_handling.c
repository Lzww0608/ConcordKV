/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 11:18:14
* @Description: ConcordKV超时处理和死锁检测测试
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>

#include "kv_distributed_transaction.h"

// 简单的内存管理函数实现
void *kv_store_malloc(size_t size) {
    return malloc(size);
}

void kv_store_free(void *ptr) {
    free(ptr);
}

// 测试统计
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static volatile int timeout_flag = 0;

// 超时保护
void timeout_handler(int sig) {
    timeout_flag = 1;
    printf("\n[TIMEOUT] Test execution timeout!\n");
}

void set_test_timeout(int seconds) {
    timeout_flag = 0;
    signal(SIGALRM, timeout_handler);
    alarm(seconds);
}

void clear_test_timeout() {
    alarm(0);
    signal(SIGALRM, SIG_DFL);
}

#define TEST_ASSERT(condition, message) do { \
    tests_run++; \
    if (timeout_flag) { \
        printf("[TIMEOUT] %s\n", message); \
        tests_failed++; \
        return; \
    } \
    if (condition) { \
        printf("[PASS] %s\n", message); \
        tests_passed++; \
    } else { \
        printf("[FAIL] %s\n", message); \
        tests_failed++; \
    } \
} while(0)

#define TEST_START(test_name) do { \
    printf("\n=== Testing %s ===\n", test_name); \
    set_test_timeout(120); \
} while(0)

#define TEST_END() do { \
    clear_test_timeout(); \
} while(0)

// 获取当前时间（毫秒）
long long get_current_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// 模拟慢响应参与者
static bool g_slow_response = false;
static int g_response_delay_ms = 0;
static bool g_network_partition = false;

// 慢响应消息发送回调
int slow_send_callback(const char *node_id, const dist_txn_message_t *msg) {
    if (g_network_partition && strcmp(node_id, "slow_node") == 0) {
        printf("[MOCK] Network partition to %s\n", node_id);
        return DIST_TXN_ERROR;
    }
    
    if (g_slow_response && strcmp(node_id, "slow_node") == 0) {
        printf("[MOCK] Slow response from %s (delay: %dms)\n", node_id, g_response_delay_ms);
        usleep(g_response_delay_ms * 1000);
    }
    
    printf("[MOCK] Message sent to %s: type=%d, txn_id=%lu\n", 
           node_id, msg->type, msg->global_txn_id);
    
    return DIST_TXN_SUCCESS;
}

// 测试基本超时检测
void test_basic_timeout_detection() {
    TEST_START("Basic Timeout Detection");
    
    // 初始化协调器
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "coordinator", NULL);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator initialization should succeed");
    
    // 添加节点
    dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    dist_txn_add_node(&coordinator, "slow_node", "127.0.0.1", 8002);
    
    // 设置慢响应
    g_slow_response = true;
    g_response_delay_ms = 1500;  // 1.5秒延迟
    dist_txn_set_send_callback(slow_send_callback);
    
    // 创建短超时事务
    const char *participants[] = {"node1", "slow_node"};
    long long start_time = get_current_time_ms();
    
    dist_transaction_t *txn = dist_txn_begin(&coordinator, TXN_PRIORITY_NORMAL, 1000,  // 1秒超时
                                           participants, 2);
    TEST_ASSERT(txn != NULL, "Transaction should be created successfully");
    
    // 执行准备阶段（应该超时）
    ret = dist_txn_prepare(&coordinator, txn);
    long long end_time = get_current_time_ms();
    long long elapsed = end_time - start_time;
    
    printf("Prepare phase took %lld ms\n", elapsed);
    
    // 验证超时检测
    TEST_ASSERT(elapsed >= 1000 && elapsed <= 2000, 
                "Prepare should timeout within expected range");
    TEST_ASSERT(ret == DIST_TXN_PREPARE_FAILED || ret == DIST_TXN_TIMEOUT,
                "Prepare should fail due to timeout");
    
    // 清理
    g_slow_response = false;
    // dist_txn_free(txn);  // 让协调器销毁时自动清理
    dist_txn_coordinator_destroy(&coordinator);
    
    TEST_END();
}

// 测试事务级超时
void test_transaction_level_timeout() {
    TEST_START("Transaction Level Timeout");
    
    // 初始化协调器
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "coordinator", NULL);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator initialization should succeed");
    
    // 添加节点
    dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    
    // 启动协调器线程
    ret = dist_txn_coordinator_start(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should start successfully");
    
    // 创建非常短超时的事务
    const char *participants[] = {"node1"};
    dist_transaction_t *txn = dist_txn_begin(&coordinator, TXN_PRIORITY_NORMAL, 100,  // 100ms超时
                                           participants, 1);
    TEST_ASSERT(txn != NULL, "Transaction should be created successfully");
    
    long long start_time = get_current_time_ms();
    
    // 等待超时检查线程工作
    sleep(1);
    
    long long end_time = get_current_time_ms();
    long long elapsed = end_time - start_time;
    
    // 检查超时统计
    long total, committed, aborted, timeout;
    dist_txn_get_stats(&coordinator, &total, &committed, &aborted, &timeout);
    
    printf("Transaction timeout test: elapsed=%lld ms, timeout_count=%ld\n", elapsed, timeout);
    
    TEST_ASSERT(timeout > 0, "At least one transaction should timeout");
    TEST_ASSERT(txn->status == DIST_TXN_ABORTED, "Timeout transaction should be aborted");
    
    // 停止协调器
    ret = dist_txn_coordinator_stop(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should stop successfully");
    
    // 清理
    g_slow_response = false;
    // dist_txn_free(txn);  // 让协调器销毁时自动清理
    dist_txn_coordinator_destroy(&coordinator);
    
    TEST_END();
}

// 测试网络分区恢复
void test_network_partition_recovery() {
    TEST_START("Network Partition Recovery");
    
    // 初始化协调器
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "coordinator", NULL);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator initialization should succeed");
    
    // 添加节点
    dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    dist_txn_add_node(&coordinator, "slow_node", "127.0.0.1", 8002);
    
    dist_txn_set_send_callback(slow_send_callback);
    
    // 模拟网络分区
    g_network_partition = true;
    
    // 创建事务
    const char *participants[] = {"node1", "slow_node"};
    dist_transaction_t *txn = dist_txn_begin(&coordinator, TXN_PRIORITY_NORMAL, 3000,
                                           participants, 2);
    TEST_ASSERT(txn != NULL, "Transaction should be created successfully");
    
    // 执行准备阶段（应该失败）
    ret = dist_txn_prepare(&coordinator, txn);
    TEST_ASSERT(ret == DIST_TXN_PREPARE_FAILED, 
                "Prepare should fail due to network partition");
    
    // 恢复网络
    g_network_partition = false;
    
    // 创建新事务测试恢复
    dist_transaction_t *txn2 = dist_txn_begin(&coordinator, TXN_PRIORITY_NORMAL, 3000,
                                            participants, 2);
    TEST_ASSERT(txn2 != NULL, "Second transaction should be created successfully");
    
    ret = dist_txn_prepare(&coordinator, txn2);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Prepare should succeed after network recovery");
    
    ret = dist_txn_commit(&coordinator, txn2);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Commit should succeed after network recovery");
    
    // 清理
    g_slow_response = false;
    // dist_txn_free(txn);  // 让协调器销毁时自动清理
    dist_txn_coordinator_destroy(&coordinator);
    
    TEST_END();
}

// 并发超时测试线程数据
typedef struct timeout_thread_data {
    dist_txn_coordinator_t *coordinator;
    int thread_id;
    int transaction_count;
    int timeout_count;
    int success_count;
    int min_timeout_ms;
    int max_timeout_ms;
} timeout_thread_data_t;

// 并发超时测试线程函数
void *timeout_test_thread(void *arg) {
    timeout_thread_data_t *data = (timeout_thread_data_t *)arg;
    
    for (int i = 0; i < data->transaction_count; i++) {
        // 随机超时时间
        int timeout_ms = data->min_timeout_ms + 
                        (rand() % (data->max_timeout_ms - data->min_timeout_ms));
        
        const char *participants[] = {"node1"};
        dist_transaction_t *txn = dist_txn_begin(data->coordinator, TXN_PRIORITY_NORMAL, 
                                               timeout_ms, participants, 1);
        
        if (txn) {
            // 等待一段时间让事务可能超时
            usleep((timeout_ms / 2) * 1000);
            
            // 检查事务状态
            if (txn->status == DIST_TXN_ABORTED) {
                data->timeout_count++;
            } else {
                data->success_count++;
            }
            
            // dist_txn_free(txn);  // 让协调器销毁时自动清理
        }
        
        // 随机延迟
        usleep((rand() % 100) * 1000);  // 0-100ms
    }
    
    return NULL;
}

// 测试并发超时处理
void test_concurrent_timeout_handling() {
    TEST_START("Concurrent Timeout Handling");
    
    // 初始化协调器
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "coordinator", NULL);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator initialization should succeed");
    
    // 添加节点
    dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    
    // 启动协调器
    ret = dist_txn_coordinator_start(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should start successfully");
    
    const int NUM_THREADS = 4;
    const int TXN_PER_THREAD = 10;
    
    pthread_t threads[NUM_THREADS];
    timeout_thread_data_t thread_data[NUM_THREADS];
    
    // 启动测试线程
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].coordinator = &coordinator;
        thread_data[i].thread_id = i;
        thread_data[i].transaction_count = TXN_PER_THREAD;
        thread_data[i].timeout_count = 0;
        thread_data[i].success_count = 0;
        thread_data[i].min_timeout_ms = 50;
        thread_data[i].max_timeout_ms = 200;
        
        pthread_create(&threads[i], NULL, timeout_test_thread, &thread_data[i]);
    }
    
    // 等待线程完成
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // 统计结果
    int total_timeout = 0;
    int total_success = 0;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        total_timeout += thread_data[i].timeout_count;
        total_success += thread_data[i].success_count;
        printf("Thread %d: timeout=%d, success=%d\n", 
               i, thread_data[i].timeout_count, thread_data[i].success_count);
    }
    
    printf("Total: timeout=%d, success=%d\n", total_timeout, total_success);
    
    TEST_ASSERT(total_timeout + total_success == NUM_THREADS * TXN_PER_THREAD,
                "All transactions should be accounted for");
    TEST_ASSERT(total_timeout > 0, "Some transactions should timeout");
    
    // 检查协调器统计
    long coordinator_total, coordinator_committed, coordinator_aborted, coordinator_timeout;
    dist_txn_get_stats(&coordinator, &coordinator_total, &coordinator_committed, 
                      &coordinator_aborted, &coordinator_timeout);
    
    printf("Coordinator stats: total=%ld, committed=%ld, aborted=%ld, timeout=%ld\n",
           coordinator_total, coordinator_committed, coordinator_aborted, coordinator_timeout);
    
    // 停止协调器
    ret = dist_txn_coordinator_stop(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should stop successfully");
    
    // 清理
    g_slow_response = false;
    // dist_txn_free(txn);  // 让协调器销毁时自动清理
    dist_txn_coordinator_destroy(&coordinator);
    
    TEST_END();
}

// 测试死锁检测和恢复
void test_deadlock_detection() {
    TEST_START("Deadlock Detection and Recovery");
    
    // 初始化协调器
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "coordinator", NULL);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator initialization should succeed");
    
    // 添加节点
    dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    dist_txn_add_node(&coordinator, "node2", "127.0.0.1", 8002);
    
    // 启动协调器
    ret = dist_txn_coordinator_start(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should start successfully");
    
    // 创建可能造成死锁的事务组合
    const char *participants1[] = {"node1", "node2"};
    const char *participants2[] = {"node2", "node1"};  // 相同节点，不同顺序
    
    // 使用短超时来快速检测死锁
    dist_transaction_t *txn1 = dist_txn_begin(&coordinator, TXN_PRIORITY_HIGH, 500,
                                            participants1, 2);
    dist_transaction_t *txn2 = dist_txn_begin(&coordinator, TXN_PRIORITY_HIGH, 500,
                                            participants2, 2);
    
    TEST_ASSERT(txn1 != NULL, "First transaction should be created");
    TEST_ASSERT(txn2 != NULL, "Second transaction should be created");
    
    // 等待超时检测机制工作
    sleep(2);
    
    // 检查是否有事务超时（模拟死锁检测）
    long total, committed, aborted, timeout;
    dist_txn_get_stats(&coordinator, &total, &committed, &aborted, &timeout);
    
    printf("Deadlock test results: total=%ld, committed=%ld, aborted=%ld, timeout=%ld\n",
           total, committed, aborted, timeout);
    
    TEST_ASSERT(timeout > 0 || aborted > 0, "Deadlock should be detected and resolved");
    TEST_ASSERT(txn1->status == DIST_TXN_ABORTED || txn1->status == DIST_TXN_COMMITTED,
                "Transaction 1 should have final state");
    TEST_ASSERT(txn2->status == DIST_TXN_ABORTED || txn2->status == DIST_TXN_COMMITTED,
                "Transaction 2 should have final state");
    
    // 停止协调器
    ret = dist_txn_coordinator_stop(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should stop successfully");
    
    // 清理
    g_slow_response = false;
    // dist_txn_free(txn1);  // 让协调器销毁时自动清理
    // dist_txn_free(txn2);  // 让协调器销毁时自动清理
    dist_txn_coordinator_destroy(&coordinator);
    
    TEST_END();
}

// 测试超时队列容量
void test_timeout_queue_capacity() {
    TEST_START("Timeout Queue Capacity");
    
    // 创建小容量的优先级队列
    txn_priority_queue_t *queue = txn_priority_queue_create(3);
    TEST_ASSERT(queue != NULL, "Queue should be created successfully");
    
    // 创建模拟事务
    dist_transaction_t *txns[5];
    for (int i = 0; i < 5; i++) {
        txns[i] = malloc(sizeof(dist_transaction_t));
        memset(txns[i], 0, sizeof(dist_transaction_t));
        txns[i]->global_txn_id = i + 1;
        txns[i]->priority = TXN_PRIORITY_NORMAL;
        txns[i]->status = DIST_TXN_PREPARING;
        pthread_mutex_init(&txns[i]->mutex, NULL);
        pthread_cond_init(&txns[i]->condition, NULL);
    }
    
    // 填满队列
    for (int i = 0; i < 3; i++) {
        int ret = txn_priority_queue_enqueue(queue, txns[i]);
        TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Enqueue should succeed");
    }
    
    // 测试队列满时的超时
    long long start_time = get_current_time_ms();
    int ret = txn_priority_queue_enqueue(queue, txns[3]);
    long long end_time = get_current_time_ms();
    long long elapsed = end_time - start_time;
    
    TEST_ASSERT(ret == DIST_TXN_QUEUE_FULL, "Enqueue to full queue should fail");
    TEST_ASSERT(elapsed >= 900 && elapsed <= 1200, 
                "Enqueue should timeout after approximately 1 second");
    
    // 清理
    for (int i = 0; i < 5; i++) {
        if (txns[i]) {
            pthread_mutex_destroy(&txns[i]->mutex);
            pthread_cond_destroy(&txns[i]->condition);
            free(txns[i]);
        }
    }
    
    // 清空队列
    dist_transaction_t *txn;
    while ((txn = txn_priority_queue_dequeue(queue, 100)) != NULL) {
        // 已在上面释放
    }
    
    txn_priority_queue_destroy(queue);
    
    TEST_END();
}

// 测试优雅降级
void test_graceful_degradation() {
    TEST_START("Graceful Degradation");
    
    // 初始化协调器
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "coordinator", NULL);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator initialization should succeed");
    
    // 添加节点
    dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    dist_txn_add_node(&coordinator, "unreliable_node", "127.0.0.1", 8002);
    
    // 设置不可靠的网络条件
    g_slow_response = true;
    g_response_delay_ms = 100;
    dist_txn_set_send_callback(slow_send_callback);
    
    // 启动协调器
    ret = dist_txn_coordinator_start(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should start successfully");
    
    // 创建多个事务测试系统在压力下的表现
    const char *participants[] = {"node1", "unreliable_node"};
    const int NUM_TXN = 10;
    
    long long start_time = get_current_time_ms();
    
    for (int i = 0; i < NUM_TXN; i++) {
        dist_transaction_t *txn = dist_txn_begin(&coordinator, TXN_PRIORITY_NORMAL, 2000,
                                               participants, 2);
        if (txn) {
            // 不等待完成，让协调器调度器处理
        }
        usleep(50000);  // 50ms间隔
    }
    
    // 等待处理完成
    sleep(3);
    
    long long end_time = get_current_time_ms();
    long long total_time = end_time - start_time;
    
    // 检查统计信息
    long total, committed, aborted, timeout;
    dist_txn_get_stats(&coordinator, &total, &committed, &aborted, &timeout);
    
    printf("Graceful degradation test: total=%ld, committed=%ld, aborted=%ld, timeout=%ld\n",
           total, committed, aborted, timeout);
    printf("Total processing time: %lld ms\n", total_time);
    
    TEST_ASSERT(total >= NUM_TXN, "All transactions should be processed");
    TEST_ASSERT(committed + aborted + timeout == total, 
                "All transactions should have final state");
    
    // 系统应该仍然响应
    TEST_ASSERT(total_time < 10000, "System should remain responsive under stress");
    
    // 停止协调器
    ret = dist_txn_coordinator_stop(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should stop gracefully");
    
    // 清理
    g_slow_response = false;
    // dist_txn_free(txn);  // 让协调器销毁时自动清理
    dist_txn_coordinator_destroy(&coordinator);
    
    TEST_END();
}

// 主测试函数
int main(int argc, char *argv[]) {
    printf("ConcordKV Timeout Handling Tests\n");
    printf("=================================\n");
    
    // 初始化随机种子
    srand(time(NULL));
    
    // 运行测试
    test_basic_timeout_detection();
    test_transaction_level_timeout();
    test_network_partition_recovery();
    test_concurrent_timeout_handling();
    test_deadlock_detection();
    test_timeout_queue_capacity();
    test_graceful_degradation();
    
    // 输出测试结果
    printf("\n=================================\n");
    printf("Test Results:\n");
    printf("  Total tests: %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("  Success rate: %.1f%%\n", 
           tests_run > 0 ? (float)tests_passed / tests_run * 100 : 0);
    
    // 生成报告模式
    if (argc > 1 && strcmp(argv[1], "--report") == 0) {
        printf("\n### Timeout Handling Test Results\n");
        printf("- **Total Tests**: %d\n", tests_run);
        printf("- **Passed**: %d\n", tests_passed);
        printf("- **Failed**: %d\n", tests_failed);
        printf("- **Success Rate**: %.1f%%\n", 
               tests_run > 0 ? (float)tests_passed / tests_run * 100 : 0);
        printf("\n#### Key Features Tested\n");
        printf("- Basic timeout detection and handling\n");
        printf("- Transaction-level timeout management\n");
        printf("- Network partition detection and recovery\n");
        printf("- Concurrent timeout processing\n");
        printf("- Deadlock detection and resolution\n");
        printf("- Queue capacity and backpressure handling\n");
        printf("- Graceful degradation under stress\n");
    }
    
    return (tests_failed == 0) ? 0 : 1;
} 