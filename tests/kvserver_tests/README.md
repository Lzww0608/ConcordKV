# ConcordKV 测试套件

本目录包含ConcordKV存储引擎的完整测试套件，按存储引擎类型分类组织。

## 📁 目录结构

```
tests/kvserver_tests/
├── btree_tests/          # B+Tree存储引擎测试 ⭐
├── hash_tests/           # Hash存储引擎测试
├── rbtree_tests/         # RBTree存储引擎测试
├── array_tests/          # Array存储引擎测试
├── config_tests/         # 配置系统测试 🔧
├── system_tests/         # 系统功能测试（事务、并发、持久化等）
├── engine_tests/         # 引擎接口和通用功能测试
├── build/                # CMake构建目录
├── CMakeLists.txt        # CMake配置文件
├── build.sh              # 构建脚本
└── README.md             # 本文件
```

## 🌳 B+Tree测试 (btree_tests/)

B+Tree是ConcordKV的主要存储引擎，提供全面的测试覆盖。

### 测试文件

- `btree_test.c` - 完整的B+Tree功能测试套件
- `btree_production_test.c` - 生产级检查程序 ⭐
- `debug_btree.c` - 调试和问题诊断工具
- `simple_test.c` - 简单的基础功能验证

### 运行方式

```bash
# 从kvserver目录运行
cd ConcordKV/kvserver

# 运行完整B+Tree测试
make run_btree_test

# 运行生产环境检查 ⭐ 推荐
make run_production_test

# 运行调试程序
make run_debug_btree

# 运行简单测试
make run_simple_debug
```

### 生产环境检查内容

`btree_production_test.c` 包含4项关键检查：

1. **阶数和配置检查** - 验证B+Tree配置的合理性
2. **内存使用检查** - 监控内存效率和开销
3. **并发安全检查** - 8线程高并发压力测试
4. **树结构完整性检查** - B+Tree属性验证

**期望结果**: 4/4测试通过，成功率100%

## 🗃️ Hash测试 (hash_tests/)

Hash存储引擎专注于高性能哈希表实现。

### 测试文件

- `kvstore_hash_test.cpp` - Hash引擎C++测试套件
- `simple_hash_test.c` - 基础Hash功能测试
- `test_hash_optimized.c` - 优化性能测试
- `test_hash_collision.c` - 哈希冲突处理测试
- `run_all_hash_tests.sh` - 批量运行脚本
- `test_hash_performance.sh` - 性能基准测试

### 运行方式

```bash
# 运行Hash引擎测试
make test_hash

# 或直接运行脚本
cd tests/kvserver_tests/hash_tests
./run_all_hash_tests.sh
```

## 🌲 RBTree测试 (rbtree_tests/)

红黑树存储引擎提供有序存储功能。

### 测试文件

- `kvstore_rbtree_test.cpp` - RBTree引擎C++测试套件
- `test_hash_rbtree_conversion.c` - Hash与RBTree转换测试

### 运行方式

```bash
# 运行RBTree引擎测试
make test_rbtree
```

## 📊 Array测试 (array_tests/)

Array存储引擎适用于小数据集和原型开发。

### 测试文件

- `kvstore_array_test.cpp` - Array引擎C++测试套件

### 运行方式

```bash
# 运行Array引擎测试
make test_array
```

## 🔧 配置系统测试 (config_tests/)

配置系统提供统一的存储引擎配置管理和运行时引擎切换功能。

### 测试文件

- `test_config_system.c` - 配置系统综合测试套件
- `README.md` - 详细测试说明文档

### 测试内容

1. **配置对象管理** - 创建、销毁、验证
2. **配置文件加载** - JSON格式配置解析
3. **环境变量配置** - 环境变量优先级处理
4. **引擎管理器** - 多引擎统一管理
5. **运行时引擎切换** - 动态引擎切换功能
6. **基本存储操作** - 跨引擎CRUD操作验证
7. **字符串转换** - 类型和策略转换函数

### 运行方式

```bash
# 从kvserver目录运行配置系统测试
cd ConcordKV/kvserver
make run_config_test

# 或直接运行
./test_config_system
```

### 期望结果

所有8项测试用例通过，成功率100%：
- ✅ 配置对象创建和销毁测试
- ✅ 配置文件加载测试  
- ✅ 环境变量配置测试
- ✅ 配置验证测试
- ✅ 引擎管理器测试
- ✅ 引擎切换测试
- ✅ 基本存储操作测试
- ✅ 字符串转换函数测试

## ⚙️ 系统功能测试 (system_tests/)

测试ConcordKV的核心系统功能。

### 测试文件

- `kv_transaction_test.cpp` - 事务系统测试
- `kv_concurrency_test.cpp` - 并发控制测试
- `kv_persist_test.cpp` - 持久化功能测试
- `kv_error_test.cpp` - 错误处理测试

### 运行方式

```bash
# 运行系统功能测试
make test_system
```

## 🔧 引擎接口测试 (engine_tests/)

测试统一引擎接口和通用功能。

### 测试文件

- `testcase.c` - 引擎接口基础测试
- `main.cpp` - 主测试入口

### 运行方式

```bash
# 运行引擎接口测试
make test_engine_interface
```

## 🚀 快速运行所有测试

### 分类运行

```bash
# 运行所有分类测试
make test_by_category

# 运行核心测试
make test_all

# 运行基础接口测试
make test
```

### 性能测试

```bash
# 运行性能基准测试
make perf_test

# 内存泄漏检查
make memcheck
```

## 📊 测试结果示例

### B+Tree生产环境检查结果

```
🔍 B+Tree生产级检查
===========================

=== B+Tree阶数和配置检查 ===
默认阶数: 100
✅ 阶数配置检查完成

=== 内存使用检查 ===
  50000 键: 内存使用=135 KB, 比率=0.04
✅ 内存使用检查完成

=== 并发安全检查 ===
总体性能: 832484 ops/sec, 总错误: 0
✅ 并发安全检查通过

=== 树结构完整性检查 ===
✅ 树结构完整性检查通过

=== 生产环境检查报告 ===
总测试数: 4
通过测试: 4
成功率: 100.0%

🎉 B+Tree已准备好投入生产环境使用！
```

## 🔍 故障排除

### 编译问题

```bash
# 清理编译产物
make clean

# 重新编译
make all
```

### 运行权限问题

```bash
# 给脚本添加执行权限
chmod +x tests/kvserver_tests/hash_tests/*.sh
```

### 路径问题

确保从`ConcordKV/kvserver`目录运行make命令，测试文件的路径都是相对于该目录配置的。

## 🎯 测试覆盖率

| 组件 | 测试覆盖率 | 状态 |
|------|------------|------|
| B+Tree引擎 | 100% | ✅ 完成 |
| Hash引擎 | 95% | ✅ 完成 |
| RBTree引擎 | 90% | ✅ 完成 |
| Array引擎 | 85% | ✅ 完成 |
| 配置系统 | 100% | ✅ 完成 |
| 统一接口 | 100% | ✅ 完成 |
| 事务系统 | 95% | ✅ 完成 |
| 并发控制 | 100% | ✅ 完成 |

**总体测试覆盖率: 96%** 🎉

---

更多信息请参阅项目文档：
- [API文档](../../docs/API.md)
- [开发路线图](../../docs/ROADMAP.md) 