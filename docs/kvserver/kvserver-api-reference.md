# ConcordKV 存储引擎 API 参考

本文档提供了ConcordKV存储引擎的API参考，包括核心存储、持久化、事务处理、并发控制和集群管理等模块的接口说明。

## 1. 核心存储API

ConcordKV存储引擎支持多种数据结构实现，每种实现都提供相同的基本接口。

### 1.1 通用接口

所有存储实现共享以下通用接口模式：

```c
// 创建存储实例
int kv_store_[implementation]_create(/* 实现特定参数 */);

// 销毁存储实例
void kv_store_[implementation]_destroy(/* 实现特定参数 */);

// 设置键值对
int kvs_[implementation]_set(/* 实现特定参数 */, char *key, char *value);

// 获取键值
char *kvs_[implementation]_get(/* 实现特定参数 */, char *key);

// 删除键值对
int kvs_[implementation]_delete(/* 实现特定参数 */, char *key);

// 修改键值
int kvs_[implementation]_modify(/* 实现特定参数 */, char *key, char *value);

// 获取键值对数量
int kvs_[implementation]_count(/* 实现特定参数 */);
```

### 1.2 数组实现API

```c
// 创建数组存储
int kv_store_array_create(array_t *arr);

// 销毁数组存储
void kv_store_array_destroy(array_t *arr);

// 设置键值对
int kvs_array_set(array_t *arr, char *key, char *value);

// 获取键值
char *kvs_array_get(array_t *arr, char *key);

// 删除键值对
int kvs_array_delete(array_t *arr, char *key);

// 修改键值
int kvs_array_modify(array_t *arr, char *key, char *value);

// 获取键值对数量
int kvs_array_count(array_t *arr);
```

### 1.3 红黑树实现API

```c
// 创建红黑树存储
int kv_store_rbtree_create(rbtree_t *tree);

// 销毁红黑树存储
void kv_store_rbtree_destroy(rbtree_t *tree);

// 设置键值对
int kvs_rbtree_set(rbtree_t *tree, char *key, char *value);

// 获取键值
char *kvs_rbtree_get(rbtree_t *tree, char *key);

// 删除键值对
int kvs_rbtree_delete(rbtree_t *tree, char *key);

// 修改键值
int kvs_rbtree_modify(rbtree_t *tree, char *key, char *value);

// 获取键值对数量
int kvs_rbtree_count(rbtree_t *tree);
```

### 1.4 哈希表实现API

```c
// 创建哈希表存储
int kv_store_hash_create(hashtable_t *hash);

// 销毁哈希表存储
void kv_store_hash_destroy(hashtable_t *hash);

// 设置键值对
int kvs_hash_set(hashtable_t *hash, char *key, char *value);

// 获取键值
char *kvs_hash_get(hashtable_t *hash, char *key);

// 删除键值对
int kvs_hash_delete(hashtable_t *hash, char *key);

// 修改键值
int kvs_hash_modify(hashtable_t *hash, char *key, char *value);

// 获取键值对数量
int kvs_hash_count(hashtable_t *hash);
```

## 2. 持久化API

### 2.1 WAL（预写日志）API

```c
// 初始化WAL
int wal_init(wal_t *wal, const char *log_dir, int sync_write);

// 销毁WAL
void wal_destroy(wal_t *wal);

// 追加日志条目
int wal_append(wal_t *wal, uint8_t op_type, const char *key, const char *value);

// 从日志恢复
int wal_recover(wal_t *wal, void *engine, 
                int (*apply_log)(void *engine, uint8_t op_type, const char *key, const char *value));
```

### 2.2 快照API

```c
// 初始化快照系统
int snapshot_init(snapshot_t *snap, const char *snap_dir);

// 销毁快照系统
void snapshot_destroy(snapshot_t *snap);

// 创建快照
int snapshot_create(snapshot_t *snap, void *engine, 
                   int (*save_data)(void *engine, FILE *fp));

// 加载快照
int snapshot_load(snapshot_t *snap, void *engine,
                 int (*load_data)(void *engine, FILE *fp));
```

### 2.3 持久化状态API

```c
// 初始化持久化状态
int persist_init(persist_state_t *ps, const char *data_dir, int sync_write);

// 销毁持久化状态
void persist_destroy(persist_state_t *ps);
```

## 3. 事务处理API

### 3.1 事务管理API

```c
// 初始化事务管理器
int txn_manager_init(txn_manager_t *manager);

// 销毁事务管理器
void txn_manager_destroy(txn_manager_t *manager);

// 开始事务
kv_transaction_t *txn_begin(txn_manager_t *manager, void *engine, uint8_t isolation_level);

// 释放事务资源
void txn_free(kv_transaction_t *txn);
```

### 3.2 事务操作API

```c
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
```

## 4. 并发控制API

### 4.1 锁管理API

```c
// 初始化读写锁
int rwlock_init(rwlock_t *lock);

// 获取读锁
int read_lock(rwlock_t *lock);

// 释放读锁
int read_unlock(rwlock_t *lock);

// 获取写锁
int write_lock(rwlock_t *lock);

// 释放写锁
int write_unlock(rwlock_t *lock);

// 销毁读写锁
void rwlock_destroy(rwlock_t *lock);
```

### 4.2 分段锁API

```c
// 初始化分段锁
int segment_lock_init(segment_lock_t *locks, int segment_count);

// 获取分段读锁
int segment_read_lock(segment_lock_t *locks, const char *key);

// 释放分段读锁
int segment_read_unlock(segment_lock_t *locks, const char *key);

// 获取分段写锁
int segment_write_lock(segment_lock_t *locks, const char *key);

// 释放分段写锁
int segment_write_unlock(segment_lock_t *locks, const char *key);

// 销毁分段锁
void segment_lock_destroy(segment_lock_t *locks);
```

## 5. 集群管理API

### 5.1 集群操作API

```c
// 初始化集群
int cluster_init(cluster_t *cluster, const char *cluster_id, int role, 
                const char *self_id, const char *self_host, int self_port, void *engine);

// 销毁集群
void cluster_destroy(cluster_t *cluster);

// 添加节点
int cluster_add_node(cluster_t *cluster, const char *node_id, const char *host, int port, int role);

// 删除节点
int cluster_remove_node(cluster_t *cluster, const char *node_id);

// 更新节点状态
int cluster_update_node_status(cluster_t *cluster, const char *node_id, int status);

// 集群健康检查
int cluster_health_check(cluster_t *cluster);
```

### 5.2 分片管理API

```c
// 添加分片
int cluster_add_shard(cluster_t *cluster, int shard_id, int primary_node);

// 添加分片副本
int cluster_add_shard_replica(cluster_t *cluster, int shard_id, int replica_node);

// 获取分片主节点
int cluster_get_shard_primary(cluster_t *cluster, int shard_id);

// 通过键获取分片ID
int cluster_get_shard_by_key(cluster_t *cluster, const char *key);

// 同步分片数据
int cluster_sync_shard(cluster_t *cluster, int shard_id, int source_node, int target_node);
```

### 5.3 Raft协议API

```c
// 开始选举
int cluster_start_election(cluster_t *cluster);

// 投票给候选人
int cluster_vote_for_candidate(cluster_t *cluster, const char *candidate_id, uint64_t term);

// 成为领导者
int cluster_become_leader(cluster_t *cluster);

// 发送心跳
int cluster_send_heartbeat(cluster_t *cluster);

// 处理收到的心跳
int cluster_process_heartbeat(cluster_t *cluster, const char *leader_id, uint64_t term);
```

## 6. 错误处理API

```c
// 获取错误码描述
const char *kv_error_string(int error_code);

// 设置错误信息
void kv_set_error(int error_code, const char *format, ...);

// 获取最后错误信息
const char *kv_get_last_error(void);

// 清除错误状态
void kv_clear_error(void);
```

## 7. 快照管理API

```c
// 创建快照
int kv_snapshot_create(const char *path, void *engine);

// 加载快照
int kv_snapshot_load(const char *path, void *engine);

// 列出可用快照
int kv_snapshot_list(const char *dir, char ***snapshots, int *count);

// 删除快照
int kv_snapshot_delete(const char *path);

// 验证快照完整性
int kv_snapshot_verify(const char *path);
```

## 8. 常量与宏定义

### 8.1 状态码

```c
// 成功状态码
#define KV_SUCCESS              0  

// 错误码
#define KV_ERROR_GENERIC       -1  // 通用错误
#define KV_ERROR_KEY_NOT_FOUND -2  // 键不存在
#define KV_ERROR_IO            -3  // IO错误
#define KV_ERROR_MEMORY        -4  // 内存分配错误
#define KV_ERROR_LOCK          -5  // 锁操作错误
#define KV_ERROR_TRANSACTION   -6  // 事务错误
#define KV_ERROR_CLUSTER       -7  // 集群操作错误
#define KV_ERROR_WAL           -8  // WAL操作错误
#define KV_ERROR_SNAPSHOT      -9  // 快照操作错误
```

### 8.2 事务隔离级别

```c
#define TXN_ISOLATION_READ_UNCOMMITTED 0   // 读未提交
#define TXN_ISOLATION_READ_COMMITTED   1   // 读已提交
#define TXN_ISOLATION_REPEATABLE_READ  2   // 可重复读
#define TXN_ISOLATION_SERIALIZABLE     3   // 串行化
```

### 8.3 节点状态与角色

```c
// 节点状态
#define NODE_STATUS_UNKNOWN    0   // 未知状态
#define NODE_STATUS_OFFLINE    1   // 离线
#define NODE_STATUS_ONLINE     2   // 在线
#define NODE_STATUS_JOINING    3   // 正在加入
#define NODE_STATUS_LEAVING    4   // 正在离开
#define NODE_STATUS_SYNCING    5   // 同步中

// 节点角色
#define NODE_ROLE_FOLLOWER     0   // 跟随者
#define NODE_ROLE_CANDIDATE    1   // 候选者
#define NODE_ROLE_LEADER       2   // 领导者
```

## 9. 数据结构

### 9.1 事务结构

```c
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
```

### 9.2 集群节点结构

```c
typedef struct {
    char node_id[64];          // 节点ID
    char host[64];             // 主机名或IP
    int port;                  // 端口
    int status;                // 节点状态
    int role;                  // 节点角色
    time_t last_heartbeat;     // 最后心跳时间
    uint64_t term;             // 当前任期
    int shard_count;           // 拥有的分片数量
    int *shards;               // 拥有的分片列表
} cluster_node_t;
```

### 9.3 WAL日志条目结构

```c
typedef struct {
    uint64_t seq;        // 序列号
    uint8_t op_type;     // 操作类型：SET/DEL/MOD
    uint32_t key_size;   // 键长度
    uint32_t value_size; // 值长度
    char *key;           // 键
    char *value;         // 值
} wal_entry_t;
```

## 10. 使用示例

### 10.1 基本操作示例

```c
#include "kv_store.h"

int main() {
    // 初始化红黑树存储
    rbtree_t tree;
    if (kv_store_rbtree_create(&tree) != 0) {
        fprintf(stderr, "Failed to create RB-tree storage\n");
        return 1;
    }
    
    // 设置键值对
    kvs_rbtree_set(&tree, "name", "ConcordKV");
    kvs_rbtree_set(&tree, "version", "1.0");
    
    // 获取值
    char *name = kvs_rbtree_get(&tree, "name");
    printf("Name: %s\n", name);
    
    // 修改值
    kvs_rbtree_modify(&tree, "version", "1.1");
    
    // 获取修改后的值
    char *version = kvs_rbtree_get(&tree, "version");
    printf("Version: %s\n", version);
    
    // 删除键值对
    kvs_rbtree_delete(&tree, "name");
    
    // 释放资源
    kv_store_rbtree_destroy(&tree);
    
    return 0;
}
```

### 10.2 事务处理示例

```c
#include "kv_store.h"
#include "kv_transaction.h"

int main() {
    // 初始化存储引擎
    rbtree_t tree;
    kv_store_rbtree_create(&tree);
    
    // 初始化事务管理器
    txn_manager_t txn_mgr;
    txn_manager_init(&txn_mgr);
    
    // 开始事务
    kv_transaction_t *txn = txn_begin(&txn_mgr, &tree, TXN_ISOLATION_REPEATABLE_READ);
    if (!txn) {
        fprintf(stderr, "Failed to begin transaction\n");
        return 1;
    }
    
    // 执行事务操作
    txn_set(txn, "user", "alice");
    txn_set(txn, "balance", "100");
    
    // 提交事务
    if (txn_commit(txn) != 0) {
        fprintf(stderr, "Failed to commit transaction\n");
        txn_rollback(txn);
    }
    
    // 释放事务资源
    txn_free(txn);
    txn_manager_destroy(&txn_mgr);
    kv_store_rbtree_destroy(&tree);
    
    return 0;
}
```

### 10.3 持久化示例

```c
#include "kv_store.h"
#include "kv_persist.h"

// 应用日志条目的回调函数
int apply_log_entry(void *engine, uint8_t op_type, const char *key, const char *value) {
    rbtree_t *tree = (rbtree_t *)engine;
    
    switch (op_type) {
        case WAL_OP_SET:
            return kvs_rbtree_set(tree, (char *)key, (char *)value);
        case WAL_OP_DEL:
            return kvs_rbtree_delete(tree, (char *)key);
        case WAL_OP_MOD:
            return kvs_rbtree_modify(tree, (char *)key, (char *)value);
        default:
            return -1;
    }
}

int main() {
    // 初始化存储引擎
    rbtree_t tree;
    kv_store_rbtree_create(&tree);
    
    // 初始化持久化状态
    persist_state_t ps;
    persist_init(&ps, "/tmp/concordkv-data", 1);
    
    // 添加数据并写入WAL
    kvs_rbtree_set(&tree, "key1", "value1");
    wal_append(&ps.wal, WAL_OP_SET, "key1", "value1");
    
    // 创建快照
    snapshot_create(&ps.snap, &tree, save_tree_data);
    
    // 模拟重启后恢复
    rbtree_t new_tree;
    kv_store_rbtree_create(&new_tree);
    
    // 加载快照
    snapshot_load(&ps.snap, &new_tree, load_tree_data);
    
    // 重放WAL日志
    wal_recover(&ps.wal, &new_tree, apply_log_entry);
    
    // 清理资源
    persist_destroy(&ps);
    kv_store_rbtree_destroy(&tree);
    kv_store_rbtree_destroy(&new_tree);
    
    return 0;
}
```

## 附录

### A. 错误码列表

详细的错误码列表和描述。

### B. 性能注意事项

各API在不同场景下的性能特性和优化建议。

### C. 线程安全性

各API的线程安全性说明和多线程使用注意事项。 