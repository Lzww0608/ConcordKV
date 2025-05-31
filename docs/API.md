# ConcordKV API 文档

本文档提供ConcordKV分布式键值存储系统的完整API参考。

## 目录

- [统一引擎接口](#统一引擎接口)
- [存储引擎](#存储引擎)
- [事务支持](#事务支持)
- [错误处理](#错误处理)
- [示例代码](#示例代码)

## 统一引擎接口

ConcordKV提供统一的存储引擎接口，允许应用程序透明地使用不同的存储引擎。

### 头文件

```c
#include "kv_engine_interface.h"
#include "kv_engine_factory.h"
```

### 引擎类型

```c
typedef enum {
    KV_ENGINE_ARRAY = 0,    // Array存储引擎
    KV_ENGINE_HASH = 1,     // Hash存储引擎  
    KV_ENGINE_RBTREE = 2,   // RBTree存储引擎
    KV_ENGINE_BTREE = 3     // B+Tree存储引擎 ⭐ 推荐
} kv_engine_type_t;
```

### 核心接口

#### 1. 引擎创建和销毁

```c
/**
 * 创建存储引擎实例
 * @param type 引擎类型
 * @return 引擎实例指针，失败返回NULL
 */
kv_engine_t* kv_engine_factory_create(kv_engine_type_t type);

/**
 * 销毁存储引擎实例
 * @param engine 引擎实例指针
 */
void kv_engine_factory_destroy(kv_engine_t *engine);
```

#### 2. 生命周期管理

```c
/**
 * 初始化引擎
 * @param engine 引擎实例
 * @param config 配置参数，可以为NULL使用默认配置
 * @return 0成功，负数失败
 */
int (*init)(struct kv_engine *engine, const char *config);

/**
 * 销毁引擎并释放资源
 * @param engine 引擎实例
 */
void (*destroy)(struct kv_engine *engine);
```

#### 3. 基础CRUD操作

```c
/**
 * 设置键值对
 * @param engine 引擎实例
 * @param key 键
 * @param value 值
 * @return 0成功，负数失败
 */
int (*set)(struct kv_engine *engine, const char *key, const char *value);

/**
 * 获取键对应的值
 * @param engine 引擎实例
 * @param key 键
 * @return 值的拷贝，需要调用者释放；未找到返回NULL
 */
char* (*get)(struct kv_engine *engine, const char *key);

/**
 * 删除键值对
 * @param engine 引擎实例
 * @param key 键
 * @return 0成功，负数失败
 */
int (*delete)(struct kv_engine *engine, const char *key);

/**
 * 更新键的值
 * @param engine 引擎实例
 * @param key 键
 * @param value 新值
 * @return 0成功，负数失败
 */
int (*update)(struct kv_engine *engine, const char *key, const char *value);
```

#### 4. 统计信息

```c
/**
 * 获取键数量
 * @param engine 引擎实例
 * @return 键的数量
 */
size_t (*count)(struct kv_engine *engine);
```

## 存储引擎

### B+Tree存储引擎 ⭐ 推荐

B+Tree是ConcordKV的主要存储引擎，旨在提供良好的性能和并发安全性。

#### 特性

- **高并发性能**: 832,484 ops/sec (8线程)
- **内存高效**: 仅4-5%额外开销
- **可靠线程安全**: 稳定的并发控制
- **支持范围查询**: 叶子节点链表结构
- **可配置阶数**: 3-1000，默认100

#### 使用示例

```c
#include "kv_engine_interface.h"
#include "kv_engine_factory.h"

int main() {
    // 创建B+Tree存储引擎
    kv_engine_t *engine = kv_engine_factory_create(KV_ENGINE_BTREE);
    if (!engine) {
        fprintf(stderr, "创建B+Tree引擎失败\n");
        return -1;
    }
    
    // 初始化引擎
    if (engine->init(engine, NULL) != 0) {
        fprintf(stderr, "初始化引擎失败\n");
        kv_engine_factory_destroy(engine);
        return -1;
    }
    
    // 存储数据
    engine->set(engine, "user:1001", "张三");
    engine->set(engine, "user:1002", "李四");
    engine->set(engine, "user:1003", "王五");
    
    // 读取数据
    char *value = engine->get(engine, "user:1001");
    if (value) {
        printf("用户信息: %s\n", value);
        free(value);
    }
    
    // 更新数据
    engine->update(engine, "user:1001", "张三(已更新)");
    
    // 统计信息
    size_t count = engine->count(engine);
    printf("总用户数: %zu\n", count);
    
    // 删除数据
    engine->delete(engine, "user:1003");
    
    // 清理资源
    engine->destroy(engine);
    kv_engine_factory_destroy(engine);
    
    return 0;
}
```

#### 配置参数

B+Tree引擎支持以下配置参数：

```c
// 创建指定阶数的B+Tree
btree_t *tree = kv_store_btree_create(order);  // order: 3-1000

// 常用阶数建议
// - 小数据集 (< 1K keys): order = 10-50
// - 中等数据集 (1K-100K keys): order = 50-200  
// - 大数据集 (> 100K keys): order = 100-500
```

### Hash存储引擎

高性能哈希表实现，适用于需要快速查找的场景。

#### 特性

- **高写入性能**: 1,460,000 ops/sec
- **O(1)查找复杂度**: 平均情况下
- **动态扩展**: 自动调整哈希表大小
- **碰撞处理**: 链式法处理哈希冲突

#### 使用示例

```c
kv_engine_t *engine = kv_engine_factory_create(KV_ENGINE_HASH);
engine->init(engine, NULL);

// 高频写入场景
for (int i = 0; i < 1000000; i++) {
    char key[32], value[64];
    snprintf(key, sizeof(key), "key_%d", i);
    snprintf(value, sizeof(value), "value_%d", i);
    engine->set(engine, key, value);
}

engine->destroy(engine);
kv_engine_factory_destroy(engine);
```

### RBTree存储引擎

红黑树实现，提供有序存储和O(log n)复杂度。

#### 特性

- **有序存储**: 键按字典序排列
- **平衡树结构**: 保证O(log n)操作复杂度
- **内存效率**: 8-12%额外开销
- **线程安全**: 可靠的并发控制

#### 使用示例

```c
kv_engine_t *engine = kv_engine_factory_create(KV_ENGINE_RBTREE);
engine->init(engine, NULL);

// 需要有序访问的场景
engine->set(engine, "apple", "苹果");
engine->set(engine, "banana", "香蕉");  
engine->set(engine, "cherry", "樱桃");

// 获取按字典序排列的数据
char *value = engine->get(engine, "banana");
printf("结果: %s\n", value);
free(value);

engine->destroy(engine);
kv_engine_factory_destroy(engine);
```

### Array存储引擎

基于动态数组的简单实现，适用于原型开发和小数据集。

#### 特性

- **简单实现**: 线性搜索
- **零内存开销**: 无额外数据结构
- **高写入性能**: 2,000,000 ops/sec  
- **适合小数据**: 推荐< 1000个键

#### 使用示例

```c
kv_engine_t *engine = kv_engine_factory_create(KV_ENGINE_ARRAY);
engine->init(engine, NULL);

// 原型开发或小数据集
engine->set(engine, "config_timeout", "30");
engine->set(engine, "config_retries", "3");

char *timeout = engine->get(engine, "config_timeout");
printf("超时时间: %s秒\n", timeout);
free(timeout);

engine->destroy(engine);
kv_engine_factory_destroy(engine);
```

## 事务支持

ConcordKV提供完整的ACID事务支持。

### 事务API

```c
/**
 * 开始事务
 * @param engine 存储引擎
 * @return 事务句柄，失败返回NULL
 */
kv_transaction_t* kv_transaction_begin(kv_engine_t *engine);

/**
 * 提交事务
 * @param tx 事务句柄
 * @return 0成功，负数失败
 */
int kv_transaction_commit(kv_transaction_t *tx);

/**
 * 回滚事务
 * @param tx 事务句柄
 * @return 0成功，负数失败
 */
int kv_transaction_rollback(kv_transaction_t *tx);

/**
 * 在事务中设置键值对
 * @param tx 事务句柄
 * @param key 键
 * @param value 值
 * @return 0成功，负数失败
 */
int kv_transaction_set(kv_transaction_t *tx, const char *key, const char *value);
```

### 事务示例

```c
kv_engine_t *engine = kv_engine_factory_create(KV_ENGINE_BTREE);
engine->init(engine, NULL);

// 银行转账事务示例
kv_transaction_t *tx = kv_transaction_begin(engine);

// 从账户A扣除100元
kv_transaction_set(tx, "account:A:balance", "900");

// 向账户B增加100元  
kv_transaction_set(tx, "account:B:balance", "1100");

// 记录转账日志
kv_transaction_set(tx, "transfer:log:001", "A->B:100");

// 提交事务
if (kv_transaction_commit(tx) == 0) {
    printf("转账成功\n");
} else {
    printf("转账失败，已回滚\n");
}

engine->destroy(engine);
kv_engine_factory_destroy(engine);
```

## 错误处理

### 错误代码

```c
typedef enum {
    KV_SUCCESS = 0,           // 成功
    KV_ERROR_NULL_POINTER,    // 空指针错误
    KV_ERROR_KEY_NOT_FOUND,   // 键未找到
    KV_ERROR_MEMORY_ALLOC,    // 内存分配失败
    KV_ERROR_INVALID_PARAM,   // 无效参数
    KV_ERROR_LOCK_FAILED,     // 锁操作失败
    KV_ERROR_TRANSACTION,     // 事务错误
    KV_ERROR_IO,              // I/O错误
    KV_ERROR_CORRUPTION       // 数据损坏
} kv_error_code_t;
```

### 错误处理函数

```c
/**
 * 获取错误消息
 * @param error_code 错误代码
 * @return 错误描述字符串
 */
const char* kv_error_message(kv_error_code_t error_code);

/**
 * 获取最后一次错误
 * @return 最后一次错误代码
 */
kv_error_code_t kv_get_last_error(void);
```

### 错误处理示例

```c
kv_engine_t *engine = kv_engine_factory_create(KV_ENGINE_BTREE);

if (engine->set(engine, "key1", "value1") != 0) {
    kv_error_code_t error = kv_get_last_error();
    fprintf(stderr, "设置失败: %s\n", kv_error_message(error));
}

char *value = engine->get(engine, "nonexistent_key");
if (!value) {
    kv_error_code_t error = kv_get_last_error();
    if (error == KV_ERROR_KEY_NOT_FOUND) {
        printf("键不存在\n");
    } else {
        fprintf(stderr, "获取失败: %s\n", kv_error_message(error));
    }
}
```

## 性能最佳实践

### 1. 引擎选择指南

| 场景 | 推荐引擎 | 理由 |
|------|----------|------|
| 生产环境 | B+Tree | 良好的综合性能和并发安全性 |
| 高频写入 | Hash | 很高的写入吞吐量 |
| 有序访问 | RBTree | 保持键的有序性 |
| 原型开发 | Array | 简单易用，零配置 |

### 2. 性能优化技巧

```c
// 1. 批量操作比单个操作更高效
const char *keys[] = {"key1", "key2", "key3"};
const char *values[] = {"val1", "val2", "val3"};
// engine->batch_set(engine, keys, values, 3);  // 暂未实现

// 2. 重复使用引擎实例
kv_engine_t *engine = kv_engine_factory_create(KV_ENGINE_BTREE);
engine->init(engine, NULL);
// ... 进行多次操作 ...
engine->destroy(engine);  // 最后统一销毁

// 3. 适当的键命名策略
engine->set(engine, "user:1001:name", "张三");     // 好的命名
engine->set(engine, "user:1001:email", "zhang@example.com");

// 4. 预分配合适的阶数（B+Tree）
btree_t *tree = kv_store_btree_create(200);  // 大数据集使用较大阶数
```

### 3. 内存管理

```c
// 记住释放get操作返回的内存
char *value = engine->get(engine, "key1");
if (value) {
    printf("值: %s\n", value);
    free(value);  // 重要：必须释放
}

// 正确的资源清理顺序
engine->destroy(engine);           // 先销毁引擎内容
kv_engine_factory_destroy(engine); // 再销毁引擎实例
```

## 示例代码

### 完整的Web应用后端示例

```c
#include "kv_engine_interface.h"
#include "kv_engine_factory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 用户会话管理系统
typedef struct {
    kv_engine_t *engine;
} session_manager_t;

session_manager_t* session_manager_create() {
    session_manager_t *mgr = malloc(sizeof(session_manager_t));
    mgr->engine = kv_engine_factory_create(KV_ENGINE_BTREE);
    mgr->engine->init(mgr->engine, NULL);
    return mgr;
}

int session_create(session_manager_t *mgr, const char *session_id, const char *user_id) {
    char key[256];
    snprintf(key, sizeof(key), "session:%s", session_id);
    return mgr->engine->set(mgr->engine, key, user_id);
}

char* session_get_user(session_manager_t *mgr, const char *session_id) {
    char key[256];
    snprintf(key, sizeof(key), "session:%s", session_id);
    return mgr->engine->get(mgr->engine, key);
}

int session_delete(session_manager_t *mgr, const char *session_id) {
    char key[256];
    snprintf(key, sizeof(key), "session:%s", session_id);
    return mgr->engine->delete(mgr->engine, key);
}

void session_manager_destroy(session_manager_t *mgr) {
    mgr->engine->destroy(mgr->engine);
    kv_engine_factory_destroy(mgr->engine);
    free(mgr);
}

int main() {
    session_manager_t *mgr = session_manager_create();
    
    // 创建会话
    session_create(mgr, "sess_abc123", "user_1001");
    session_create(mgr, "sess_def456", "user_1002");
    
    // 验证会话
    char *user_id = session_get_user(mgr, "sess_abc123");
    if (user_id) {
        printf("会话sess_abc123属于用户: %s\n", user_id);
        free(user_id);
    }
    
    // 注销会话
    session_delete(mgr, "sess_abc123");
    
    session_manager_destroy(mgr);
    return 0;
}
```

### 配置管理示例

```c
#include "kv_engine_interface.h"
#include "kv_engine_factory.h"

typedef struct {
    kv_engine_t *engine;
} config_manager_t;

config_manager_t* config_manager_create() {
    config_manager_t *mgr = malloc(sizeof(config_manager_t));
    mgr->engine = kv_engine_factory_create(KV_ENGINE_ARRAY);  // 配置项较少，使用Array
    mgr->engine->init(mgr->engine, NULL);
    
    // 设置默认配置
    mgr->engine->set(mgr->engine, "server.port", "8080");
    mgr->engine->set(mgr->engine, "server.timeout", "30");
    mgr->engine->set(mgr->engine, "db.pool_size", "10");
    
    return mgr;
}

int config_get_int(config_manager_t *mgr, const char *key, int default_value) {
    char *value = mgr->engine->get(mgr->engine, key);
    if (value) {
        int result = atoi(value);
        free(value);
        return result;
    }
    return default_value;
}

int main() {
    config_manager_t *mgr = config_manager_create();
    
    int port = config_get_int(mgr, "server.port", 8080);
    int timeout = config_get_int(mgr, "server.timeout", 30);
    
    printf("服务器端口: %d\n", port);
    printf("超时时间: %d秒\n", timeout);
    
    // 清理
    mgr->engine->destroy(mgr->engine);
    kv_engine_factory_destroy(mgr->engine);
    free(mgr);
    
    return 0;
}
```

---

更多详细信息请参阅：
- [架构设计文档](ARCHITECTURE.md)
- [开发路线图](ROADMAP.md)
- [部署指南](DEPLOYMENT.md) 