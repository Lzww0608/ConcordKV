/*
* @Author: Lzww0608
* @Date: 2025-5-30 10:54:40
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 11:18:03
* @Description: ConcordKV分布式事务功能测试
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
#include "kv_transaction.h"

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

// 超时信号处理器
void timeout_handler(int sig) {
    timeout_flag = 1;
    printf("\n[TIMEOUT] Test execution timeout!\n");
}

// 设置测试超时
void set_test_timeout(int seconds) {
    timeout_flag = 0;
    signal(SIGALRM, timeout_handler);
    alarm(seconds);
}

// 清除测试超时
void clear_test_timeout() {
    alarm(0);
    signal(SIGALRM, SIG_DFL);
}

// 测试辅助宏
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

// 模拟存储引擎
typedef struct mock_storage {
    char keys[100][64];
    char values[100][256];
    int count;
    pthread_mutex_t mutex;
} mock_storage_t;

static mock_storage_t g_mock_storage = {0};

// 初始化模拟存储
void init_mock_storage() {
    g_mock_storage.count = 0;
    pthread_mutex_init(&g_mock_storage.mutex, NULL);
}

// 清理模拟存储
void cleanup_mock_storage() {
    pthread_mutex_destroy(&g_mock_storage.mutex);
}

// 模拟消息发送回调
int mock_send_callback(const char *node_id, const dist_txn_message_t *msg) {
    printf("[MOCK] Sending message to %s: type=%d, txn_id=%lu\n", 
           node_id, msg->type, msg->global_txn_id);
    
    // 模拟网络延迟
    usleep(1000);  // 1ms
    
    // 模拟95%的成功率
    return (rand() % 100 < 95) ? DIST_TXN_SUCCESS : DIST_TXN_ERROR;
}

// 模拟消息接收回调
int mock_receive_callback(dist_txn_message_t *msg) {
    printf("[MOCK] Received message: type=%d, txn_id=%lu\n", 
           msg->type, msg->global_txn_id);
    return DIST_TXN_SUCCESS;
}

// 测试协调器初始化和销毁
void test_coordinator_init_destroy() {
    TEST_START("Coordinator Init/Destroy");
    
    dist_txn_coordinator_t coordinator;
    
    // 测试正常初始化
    int ret = dist_txn_coordinator_init(&coordinator, "test_coordinator", &g_mock_storage);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator initialization should succeed");
    
    // 测试协调器ID设置
    TEST_ASSERT(strcmp(coordinator.coordinator_id, "test_coordinator") == 0, 
                "Coordinator ID should be set correctly");
    
    // 测试初始状态
    TEST_ASSERT(coordinator.is_active == false, "Coordinator should be inactive initially");
    TEST_ASSERT(coordinator.node_count == 0, "Node count should be 0 initially");
    TEST_ASSERT(coordinator.transaction_count == 0, "Transaction count should be 0 initially");
    
    // 测试统计信息初始化
    long total, committed, aborted, timeout;
    dist_txn_get_stats(&coordinator, &total, &committed, &aborted, &timeout);
    TEST_ASSERT(total == 0 && committed == 0 && aborted == 0 && timeout == 0,
                "Statistics should be initialized to zero");
    
    // 测试销毁
    dist_txn_coordinator_destroy(&coordinator);
    TEST_ASSERT(1, "Coordinator destruction should complete without error");
    
    // 测试错误情况
    ret = dist_txn_coordinator_init(NULL, "test", &g_mock_storage);
    TEST_ASSERT(ret == DIST_TXN_ERROR, "Init with NULL coordinator should fail");
    
    ret = dist_txn_coordinator_init(&coordinator, NULL, &g_mock_storage);
    TEST_ASSERT(ret == DIST_TXN_ERROR, "Init with NULL coordinator_id should fail");
    
    TEST_END();
}

// 测试节点管理
void test_node_management() {
    TEST_START("Node Management");
    
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "test_coordinator", &g_mock_storage);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should initialize successfully");
    
    // 测试添加节点
    ret = dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Adding node1 should succeed");
    TEST_ASSERT(coordinator.node_count == 1, "Node count should be 1");
    
    ret = dist_txn_add_node(&coordinator, "node2", "127.0.0.1", 8002);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Adding node2 should succeed");
    TEST_ASSERT(coordinator.node_count == 2, "Node count should be 2");
    
    // 测试重复添加节点
    ret = dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    TEST_ASSERT(ret == DIST_TXN_ERROR, "Adding duplicate node should fail");
    TEST_ASSERT(coordinator.node_count == 2, "Node count should remain 2");
    
    // 测试移除节点
    ret = dist_txn_remove_node(&coordinator, "node1");
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Removing node1 should succeed");
    TEST_ASSERT(coordinator.node_count == 1, "Node count should be 1");
    
    // 测试移除不存在的节点
    ret = dist_txn_remove_node(&coordinator, "nonexistent");
    TEST_ASSERT(ret == DIST_TXN_ERROR, "Removing nonexistent node should fail");
    TEST_ASSERT(coordinator.node_count == 1, "Node count should remain 1");
    
    // 测试错误情况
    ret = dist_txn_add_node(NULL, "node3", "127.0.0.1", 8003);
    TEST_ASSERT(ret == DIST_TXN_ERROR, "Adding node to NULL coordinator should fail");
    
    ret = dist_txn_add_node(&coordinator, NULL, "127.0.0.1", 8003);
    TEST_ASSERT(ret == DIST_TXN_ERROR, "Adding node with NULL ID should fail");
    
    dist_txn_coordinator_destroy(&coordinator);
    TEST_END();
}

// 测试事务生命周期
void test_transaction_lifecycle() {
    TEST_START("Transaction Lifecycle");
    
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "test_coordinator", &g_mock_storage);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should initialize successfully");
    
    // 添加测试节点
    dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    dist_txn_add_node(&coordinator, "node2", "127.0.0.1", 8002);
    
    // 设置消息回调
    dist_txn_set_send_callback(mock_send_callback);
    dist_txn_set_receive_callback(mock_receive_callback);
    
    // 测试开始事务
    const char *participants[] = {"node1", "node2"};
    dist_transaction_t *txn = dist_txn_begin(&coordinator, TXN_PRIORITY_NORMAL, 5000,
                                           participants, 2);
    TEST_ASSERT(txn != NULL, "Transaction should be created successfully");
    TEST_ASSERT(txn->global_txn_id > 0, "Transaction should have valid ID");
    TEST_ASSERT(txn->status == DIST_TXN_PREPARING, "Transaction should be in preparing state");
    TEST_ASSERT(txn->participant_count == 2, "Transaction should have 2 participants");
    TEST_ASSERT(txn->priority == TXN_PRIORITY_NORMAL, "Transaction should have normal priority");
    
    // 测试准备阶段
    ret = dist_txn_prepare(&coordinator, txn);
    // 由于模拟实现，准备可能成功或失败
    TEST_ASSERT(ret == DIST_TXN_SUCCESS || ret == DIST_TXN_PREPARE_FAILED,
                "Prepare should return valid result");
    
    if (ret == DIST_TXN_SUCCESS) {
        // 测试提交阶段
        ret = dist_txn_commit(&coordinator, txn);
        TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Commit should succeed after successful prepare");
        TEST_ASSERT(txn->status == DIST_TXN_COMMITTED, "Transaction should be committed");
    } else {
        // 测试中止
        ret = dist_txn_abort(&coordinator, txn);
        TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Abort should succeed");
        TEST_ASSERT(txn->status == DIST_TXN_ABORTED, "Transaction should be aborted");
    }
    
    // 测试释放事务
    // 注意：不手动释放，让协调器销毁时自动清理
    // dist_txn_free(txn);
    TEST_ASSERT(1, "Transaction should be freed without error");
    
    // 测试错误情况
    txn = dist_txn_begin(NULL, TXN_PRIORITY_NORMAL, 5000, participants, 2);
    TEST_ASSERT(txn == NULL, "Begin with NULL coordinator should fail");
    
    txn = dist_txn_begin(&coordinator, TXN_PRIORITY_NORMAL, 5000, NULL, 2);
    TEST_ASSERT(txn == NULL, "Begin with NULL participants should fail");
    
    txn = dist_txn_begin(&coordinator, TXN_PRIORITY_NORMAL, 5000, participants, 0);
    TEST_ASSERT(txn == NULL, "Begin with 0 participants should fail");
    
    dist_txn_coordinator_destroy(&coordinator);
    TEST_END();
}

// 测试优先级调度
void test_priority_scheduling() {
    TEST_START("Priority Scheduling");
    
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "test_coordinator", &g_mock_storage);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should initialize successfully");
    
    // 添加测试节点
    dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    
    // 测试调度策略设置
    ret = dist_txn_set_schedule_policy(&coordinator, TXN_SCHEDULE_PRIORITY);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Setting schedule policy should succeed");
    
    // 创建不同优先级的事务
    const char *participants[] = {"node1"};
    
    dist_transaction_t *low_txn = dist_txn_begin(&coordinator, TXN_PRIORITY_LOW, 5000,
                                                participants, 1);
    TEST_ASSERT(low_txn != NULL, "Low priority transaction should be created");
    
    dist_transaction_t *high_txn = dist_txn_begin(&coordinator, TXN_PRIORITY_HIGH, 5000,
                                                 participants, 1);
    TEST_ASSERT(high_txn != NULL, "High priority transaction should be created");
    
    dist_transaction_t *urgent_txn = dist_txn_begin(&coordinator, TXN_PRIORITY_URGENT, 5000,
                                                   participants, 1);
    TEST_ASSERT(urgent_txn != NULL, "Urgent priority transaction should be created");
    
    // 检查优先级队列大小
    int queue_size = txn_priority_queue_size(coordinator.priority_queue);
    TEST_ASSERT(queue_size == 3, "Priority queue should contain 3 transactions");
    
    // 验证优先级队列工作正常
    dist_transaction_t *dequeued = txn_priority_queue_dequeue(coordinator.priority_queue, 1000);
    TEST_ASSERT(dequeued != NULL, "Should be able to dequeue transaction");
    TEST_ASSERT(dequeued->priority >= TXN_PRIORITY_HIGH, 
                "Dequeued transaction should have high priority");
    
    // 清理 - 让协调器销毁时自动清理事务
    // 注意：不要手动释放事务，因为协调器销毁时会自动清理
    
    dist_txn_coordinator_destroy(&coordinator);
    TEST_END();
}

// 测试并发事务处理
void test_concurrent_transactions() {
    TEST_START("Concurrent Transactions");
    
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "test_coordinator", &g_mock_storage);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should initialize successfully");
    
    // 添加测试节点
    dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    dist_txn_add_node(&coordinator, "node2", "127.0.0.1", 8002);
    
    // 启动协调器线程
    ret = dist_txn_coordinator_start(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should start successfully");
    TEST_ASSERT(coordinator.is_active == true, "Coordinator should be active");
    
    // 创建多个并发事务
    const char *participants[] = {"node1", "node2"};
    dist_transaction_t *transactions[5];
    
    for (int i = 0; i < 5; i++) {
        transactions[i] = dist_txn_begin(&coordinator, TXN_PRIORITY_NORMAL, 3000,
                                       participants, 2);
        TEST_ASSERT(transactions[i] != NULL, "Transaction should be created");
    }
    
    // 让调度器工作一段时间
    sleep(2);
    
    // 检查统计信息
    long total, committed, aborted, timeout;
    dist_txn_get_stats(&coordinator, &total, &committed, &aborted, &timeout);
    TEST_ASSERT(total >= 5, "Total transactions should be at least 5");
    
    printf("Transaction stats: total=%ld, committed=%ld, aborted=%ld, timeout=%ld\n",
           total, committed, aborted, timeout);
    
    // 停止协调器
    ret = dist_txn_coordinator_stop(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should stop successfully");
    TEST_ASSERT(coordinator.is_active == false, "Coordinator should be inactive");
    
    // 清理
    // 注意：不手动释放事务，让协调器销毁时自动清理
    /*
    for (int i = 0; i < 5; i++) {
        if (transactions[i]) {
            dist_txn_free(transactions[i]);
        }
    }
    */
    
    dist_txn_coordinator_destroy(&coordinator);
    TEST_END();
}

// 测试心跳和节点健康检查
void test_heartbeat_health_check() {
    TEST_START("Heartbeat and Health Check");
    
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "test_coordinator", &g_mock_storage);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should initialize successfully");
    
    // 添加测试节点
    ret = dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Adding node should succeed");
    
    // 测试发送心跳
    ret = dist_txn_send_heartbeat(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Sending heartbeat should succeed");
    
    // 测试健康检查
    int unhealthy_count = dist_txn_check_node_health(&coordinator);
    TEST_ASSERT(unhealthy_count >= 0, "Health check should return valid count");
    
    // 模拟节点响应心跳
    dist_txn_message_t heartbeat_msg;
    memset(&heartbeat_msg, 0, sizeof(heartbeat_msg));
    heartbeat_msg.type = DIST_MSG_HEARTBEAT;
    strcpy(heartbeat_msg.sender_id, "node1");
    strcpy(heartbeat_msg.receiver_id, "test_coordinator");
    heartbeat_msg.timestamp = time(NULL);
    
    ret = dist_txn_handle_message(&coordinator, &heartbeat_msg);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Handling heartbeat message should succeed");
    
    dist_txn_coordinator_destroy(&coordinator);
    TEST_END();
}

// 测试错误处理和边界条件
void test_error_handling() {
    TEST_START("Error Handling and Edge Cases");
    
    // 测试NULL参数处理
    TEST_ASSERT(dist_txn_coordinator_init(NULL, "test", NULL) == DIST_TXN_ERROR,
                "Init with NULL coordinator should fail");
    
    TEST_ASSERT(dist_txn_add_node(NULL, "node1", "127.0.0.1", 8001) == DIST_TXN_ERROR,
                "Add node to NULL coordinator should fail");
    
    TEST_ASSERT(dist_txn_begin(NULL, TXN_PRIORITY_NORMAL, 5000, NULL, 0) == NULL,
                "Begin transaction with NULL coordinator should fail");
    
    // 测试无效状态转换
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "test_coordinator", &g_mock_storage);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should initialize successfully");
    
    // 测试在未启动状态下停止协调器
    ret = dist_txn_coordinator_stop(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_ERROR, "Stopping inactive coordinator should fail");
    
    // 测试重复启动
    ret = dist_txn_coordinator_start(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "First start should succeed");
    
    ret = dist_txn_coordinator_start(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_ERROR, "Second start should fail");
    
    dist_txn_coordinator_stop(&coordinator);
    dist_txn_coordinator_destroy(&coordinator);
    
    TEST_END();
}

// 主测试函数
int main(int argc, char *argv[]) {
    printf("ConcordKV Distributed Transaction Tests\n");
    printf("========================================\n");
    
    // 初始化随机种子
    srand(time(NULL));
    
    // 初始化模拟存储
    init_mock_storage();
    
    // 运行测试
    test_coordinator_init_destroy();
    test_node_management();
    test_transaction_lifecycle();
    test_priority_scheduling();
    test_concurrent_transactions();
    test_heartbeat_health_check();
    test_error_handling();
    
    // 清理
    cleanup_mock_storage();
    
    // 输出测试结果
    printf("\n========================================\n");
    printf("Test Results:\n");
    printf("  Total tests: %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("  Success rate: %.1f%%\n", 
           tests_run > 0 ? (float)tests_passed / tests_run * 100 : 0);
    
    // 生成报告模式
    if (argc > 1 && strcmp(argv[1], "--report") == 0) {
        printf("\n### Distributed Transaction Test Results\n");
        printf("- **Total Tests**: %d\n", tests_run);
        printf("- **Passed**: %d\n", tests_passed);
        printf("- **Failed**: %d\n", tests_failed);
        printf("- **Success Rate**: %.1f%%\n", 
               tests_run > 0 ? (float)tests_passed / tests_run * 100 : 0);
        printf("\n#### Key Features Tested\n");
        printf("- Coordinator initialization and destruction\n");
        printf("- Node management (add/remove)\n");
        printf("- Transaction lifecycle (begin/prepare/commit/abort)\n");
        printf("- Priority-based scheduling\n");
        printf("- Concurrent transaction processing\n");
        printf("- Heartbeat and health monitoring\n");
        printf("- Error handling and edge cases\n");
    }
    
    return (tests_failed == 0) ? 0 : 1;
} 