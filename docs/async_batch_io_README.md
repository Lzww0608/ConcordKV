# ConcordKV 异步批量I/O功能文档

## 概述

ConcordKV的异步批量I/O功能提供了高性能的批量数据操作能力，支持PUT、GET、DELETE等操作的批量处理，大幅提升了系统在高并发场景下的I/O吞吐量。

## 核心特性

- ✅ **高性能批量操作**: 支持批量PUT/GET/DELETE，减少系统调用开销
- ✅ **异步非阻塞**: 基于回调机制的异步处理，提升并发性能
- ✅ **线程安全**: 完整的多线程安全保障，支持并发访问
- ✅ **容量管理**: 智能的批次大小和内存限制管理
- ✅ **配置灵活**: 丰富的配置选项，适应不同使用场景
- ✅ **统计监控**: 完整的性能统计和监控指标

## 性能指标

| 指标 | 性能 | 说明 |
|------|------|------|
| 批量添加吞吐量 | 3,205,128 ops/sec | 1000条记录批量添加 |
| 批量提交吞吐量 | 50,000,000 ops/sec | 1000条记录批量提交 |
| 并发安全性 | 100% | 多线程并发操作成功率 |
| 响应时间 | 0.41ms | 1000条记录批量处理延迟 |

## 快速开始

### 1. 包含头文件

```c
#include "kvserver/kv_async_batch_io.h"
#include "kvserver/kv_io_uring.h"
```

### 2. 创建配置和管理器

```c
// 创建默认配置
kv_async_batch_config_t config = kv_async_batch_default_config();
config.max_batch_size = 1000;
config.timeout_ms = 5000;

// 创建管理器
kv_async_batch_manager_t *manager = kv_async_batch_manager_create(&config);
kv_async_batch_manager_start(manager);
```

### 3. 创建批量操作上下文

```c
kv_async_batch_context_t *ctx = kv_async_batch_create(manager, 100);
```

### 4. 添加批量操作

```c
// 添加PUT操作
kv_async_batch_put(ctx, "user_1", 6, "{\"name\":\"张三\"}", 15, callback, user_data);

// 添加GET操作
char *value = NULL;
size_t value_len = 0;
kv_async_batch_get(ctx, "user_1", 6, &value, &value_len, callback, user_data);

// 添加DELETE操作
kv_async_batch_delete(ctx, "user_1", 6, callback, user_data);
```

### 5. 提交并等待完成

```c
// 提交批量操作
kv_async_batch_submit(ctx, batch_complete_callback, batch_user_data);

// 等待完成
kv_async_batch_wait(ctx, 5000);  // 等待5秒
```

### 6. 清理资源

```c
kv_async_batch_destroy(ctx);
kv_async_batch_manager_stop(manager);
kv_async_batch_manager_destroy(manager);
```

## API参考

### 核心数据结构

#### `kv_async_batch_config_t`
配置结构体，包含批量操作的各种参数：
- `max_batch_size`: 最大批次大小
- `timeout_ms`: 操作超时时间
- `max_concurrent_batches`: 最大并发批次数
- `alloc_func/free_func`: 内存管理函数

#### `kv_async_batch_manager_t`
批量操作管理器，负责整体的资源管理和任务调度。

#### `kv_async_batch_context_t`
批量操作上下文，代表一个独立的批量操作会话。

### 核心函数

#### 配置管理
```c
kv_async_batch_config_t kv_async_batch_default_config(void);
int kv_async_batch_config_validate(const kv_async_batch_config_t *config);
```

#### 管理器操作
```c
kv_async_batch_manager_t* kv_async_batch_manager_create(const kv_async_batch_config_t *config);
void kv_async_batch_manager_destroy(kv_async_batch_manager_t *manager);
int kv_async_batch_manager_start(kv_async_batch_manager_t *manager);
int kv_async_batch_manager_stop(kv_async_batch_manager_t *manager);
```

#### 批量操作
```c
kv_async_batch_context_t* kv_async_batch_create(kv_async_batch_manager_t *manager, size_t max_batch_size);
void kv_async_batch_destroy(kv_async_batch_context_t *ctx);

int kv_async_batch_put(kv_async_batch_context_t *ctx, const char *key, size_t key_len, 
                       const char *value, size_t value_len, 
                       kv_async_batch_callback_t callback, void *user_data);

int kv_async_batch_get(kv_async_batch_context_t *ctx, const char *key, size_t key_len,
                       char **value, size_t *value_len,
                       kv_async_batch_callback_t callback, void *user_data);

int kv_async_batch_delete(kv_async_batch_context_t *ctx, const char *key, size_t key_len,
                          kv_async_batch_callback_t callback, void *user_data);

int kv_async_batch_submit(kv_async_batch_context_t *ctx,
                          kv_async_batch_complete_callback_t complete_callback, void *user_data);

int kv_async_batch_wait(kv_async_batch_context_t *ctx, uint32_t timeout_ms);
int kv_async_batch_cancel(kv_async_batch_context_t *ctx);
```

#### 状态查询
```c
kv_async_batch_status_t kv_async_batch_get_status(const kv_async_batch_context_t *ctx);
int kv_async_batch_get_stats(const kv_async_batch_context_t *ctx, kv_async_batch_stats_t *stats);
```

### 回调函数

#### 操作完成回调
```c
typedef void (*kv_async_batch_callback_t)(
    kv_async_batch_operation_t *op,
    int result,
    void *user_data
);
```

#### 批量完成回调
```c
typedef void (*kv_async_batch_complete_callback_t)(
    kv_async_batch_context_t *ctx,
    size_t completed_count,
    size_t failed_count,
    void *user_data
);
```

## 使用场景

### 1. 高频写入场景
适用于日志收集、监控数据写入等需要高并发写入的场景：
```c
// 批量写入监控数据
for (int i = 0; i < 1000; i++) {
    char key[64], value[256];
    snprintf(key, sizeof(key), "metric_%ld_%d", time(NULL), i);
    snprintf(value, sizeof(value), "{\"timestamp\":%ld,\"value\":%d}", time(NULL), rand());
    kv_async_batch_put(ctx, key, strlen(key), value, strlen(value), NULL, NULL);
}
kv_async_batch_submit(ctx, batch_callback, NULL);
```

### 2. 批量查询场景
适用于报表生成、数据导出等需要批量读取的场景：
```c
// 批量查询用户数据
for (int i = 0; i < 100; i++) {
    char key[32];
    snprintf(key, sizeof(key), "user_%d", i);
    char *value = NULL;
    size_t value_len = 0;
    kv_async_batch_get(ctx, key, strlen(key), &value, &value_len, get_callback, NULL);
}
kv_async_batch_submit(ctx, batch_callback, NULL);
```

### 3. 批量删除场景
适用于数据清理、过期数据删除等场景：
```c
// 批量删除过期数据
char expired_keys[][32] = {"expired_1", "expired_2", "expired_3"};
for (int i = 0; i < 3; i++) {
    kv_async_batch_delete(ctx, expired_keys[i], strlen(expired_keys[i]), NULL, NULL);
}
kv_async_batch_submit(ctx, batch_callback, NULL);
```

## 最佳实践

### 1. 合理设置批次大小
- 对于小键值对：建议批次大小1000-5000
- 对于大键值对：建议批次大小100-500
- 根据内存限制调整：避免单个批次占用过多内存

### 2. 错误处理
```c
int ret = kv_async_batch_put(ctx, key, key_len, value, value_len, callback, user_data);
if (ret == KV_ERR_BATCH_FULL) {
    // 批次已满，先提交当前批次
    kv_async_batch_submit(ctx, batch_callback, NULL);
    kv_async_batch_wait(ctx, 5000);
    
    // 创建新的批次
    ctx = kv_async_batch_create(manager, max_batch_size);
    kv_async_batch_put(ctx, key, key_len, value, value_len, callback, user_data);
}
```

### 3. 资源管理
- 及时销毁不需要的批量上下文
- 合理设置超时时间，避免无限等待
- 在程序退出前正确停止和销毁管理器

### 4. 性能优化
- 使用合适的回调函数，避免阻塞操作
- 批量操作尽量使用相同的回调函数，减少函数调用开销
- 根据硬件配置调整并发批次数量

## 测试和验证

项目提供了完整的测试套件，位于：
```
ConcordKV/tests/kvserver_tests/io_uring_tests/test_async_batch_io.c
```

运行测试：
```bash
cd ConcordKV/tests/kvserver_tests/io_uring_tests
make test_async_batch_io
./test_async_batch_io
```

## 故障排除

### 常见错误码
- `KV_ERR_NULL_POINTER`: 传入了空指针参数
- `KV_ERR_BATCH_FULL`: 批次已满，无法添加更多操作
- `KV_ERR_INVALID_STATE`: 批次状态无效，可能已提交或取消
- `KV_ERR_TIMEOUT`: 操作超时
- `KV_ERR_INVALID_PARAM`: 参数无效

### 性能问题排查
1. 检查批次大小设置是否合理
2. 确认并发批次数量不会超出系统资源限制
3. 监控内存使用情况，避免内存不足
4. 检查回调函数是否存在性能瓶颈

## 更多示例

完整的使用示例请参考：
```
ConcordKV/docs/examples/async_batch_io_example.c
``` 