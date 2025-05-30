/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV storage engine - kv_transaction.h
 */
#ifndef __KV_TRANSACTION_H__
#define __KV_TRANSACTION_H__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// 事务操作类型
#define TXN_OP_SET     1
#define TXN_OP_DEL     2
#define TXN_OP_MOD     3

// 事务状态
#define TXN_STATUS_ACTIVE    0   // 活动状态
#define TXN_STATUS_COMMITTED 1   // 已提交
#define TXN_STATUS_ABORTED   2   // 已回滚

// 事务隔离级别
#define TXN_ISOLATION_READ_UNCOMMITTED 0   // 读未提交
#define TXN_ISOLATION_READ_COMMITTED   1   // 读已提交
#define TXN_ISOLATION_REPEATABLE_READ  2   // 可重复读
#define TXN_ISOLATION_SERIALIZABLE     3   // 串行化

// 事务操作项
typedef struct txn_op_item {
    uint8_t op_type;          // 操作类型
    char *key;                // 键
    char *value;              // 值
    char *old_value;          // 旧值（用于回滚）
    struct txn_op_item *next; // 下一个操作
} txn_op_item_t;

// 事务结构
typedef struct {
    uint64_t txn_id;              // 事务ID
    uint8_t status;               // 事务状态
    uint8_t isolation_level;      // 隔离级别
    time_t start_time;            // 开始时间
    time_t end_time;              // 结束时间
    txn_op_item_t *op_list;       // 操作列表
    txn_op_item_t *op_list_tail;  // 操作列表尾
    int op_count;                 // 操作数量
    void *engine;                 // 存储引擎指针
} kv_transaction_t;

// 全局事务管理
typedef struct {
    uint64_t next_txn_id;      // 下一个事务ID
    int txn_count;             // 当前活动事务数量
    uint8_t default_isolation; // 默认隔离级别
} txn_manager_t;

// 初始化事务管理器
int txn_manager_init(txn_manager_t *manager);

// 销毁事务管理器
void txn_manager_destroy(txn_manager_t *manager);

// 开始一个新事务
kv_transaction_t *txn_begin(txn_manager_t *manager, void *engine, uint8_t isolation_level);

// 在事务中设置键值
int txn_set(kv_transaction_t *txn, const char *key, const char *value);

// 在事务中获取键值
char *txn_get(kv_transaction_t *txn, const char *key);

// 在事务中删除键值
int txn_delete(kv_transaction_t *txn, const char *key);

// 在事务中修改键值
int txn_modify(kv_transaction_t *txn, const char *key, const char *value);

// 提交事务
int txn_commit(kv_transaction_t *txn);

// 回滚事务
int txn_rollback(kv_transaction_t *txn);

// 释放事务资源
void txn_free(kv_transaction_t *txn);

#endif // __KV_TRANSACTION_H__ 