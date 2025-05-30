/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV storage engine - kv_transaction.c
 */
#define _GNU_SOURCE     // 启用strdup等扩展函数
#define _POSIX_C_SOURCE 200809L  // 启用POSIX扩展

#include "kv_transaction.h"
#include "kv_store.h"
#include <string.h>
#include <assert.h>

// 全局事务管理器
static txn_manager_t g_txn_manager = {0};

// 初始化事务管理器
int txn_manager_init(txn_manager_t *manager) {
    if (!manager) return -1;
    
    manager->next_txn_id = 1;
    manager->txn_count = 0;
    manager->default_isolation = TXN_ISOLATION_READ_COMMITTED;
    
    return 0;
}

// 销毁事务管理器
void txn_manager_destroy(txn_manager_t *manager) {
    if (!manager) return;
    
    // 清理资源，未来可能需要处理未完成的事务
    manager->next_txn_id = 0;
    manager->txn_count = 0;
}

// 开始一个新事务
kv_transaction_t *txn_begin(txn_manager_t *manager, void *engine, uint8_t isolation_level) {
    if (!manager || !engine) return NULL;
    
    kv_transaction_t *txn = (kv_transaction_t *)malloc(sizeof(kv_transaction_t));
    if (!txn) return NULL;
    
    txn->txn_id = manager->next_txn_id++;
    txn->status = TXN_STATUS_ACTIVE;
    txn->isolation_level = isolation_level;
    txn->start_time = time(NULL);
    txn->end_time = 0;
    txn->op_list = NULL;
    txn->op_list_tail = NULL;
    txn->op_count = 0;
    txn->engine = engine;
    
    manager->txn_count++;
    
    return txn;
}

// 在事务中添加操作项
static txn_op_item_t *txn_add_op(kv_transaction_t *txn, uint8_t op_type, const char *key, const char *value) {
    if (!txn || !key || txn->status != TXN_STATUS_ACTIVE) return NULL;
    
    txn_op_item_t *item = (txn_op_item_t *)malloc(sizeof(txn_op_item_t));
    if (!item) return NULL;
    
    item->op_type = op_type;
    item->key = strdup(key);
    item->value = (value) ? strdup(value) : NULL;
    item->old_value = NULL;
    item->next = NULL;
    
    // 将操作添加到列表
    if (!txn->op_list) {
        txn->op_list = item;
        txn->op_list_tail = item;
    } else {
        txn->op_list_tail->next = item;
        txn->op_list_tail = item;
    }
    
    txn->op_count++;
    
    return item;
}

// 在事务中设置键值
int txn_set(kv_transaction_t *txn, const char *key, const char *value) {
    if (!txn || !key || !value || txn->status != TXN_STATUS_ACTIVE) return -1;
    
    txn_op_item_t *item = txn_add_op(txn, TXN_OP_SET, key, value);
    if (!item) return -1;
    
    // 针对不同的隔离级别，可能需要立即执行或者延迟执行
    if (txn->isolation_level == TXN_ISOLATION_READ_UNCOMMITTED) {
        // 在读未提交级别下，立即执行操作
        void *engine = txn->engine;
        
#if ENABLE_RBTREE_KVENGINE
        // 使用红黑树引擎
        if (engine) {
            rbtree_t *tree = (rbtree_t *)engine;
            
            // 获取当前值（如果有）
            char *current_value = kvs_rbtree_get(tree, (char *)key);
            if (current_value) {
                item->old_value = strdup(current_value);
            }
            
            // 设置新值
            kvs_rbtree_set(tree, (char *)key, (char *)value);
        }
#endif
    }
    
    return 0;
}

// 在事务中获取键值
char *txn_get(kv_transaction_t *txn, const char *key) {
    if (!txn || !key) return NULL;
    
    void *engine = txn->engine;
    
    // 如果是可重复读或串行化级别，首先检查事务内的修改
    if (txn->isolation_level >= TXN_ISOLATION_REPEATABLE_READ) {
        txn_op_item_t *item = txn->op_list;
        while (item) {
            if (strcmp(item->key, key) == 0) {
                if (item->op_type == TXN_OP_DEL) {
                    return NULL; // 已在事务中删除
                } else if (item->op_type == TXN_OP_SET || item->op_type == TXN_OP_MOD) {
                    return strdup(item->value); // 返回事务中的值
                }
            }
            item = item->next;
        }
    }
    
    // 从存储引擎中读取
#if ENABLE_RBTREE_KVENGINE
    if (engine) {
        rbtree_t *tree = (rbtree_t *)engine;
        return kvs_rbtree_get(tree, (char *)key);
    }
#endif
    
    return NULL;
}

// 在事务中删除键值
int txn_delete(kv_transaction_t *txn, const char *key) {
    if (!txn || !key || txn->status != TXN_STATUS_ACTIVE) return -1;
    
    void *engine = txn->engine;
    
    // 获取当前值
    char *current_value = NULL;
#if ENABLE_RBTREE_KVENGINE
    if (engine) {
        rbtree_t *tree = (rbtree_t *)engine;
        current_value = kvs_rbtree_get(tree, (char *)key);
    }
#endif
    
    txn_op_item_t *item = txn_add_op(txn, TXN_OP_DEL, key, NULL);
    if (!item) return -1;
    
    if (current_value) {
        item->old_value = strdup(current_value);
    }
    
    // 针对不同的隔离级别，可能需要立即执行或者延迟执行
    if (txn->isolation_level == TXN_ISOLATION_READ_UNCOMMITTED) {
        // 在读未提交级别下，立即执行操作
#if ENABLE_RBTREE_KVENGINE
        if (engine) {
            rbtree_t *tree = (rbtree_t *)engine;
            kvs_rbtree_delete(tree, (char *)key);
        }
#endif
    }
    
    return 0;
}

// 在事务中修改键值
int txn_modify(kv_transaction_t *txn, const char *key, const char *value) {
    if (!txn || !key || !value || txn->status != TXN_STATUS_ACTIVE) return -1;
    
    void *engine = txn->engine;
    
    // 获取当前值
    char *current_value = NULL;
#if ENABLE_RBTREE_KVENGINE
    if (engine) {
        rbtree_t *tree = (rbtree_t *)engine;
        current_value = kvs_rbtree_get(tree, (char *)key);
    }
#endif
    
    if (!current_value) {
        return -1; // 键不存在，无法修改
    }
    
    txn_op_item_t *item = txn_add_op(txn, TXN_OP_MOD, key, value);
    if (!item) return -1;
    
    item->old_value = strdup(current_value);
    
    // 针对不同的隔离级别，可能需要立即执行或者延迟执行
    if (txn->isolation_level == TXN_ISOLATION_READ_UNCOMMITTED) {
        // 在读未提交级别下，立即执行操作
#if ENABLE_RBTREE_KVENGINE
        if (engine) {
            rbtree_t *tree = (rbtree_t *)engine;
            kvs_rbtree_modify(tree, (char *)key, (char *)value);
        }
#endif
    }
    
    return 0;
}

// 提交事务
int txn_commit(kv_transaction_t *txn) {
    if (!txn || txn->status != TXN_STATUS_ACTIVE) return -1;
    
    void *engine = txn->engine;
    if (!engine) return -1;
    
    // 如果不是读未提交级别，需要在提交时执行操作
    if (txn->isolation_level != TXN_ISOLATION_READ_UNCOMMITTED) {
        txn_op_item_t *item = txn->op_list;
        while (item) {
#if ENABLE_RBTREE_KVENGINE
            rbtree_t *tree = (rbtree_t *)engine;
            
            switch (item->op_type) {
                case TXN_OP_SET:
                    kvs_rbtree_set(tree, item->key, item->value);
                    break;
                case TXN_OP_DEL:
                    kvs_rbtree_delete(tree, item->key);
                    break;
                case TXN_OP_MOD:
                    kvs_rbtree_modify(tree, item->key, item->value);
                    break;
            }
#endif
            item = item->next;
        }
    }
    
    txn->status = TXN_STATUS_COMMITTED;
    txn->end_time = time(NULL);
    
    return 0;
}

// 回滚事务
int txn_rollback(kv_transaction_t *txn) {
    if (!txn || txn->status != TXN_STATUS_ACTIVE) return -1;
    
    void *engine = txn->engine;
    if (!engine) return -1;
    
    // 如果是读未提交级别，需要回滚已经执行的操作
    if (txn->isolation_level == TXN_ISOLATION_READ_UNCOMMITTED) {
        // 反向遍历操作列表，撤销操作
        txn_op_item_t *items[txn->op_count];
        int idx = 0;
        
        txn_op_item_t *item = txn->op_list;
        while (item) {
            items[idx++] = item;
            item = item->next;
        }
        
        // 反向执行
        for (idx = txn->op_count - 1; idx >= 0; idx--) {
            item = items[idx];
            
#if ENABLE_RBTREE_KVENGINE
            rbtree_t *tree = (rbtree_t *)engine;
            
            switch (item->op_type) {
                case TXN_OP_SET:
                    if (item->old_value) {
                        // 恢复原值
                        kvs_rbtree_set(tree, item->key, item->old_value);
                    } else {
                        // 删除新增的键
                        kvs_rbtree_delete(tree, item->key);
                    }
                    break;
                case TXN_OP_DEL:
                    if (item->old_value) {
                        // 恢复删除的键
                        kvs_rbtree_set(tree, item->key, item->old_value);
                    }
                    break;
                case TXN_OP_MOD:
                    if (item->old_value) {
                        // 恢复修改前的值
                        kvs_rbtree_set(tree, item->key, item->old_value);
                    }
                    break;
            }
#endif
        }
    }
    
    txn->status = TXN_STATUS_ABORTED;
    txn->end_time = time(NULL);
    
    return 0;
}

// 释放事务资源
void txn_free(kv_transaction_t *txn) {
    if (!txn) return;
    
    // 释放操作列表
    txn_op_item_t *item = txn->op_list;
    while (item) {
        txn_op_item_t *next = item->next;
        
        if (item->key) free(item->key);
        if (item->value) free(item->value);
        if (item->old_value) free(item->old_value);
        free(item);
        
        item = next;
    }
    
    free(txn);
}

// 获取默认事务管理器
txn_manager_t *get_default_txn_manager() {
    static int initialized = 0;
    
    if (!initialized) {
        txn_manager_init(&g_txn_manager);
        initialized = 1;
    }
    
    return &g_txn_manager;
} 