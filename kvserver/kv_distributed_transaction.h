/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 11:16:16
* @Description: ConcordKV分布式事务支持头文件
 */

#ifndef __KV_DISTRIBUTED_TRANSACTION_H__
#define __KV_DISTRIBUTED_TRANSACTION_H__

#include "kv_transaction.h"
#include "kv_concurrency.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <sys/time.h>

// 分布式事务状态
typedef enum {
    DIST_TXN_PREPARING = 0,    // 准备阶段
    DIST_TXN_PREPARED,         // 已准备
    DIST_TXN_COMMITTING,       // 提交中
    DIST_TXN_COMMITTED,        // 已提交
    DIST_TXN_ABORTING,         // 中止中
    DIST_TXN_ABORTED          // 已中止
} dist_txn_status_t;

// 事务优先级级别
typedef enum {
    TXN_PRIORITY_LOW = 1,      // 低优先级
    TXN_PRIORITY_NORMAL = 5,   // 正常优先级
    TXN_PRIORITY_HIGH = 10,    // 高优先级
    TXN_PRIORITY_URGENT = 15   // 紧急优先级
} txn_priority_t;

// 调度策略
typedef enum {
    TXN_SCHEDULE_FIFO = 0,     // 先进先出
    TXN_SCHEDULE_PRIORITY,     // 优先级调度
    TXN_SCHEDULE_DEADLINE,     // 截止时间调度
    TXN_SCHEDULE_ADAPTIVE      // 自适应调度
} txn_schedule_policy_t;

// 分布式事务错误码
typedef enum {
    DIST_TXN_SUCCESS = 0,      // 成功
    DIST_TXN_ERROR = -1,       // 一般错误
    DIST_TXN_TIMEOUT = -2,     // 超时
    DIST_TXN_PREPARE_FAILED = -3,    // 准备失败
    DIST_TXN_COMMIT_FAILED = -4,     // 提交失败
    DIST_TXN_ABORT_FAILED = -5,      // 中止失败
    DIST_TXN_INVALID_STATE = -6,     // 无效状态
    DIST_TXN_NODE_UNAVAILABLE = -7,  // 节点不可用
    DIST_TXN_QUEUE_FULL = -8,        // 队列已满
    DIST_TXN_DEADLOCK = -9     // 死锁
} dist_txn_error_t;

// 消息类型
typedef enum {
    DIST_MSG_PREPARE = 1,      // 准备消息
    DIST_MSG_PREPARE_OK,       // 准备成功
    DIST_MSG_PREPARE_FAIL,     // 准备失败
    DIST_MSG_COMMIT,           // 提交消息
    DIST_MSG_COMMIT_OK,        // 提交成功
    DIST_MSG_ABORT,            // 中止消息
    DIST_MSG_ABORT_OK,         // 中止成功
    DIST_MSG_HEARTBEAT,        // 心跳消息
    DIST_MSG_RECOVERY          // 恢复消息
} dist_msg_type_t;

// 分布式节点
typedef struct dist_node {
    char node_id[64];          // 节点ID
    char address[256];         // 节点地址
    int port;                  // 端口
    bool is_coordinator;       // 是否为协调器
    bool is_alive;             // 是否存活
    time_t last_heartbeat;     // 最后心跳时间
    struct dist_node *next;    // 下一个节点
} dist_node_t;

// 分布式事务参与者
typedef struct dist_participant {
    char node_id[64];          // 参与者节点ID
    dist_txn_status_t status;  // 参与者状态
    time_t prepare_time;       // 准备时间
    time_t response_time;      // 响应时间
    char prepare_result[256];  // 准备结果信息
    struct dist_participant *next;  // 下一个参与者
} dist_participant_t;

// 分布式事务
typedef struct dist_transaction {
    uint64_t global_txn_id;    // 全局事务ID
    char coordinator_id[64];   // 协调器ID
    dist_txn_status_t status;  // 事务状态
    txn_priority_t priority;   // 事务优先级
    
    time_t create_time;        // 创建时间
    time_t start_time;         // 开始时间
    time_t deadline;           // 截止时间
    int timeout_ms;            // 超时时间(毫秒)
    
    // 参与者信息
    dist_participant_t *participants;  // 参与者列表
    int participant_count;     // 参与者数量
    int prepared_count;        // 已准备参与者数量
    int committed_count;       // 已提交参与者数量
    
    // 本地事务
    kv_transaction_t *local_txn;  // 本地事务对象
    
    // 同步对象
    pthread_mutex_t mutex;     // 事务互斥锁
    pthread_cond_t condition;  // 条件变量
    
    struct dist_transaction *next;  // 下一个事务
} dist_transaction_t;

// 事务队列节点
typedef struct txn_queue_node {
    dist_transaction_t *txn;   // 事务指针
    txn_priority_t priority;   // 优先级
    time_t queue_time;         // 入队时间
    struct txn_queue_node *next;  // 下一个节点
} txn_queue_node_t;

// 事务优先级队列
typedef struct txn_priority_queue {
    txn_queue_node_t *head;    // 队列头
    txn_queue_node_t *tail;    // 队列尾
    int size;                  // 队列大小
    int max_size;              // 最大大小
    pthread_mutex_t mutex;     // 队列互斥锁
    pthread_cond_t not_empty;  // 非空条件变量
    pthread_cond_t not_full;   // 非满条件变量
} txn_priority_queue_t;

// 分布式事务消息
typedef struct dist_txn_message {
    dist_msg_type_t type;      // 消息类型
    uint64_t global_txn_id;    // 全局事务ID
    char sender_id[64];        // 发送者ID
    char receiver_id[64];      // 接收者ID
    time_t timestamp;          // 时间戳
    int timeout_ms;            // 超时时间
    char data[512];            // 消息数据
} dist_txn_message_t;

// 分布式事务协调器
typedef struct dist_txn_coordinator {
    char coordinator_id[64];   // 协调器ID
    bool is_active;            // 是否激活
    void *storage_engine;      // 存储引擎
    
    // 节点管理
    dist_node_t *nodes;        // 节点列表
    int node_count;            // 节点数量
    pthread_mutex_t nodes_mutex;  // 节点列表互斥锁
    
    // 事务管理
    dist_transaction_t *transactions;  // 事务列表
    int transaction_count;     // 事务数量
    uint64_t next_global_txn_id;  // 下一个全局事务ID
    pthread_mutex_t txn_mutex; // 事务列表互斥锁
    
    // 优先级队列
    txn_priority_queue_t *priority_queue;  // 优先级队列
    
    // 统计信息
    atomic_long total_transactions;      // 总事务数
    atomic_long committed_transactions;  // 已提交事务数
    atomic_long aborted_transactions;    // 已中止事务数
    atomic_long timeout_transactions;    // 超时事务数
    
    // 线程管理
    bool threads_running;      // 线程运行标志
    pthread_t scheduler_thread;  // 调度器线程
    pthread_t heartbeat_thread;  // 心跳线程
    pthread_t timeout_thread;    // 超时检查线程
} dist_txn_coordinator_t;

// 消息回调函数类型
typedef int (*dist_txn_send_callback_t)(const char *node_id, const dist_txn_message_t *msg);
typedef int (*dist_txn_receive_callback_t)(dist_txn_message_t *msg);

// 优先级队列操作
txn_priority_queue_t *txn_priority_queue_create(int max_size);
void txn_priority_queue_destroy(txn_priority_queue_t *queue);
int txn_priority_queue_enqueue(txn_priority_queue_t *queue, dist_transaction_t *txn);
dist_transaction_t *txn_priority_queue_dequeue(txn_priority_queue_t *queue, int timeout_ms);
int txn_priority_queue_size(txn_priority_queue_t *queue);

// 分布式事务协调器操作
int dist_txn_coordinator_init(dist_txn_coordinator_t *coordinator, 
                             const char *coordinator_id,
                             void *storage_engine);
void dist_txn_coordinator_destroy(dist_txn_coordinator_t *coordinator);
int dist_txn_coordinator_start(dist_txn_coordinator_t *coordinator);
int dist_txn_coordinator_stop(dist_txn_coordinator_t *coordinator);

// 节点管理
int dist_txn_add_node(dist_txn_coordinator_t *coordinator, 
                      const char *node_id, 
                      const char *address, 
                      int port);
int dist_txn_remove_node(dist_txn_coordinator_t *coordinator, const char *node_id);

// 分布式事务操作
dist_transaction_t *dist_txn_begin(dist_txn_coordinator_t *coordinator,
                                   txn_priority_t priority,
                                   int timeout_ms,
                                   const char **participant_ids,
                                   int participant_count);
int dist_txn_prepare(dist_txn_coordinator_t *coordinator, dist_transaction_t *txn);
int dist_txn_commit(dist_txn_coordinator_t *coordinator, dist_transaction_t *txn);
int dist_txn_abort(dist_txn_coordinator_t *coordinator, dist_transaction_t *txn);
void dist_txn_free(dist_transaction_t *txn);

// 消息处理
int dist_txn_send_message(dist_txn_coordinator_t *coordinator,
                         const char *node_id,
                         const dist_txn_message_t *msg);
int dist_txn_handle_message(dist_txn_coordinator_t *coordinator,
                           const dist_txn_message_t *msg);

// 健康监控
int dist_txn_send_heartbeat(dist_txn_coordinator_t *coordinator);
int dist_txn_check_node_health(dist_txn_coordinator_t *coordinator);
int dist_txn_check_timeouts(dist_txn_coordinator_t *coordinator);

// 统计信息
void dist_txn_get_stats(dist_txn_coordinator_t *coordinator,
                       long *total, long *committed, 
                       long *aborted, long *timeout);

// 配置和回调
int dist_txn_set_schedule_policy(dist_txn_coordinator_t *coordinator,
                                txn_schedule_policy_t policy);
void dist_txn_set_send_callback(dist_txn_send_callback_t callback);
void dist_txn_set_receive_callback(dist_txn_receive_callback_t callback);

// 工具函数
const char *dist_txn_status_to_string(dist_txn_status_t status);
const char *dist_txn_error_to_string(dist_txn_error_t error);

#endif // __KV_DISTRIBUTED_TRANSACTION_H__ 