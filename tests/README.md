# ConcordKV 测试系统

## 概述

ConcordKV 测试系统提供了全面的存储引擎接口测试，包含超时机制防止死锁，确保测试的可靠性和效率。

## 目录结构

```
tests/
├── Makefile                     # 测试系统构建文件
├── README.md                    # 本说明文档
└── engine_tests/
    ├── kv_engine_test_enhanced.c    # 增强版测试（包含超时机制）
    └── kv_engine_test_original.c    # 原版测试（编译时自动复制）
```

## 功能特性

### 🔒 超时机制
- **普通测试超时**: 30秒
- **压力测试超时**: 60秒
- **信号处理**: 使用 SIGALRM 和 setjmp/longjmp 实现
- **死锁预防**: 自动终止超时的测试用例

### 🧪 测试覆盖

#### 基础功能测试
- ✅ 工厂函数测试（创建/销毁引擎）
- ✅ CRUD操作测试（Set/Get/Delete/Update）
- ✅ 参数验证测试
- ✅ 统计信息测试

#### 已实现引擎测试
- ✅ Array 存储引擎
- ✅ RBTree 存储引擎  
- ✅ Hash 存储引擎
- ⏳ BTree 存储引擎（待实现）
- ⏳ LSM 存储引擎（待实现）

#### 高级测试
- 🔄 **并发安全测试**: 8线程并发读写，每线程100操作
- 💾 **内存泄漏测试**: 1000次创建/销毁循环
- ⚡ **压力测试**: 10,000个键值对的大数据量测试
- 📊 **性能测试**: 吞吐量和延迟统计

### 📊 测试报告

测试系统提供详细的统计报告：
- 总测试数量
- 通过/失败/超时测试数量
- 执行时间统计
- 成功率计算
- 性能指标（ops/sec）

## 使用方法

### 基础编译和测试

```bash
# 编译所有测试程序
make all

# 运行增强版测试（推荐）
make test

# 运行原版测试
make test_original

# 运行所有测试
make test_all
```

### 高级测试

```bash
# 内存泄漏检查（需要安装valgrind）
make memcheck

# 性能基准测试
make benchmark

# 并发压力测试
make stress
```

### 维护操作

```bash
# 查看帮助信息
make help

# 查看系统信息
make info

# 清理编译产物
make clean

# 深度清理
make clean_all
```

## 超时机制详解

### 实现原理

1. **信号机制**: 使用 `alarm()` 设置定时器，`SIGALRM` 信号触发超时
2. **非局部跳转**: 使用 `setjmp()/longjmp()` 实现从信号处理器跳出
3. **状态管理**: 维护测试状态，区分正常结束、失败和超时

### 关键代码

```c
// 设置超时
static void set_test_timeout(int seconds) {
    test_timed_out = 0;
    signal(SIGALRM, timeout_handler);
    alarm(seconds);
}

// 超时处理器
static void timeout_handler(int sig) {
    test_timed_out = 1;
    longjmp(timeout_jmp, 1);
}

// 测试执行
if (setjmp(timeout_jmp) == 0) {
    set_test_timeout(timeout_sec);
    result = test_func();
    clear_test_timeout();
} else {
    // 处理超时
    g_test_stats.timeout_tests++;
}
```

### 超时配置

- `TEST_TIMEOUT_SECONDS`: 普通测试超时（30秒）
- `STRESS_TEST_TIMEOUT_SECONDS`: 压力测试超时（60秒）
- 可通过修改宏定义调整超时时间

## 并发安全测试

### 测试设计
- **多线程**: 8个工作线程同时操作同一引擎
- **操作分离**: 每线程使用不同的键前缀避免冲突
- **统计同步**: 使用互斥锁保护全局统计计数器
- **优雅退出**: 支持提前终止机制

### 测试指标
- 成功操作数量
- 错误操作数量
- 引擎功能完整性验证

## 内存泄漏检测

### 内置检测
- 1000次引擎创建/销毁循环
- 每次循环进行完整的CRUD操作
- 监控内存使用模式

### Valgrind集成
```bash
make memcheck
```
- 详细的内存泄漏报告
- 源码行号定位
- 120秒超时保护

## 性能测试

### 测试项目
- **写入性能**: 批量SET操作的吞吐量
- **读取性能**: 批量GET操作的吞吐量  
- **数据完整性**: 验证写入数据的正确性

### 性能指标
- 操作吞吐量（ops/sec）
- 平均延迟（ms）
- 数据验证成功率

## 故障排除

### 常见问题

1. **编译错误**: 确保kvserver目录已正确编译
2. **链接错误**: 检查依赖的对象文件是否存在
3. **测试超时**: 调整超时时间或检查死锁问题
4. **并发失败**: 验证引擎的线程安全实现

### 调试技巧

```bash
# 启用调试信息
CFLAGS="-DDEBUG -g" make test

# 单独运行特定测试
./engine_tests/kv_engine_test_enhanced

# 使用GDB调试
gdb ./engine_tests/kv_engine_test_enhanced
```

## 开发指南

### 添加新测试

1. 在 `kv_engine_test_enhanced.c` 中添加测试函数
2. 使用 `TEST_ASSERT` 和 `TEST_SUCCESS` 宏
3. 在 `main()` 函数中使用 `RUN_TEST` 调用
4. 考虑是否需要特殊的超时设置

### 测试函数模板

```c
int test_new_feature() {
    // 设置测试环境
    kv_engine_t *engine = kv_engine_create(KV_ENGINE_ARRAY, NULL);
    TEST_ASSERT(engine != NULL, "Failed to create engine");
    
    // 执行测试逻辑
    int ret = your_test_operation(engine);
    TEST_ASSERT(ret == expected_result, "Operation failed");
    
    // 清理资源
    kv_engine_destroy(engine);
    
    TEST_SUCCESS("New feature test passed");
}
```

### 超时测试模板

```c
// 需要特殊超时的测试
RUN_TEST_WITH_TIMEOUT(test_long_running, 120);
```

## 与CI/CD集成

### GitHub Actions示例

```yaml
- name: Run ConcordKV Tests
  run: |
    cd ConcordKV/tests
    make test
    make memcheck
```

### 返回值说明
- `0`: 所有测试通过
- `1`: 有测试失败或超时
- `2`: 编译错误

## 未来扩展

### 计划中的测试

- [ ] 分布式一致性测试
- [ ] 故障恢复测试
- [ ] 网络分区测试
- [ ] 事务ACID测试
- [ ] 持久化测试

### 性能基准

- [ ] 与其他KV存储对比
- [ ] 不同数据大小的性能曲线
- [ ] 内存使用优化验证

---

## 许可证

本测试系统是 ConcordKV 项目的一部分，遵循项目的开源许可证。 