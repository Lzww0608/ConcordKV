# 贡献指南

感谢您对 ConcordKV 项目的关注和支持！我们欢迎各种形式的贡献，包括代码、文档、问题报告和新功能建议。

## 如何贡献

### 报告问题

如果您发现了 bug 或有改进建议，请提交 issue：

1. 确保该问题尚未被报告
2. 使用清晰的标题和详细描述
3. 包含必要的信息：
   - 问题的复现步骤
   - 预期行为和实际行为
   - 环境信息（操作系统、编译器等）
   - 如可能，提供相关日志或截图

### 贡献代码

1. Fork 本仓库
2. 创建您的特性分支 (`git checkout -b feature/amazing-feature`)
3. 提交您的更改 (`git commit -m 'Add some amazing feature'`)
4. 推送到分支 (`git push origin feature/amazing-feature`)
5. 创建一个 Pull Request

### 代码风格

#### C/C++ 代码

- 遵循 K&R 风格的缩进
- 使用 4 空格缩进，不使用制表符
- 函数和变量使用小写字母和下划线（snake_case）
- 宏和常量使用大写字母和下划线
- 每行不超过 80 个字符
- 为每个函数添加文档注释

#### Go 代码

- 遵循 [Go 代码规范](https://golang.org/doc/effective_go)
- 使用 `gofmt` 格式化代码
- 使用驼峰命名法（CamelCase）
- 添加适当的注释和文档字符串

### 提交消息规范

遵循 [Conventional Commits](https://www.conventionalcommits.org/) 规范：

```
<类型>[可选的作用域]: <描述>

[可选的正文]

[可选的脚注]
```

类型包括：
- `feat`: 新功能
- `fix`: 错误修复
- `docs`: 文档更改
- `style`: 不影响代码含义的格式更改
- `refactor`: 既不修复错误也不添加功能的代码更改
- `perf`: 改进性能的代码更改
- `test`: 添加或修正测试
- `chore`: 对构建过程或辅助工具的更改

### 开发流程

1. **选择任务**：从 [开发计划](ROADMAP.md) 中选择您感兴趣的任务，或提出您自己的想法。
2. **讨论实现**：在实现大型功能前，先创建 issue 讨论您的实现方案。
3. **编写测试**：确保新功能或修复有对应的测试。
4. **提交 PR**：完成后提交 PR，并耐心等待审查。
5. **合并**：通过审查后的代码将被合并到主分支。

## 项目结构

```
ConcordKV/
├── kvserver/        - C/C++实现的KV存储引擎
├── raftserver/      - Go实现的Raft共识服务
├── client/          - 客户端库
├── common/          - 通用工具和配置
├── docs/            - 文档
└── tests/           - 测试
```

## 开发环境设置

### 存储引擎 (C/C++)

需要安装：
- GCC 或 Clang 编译器
- Make
- CMake (可选)

编译存储引擎：
```bash
cd kvserver
make
```

### Raft 服务器 (Go)

需要安装：
- Go 1.16 或更高版本

编译 Raft 服务器：
```bash
cd raftserver
go build
```

## 测试

- 为每个新功能添加单元测试
- 运行存储引擎测试：`cd kvserver && make test`
- 运行 Raft 服务器测试：`cd raftserver && go test ./...`

## 文档

- 更新与您的更改相关的文档
- 如果您添加了新功能，请确保添加适当的文档
- 文档使用 Markdown 格式

## 许可证

贡献的代码将采用项目的 MIT 许可证。 