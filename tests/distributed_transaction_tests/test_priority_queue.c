/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 11:18:08
* @Description: ConcordKV优先级队列测试
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

// 超时保护
static volatile int timeout_flag = 0;

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
    set_test_timeout(30); \
} while(0)

#define TEST_END() do { \
    clear_test_timeout(); \
} while(0)

// 创建模拟事务
dist_transaction_t *create_mock_transaction(uint64_t id, txn_priority_t priority) {
    dist_transaction_t *txn = malloc(sizeof(dist_transaction_t));
    if (!txn) return NULL;
    
    memset(txn, 0, sizeof(dist_transaction_t));
    txn->global_txn_id = id;
    txn->priority = priority;
    txn->status = DIST_TXN_PREPARING;
    txn->create_time = time(NULL);
    
    pthread_mutex_init(&txn->mutex, NULL);
    pthread_cond_init(&txn->condition, NULL);
    
    return txn;
}

// 释放模拟事务
void free_mock_transaction(dist_transaction_t *txn) {
    if (!txn) return;
    
    pthread_mutex_destroy(&txn->mutex);
    pthread_cond_destroy(&txn->condition);
    free(txn);
}

// 测试优先级队列基础操作
void test_basic_operations() {
    TEST_START("Priority Queue Basic Operations");
    
    // 创建队列
    txn_priority_queue_t *queue = txn_priority_queue_create(10);
    TEST_ASSERT(queue != NULL, "Queue creation should succeed");
    TEST_ASSERT(txn_priority_queue_size(queue) == 0, "New queue should be empty");
    
    // 测试入队
    dist_transaction_t *txn1 = create_mock_transaction(1, TXN_PRIORITY_NORMAL);
    int ret = txn_priority_queue_enqueue(queue, txn1);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Enqueue should succeed");
    TEST_ASSERT(txn_priority_queue_size(queue) == 1, "Queue size should be 1");
    
    dist_transaction_t *txn2 = create_mock_transaction(2, TXN_PRIORITY_HIGH);
    ret = txn_priority_queue_enqueue(queue, txn2);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Enqueue should succeed");
    TEST_ASSERT(txn_priority_queue_size(queue) == 2, "Queue size should be 2");
    
    // 测试出队
    dist_transaction_t *dequeued = txn_priority_queue_dequeue(queue, 1000);
    TEST_ASSERT(dequeued != NULL, "Dequeue should succeed");
    TEST_ASSERT(dequeued->priority == TXN_PRIORITY_HIGH, "Higher priority should come first");
    TEST_ASSERT(txn_priority_queue_size(queue) == 1, "Queue size should be 1 after dequeue");
    
    dequeued = txn_priority_queue_dequeue(queue, 1000);
    TEST_ASSERT(dequeued != NULL, "Dequeue should succeed");
    TEST_ASSERT(dequeued->priority == TXN_PRIORITY_NORMAL, "Normal priority should come second");
    TEST_ASSERT(txn_priority_queue_size(queue) == 0, "Queue should be empty");
    
    // 测试空队列出队
    dequeued = txn_priority_queue_dequeue(queue, 100);  // 100ms timeout
    TEST_ASSERT(dequeued == NULL, "Dequeue from empty queue should timeout");
    
    // 清理
    free_mock_transaction(txn1);
    free_mock_transaction(txn2);
    txn_priority_queue_destroy(queue);
    
    TEST_END();
}

// 测试优先级排序
void test_priority_ordering() {
    TEST_START("Priority Ordering");
    
    txn_priority_queue_t *queue = txn_priority_queue_create(10);
    TEST_ASSERT(queue != NULL, "Queue creation should succeed");
    
    // 创建不同优先级的事务
    dist_transaction_t *txn_low = create_mock_transaction(1, TXN_PRIORITY_LOW);
    dist_transaction_t *txn_normal = create_mock_transaction(2, TXN_PRIORITY_NORMAL);
    dist_transaction_t *txn_high = create_mock_transaction(3, TXN_PRIORITY_HIGH);
    dist_transaction_t *txn_urgent = create_mock_transaction(4, TXN_PRIORITY_URGENT);
    
    // 按随机顺序入队
    txn_priority_queue_enqueue(queue, txn_normal);
    txn_priority_queue_enqueue(queue, txn_low);
    txn_priority_queue_enqueue(queue, txn_urgent);
    txn_priority_queue_enqueue(queue, txn_high);
    
    TEST_ASSERT(txn_priority_queue_size(queue) == 4, "Queue should contain 4 transactions");
    
    // 按优先级顺序出队
    dist_transaction_t *dequeued;
    
    dequeued = txn_priority_queue_dequeue(queue, 1000);
    TEST_ASSERT(dequeued != NULL && dequeued->priority == TXN_PRIORITY_URGENT,
                "First dequeued should be urgent priority");
    
    dequeued = txn_priority_queue_dequeue(queue, 1000);
    TEST_ASSERT(dequeued != NULL && dequeued->priority == TXN_PRIORITY_HIGH,
                "Second dequeued should be high priority");
    
    dequeued = txn_priority_queue_dequeue(queue, 1000);
    TEST_ASSERT(dequeued != NULL && dequeued->priority == TXN_PRIORITY_NORMAL,
                "Third dequeued should be normal priority");
    
    dequeued = txn_priority_queue_dequeue(queue, 1000);
    TEST_ASSERT(dequeued != NULL && dequeued->priority == TXN_PRIORITY_LOW,
                "Last dequeued should be low priority");
    
    TEST_ASSERT(txn_priority_queue_size(queue) == 0, "Queue should be empty");
    
    // 清理
    free_mock_transaction(txn_low);
    free_mock_transaction(txn_normal);
    free_mock_transaction(txn_high);
    free_mock_transaction(txn_urgent);
    txn_priority_queue_destroy(queue);
    
    TEST_END();
}

// 测试队列容量限制
void test_queue_capacity() {
    TEST_START("Queue Capacity Limits");
    
    txn_priority_queue_t *queue = txn_priority_queue_create(3);  // 最大容量3
    TEST_ASSERT(queue != NULL, "Queue creation should succeed");
    
    // 填满队列
    dist_transaction_t *txns[5];
    for (int i = 0; i < 3; i++) {
        txns[i] = create_mock_transaction(i + 1, TXN_PRIORITY_NORMAL);
        int ret = txn_priority_queue_enqueue(queue, txns[i]);
        TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Enqueue should succeed");
    }
    
    TEST_ASSERT(txn_priority_queue_size(queue) == 3, "Queue should be full");
    
    // 尝试超容量入队（应该阻塞后超时）
    txns[3] = create_mock_transaction(4, TXN_PRIORITY_NORMAL);
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    int ret = txn_priority_queue_enqueue(queue, txns[3]);
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 + 
                     (end.tv_nsec - start.tv_nsec) / 1000000;
    
    TEST_ASSERT(ret == DIST_TXN_QUEUE_FULL, "Enqueue to full queue should fail");
    TEST_ASSERT(elapsed_ms >= 900 && elapsed_ms <= 1100, 
                "Enqueue should timeout after approximately 1 second");
    
    // 出队一个元素后再次尝试入队
    dist_transaction_t *dequeued = txn_priority_queue_dequeue(queue, 1000);
    TEST_ASSERT(dequeued != NULL, "Dequeue should succeed");
    TEST_ASSERT(txn_priority_queue_size(queue) == 2, "Queue size should be 2");
    
    ret = txn_priority_queue_enqueue(queue, txns[3]);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Enqueue should succeed after dequeue");
    TEST_ASSERT(txn_priority_queue_size(queue) == 3, "Queue should be full again");
    
    // 清理
    for (int i = 0; i < 4; i++) {
        if (txns[i]) free_mock_transaction(txns[i]);
    }
    txn_priority_queue_destroy(queue);
    
    TEST_END();
}

// 并发测试线程参数
typedef struct thread_data {
    txn_priority_queue_t *queue;
    int thread_id;
    int operations;
    int enqueue_count;
    int dequeue_count;
    volatile bool stop_flag;
} thread_data_t;

// 生产者线程函数
void *producer_thread(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    
    for (int i = 0; i < data->operations && !data->stop_flag; i++) {
        dist_transaction_t *txn = create_mock_transaction(
            data->thread_id * 1000 + i, 
            TXN_PRIORITY_LOW + (rand() % 4)  // 随机优先级
        );
        
        if (txn_priority_queue_enqueue(data->queue, txn) == DIST_TXN_SUCCESS) {
            data->enqueue_count++;
        }
        
        // 随机延迟
        usleep(rand() % 1000);  // 0-1ms
    }
    
    return NULL;
}

// 消费者线程函数
void *consumer_thread(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    
    while (!data->stop_flag || txn_priority_queue_size(data->queue) > 0) {
        dist_transaction_t *txn = txn_priority_queue_dequeue(data->queue, 100);
        if (txn) {
            data->dequeue_count++;
            free_mock_transaction(txn);
        }
        
        // 随机延迟
        usleep(rand() % 1000);  // 0-1ms
    }
    
    return NULL;
}

// 测试并发操作
void test_concurrent_operations() {
    TEST_START("Concurrent Operations");
    
    txn_priority_queue_t *queue = txn_priority_queue_create(100);
    TEST_ASSERT(queue != NULL, "Queue creation should succeed");
    
    const int NUM_PRODUCERS = 3;
    const int NUM_CONSUMERS = 2;
    const int OPERATIONS_PER_PRODUCER = 50;
    
    pthread_t producer_threads[NUM_PRODUCERS];
    pthread_t consumer_threads[NUM_CONSUMERS];
    thread_data_t producer_data[NUM_PRODUCERS];
    thread_data_t consumer_data[NUM_CONSUMERS];
    
    // 启动生产者线程
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        producer_data[i].queue = queue;
        producer_data[i].thread_id = i;
        producer_data[i].operations = OPERATIONS_PER_PRODUCER;
        producer_data[i].enqueue_count = 0;
        producer_data[i].dequeue_count = 0;
        producer_data[i].stop_flag = false;
        
        pthread_create(&producer_threads[i], NULL, producer_thread, &producer_data[i]);
    }
    
    // 启动消费者线程
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        consumer_data[i].queue = queue;
        consumer_data[i].thread_id = i + NUM_PRODUCERS;
        consumer_data[i].operations = 0;
        consumer_data[i].enqueue_count = 0;
        consumer_data[i].dequeue_count = 0;
        consumer_data[i].stop_flag = false;
        
        pthread_create(&consumer_threads[i], NULL, consumer_thread, &consumer_data[i]);
    }
    
    // 等待生产者完成
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        pthread_join(producer_threads[i], NULL);
    }
    
    // 通知消费者停止
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        consumer_data[i].stop_flag = true;
    }
    
    // 等待消费者完成
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        pthread_join(consumer_threads[i], NULL);
    }
    
    // 验证结果
    int total_enqueued = 0;
    int total_dequeued = 0;
    
    for (int i = 0; i < NUM_PRODUCERS; i++) {
        total_enqueued += producer_data[i].enqueue_count;
    }
    
    for (int i = 0; i < NUM_CONSUMERS; i++) {
        total_dequeued += consumer_data[i].dequeue_count;
    }
    
    printf("Total enqueued: %d, dequeued: %d, remaining: %d\n",
           total_enqueued, total_dequeued, txn_priority_queue_size(queue));
    
    TEST_ASSERT(total_enqueued == NUM_PRODUCERS * OPERATIONS_PER_PRODUCER,
                "All enqueue operations should succeed");
    TEST_ASSERT(total_dequeued + txn_priority_queue_size(queue) == total_enqueued,
                "Total dequeued + remaining should equal total enqueued");
    
    // 清理剩余元素
    dist_transaction_t *txn;
    while ((txn = txn_priority_queue_dequeue(queue, 100)) != NULL) {
        free_mock_transaction(txn);
    }
    
    txn_priority_queue_destroy(queue);
    
    TEST_END();
}

// 测试超时机制
void test_timeout_mechanism() {
    TEST_START("Timeout Mechanism");
    
    txn_priority_queue_t *queue = txn_priority_queue_create(10);
    TEST_ASSERT(queue != NULL, "Queue creation should succeed");
    
    // 测试出队超时
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    dist_transaction_t *txn = txn_priority_queue_dequeue(queue, 500);  // 500ms timeout
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 + 
                     (end.tv_nsec - start.tv_nsec) / 1000000;
    
    TEST_ASSERT(txn == NULL, "Dequeue from empty queue should return NULL");
    TEST_ASSERT(elapsed_ms >= 450 && elapsed_ms <= 550, 
                "Dequeue should timeout after approximately 500ms");
    
    // 测试入队超时（填满队列后）
    for (int i = 0; i < 10; i++) {
        dist_transaction_t *test_txn = create_mock_transaction(i, TXN_PRIORITY_NORMAL);
        txn_priority_queue_enqueue(queue, test_txn);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    dist_transaction_t *overflow_txn = create_mock_transaction(100, TXN_PRIORITY_NORMAL);
    int ret = txn_priority_queue_enqueue(queue, overflow_txn);
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 + 
                (end.tv_nsec - start.tv_nsec) / 1000000;
    
    TEST_ASSERT(ret == DIST_TXN_QUEUE_FULL, "Enqueue to full queue should fail");
    TEST_ASSERT(elapsed_ms >= 900 && elapsed_ms <= 1100, 
                "Enqueue should timeout after approximately 1 second");
    
    free_mock_transaction(overflow_txn);
    
    // 清理
    while ((txn = txn_priority_queue_dequeue(queue, 100)) != NULL) {
        free_mock_transaction(txn);
    }
    txn_priority_queue_destroy(queue);
    
    TEST_END();
}

// 测试错误处理
void test_error_handling() {
    TEST_START("Error Handling");
    
    // 测试NULL参数
    TEST_ASSERT(txn_priority_queue_create(-1) != NULL, 
                "Creating queue with negative size should use default size");
    
    // 验证负数输入使用默认大小
    txn_priority_queue_t *default_queue = txn_priority_queue_create(-1);
    TEST_ASSERT(default_queue != NULL, "Queue with default size should be created");
    if (default_queue) {
        txn_priority_queue_destroy(default_queue);
    }
    
    TEST_ASSERT(txn_priority_queue_enqueue(NULL, NULL) == DIST_TXN_ERROR,
                "Enqueue with NULL queue should fail");
    
    TEST_ASSERT(txn_priority_queue_dequeue(NULL, 1000) == NULL,
                "Dequeue with NULL queue should fail");
    
    TEST_ASSERT(txn_priority_queue_size(NULL) == 0,
                "Size of NULL queue should be 0");
    
    // 测试正常队列的错误情况
    txn_priority_queue_t *queue = txn_priority_queue_create(5);
    TEST_ASSERT(queue != NULL, "Queue creation should succeed");
    
    TEST_ASSERT(txn_priority_queue_enqueue(queue, NULL) == DIST_TXN_ERROR,
                "Enqueue NULL transaction should fail");
    
    // 清理
    txn_priority_queue_destroy(queue);
    txn_priority_queue_destroy(NULL);  // 应该不会崩溃
    
    TEST_END();
}

// 主测试函数
int main(int argc, char *argv[]) {
    printf("ConcordKV Priority Queue Tests\n");
    printf("===============================\n");
    
    // 初始化随机种子
    srand(time(NULL));
    
    // 运行测试
    test_basic_operations();
    test_priority_ordering();
    test_queue_capacity();
    test_concurrent_operations();
    test_timeout_mechanism();
    test_error_handling();
    
    // 输出测试结果
    printf("\n===============================\n");
    printf("Test Results:\n");
    printf("  Total tests: %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("  Success rate: %.1f%%\n", 
           tests_run > 0 ? (float)tests_passed / tests_run * 100 : 0);
    
    // 生成报告模式
    if (argc > 1 && strcmp(argv[1], "--report") == 0) {
        printf("\n### Priority Queue Test Results\n");
        printf("- **Total Tests**: %d\n", tests_run);
        printf("- **Passed**: %d\n", tests_passed);
        printf("- **Failed**: %d\n", tests_failed);
        printf("- **Success Rate**: %.1f%%\n", 
               tests_run > 0 ? (float)tests_passed / tests_run * 100 : 0);
        printf("\n#### Key Features Tested\n");
        printf("- Basic enqueue/dequeue operations\n");
        printf("- Priority-based ordering\n");
        printf("- Queue capacity limits and blocking\n");
        printf("- Concurrent producer-consumer operations\n");
        printf("- Timeout mechanisms\n");
        printf("- Error handling and edge cases\n");
    }
    
    return (tests_failed == 0) ? 0 : 1;
} 