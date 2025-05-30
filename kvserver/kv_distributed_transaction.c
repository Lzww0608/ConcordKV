/*
* @Author: Lzww0608
* @Date: 2025-5-30 10:54:01
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 11:16:07
* @Description: ConcordKV分布式事务支持实现
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "kv_distributed_transaction.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// 外部函数声明
extern txn_manager_t *get_default_txn_manager();

// 全局回调函数
static dist_txn_send_callback_t g_send_callback = NULL;
static dist_txn_receive_callback_t g_receive_callback = NULL;

// 全局调度策略
static txn_schedule_policy_t g_schedule_policy = TXN_SCHEDULE_PRIORITY;

// 工具函数：获取当前时间(毫秒)
static long long get_current_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// 工具函数：检查超时
static bool is_timeout(time_t start_time, int timeout_ms) {
    if (timeout_ms <= 0) return false;
    
    long long current = get_current_time_ms();
    long long start = (long long)start_time * 1000;
    return (current - start) > timeout_ms;
}

// 创建优先级队列
txn_priority_queue_t *txn_priority_queue_create(int max_size) {
    if (max_size <= 0) max_size = 1000;  // 默认最大1000个事务
    
    txn_priority_queue_t *queue = malloc(sizeof(txn_priority_queue_t));
    if (!queue) return NULL;
    
    queue->head = NULL;
    queue->tail = NULL;
    queue->size = 0;
    queue->max_size = max_size;
    
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        free(queue);
        return NULL;
    }
    
    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        free(queue);
        return NULL;
    }
    
    if (pthread_cond_init(&queue->not_full, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        pthread_cond_destroy(&queue->not_empty);
        free(queue);
        return NULL;
    }
    
    return queue;
}

// 销毁优先级队列
void txn_priority_queue_destroy(txn_priority_queue_t *queue) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->mutex);
    
    // 清空队列
    txn_queue_node_t *current = queue->head;
    while (current) {
        txn_queue_node_t *next = current->next;
        free(current);
        current = next;
    }
    
    pthread_mutex_unlock(&queue->mutex);
    
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
    free(queue);
}

// 入队（按优先级排序）
int txn_priority_queue_enqueue(txn_priority_queue_t *queue, dist_transaction_t *txn) {
    if (!queue || !txn) return DIST_TXN_ERROR;
    
    pthread_mutex_lock(&queue->mutex);
    
    // 检查队列是否已满
    while (queue->size >= queue->max_size) {
        struct timespec timeout;
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 1;  // 等待1秒
        
        int ret = pthread_cond_timedwait(&queue->not_full, &queue->mutex, &timeout);
        if (ret == ETIMEDOUT) {
            pthread_mutex_unlock(&queue->mutex);
            return DIST_TXN_QUEUE_FULL;
        }
    }
    
    // 创建新节点
    txn_queue_node_t *new_node = malloc(sizeof(txn_queue_node_t));
    if (!new_node) {
        pthread_mutex_unlock(&queue->mutex);
        return DIST_TXN_ERROR;
    }
    
    new_node->txn = txn;
    new_node->priority = txn->priority;
    new_node->queue_time = time(NULL);
    new_node->next = NULL;
    
    // 按优先级插入（高优先级在前）
    if (!queue->head || new_node->priority > queue->head->priority) {
        // 插入到队列头
        new_node->next = queue->head;
        queue->head = new_node;
        if (!queue->tail) queue->tail = new_node;
    } else {
        // 找到合适的插入位置
        txn_queue_node_t *current = queue->head;
        while (current->next && current->next->priority >= new_node->priority) {
            current = current->next;
        }
        
        new_node->next = current->next;
        current->next = new_node;
        if (!new_node->next) queue->tail = new_node;
    }
    
    queue->size++;
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    
    return DIST_TXN_SUCCESS;
}

// 出队
dist_transaction_t *txn_priority_queue_dequeue(txn_priority_queue_t *queue, int timeout_ms) {
    if (!queue) return NULL;
    
    pthread_mutex_lock(&queue->mutex);
    
    // 等待非空
    while (queue->size == 0) {
        if (timeout_ms > 0) {
            struct timespec timeout;
            clock_gettime(CLOCK_REALTIME, &timeout);
            timeout.tv_sec += timeout_ms / 1000;
            timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
            
            int ret = pthread_cond_timedwait(&queue->not_empty, &queue->mutex, &timeout);
            if (ret == ETIMEDOUT) {
                pthread_mutex_unlock(&queue->mutex);
                return NULL;
            }
        } else {
            pthread_cond_wait(&queue->not_empty, &queue->mutex);
        }
    }
    
    // 从队列头取出
    txn_queue_node_t *node = queue->head;
    dist_transaction_t *txn = node->txn;
    
    queue->head = node->next;
    if (!queue->head) queue->tail = NULL;
    
    queue->size--;
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    
    free(node);
    return txn;
}

// 获取队列大小
int txn_priority_queue_size(txn_priority_queue_t *queue) {
    if (!queue) return 0;
    
    pthread_mutex_lock(&queue->mutex);
    int size = queue->size;
    pthread_mutex_unlock(&queue->mutex);
    
    return size;
}

// 初始化分布式事务协调器
int dist_txn_coordinator_init(dist_txn_coordinator_t *coordinator, 
                             const char *coordinator_id,
                             void *storage_engine) {
    if (!coordinator || !coordinator_id) return DIST_TXN_ERROR;
    
    memset(coordinator, 0, sizeof(dist_txn_coordinator_t));
    
    strncpy(coordinator->coordinator_id, coordinator_id, sizeof(coordinator->coordinator_id) - 1);
    coordinator->is_active = false;
    coordinator->storage_engine = storage_engine;
    
    // 初始化节点管理
    coordinator->nodes = NULL;
    coordinator->node_count = 0;
    if (pthread_mutex_init(&coordinator->nodes_mutex, NULL) != 0) {
        return DIST_TXN_ERROR;
    }
    
    // 初始化事务管理
    coordinator->transactions = NULL;
    coordinator->transaction_count = 0;
    coordinator->next_global_txn_id = 1;
    if (pthread_mutex_init(&coordinator->txn_mutex, NULL) != 0) {
        pthread_mutex_destroy(&coordinator->nodes_mutex);
        return DIST_TXN_ERROR;
    }
    
    // 创建优先级队列
    coordinator->priority_queue = txn_priority_queue_create(1000);
    if (!coordinator->priority_queue) {
        pthread_mutex_destroy(&coordinator->nodes_mutex);
        pthread_mutex_destroy(&coordinator->txn_mutex);
        return DIST_TXN_ERROR;
    }
    
    // 初始化统计信息
    atomic_store(&coordinator->total_transactions, 0);
    atomic_store(&coordinator->committed_transactions, 0);
    atomic_store(&coordinator->aborted_transactions, 0);
    atomic_store(&coordinator->timeout_transactions, 0);
    
    coordinator->threads_running = false;
    
    return DIST_TXN_SUCCESS;
}

// 调度器线程函数
static void *scheduler_thread_func(void *arg) {
    dist_txn_coordinator_t *coordinator = (dist_txn_coordinator_t *)arg;
    
    while (coordinator->threads_running) {
        // 从优先级队列中取出事务进行处理
        dist_transaction_t *txn = txn_priority_queue_dequeue(coordinator->priority_queue, 1000);
        
        if (txn) {
            // 检查事务是否超时
            if (is_timeout(txn->create_time, txn->timeout_ms)) {
                printf("Transaction %lu timeout, aborting...\n", txn->global_txn_id);
                dist_txn_abort(coordinator, txn);
                atomic_fetch_add(&coordinator->timeout_transactions, 1);
                continue;
            }
            
            // 根据事务状态进行处理
            pthread_mutex_lock(&txn->mutex);
            switch (txn->status) {
                case DIST_TXN_PREPARING:
                    pthread_mutex_unlock(&txn->mutex);
                    // 执行准备阶段
                    if (dist_txn_prepare(coordinator, txn) == DIST_TXN_SUCCESS) {
                        // 准备成功，进入提交阶段
                        pthread_mutex_lock(&txn->mutex);
                        txn->status = DIST_TXN_COMMITTING;
                        pthread_mutex_unlock(&txn->mutex);
                        
                        // 重新入队等待提交
                        txn_priority_queue_enqueue(coordinator->priority_queue, txn);
                    } else {
                        // 准备失败，中止事务
                        dist_txn_abort(coordinator, txn);
                    }
                    break;
                    
                case DIST_TXN_COMMITTING:
                    pthread_mutex_unlock(&txn->mutex);
                    // 执行提交阶段
                    if (dist_txn_commit(coordinator, txn) == DIST_TXN_SUCCESS) {
                        atomic_fetch_add(&coordinator->committed_transactions, 1);
                    } else {
                        // 提交失败，中止事务
                        dist_txn_abort(coordinator, txn);
                    }
                    break;
                    
                default:
                    pthread_mutex_unlock(&txn->mutex);
                    break;
            }
        }
        
        // 避免CPU占用过高
        usleep(10000);  // 10ms
    }
    
    return NULL;
}

// 心跳线程函数
static void *heartbeat_thread_func(void *arg) {
    dist_txn_coordinator_t *coordinator = (dist_txn_coordinator_t *)arg;
    
    while (coordinator->threads_running) {
        // 发送心跳
        dist_txn_send_heartbeat(coordinator);
        
        // 检查节点健康状态
        dist_txn_check_node_health(coordinator);
        
        // 每5秒发送一次心跳
        sleep(5);
    }
    
    return NULL;
}

// 超时检查线程函数
static void *timeout_thread_func(void *arg) {
    dist_txn_coordinator_t *coordinator = (dist_txn_coordinator_t *)arg;
    
    while (coordinator->threads_running) {
        // 检查超时事务
        dist_txn_check_timeouts(coordinator);
        
        // 每秒检查一次
        sleep(1);
    }
    
    return NULL;
}

// 启动协调器线程
int dist_txn_coordinator_start(dist_txn_coordinator_t *coordinator) {
    if (!coordinator || coordinator->threads_running) return DIST_TXN_ERROR;
    
    coordinator->threads_running = true;
    coordinator->is_active = true;
    
    // 启动调度器线程
    if (pthread_create(&coordinator->scheduler_thread, NULL, 
                      scheduler_thread_func, coordinator) != 0) {
        coordinator->threads_running = false;
        coordinator->is_active = false;
        return DIST_TXN_ERROR;
    }
    
    // 启动心跳线程
    if (pthread_create(&coordinator->heartbeat_thread, NULL, 
                      heartbeat_thread_func, coordinator) != 0) {
        coordinator->threads_running = false;
        coordinator->is_active = false;
        pthread_cancel(coordinator->scheduler_thread);
        return DIST_TXN_ERROR;
    }
    
    // 启动超时检查线程
    if (pthread_create(&coordinator->timeout_thread, NULL, 
                      timeout_thread_func, coordinator) != 0) {
        coordinator->threads_running = false;
        coordinator->is_active = false;
        pthread_cancel(coordinator->scheduler_thread);
        pthread_cancel(coordinator->heartbeat_thread);
        return DIST_TXN_ERROR;
    }
    
    printf("Distributed transaction coordinator started: %s\n", coordinator->coordinator_id);
    return DIST_TXN_SUCCESS;
}

// 停止协调器线程
int dist_txn_coordinator_stop(dist_txn_coordinator_t *coordinator) {
    if (!coordinator || !coordinator->threads_running) return DIST_TXN_ERROR;
    
    coordinator->threads_running = false;
    coordinator->is_active = false;
    
    // 等待线程结束
    pthread_join(coordinator->scheduler_thread, NULL);
    pthread_join(coordinator->heartbeat_thread, NULL);
    pthread_join(coordinator->timeout_thread, NULL);
    
    printf("Distributed transaction coordinator stopped: %s\n", coordinator->coordinator_id);
    return DIST_TXN_SUCCESS;
}

// 销毁分布式事务协调器
void dist_txn_coordinator_destroy(dist_txn_coordinator_t *coordinator) {
    if (!coordinator) return;
    
    // 停止线程
    if (coordinator->threads_running) {
        dist_txn_coordinator_stop(coordinator);
    }
    
    // 清理节点列表
    pthread_mutex_lock(&coordinator->nodes_mutex);
    dist_node_t *node = coordinator->nodes;
    while (node) {
        dist_node_t *next = node->next;
        free(node);
        node = next;
    }
    pthread_mutex_unlock(&coordinator->nodes_mutex);
    
    // 清理事务列表
    pthread_mutex_lock(&coordinator->txn_mutex);
    dist_transaction_t *txn = coordinator->transactions;
    while (txn) {
        dist_transaction_t *next = txn->next;
        dist_txn_free(txn);
        txn = next;
    }
    pthread_mutex_unlock(&coordinator->txn_mutex);
    
    // 销毁优先级队列
    if (coordinator->priority_queue) {
        txn_priority_queue_destroy(coordinator->priority_queue);
    }
    
    pthread_mutex_destroy(&coordinator->nodes_mutex);
    pthread_mutex_destroy(&coordinator->txn_mutex);
}

// 添加节点
int dist_txn_add_node(dist_txn_coordinator_t *coordinator, 
                      const char *node_id, 
                      const char *address, 
                      int port) {
    if (!coordinator || !node_id || !address) return DIST_TXN_ERROR;
    
    pthread_mutex_lock(&coordinator->nodes_mutex);
    
    // 检查节点是否已存在
    dist_node_t *current = coordinator->nodes;
    while (current) {
        if (strcmp(current->node_id, node_id) == 0) {
            pthread_mutex_unlock(&coordinator->nodes_mutex);
            return DIST_TXN_ERROR;  // 节点已存在
        }
        current = current->next;
    }
    
    // 创建新节点
    dist_node_t *new_node = malloc(sizeof(dist_node_t));
    if (!new_node) {
        pthread_mutex_unlock(&coordinator->nodes_mutex);
        return DIST_TXN_ERROR;
    }
    
    strncpy(new_node->node_id, node_id, sizeof(new_node->node_id) - 1);
    strncpy(new_node->address, address, sizeof(new_node->address) - 1);
    new_node->port = port;
    new_node->is_coordinator = false;
    new_node->is_alive = true;
    new_node->last_heartbeat = time(NULL);
    
    // 添加到链表头
    new_node->next = coordinator->nodes;
    coordinator->nodes = new_node;
    coordinator->node_count++;
    
    pthread_mutex_unlock(&coordinator->nodes_mutex);
    
    printf("Added node: %s (%s:%d)\n", node_id, address, port);
    return DIST_TXN_SUCCESS;
}

// 移除节点
int dist_txn_remove_node(dist_txn_coordinator_t *coordinator, const char *node_id) {
    if (!coordinator || !node_id) return DIST_TXN_ERROR;
    
    pthread_mutex_lock(&coordinator->nodes_mutex);
    
    dist_node_t *current = coordinator->nodes;
    dist_node_t *prev = NULL;
    
    while (current) {
        if (strcmp(current->node_id, node_id) == 0) {
            if (prev) {
                prev->next = current->next;
            } else {
                coordinator->nodes = current->next;
            }
            
            coordinator->node_count--;
            free(current);
            
            pthread_mutex_unlock(&coordinator->nodes_mutex);
            printf("Removed node: %s\n", node_id);
            return DIST_TXN_SUCCESS;
        }
        
        prev = current;
        current = current->next;
    }
    
    pthread_mutex_unlock(&coordinator->nodes_mutex);
    return DIST_TXN_ERROR;  // 节点不存在
}

// 开始分布式事务
dist_transaction_t *dist_txn_begin(dist_txn_coordinator_t *coordinator,
                                   txn_priority_t priority,
                                   int timeout_ms,
                                   const char **participant_ids,
                                   int participant_count) {
    if (!coordinator || !participant_ids || participant_count <= 0) return NULL;
    
    // 创建分布式事务
    dist_transaction_t *txn = malloc(sizeof(dist_transaction_t));
    if (!txn) return NULL;
    
    memset(txn, 0, sizeof(dist_transaction_t));
    
    pthread_mutex_lock(&coordinator->txn_mutex);
    txn->global_txn_id = coordinator->next_global_txn_id++;
    pthread_mutex_unlock(&coordinator->txn_mutex);
    
    strncpy(txn->coordinator_id, coordinator->coordinator_id, sizeof(txn->coordinator_id) - 1);
    txn->status = DIST_TXN_PREPARING;
    txn->priority = priority;
    txn->create_time = time(NULL);
    txn->start_time = 0;
    txn->deadline = txn->create_time + (timeout_ms / 1000);
    txn->timeout_ms = timeout_ms;
    
    // 初始化参与者列表
    txn->participants = NULL;
    txn->participant_count = participant_count;
    txn->prepared_count = 0;
    txn->committed_count = 0;
    
    // 创建参与者
    for (int i = 0; i < participant_count; i++) {
        dist_participant_t *participant = malloc(sizeof(dist_participant_t));
        if (!participant) {
            // 清理已创建的参与者
            dist_participant_t *p = txn->participants;
            while (p) {
                dist_participant_t *next = p->next;
                free(p);
                p = next;
            }
            free(txn);
            return NULL;
        }
        
        strncpy(participant->node_id, participant_ids[i], sizeof(participant->node_id) - 1);
        participant->status = DIST_TXN_PREPARING;
        participant->prepare_time = 0;
        participant->response_time = 0;
        memset(participant->prepare_result, 0, sizeof(participant->prepare_result));
        
        // 添加到参与者列表
        participant->next = txn->participants;
        txn->participants = participant;
    }
    
    // 创建本地事务
    txn_manager_t *txn_manager = get_default_txn_manager();
    txn->local_txn = (kv_transaction_t*)txn_begin(txn_manager, coordinator->storage_engine, TXN_ISOLATION_SERIALIZABLE);
    
    // 初始化同步对象
    if (pthread_mutex_init(&txn->mutex, NULL) != 0) {
        dist_txn_free(txn);
        return NULL;
    }
    
    if (pthread_cond_init(&txn->condition, NULL) != 0) {
        pthread_mutex_destroy(&txn->mutex);
        dist_txn_free(txn);
        return NULL;
    }
    
    // 添加到协调器的事务列表
    pthread_mutex_lock(&coordinator->txn_mutex);
    txn->next = coordinator->transactions;
    coordinator->transactions = txn;
    coordinator->transaction_count++;
    pthread_mutex_unlock(&coordinator->txn_mutex);
    
    // 更新统计信息
    atomic_fetch_add(&coordinator->total_transactions, 1);
    
    // 将事务加入优先级队列
    txn_priority_queue_enqueue(coordinator->priority_queue, txn);
    
    printf("Started distributed transaction %lu with priority %d\n", 
           txn->global_txn_id, txn->priority);
    
    return txn;
}

// 准备阶段 - 2PC第一阶段
int dist_txn_prepare(dist_txn_coordinator_t *coordinator, dist_transaction_t *txn) {
    if (!coordinator || !txn) return DIST_TXN_ERROR;
    
    printf("Preparing distributed transaction %lu...\n", txn->global_txn_id);
    
    pthread_mutex_lock(&txn->mutex);
    
    if (txn->status != DIST_TXN_PREPARING) {
        pthread_mutex_unlock(&txn->mutex);
        return DIST_TXN_INVALID_STATE;
    }
    
    txn->start_time = time(NULL);
    
    // 向所有参与者发送准备消息
    dist_participant_t *participant = txn->participants;
    while (participant) {
        dist_txn_message_t msg;
        memset(&msg, 0, sizeof(msg));
        
        msg.type = DIST_MSG_PREPARE;
        msg.global_txn_id = txn->global_txn_id;
        strncpy(msg.sender_id, coordinator->coordinator_id, sizeof(msg.sender_id) - 1);
        strncpy(msg.receiver_id, participant->node_id, sizeof(msg.receiver_id) - 1);
        msg.timestamp = time(NULL);
        msg.timeout_ms = txn->timeout_ms;
        
        // 发送消息
        if (dist_txn_send_message(coordinator, participant->node_id, &msg) != DIST_TXN_SUCCESS) {
            printf("Failed to send prepare message to %s\n", participant->node_id);
            participant->status = DIST_TXN_ABORTED;
        } else {
            participant->prepare_time = time(NULL);
            printf("Sent prepare message to %s\n", participant->node_id);
        }
        
        participant = participant->next;
    }
    
    pthread_mutex_unlock(&txn->mutex);
    
    // 等待所有参与者响应（简化实现：假设立即收到响应）
    // 在实际实现中，这里应该等待网络消息
    
    // 模拟参与者响应
    pthread_mutex_lock(&txn->mutex);
    participant = txn->participants;
    while (participant) {
        if (participant->status == DIST_TXN_PREPARING) {
            // 模拟90%的成功率
            if (rand() % 100 < 90) {
                participant->status = DIST_TXN_PREPARED;
                participant->response_time = time(NULL);
                txn->prepared_count++;
                printf("Participant %s prepared\n", participant->node_id);
            } else {
                participant->status = DIST_TXN_ABORTED;
                strcpy(participant->prepare_result, "Prepare failed");
                printf("Participant %s prepare failed\n", participant->node_id);
            }
        }
        participant = participant->next;
    }
    
    // 检查是否所有参与者都准备成功
    bool all_prepared = (txn->prepared_count == txn->participant_count);
    
    if (all_prepared) {
        txn->status = DIST_TXN_PREPARED;
        printf("All participants prepared for transaction %lu\n", txn->global_txn_id);
        pthread_mutex_unlock(&txn->mutex);
        return DIST_TXN_SUCCESS;
    } else {
        txn->status = DIST_TXN_ABORTING;
        printf("Some participants failed to prepare transaction %lu\n", txn->global_txn_id);
        pthread_mutex_unlock(&txn->mutex);
        return DIST_TXN_PREPARE_FAILED;
    }
}

// 提交阶段 - 2PC第二阶段
int dist_txn_commit(dist_txn_coordinator_t *coordinator, dist_transaction_t *txn) {
    if (!coordinator || !txn) return DIST_TXN_ERROR;
    
    printf("Committing distributed transaction %lu...\n", txn->global_txn_id);
    
    pthread_mutex_lock(&txn->mutex);
    
    if (txn->status != DIST_TXN_COMMITTING && txn->status != DIST_TXN_PREPARED) {
        pthread_mutex_unlock(&txn->mutex);
        return DIST_TXN_INVALID_STATE;
    }
    
    // 提交本地事务
    if (txn->local_txn) {
        if (txn_commit(txn->local_txn) != 0) {
            printf("Local transaction commit failed for %lu\n", txn->global_txn_id);
            txn->status = DIST_TXN_ABORTING;
            pthread_mutex_unlock(&txn->mutex);
            return DIST_TXN_COMMIT_FAILED;
        }
    }
    
    // 向所有参与者发送提交消息
    dist_participant_t *participant = txn->participants;
    while (participant) {
        if (participant->status == DIST_TXN_PREPARED) {
            dist_txn_message_t msg;
            memset(&msg, 0, sizeof(msg));
            
            msg.type = DIST_MSG_COMMIT;
            msg.global_txn_id = txn->global_txn_id;
            strncpy(msg.sender_id, coordinator->coordinator_id, sizeof(msg.sender_id) - 1);
            strncpy(msg.receiver_id, participant->node_id, sizeof(msg.receiver_id) - 1);
            msg.timestamp = time(NULL);
            
            if (dist_txn_send_message(coordinator, participant->node_id, &msg) == DIST_TXN_SUCCESS) {
                participant->status = DIST_TXN_COMMITTED;
                txn->committed_count++;
                printf("Sent commit message to %s\n", participant->node_id);
            } else {
                printf("Failed to send commit message to %s\n", participant->node_id);
            }
        }
        participant = participant->next;
    }
    
    txn->status = DIST_TXN_COMMITTED;
    printf("Distributed transaction %lu committed successfully\n", txn->global_txn_id);
    
    pthread_mutex_unlock(&txn->mutex);
    return DIST_TXN_SUCCESS;
}

// 中止事务
int dist_txn_abort(dist_txn_coordinator_t *coordinator, dist_transaction_t *txn) {
    if (!coordinator || !txn) return DIST_TXN_ERROR;
    
    printf("Aborting distributed transaction %lu...\n", txn->global_txn_id);
    
    pthread_mutex_lock(&txn->mutex);
    
    if (txn->status == DIST_TXN_COMMITTED) {
        pthread_mutex_unlock(&txn->mutex);
        return DIST_TXN_INVALID_STATE;  // 已提交的事务不能中止
    }
    
    // 回滚本地事务
    if (txn->local_txn) {
        txn_rollback(txn->local_txn);
    }
    
    // 向所有参与者发送中止消息
    dist_participant_t *participant = txn->participants;
    while (participant) {
        if (participant->status != DIST_TXN_ABORTED) {
            dist_txn_message_t msg;
            memset(&msg, 0, sizeof(msg));
            
            msg.type = DIST_MSG_ABORT;
            msg.global_txn_id = txn->global_txn_id;
            strncpy(msg.sender_id, coordinator->coordinator_id, sizeof(msg.sender_id) - 1);
            strncpy(msg.receiver_id, participant->node_id, sizeof(msg.receiver_id) - 1);
            msg.timestamp = time(NULL);
            
            dist_txn_send_message(coordinator, participant->node_id, &msg);
            participant->status = DIST_TXN_ABORTED;
            printf("Sent abort message to %s\n", participant->node_id);
        }
        participant = participant->next;
    }
    
    txn->status = DIST_TXN_ABORTED;
    atomic_fetch_add(&coordinator->aborted_transactions, 1);
    
    printf("Distributed transaction %lu aborted\n", txn->global_txn_id);
    
    pthread_mutex_unlock(&txn->mutex);
    return DIST_TXN_SUCCESS;
}

// 释放分布式事务
void dist_txn_free(dist_transaction_t *txn) {
    if (!txn) return;
    
    // 释放参与者列表
    dist_participant_t *participant = txn->participants;
    while (participant) {
        dist_participant_t *next = participant->next;
        free(participant);
        participant = next;
    }
    
    // 释放本地事务
    if (txn->local_txn) {
        txn_free(txn->local_txn);
    }
    
    // 销毁同步对象
    pthread_mutex_destroy(&txn->mutex);
    pthread_cond_destroy(&txn->condition);
    
    free(txn);
}

// 发送消息（简化实现）
int dist_txn_send_message(dist_txn_coordinator_t *coordinator,
                         const char *node_id,
                         const dist_txn_message_t *msg) {
    if (!coordinator || !node_id || !msg) return DIST_TXN_ERROR;
    
    // 使用回调函数发送消息
    if (g_send_callback) {
        return g_send_callback(node_id, msg);
    }
    
    // 模拟网络延迟
    usleep(1000);  // 1ms
    
    printf("Message sent to %s: type=%d, txn_id=%lu\n", 
           node_id, msg->type, msg->global_txn_id);
    
    return DIST_TXN_SUCCESS;
}

// 处理接收到的消息
int dist_txn_handle_message(dist_txn_coordinator_t *coordinator,
                           const dist_txn_message_t *msg) {
    if (!coordinator || !msg) return DIST_TXN_ERROR;
    
    printf("Received message: type=%d, txn_id=%lu, from=%s\n", 
           msg->type, msg->global_txn_id, msg->sender_id);
    
    // 根据消息类型处理
    switch (msg->type) {
        case DIST_MSG_PREPARE_OK:
        case DIST_MSG_PREPARE_FAIL:
        case DIST_MSG_COMMIT_OK:
        case DIST_MSG_ABORT_OK:
            // 更新事务状态
            // 在实际实现中，需要找到对应的事务并更新状态
            break;
            
        case DIST_MSG_HEARTBEAT:
            // 更新节点心跳时间
            pthread_mutex_lock(&coordinator->nodes_mutex);
            dist_node_t *node = coordinator->nodes;
            while (node) {
                if (strcmp(node->node_id, msg->sender_id) == 0) {
                    node->last_heartbeat = time(NULL);
                    node->is_alive = true;
                    break;
                }
                node = node->next;
            }
            pthread_mutex_unlock(&coordinator->nodes_mutex);
            break;
            
        default:
            printf("Unknown message type: %d\n", msg->type);
            return DIST_TXN_ERROR;
    }
    
    return DIST_TXN_SUCCESS;
}

// 检查超时事务
int dist_txn_check_timeouts(dist_txn_coordinator_t *coordinator) {
    if (!coordinator) return DIST_TXN_ERROR;
    
    time_t current_time = time(NULL);
    int timeout_count = 0;
    
    pthread_mutex_lock(&coordinator->txn_mutex);
    
    dist_transaction_t *txn = coordinator->transactions;
    while (txn) {
        pthread_mutex_lock(&txn->mutex);
        
        // 检查事务是否超时
        if (txn->status != DIST_TXN_COMMITTED && txn->status != DIST_TXN_ABORTED) {
            if (is_timeout(txn->create_time, txn->timeout_ms)) {
                printf("Transaction %lu timeout detected\n", txn->global_txn_id);
                txn->status = DIST_TXN_ABORTING;
                timeout_count++;
                
                // 在下一次调度中处理超时事务
                txn_priority_queue_enqueue(coordinator->priority_queue, txn);
            }
        }
        
        pthread_mutex_unlock(&txn->mutex);
        txn = txn->next;
    }
    
    pthread_mutex_unlock(&coordinator->txn_mutex);
    
    return timeout_count;
}

// 发送心跳
int dist_txn_send_heartbeat(dist_txn_coordinator_t *coordinator) {
    if (!coordinator) return DIST_TXN_ERROR;
    
    pthread_mutex_lock(&coordinator->nodes_mutex);
    
    dist_node_t *node = coordinator->nodes;
    while (node) {
        dist_txn_message_t msg;
        memset(&msg, 0, sizeof(msg));
        
        msg.type = DIST_MSG_HEARTBEAT;
        msg.global_txn_id = 0;  // 心跳消息不关联事务
        strncpy(msg.sender_id, coordinator->coordinator_id, sizeof(msg.sender_id) - 1);
        strncpy(msg.receiver_id, node->node_id, sizeof(msg.receiver_id) - 1);
        msg.timestamp = time(NULL);
        
        dist_txn_send_message(coordinator, node->node_id, &msg);
        
        node = node->next;
    }
    
    pthread_mutex_unlock(&coordinator->nodes_mutex);
    
    return DIST_TXN_SUCCESS;
}

// 检查节点健康状态
int dist_txn_check_node_health(dist_txn_coordinator_t *coordinator) {
    if (!coordinator) return DIST_TXN_ERROR;
    
    time_t current_time = time(NULL);
    int unhealthy_count = 0;
    
    pthread_mutex_lock(&coordinator->nodes_mutex);
    
    dist_node_t *node = coordinator->nodes;
    while (node) {
        // 检查节点是否超过30秒没有心跳
        if (current_time - node->last_heartbeat > 30) {
            if (node->is_alive) {
                printf("Node %s is unresponsive\n", node->node_id);
                node->is_alive = false;
                unhealthy_count++;
            }
        }
        
        node = node->next;
    }
    
    pthread_mutex_unlock(&coordinator->nodes_mutex);
    
    return unhealthy_count;
}

// 获取统计信息
void dist_txn_get_stats(dist_txn_coordinator_t *coordinator,
                       long *total, long *committed, 
                       long *aborted, long *timeout) {
    if (!coordinator) return;
    
    if (total) *total = atomic_load(&coordinator->total_transactions);
    if (committed) *committed = atomic_load(&coordinator->committed_transactions);
    if (aborted) *aborted = atomic_load(&coordinator->aborted_transactions);
    if (timeout) *timeout = atomic_load(&coordinator->timeout_transactions);
}

// 设置调度策略
int dist_txn_set_schedule_policy(dist_txn_coordinator_t *coordinator,
                                txn_schedule_policy_t policy) {
    if (!coordinator) return DIST_TXN_ERROR;
    
    g_schedule_policy = policy;
    printf("Transaction scheduling policy set to: %d\n", policy);
    
    return DIST_TXN_SUCCESS;
}

// 设置消息回调函数
void dist_txn_set_send_callback(dist_txn_send_callback_t callback) {
    g_send_callback = callback;
}

void dist_txn_set_receive_callback(dist_txn_receive_callback_t callback) {
    g_receive_callback = callback;
} 