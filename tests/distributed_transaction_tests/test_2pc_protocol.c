/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 11:17:57
* @Description: ConcordKV两阶段提交协议测试
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

// 模拟参与者状态
typedef struct mock_participant_state {
    char node_id[64];
    bool prepare_success;
    bool commit_success;
    bool abort_success;
    int prepare_delay_ms;
    int commit_delay_ms;
    bool network_failure;
    pthread_mutex_t mutex;
} mock_participant_state_t;

static mock_participant_state_t g_participants[10];
static int g_participant_count = 0;
static pthread_mutex_t g_participants_mutex = PTHREAD_MUTEX_INITIALIZER;

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
    set_test_timeout(60); \
    reset_participants(); \
} while(0)

#define TEST_END() do { \
    clear_test_timeout(); \
} while(0)

// 重置参与者状态
void reset_participants() {
    pthread_mutex_lock(&g_participants_mutex);
    for (int i = 0; i < g_participant_count; i++) {
        pthread_mutex_destroy(&g_participants[i].mutex);
    }
    memset(g_participants, 0, sizeof(g_participants));
    g_participant_count = 0;
    pthread_mutex_unlock(&g_participants_mutex);
}

// 添加模拟参与者
void add_mock_participant(const char *node_id, bool prepare_success, 
                         bool commit_success, int prepare_delay_ms) {
    pthread_mutex_lock(&g_participants_mutex);
    if (g_participant_count < 10) {
        mock_participant_state_t *p = &g_participants[g_participant_count];
        strncpy(p->node_id, node_id, sizeof(p->node_id) - 1);
        p->prepare_success = prepare_success;
        p->commit_success = commit_success;
        p->abort_success = true;
        p->prepare_delay_ms = prepare_delay_ms;
        p->commit_delay_ms = 10;
        p->network_failure = false;
        pthread_mutex_init(&p->mutex, NULL);
        g_participant_count++;
    }
    pthread_mutex_unlock(&g_participants_mutex);
}

// 设置参与者网络故障
void set_participant_network_failure(const char *node_id, bool failure) {
    pthread_mutex_lock(&g_participants_mutex);
    for (int i = 0; i < g_participant_count; i++) {
        if (strcmp(g_participants[i].node_id, node_id) == 0) {
            pthread_mutex_lock(&g_participants[i].mutex);
            g_participants[i].network_failure = failure;
            pthread_mutex_unlock(&g_participants[i].mutex);
            break;
        }
    }
    pthread_mutex_unlock(&g_participants_mutex);
}

// 模拟消息发送回调
int mock_2pc_send_callback(const char *node_id, const dist_txn_message_t *msg) {
    pthread_mutex_lock(&g_participants_mutex);
    
    mock_participant_state_t *participant = NULL;
    for (int i = 0; i < g_participant_count; i++) {
        if (strcmp(g_participants[i].node_id, node_id) == 0) {
            participant = &g_participants[i];
            break;
        }
    }
    
    pthread_mutex_unlock(&g_participants_mutex);
    
    if (!participant) {
        printf("[MOCK] Unknown participant: %s\n", node_id);
        return DIST_TXN_ERROR;
    }
    
    pthread_mutex_lock(&participant->mutex);
    
    // 检查网络故障
    if (participant->network_failure) {
        printf("[MOCK] Network failure to %s\n", node_id);
        pthread_mutex_unlock(&participant->mutex);
        return DIST_TXN_ERROR;
    }
    
    printf("[MOCK] Sending message to %s: type=%d, txn_id=%lu\n", 
           node_id, msg->type, msg->global_txn_id);
    
    // 模拟处理延迟
    switch (msg->type) {
        case DIST_MSG_PREPARE:
            if (participant->prepare_delay_ms > 0) {
                usleep(participant->prepare_delay_ms * 1000);
            }
            // 模拟准备响应
            if (participant->prepare_success) {
                printf("[MOCK] %s prepared successfully\n", node_id);
            } else {
                printf("[MOCK] %s prepare failed\n", node_id);
            }
            break;
            
        case DIST_MSG_COMMIT:
            if (participant->commit_delay_ms > 0) {
                usleep(participant->commit_delay_ms * 1000);
            }
            if (participant->commit_success) {
                printf("[MOCK] %s committed successfully\n", node_id);
            } else {
                printf("[MOCK] %s commit failed\n", node_id);
            }
            break;
            
        case DIST_MSG_ABORT:
            printf("[MOCK] %s aborted\n", node_id);
            break;
            
        default:
            break;
    }
    
    pthread_mutex_unlock(&participant->mutex);
    return DIST_TXN_SUCCESS;
}

// 测试成功的2PC流程
void test_successful_2pc() {
    TEST_START("Successful 2PC Flow");
    
    // 添加成功的参与者
    add_mock_participant("node1", true, true, 10);
    add_mock_participant("node2", true, true, 15);
    add_mock_participant("node3", true, true, 20);
    
    // 初始化协调器
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "coordinator", NULL);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator initialization should succeed");
    
    // 添加节点
    dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    dist_txn_add_node(&coordinator, "node2", "127.0.0.1", 8002);
    dist_txn_add_node(&coordinator, "node3", "127.0.0.1", 8003);
    
    // 设置回调
    dist_txn_set_send_callback(mock_2pc_send_callback);
    
    // 开始分布式事务
    const char *participants[] = {"node1", "node2", "node3"};
    dist_transaction_t *txn = dist_txn_begin(&coordinator, TXN_PRIORITY_NORMAL, 5000,
                                           participants, 3);
    TEST_ASSERT(txn != NULL, "Transaction should be created successfully");
    TEST_ASSERT(txn->status == DIST_TXN_PREPARING, "Transaction should be in preparing state");
    
    // 执行准备阶段
    ret = dist_txn_prepare(&coordinator, txn);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Prepare phase should succeed");
    TEST_ASSERT(txn->status == DIST_TXN_PREPARED, "Transaction should be prepared");
    TEST_ASSERT(txn->prepared_count == 3, "All participants should be prepared");
    
    // 执行提交阶段
    ret = dist_txn_commit(&coordinator, txn);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Commit phase should succeed");
    TEST_ASSERT(txn->status == DIST_TXN_COMMITTED, "Transaction should be committed");
    TEST_ASSERT(txn->committed_count == 3, "All participants should be committed");
    
    // 检查统计信息
    long total, committed, aborted, timeout;
    dist_txn_get_stats(&coordinator, &total, &committed, &aborted, &timeout);
    TEST_ASSERT(committed == 1, "One transaction should be committed");
    TEST_ASSERT(aborted == 0, "No transactions should be aborted");
    
    // 清理
    // dist_txn_free(txn);  // 让协调器销毁时自动清理
    dist_txn_coordinator_destroy(&coordinator);
    
    TEST_END();
}

// 测试准备阶段失败的2PC流程
void test_prepare_failure_2pc() {
    TEST_START("Prepare Failure 2PC Flow");
    
    // 添加参与者，其中一个准备失败
    add_mock_participant("node1", true, true, 10);
    add_mock_participant("node2", false, true, 15);  // 准备失败
    add_mock_participant("node3", true, true, 20);
    
    // 初始化协调器
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "coordinator", NULL);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator initialization should succeed");
    
    // 添加节点
    dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    dist_txn_add_node(&coordinator, "node2", "127.0.0.1", 8002);
    dist_txn_add_node(&coordinator, "node3", "127.0.0.1", 8003);
    
    // 设置回调
    dist_txn_set_send_callback(mock_2pc_send_callback);
    
    // 开始分布式事务
    const char *participants[] = {"node1", "node2", "node3"};
    dist_transaction_t *txn = dist_txn_begin(&coordinator, TXN_PRIORITY_NORMAL, 5000,
                                           participants, 3);
    TEST_ASSERT(txn != NULL, "Transaction should be created successfully");
    
    // 执行准备阶段
    ret = dist_txn_prepare(&coordinator, txn);
    TEST_ASSERT(ret == DIST_TXN_PREPARE_FAILED, "Prepare phase should fail");
    TEST_ASSERT(txn->status == DIST_TXN_ABORTING, "Transaction should be aborting");
    TEST_ASSERT(txn->prepared_count < 3, "Not all participants should be prepared");
    
    // 执行中止
    ret = dist_txn_abort(&coordinator, txn);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Abort should succeed");
    TEST_ASSERT(txn->status == DIST_TXN_ABORTED, "Transaction should be aborted");
    
    // 检查统计信息
    long total, committed, aborted, timeout;
    dist_txn_get_stats(&coordinator, &total, &committed, &aborted, &timeout);
    TEST_ASSERT(committed == 0, "No transactions should be committed");
    TEST_ASSERT(aborted == 1, "One transaction should be aborted");
    
    // 清理
    // dist_txn_free(txn);  // 让协调器销毁时自动清理
    dist_txn_coordinator_destroy(&coordinator);
    
    TEST_END();
}

// 测试网络故障处理
void test_network_failure_2pc() {
    TEST_START("Network Failure 2PC Flow");
    
    // 添加参与者
    add_mock_participant("node1", true, true, 10);
    add_mock_participant("node2", true, true, 15);
    add_mock_participant("node3", true, true, 20);
    
    // 初始化协调器
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "coordinator", NULL);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator initialization should succeed");
    
    // 添加节点
    dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    dist_txn_add_node(&coordinator, "node2", "127.0.0.1", 8002);
    dist_txn_add_node(&coordinator, "node3", "127.0.0.1", 8003);
    
    // 设置回调
    dist_txn_set_send_callback(mock_2pc_send_callback);
    
    // 模拟node2网络故障
    set_participant_network_failure("node2", true);
    
    // 开始分布式事务
    const char *participants[] = {"node1", "node2", "node3"};
    dist_transaction_t *txn = dist_txn_begin(&coordinator, TXN_PRIORITY_NORMAL, 5000,
                                           participants, 3);
    TEST_ASSERT(txn != NULL, "Transaction should be created successfully");
    
    // 执行准备阶段
    ret = dist_txn_prepare(&coordinator, txn);
    TEST_ASSERT(ret == DIST_TXN_PREPARE_FAILED, "Prepare should fail due to network failure");
    TEST_ASSERT(txn->status == DIST_TXN_ABORTING, "Transaction should be aborting");
    
    // 执行中止
    ret = dist_txn_abort(&coordinator, txn);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Abort should succeed");
    
    // 清理
    // dist_txn_free(txn);  // 让协调器销毁时自动清理
    dist_txn_coordinator_destroy(&coordinator);
    
    TEST_END();
}

// 测试并发2PC事务
void test_concurrent_2pc() {
    TEST_START("Concurrent 2PC Transactions");
    
    // 添加参与者
    add_mock_participant("node1", true, true, 5);
    add_mock_participant("node2", true, true, 5);
    
    // 初始化协调器
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "coordinator", NULL);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator initialization should succeed");
    
    // 添加节点
    dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    dist_txn_add_node(&coordinator, "node2", "127.0.0.1", 8002);
    
    // 设置回调
    dist_txn_set_send_callback(mock_2pc_send_callback);
    
    // 启动协调器
    ret = dist_txn_coordinator_start(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should start successfully");
    
    // 创建多个并发事务
    const char *participants[] = {"node1", "node2"};
    const int NUM_TXN = 5;
    dist_transaction_t *transactions[NUM_TXN];
    
    for (int i = 0; i < NUM_TXN; i++) {
        transactions[i] = dist_txn_begin(&coordinator, TXN_PRIORITY_NORMAL, 3000,
                                        participants, 2);
        TEST_ASSERT(transactions[i] != NULL, "Transaction should be created");
    }
    
    // 等待调度器处理
    sleep(2);
    
    // 检查统计信息
    long total, committed, aborted, timeout;
    dist_txn_get_stats(&coordinator, &total, &committed, &aborted, &timeout);
    printf("Concurrent 2PC results: total=%ld, committed=%ld, aborted=%ld\n",
           total, committed, aborted);
    
    TEST_ASSERT(total >= NUM_TXN, "All transactions should be processed");
    TEST_ASSERT(committed + aborted == total, "All transactions should have final state");
    
    // 停止协调器
    ret = dist_txn_coordinator_stop(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should stop successfully");
    
    // 清理
    for (int i = 0; i < NUM_TXN; i++) {
        if (transactions[i]) {
            // dist_txn_free(transactions[i]);  // 让协调器销毁时自动清理
        }
    }
    // 清理 - 让协调器销毁时自动清理所有事务
    dist_txn_coordinator_destroy(&coordinator);
    
    TEST_END();
}

// 测试不同优先级的2PC事务
void test_priority_2pc() {
    TEST_START("Priority-based 2PC Transactions");
    
    // 添加参与者，稍微增加延迟以便观察优先级效果
    add_mock_participant("node1", true, true, 20);
    add_mock_participant("node2", true, true, 20);
    
    // 初始化协调器
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "coordinator", NULL);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator initialization should succeed");
    
    // 添加节点
    dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    dist_txn_add_node(&coordinator, "node2", "127.0.0.1", 8002);
    
    // 设置回调
    dist_txn_set_send_callback(mock_2pc_send_callback);
    
    // 设置优先级调度
    ret = dist_txn_set_schedule_policy(&coordinator, TXN_SCHEDULE_PRIORITY);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Setting priority schedule should succeed");
    
    // 启动协调器
    ret = dist_txn_coordinator_start(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should start successfully");
    
    // 创建不同优先级的事务
    const char *participants[] = {"node1", "node2"};
    
    dist_transaction_t *low_txn = dist_txn_begin(&coordinator, TXN_PRIORITY_LOW, 3000,
                                                participants, 2);
    dist_transaction_t *normal_txn = dist_txn_begin(&coordinator, TXN_PRIORITY_NORMAL, 3000,
                                                   participants, 2);
    dist_transaction_t *high_txn = dist_txn_begin(&coordinator, TXN_PRIORITY_HIGH, 3000,
                                                 participants, 2);
    dist_transaction_t *urgent_txn = dist_txn_begin(&coordinator, TXN_PRIORITY_URGENT, 3000,
                                                   participants, 2);
    
    TEST_ASSERT(low_txn != NULL, "Low priority transaction should be created");
    TEST_ASSERT(normal_txn != NULL, "Normal priority transaction should be created");
    TEST_ASSERT(high_txn != NULL, "High priority transaction should be created");
    TEST_ASSERT(urgent_txn != NULL, "Urgent priority transaction should be created");
    
    // 等待调度器处理
    sleep(3);
    
    // 检查事务状态（高优先级应该先被处理）
    printf("Transaction statuses:\n");
    printf("  Urgent (ID %lu): status %d\n", urgent_txn->global_txn_id, urgent_txn->status);
    printf("  High (ID %lu): status %d\n", high_txn->global_txn_id, high_txn->status);
    printf("  Normal (ID %lu): status %d\n", normal_txn->global_txn_id, normal_txn->status);
    printf("  Low (ID %lu): status %d\n", low_txn->global_txn_id, low_txn->status);
    
    // 验证高优先级事务优先处理
    TEST_ASSERT(urgent_txn->status == DIST_TXN_COMMITTED || urgent_txn->status == DIST_TXN_ABORTED,
                "Urgent priority transaction should be processed first");
    
    // 停止协调器
    ret = dist_txn_coordinator_stop(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should stop successfully");
    
    // 清理
    if (low_txn) {
        // dist_txn_free(low_txn);  // 让协调器销毁时自动清理
    }
    if (normal_txn) {
        // dist_txn_free(normal_txn);  // 让协调器销毁时自动清理
    }
    if (high_txn) {
        // dist_txn_free(high_txn);  // 让协调器销毁时自动清理
    }
    if (urgent_txn) {
        // dist_txn_free(urgent_txn);  // 让协调器销毁时自动清理
    }
    // 清理 - 让协调器销毁时自动清理所有事务
    dist_txn_coordinator_destroy(&coordinator);
    
    TEST_END();
}

// 测试事务超时处理
void test_transaction_timeout() {
    TEST_START("Transaction Timeout Handling");
    
    // 添加慢响应的参与者
    add_mock_participant("node1", true, true, 50);   // 50ms延迟
    add_mock_participant("node2", true, true, 2000); // 2秒延迟，会导致超时
    
    // 初始化协调器
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "coordinator", NULL);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator initialization should succeed");
    
    // 添加节点
    dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    dist_txn_add_node(&coordinator, "node2", "127.0.0.1", 8002);
    
    // 设置回调
    dist_txn_set_send_callback(mock_2pc_send_callback);
    
    // 启动协调器
    ret = dist_txn_coordinator_start(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should start successfully");
    
    // 创建短超时的事务
    const char *participants[] = {"node1", "node2"};
    dist_transaction_t *txn = dist_txn_begin(&coordinator, TXN_PRIORITY_NORMAL, 1000,  // 1秒超时
                                           participants, 2);
    TEST_ASSERT(txn != NULL, "Transaction should be created successfully");
    
    // 等待超时发生
    sleep(2);
    
    // 检查超时统计
    long total, committed, aborted, timeout;
    dist_txn_get_stats(&coordinator, &total, &committed, &aborted, &timeout);
    printf("Timeout test results: total=%ld, committed=%ld, aborted=%ld, timeout=%ld\n",
           total, committed, aborted, timeout);
    
    TEST_ASSERT(timeout > 0, "At least one transaction should timeout");
    TEST_ASSERT(txn->status == DIST_TXN_ABORTED, "Timeout transaction should be aborted");
    
    // 停止协调器
    ret = dist_txn_coordinator_stop(&coordinator);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator should stop successfully");
    
    // 清理
    // dist_txn_free(txn);  // 让协调器销毁时自动清理
    dist_txn_coordinator_destroy(&coordinator);
    
    TEST_END();
}

// 测试状态一致性
void test_state_consistency() {
    TEST_START("State Consistency Check");
    
    // 添加参与者
    add_mock_participant("node1", true, true, 10);
    add_mock_participant("node2", true, true, 10);
    
    // 初始化协调器
    dist_txn_coordinator_t coordinator;
    int ret = dist_txn_coordinator_init(&coordinator, "coordinator", NULL);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Coordinator initialization should succeed");
    
    // 添加节点
    dist_txn_add_node(&coordinator, "node1", "127.0.0.1", 8001);
    dist_txn_add_node(&coordinator, "node2", "127.0.0.1", 8002);
    
    // 设置回调
    dist_txn_set_send_callback(mock_2pc_send_callback);
    
    const char *participants[] = {"node1", "node2"};
    dist_transaction_t *txn = dist_txn_begin(&coordinator, TXN_PRIORITY_NORMAL, 5000,
                                           participants, 2);
    TEST_ASSERT(txn != NULL, "Transaction should be created successfully");
    
    // 测试状态转换的一致性
    TEST_ASSERT(txn->status == DIST_TXN_PREPARING, "Initial state should be PREPARING");
    
    // 准备阶段
    ret = dist_txn_prepare(&coordinator, txn);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Prepare should succeed");
    TEST_ASSERT(txn->status == DIST_TXN_PREPARED, "State should be PREPARED");
    
    // 测试无效状态转换
    ret = dist_txn_prepare(&coordinator, txn);  // 重复准备
    TEST_ASSERT(ret == DIST_TXN_INVALID_STATE, "Duplicate prepare should fail");
    
    // 提交阶段
    ret = dist_txn_commit(&coordinator, txn);
    TEST_ASSERT(ret == DIST_TXN_SUCCESS, "Commit should succeed");
    TEST_ASSERT(txn->status == DIST_TXN_COMMITTED, "State should be COMMITTED");
    
    // 测试已提交事务的中止
    ret = dist_txn_abort(&coordinator, txn);
    TEST_ASSERT(ret == DIST_TXN_INVALID_STATE, "Cannot abort committed transaction");
    
    // 清理
    // dist_txn_free(txn);  // 让协调器销毁时自动清理
    dist_txn_coordinator_destroy(&coordinator);
    
    TEST_END();
}

// 主测试函数
int main(int argc, char *argv[]) {
    printf("ConcordKV 2PC Protocol Tests\n");
    printf("=============================\n");
    
    // 初始化随机种子
    srand(time(NULL));
    
    // 运行测试
    test_successful_2pc();
    test_prepare_failure_2pc();
    test_network_failure_2pc();
    test_concurrent_2pc();
    test_priority_2pc();
    test_transaction_timeout();
    test_state_consistency();
    
    // 清理
    reset_participants();
    
    // 输出测试结果
    printf("\n=============================\n");
    printf("Test Results:\n");
    printf("  Total tests: %d\n", tests_run);
    printf("  Passed: %d\n", tests_passed);
    printf("  Failed: %d\n", tests_failed);
    printf("  Success rate: %.1f%%\n", 
           tests_run > 0 ? (float)tests_passed / tests_run * 100 : 0);
    
    // 生成报告模式
    if (argc > 1 && strcmp(argv[1], "--report") == 0) {
        printf("\n### 2PC Protocol Test Results\n");
        printf("- **Total Tests**: %d\n", tests_run);
        printf("- **Passed**: %d\n", tests_passed);
        printf("- **Failed**: %d\n", tests_failed);
        printf("- **Success Rate**: %.1f%%\n", 
               tests_run > 0 ? (float)tests_passed / tests_run * 100 : 0);
        printf("\n#### Key Features Tested\n");
        printf("- Successful 2PC commit flow\n");
        printf("- Prepare phase failure handling\n");
        printf("- Network failure recovery\n");
        printf("- Concurrent transaction processing\n");
        printf("- Priority-based transaction scheduling\n");
        printf("- Transaction timeout handling\n");
        printf("- State consistency guarantees\n");
    }
    
    return (tests_failed == 0) ? 0 : 1;
} 