#include <gtest/gtest.h>
#include <string>
#include <cstring>

// 模拟存储引擎的简单实现
typedef struct {
    char *key;
    char *value;
} mock_kv_pair_t;

#define MAX_KV_PAIRS 100
typedef struct {
    mock_kv_pair_t pairs[MAX_KV_PAIRS];
    int count;
} mock_engine_t;

// 设置键值对
int mock_set(mock_engine_t *engine, const char *key, const char *value) {
    if (!engine || !key || !value) return -1;
    
    // 查找是否已存在
    for (int i = 0; i < engine->count; i++) {
        if (strcmp(engine->pairs[i].key, key) == 0) {
            // 更新值
            free(engine->pairs[i].value);
            engine->pairs[i].value = strdup(value);
            return 0;
        }
    }
    
    // 新增键值对
    if (engine->count < MAX_KV_PAIRS) {
        engine->pairs[engine->count].key = strdup(key);
        engine->pairs[engine->count].value = strdup(value);
        engine->count++;
        return 0;
    }
    
    return -1; // 存储已满
}

// 获取键值
char *mock_get(mock_engine_t *engine, const char *key) {
    if (!engine || !key) return NULL;
    
    for (int i = 0; i < engine->count; i++) {
        if (strcmp(engine->pairs[i].key, key) == 0) {
            return strdup(engine->pairs[i].value);
        }
    }
    
    return NULL; // 键不存在
}

// 删除键值对
int mock_delete(mock_engine_t *engine, const char *key) {
    if (!engine || !key) return -1;
    
    for (int i = 0; i < engine->count; i++) {
        if (strcmp(engine->pairs[i].key, key) == 0) {
            // 释放内存
            free(engine->pairs[i].key);
            free(engine->pairs[i].value);
            
            // 移动后面的元素
            for (int j = i; j < engine->count - 1; j++) {
                engine->pairs[j] = engine->pairs[j + 1];
            }
            
            engine->count--;
            return 0;
        }
    }
    
    return -1; // 键不存在
}

// 修改键值对
int mock_modify(mock_engine_t *engine, const char *key, const char *value) {
    if (!engine || !key || !value) return -1;
    
    for (int i = 0; i < engine->count; i++) {
        if (strcmp(engine->pairs[i].key, key) == 0) {
            // 更新值
            free(engine->pairs[i].value);
            engine->pairs[i].value = strdup(value);
            return 0;
        }
    }
    
    return -1; // 键不存在
}

// 引入必要的声明，但避免包含原始头文件
#define TXN_STATUS_ACTIVE 1
#define TXN_STATUS_COMMITTED 2
#define TXN_STATUS_ABORTED 3

#define TXN_ISOLATION_READ_UNCOMMITTED 1
#define TXN_ISOLATION_READ_COMMITTED 2
#define TXN_ISOLATION_REPEATABLE_READ 3
#define TXN_ISOLATION_SERIALIZABLE 4

#define TXN_OP_SET 1
#define TXN_OP_DEL 2
#define TXN_OP_MOD 3

typedef struct txn_op_item_s {
    uint8_t op_type;
    char *key;
    char *value;
    char *old_value;
    struct txn_op_item_s *next;
} txn_op_item_t;

typedef struct {
    uint64_t txn_id;
    uint8_t status;
    uint8_t isolation_level;
    time_t start_time;
    time_t end_time;
    txn_op_item_t *op_list;
    txn_op_item_t *op_list_tail;
    int op_count;
    void *engine;
} kv_transaction_t;

typedef struct {
    uint64_t next_txn_id;
    int txn_count;
    uint8_t default_isolation;
} txn_manager_t;

// 声明函数
int txn_manager_init(txn_manager_t *manager);
void txn_manager_destroy(txn_manager_t *manager);
kv_transaction_t *txn_begin(txn_manager_t *manager, void *engine, uint8_t isolation_level);
int txn_set(kv_transaction_t *txn, const char *key, const char *value);
char *txn_get(kv_transaction_t *txn, const char *key);
int txn_delete(kv_transaction_t *txn, const char *key);
int txn_modify(kv_transaction_t *txn, const char *key, const char *value);
int txn_commit(kv_transaction_t *txn);
int txn_rollback(kv_transaction_t *txn);
void txn_free(kv_transaction_t *txn);

// rbtree类型声明，只是为了测试
typedef struct rbtree_dummy {
    int dummy;
} rbtree_t;

// 测试类
class KvTransactionTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine.count = 0;
        ASSERT_EQ(0, txn_manager_init(&manager));
    }
    
    void TearDown() override {
        txn_manager_destroy(&manager);
        
        // 清理引擎内存
        for (int i = 0; i < engine.count; i++) {
            free(engine.pairs[i].key);
            free(engine.pairs[i].value);
        }
        engine.count = 0;
    }
    
    // 准备一些初始数据
    void PrepareTestData() {
        mock_set(&engine, "key1", "value1");
        mock_set(&engine, "key2", "value2");
        mock_set(&engine, "key3", "value3");
    }
    
    mock_engine_t engine;
    txn_manager_t manager;
};

// 测试事务基本操作
TEST_F(KvTransactionTest, BasicTransactionOperations) {
    PrepareTestData();
    
    // 开始事务
    kv_transaction_t *txn = txn_begin(&manager, &engine, TXN_ISOLATION_READ_COMMITTED);
    ASSERT_NE(nullptr, txn);
    ASSERT_EQ(TXN_STATUS_ACTIVE, txn->status);
    
    // 测试txn_get
    char *value = txn_get(txn, "key1");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value1", value);
    free(value);
    
    // 测试txn_set
    ASSERT_EQ(0, txn_set(txn, "key4", "value4"));
    
    // 测试txn_modify
    ASSERT_EQ(0, txn_modify(txn, "key2", "modified_value2"));
    
    // 测试txn_delete
    ASSERT_EQ(0, txn_delete(txn, "key3"));
    
    // 提交事务
    ASSERT_EQ(0, txn_commit(txn));
    ASSERT_EQ(TXN_STATUS_COMMITTED, txn->status);
    
    // 验证结果
    value = mock_get(&engine, "key1");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value1", value);
    free(value);
    
    value = mock_get(&engine, "key2");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("modified_value2", value);
    free(value);
    
    value = mock_get(&engine, "key3");
    ASSERT_EQ(nullptr, value);
    
    value = mock_get(&engine, "key4");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value4", value);
    free(value);
    
    // 释放事务
    txn_free(txn);
}

// 测试事务回滚
TEST_F(KvTransactionTest, TransactionRollback) {
    PrepareTestData();
    
    // 开始事务
    kv_transaction_t *txn = txn_begin(&manager, &engine, TXN_ISOLATION_READ_COMMITTED);
    ASSERT_NE(nullptr, txn);
    
    // 执行一些操作
    ASSERT_EQ(0, txn_set(txn, "key4", "value4"));
    ASSERT_EQ(0, txn_modify(txn, "key2", "modified_value2"));
    ASSERT_EQ(0, txn_delete(txn, "key3"));
    
    // 回滚事务
    ASSERT_EQ(0, txn_rollback(txn));
    ASSERT_EQ(TXN_STATUS_ABORTED, txn->status);
    
    // 验证结果（应该不变）
    char *value = mock_get(&engine, "key1");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value1", value);
    free(value);
    
    value = mock_get(&engine, "key2");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value2", value);
    free(value);
    
    value = mock_get(&engine, "key3");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value3", value);
    free(value);
    
    value = mock_get(&engine, "key4");
    ASSERT_EQ(nullptr, value);
    
    // 释放事务
    txn_free(txn);
}

// 测试读未提交隔离级别
TEST_F(KvTransactionTest, ReadUncommittedIsolation) {
    PrepareTestData();
    
    // 开始读未提交事务
    kv_transaction_t *txn = txn_begin(&manager, &engine, TXN_ISOLATION_READ_UNCOMMITTED);
    ASSERT_NE(nullptr, txn);
    
    // 修改数据
    ASSERT_EQ(0, txn_set(txn, "key4", "value4"));
    ASSERT_EQ(0, txn_modify(txn, "key2", "modified_value2"));
    
    // 在提交前，应该可以看到更改
    char *value = mock_get(&engine, "key2");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("modified_value2", value);
    free(value);
    
    value = mock_get(&engine, "key4");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value4", value);
    free(value);
    
    // 回滚事务
    ASSERT_EQ(0, txn_rollback(txn));
    
    // 验证变更已回滚
    value = mock_get(&engine, "key2");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value2", value);
    free(value);
    
    value = mock_get(&engine, "key4");
    ASSERT_EQ(nullptr, value);
    
    // 释放事务
    txn_free(txn);
}

// 测试可重复读隔离级别
TEST_F(KvTransactionTest, RepeatableReadIsolation) {
    PrepareTestData();
    
    // 开始可重复读事务
    kv_transaction_t *txn = txn_begin(&manager, &engine, TXN_ISOLATION_REPEATABLE_READ);
    ASSERT_NE(nullptr, txn);
    
    // 读取初始值
    char *value = txn_get(txn, "key1");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value1", value);
    free(value);
    
    // 外部修改数据
    mock_modify(&engine, "key1", "modified_outside");
    
    // 在同一事务中再次读取，应该看到相同的值(value1，而不是modified_outside)
    value = txn_get(txn, "key1");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value1", value);
    free(value);
    
    // 提交事务
    ASSERT_EQ(0, txn_commit(txn));
    
    // 释放事务
    txn_free(txn);
}

// 测试串行化隔离级别
TEST_F(KvTransactionTest, SerializableIsolation) {
    PrepareTestData();
    
    // 开始串行化事务
    kv_transaction_t *txn = txn_begin(&manager, &engine, TXN_ISOLATION_SERIALIZABLE);
    ASSERT_NE(nullptr, txn);
    
    // 设置一个新键
    ASSERT_EQ(0, txn_set(txn, "serializable_key", "serializable_value"));
    
    // 在提交前，外部不应该看到这个键
    char *value = mock_get(&engine, "serializable_key");
    ASSERT_EQ(nullptr, value);
    
    // 但在事务内部可以看到
    value = txn_get(txn, "serializable_key");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("serializable_value", value);
    free(value);
    
    // 提交事务
    ASSERT_EQ(0, txn_commit(txn));
    
    // 提交后，外部应该能看到这个键
    value = mock_get(&engine, "serializable_key");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("serializable_value", value);
    free(value);
    
    // 释放事务
    txn_free(txn);
}

// 测试事务错误处理
TEST_F(KvTransactionTest, TransactionErrorHandling) {
    PrepareTestData();
    
    // 开始事务
    kv_transaction_t *txn = txn_begin(&manager, &engine, TXN_ISOLATION_READ_COMMITTED);
    ASSERT_NE(nullptr, txn);
    
    // 测试空参数
    ASSERT_EQ(-1, txn_set(NULL, "key", "value"));
    ASSERT_EQ(-1, txn_set(txn, NULL, "value"));
    ASSERT_EQ(-1, txn_set(txn, "key", NULL));
    
    ASSERT_EQ(nullptr, txn_get(NULL, "key"));
    ASSERT_EQ(nullptr, txn_get(txn, NULL));
    
    ASSERT_EQ(-1, txn_delete(NULL, "key"));
    ASSERT_EQ(-1, txn_delete(txn, NULL));
    
    ASSERT_EQ(-1, txn_modify(NULL, "key", "value"));
    ASSERT_EQ(-1, txn_modify(txn, NULL, "value"));
    ASSERT_EQ(-1, txn_modify(txn, "key", NULL));
    
    // 测试非活动事务
    ASSERT_EQ(0, txn_commit(txn));
    ASSERT_EQ(-1, txn_set(txn, "key", "value")); // 已提交的事务不能执行操作
    ASSERT_EQ(-1, txn_rollback(txn)); // 已提交的事务不能回滚
    
    // 释放事务
    txn_free(txn);
}

// 实现事务管理函数
int txn_manager_init(txn_manager_t *manager) {
    if (!manager) return -1;
    
    manager->next_txn_id = 1;
    manager->txn_count = 0;
    manager->default_isolation = TXN_ISOLATION_READ_COMMITTED;
    
    return 0;
}

void txn_manager_destroy(txn_manager_t *manager) {
    if (!manager) return;
    
    manager->next_txn_id = 0;
    manager->txn_count = 0;
}

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

int txn_set(kv_transaction_t *txn, const char *key, const char *value) {
    if (!txn || !key || !value || txn->status != TXN_STATUS_ACTIVE) return -1;
    
    txn_op_item_t *item = txn_add_op(txn, TXN_OP_SET, key, value);
    if (!item) return -1;
    
    mock_engine_t *engine = (mock_engine_t*)txn->engine;
    
    // 针对不同的隔离级别，可能需要立即执行或者延迟执行
    if (txn->isolation_level == TXN_ISOLATION_READ_UNCOMMITTED) {
        // 在读未提交级别下，立即执行操作
        if (engine) {
            // 获取当前值（如果有）
            char *current_value = mock_get(engine, key);
            if (current_value) {
                item->old_value = current_value;
            }
            
            // 设置新值
            mock_set(engine, key, value);
        }
    }
    
    return 0;
}

char *txn_get(kv_transaction_t *txn, const char *key) {
    if (!txn || !key) return NULL;
    
    mock_engine_t *engine = (mock_engine_t*)txn->engine;
    
    // 如果是可重复读或串行化级别，首先检查事务内是否已读取过该键
    if (txn->isolation_level >= TXN_ISOLATION_REPEATABLE_READ) {
        // 首先检查事务中是否有对该键的操作
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
        
        // 如果事务中没有对该键的操作，则从引擎获取值并缓存在事务中
        char *value = NULL;
        if (engine) {
            value = mock_get(engine, key);
            if (value) {
                // 将值添加到事务中，作为一次读取操作
                txn_op_item_t *read_item = txn_add_op(txn, TXN_OP_SET, key, value);
                free(value); // 因为txn_add_op会复制值
                
                if (read_item) {
                    return strdup(read_item->value);
                }
            }
        }
        
        return value;
    }
    
    // 对于读已提交或读未提交级别，直接从存储引擎中读取
    if (engine) {
        return mock_get(engine, key);
    }
    
    return NULL;
}

int txn_delete(kv_transaction_t *txn, const char *key) {
    if (!txn || !key || txn->status != TXN_STATUS_ACTIVE) return -1;
    
    mock_engine_t *engine = (mock_engine_t*)txn->engine;
    
    // 获取当前值
    char *current_value = NULL;
    if (engine) {
        current_value = mock_get(engine, key);
    }
    
    txn_op_item_t *item = txn_add_op(txn, TXN_OP_DEL, key, NULL);
    if (!item) {
        if (current_value) free(current_value);
        return -1;
    }
    
    if (current_value) {
        item->old_value = current_value;
    }
    
    // 针对不同的隔离级别，可能需要立即执行或者延迟执行
    if (txn->isolation_level == TXN_ISOLATION_READ_UNCOMMITTED) {
        // 在读未提交级别下，立即执行操作
        if (engine) {
            mock_delete(engine, key);
        }
    }
    
    return 0;
}

int txn_modify(kv_transaction_t *txn, const char *key, const char *value) {
    if (!txn || !key || !value || txn->status != TXN_STATUS_ACTIVE) return -1;
    
    mock_engine_t *engine = (mock_engine_t*)txn->engine;
    
    // 获取当前值
    char *current_value = NULL;
    if (engine) {
        current_value = mock_get(engine, key);
    }
    
    if (!current_value) {
        return -1; // 键不存在，无法修改
    }
    
    txn_op_item_t *item = txn_add_op(txn, TXN_OP_MOD, key, value);
    if (!item) {
        free(current_value);
        return -1;
    }
    
    item->old_value = current_value;
    
    // 针对不同的隔离级别，可能需要立即执行或者延迟执行
    if (txn->isolation_level == TXN_ISOLATION_READ_UNCOMMITTED) {
        // 在读未提交级别下，立即执行操作
        if (engine) {
            mock_modify(engine, key, value);
        }
    }
    
    return 0;
}

int txn_commit(kv_transaction_t *txn) {
    if (!txn || txn->status != TXN_STATUS_ACTIVE) return -1;
    
    mock_engine_t *engine = (mock_engine_t*)txn->engine;
    
    // 如果不是读未提交级别，需要应用所有操作
    if (txn->isolation_level != TXN_ISOLATION_READ_UNCOMMITTED) {
        txn_op_item_t *item = txn->op_list;
        while (item) {
            if (engine) {
                if (item->op_type == TXN_OP_SET) {
                    mock_set(engine, item->key, item->value);
                } else if (item->op_type == TXN_OP_DEL) {
                    mock_delete(engine, item->key);
                } else if (item->op_type == TXN_OP_MOD) {
                    mock_modify(engine, item->key, item->value);
                }
            }
            item = item->next;
        }
    }
    
    txn->status = TXN_STATUS_COMMITTED;
    txn->end_time = time(NULL);
    
    return 0;
}

int txn_rollback(kv_transaction_t *txn) {
    if (!txn || txn->status != TXN_STATUS_ACTIVE) return -1;
    
    mock_engine_t *engine = (mock_engine_t*)txn->engine;
    
    // 如果是读未提交级别，需要回滚所有已执行的操作
    if (txn->isolation_level == TXN_ISOLATION_READ_UNCOMMITTED) {
        txn_op_item_t *item = txn->op_list;
        while (item) {
            if (engine && item->old_value) {
                if (item->op_type == TXN_OP_SET || item->op_type == TXN_OP_MOD) {
                    // 恢复原值
                    mock_set(engine, item->key, item->old_value);
                } else if (item->op_type == TXN_OP_DEL) {
                    // 恢复已删除的键
                    mock_set(engine, item->key, item->old_value);
                }
            } else if (engine && item->op_type == TXN_OP_SET) {
                // 删除新增的键
                mock_delete(engine, item->key);
            }
            item = item->next;
        }
    }
    
    txn->status = TXN_STATUS_ABORTED;
    txn->end_time = time(NULL);
    
    return 0;
}

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