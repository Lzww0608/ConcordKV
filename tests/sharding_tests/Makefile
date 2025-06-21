# ConcordKV Sharding Tests Makefile
# Author: Lzww0608
# Date: 2025-6-21 21:31:41
# Go相关设置
GO := go
GOFLAGS := -v
GOBUILD := $(GO) build $(GOFLAGS)
GOTEST := $(GO) test $(GOFLAGS)
GORUN := $(GO) run $(GOFLAGS)

# 项目路径
PROJECT_ROOT := ../..
SHARDING_PKG := $(PROJECT_ROOT)/raftserver/sharding

# 测试文件
TEST_FILES := test_consistent_hash.go test_shard_metadata_only.go test_key_router_simple.go

# 编译输出目录
BUILD_DIR := build
BINARIES := $(BUILD_DIR)/test_consistent_hash $(BUILD_DIR)/test_shard_metadata_only $(BUILD_DIR)/test_key_router_simple

# 默认目标
.PHONY: all
all: build

# 创建构建目录
$(BUILD_DIR):
	@echo "创建构建目录..."
	@mkdir -p $(BUILD_DIR)

# 编译所有测试
.PHONY: build
build: $(BUILD_DIR) $(BINARIES)
	@echo "所有测试编译完成"

# 编译一致性哈希测试
$(BUILD_DIR)/test_consistent_hash: test_consistent_hash.go
	@echo "编译一致性哈希测试..."
	@$(GOBUILD) -o $(BUILD_DIR)/test_consistent_hash test_consistent_hash.go

# 编译分片元数据测试
$(BUILD_DIR)/test_shard_metadata_only: test_shard_metadata_only.go
	@echo "编译分片元数据测试..."
	@$(GOBUILD) -o $(BUILD_DIR)/test_shard_metadata_only test_shard_metadata_only.go

# 编译键路由测试
$(BUILD_DIR)/test_key_router_simple: test_key_router_simple.go
	@echo "编译键路由测试..."
	@$(GOBUILD) -o $(BUILD_DIR)/test_key_router_simple test_key_router_simple.go

# 运行所有测试
.PHONY: test
test: build
	@echo "=== 运行分片功能测试套件 ==="
	@echo ""
	@echo "1. 运行一致性哈希测试..."
	@./$(BUILD_DIR)/test_consistent_hash || (echo "❌ 一致性哈希测试失败" && exit 1)
	@echo ""
	@echo "2. 运行分片元数据测试..."
	@./$(BUILD_DIR)/test_shard_metadata_only || (echo "❌ 分片元数据测试失败" && exit 1)
	@echo ""
	@echo "3. 运行键路由测试..."
	@./$(BUILD_DIR)/test_key_router_simple || (echo "❌ 键路由测试失败" && exit 1)
	@echo ""
	@echo "🎉 所有分片功能测试通过！"

# 单独运行一致性哈希测试
.PHONY: test-hash
test-hash: $(BUILD_DIR)/test_consistent_hash
	@echo "运行一致性哈希测试..."
	@./$(BUILD_DIR)/test_consistent_hash

# 单独运行分片元数据测试
.PHONY: test-metadata
test-metadata: $(BUILD_DIR)/test_shard_metadata_only
	@echo "运行分片元数据测试..."
	@./$(BUILD_DIR)/test_shard_metadata_only

# 单独运行键路由测试
.PHONY: test-router
test-router: $(BUILD_DIR)/test_key_router_simple
	@echo "运行键路由测试..."
	@./$(BUILD_DIR)/test_key_router_simple

# 运行性能基准测试
.PHONY: benchmark
benchmark: build
	@echo "=== 运行性能基准测试 ==="
	@echo ""
	@echo "1. 一致性哈希性能测试..."
	@cd $(PROJECT_ROOT) && $(GO) test -bench=BenchmarkConsistentHashRingRouting -benchmem tests/sharding_tests/test_consistent_hash.go
	@echo ""
	@echo "2. 分片元数据性能测试..."
	@cd $(PROJECT_ROOT) && $(GO) test -bench=BenchmarkShardMetadataManagerRouting -benchmem tests/sharding_tests/test_shard_metadata_only.go
	@echo ""
	@echo "3. 键路由性能测试..."
	@cd $(PROJECT_ROOT) && $(GO) test -bench=BenchmarkKeyRouter -benchmem tests/sharding_tests/test_key_router_simple.go

# 运行代码覆盖率测试
.PHONY: coverage
coverage:
	@echo "=== 生成代码覆盖率报告 ==="
	@cd $(PROJECT_ROOT) && $(GO) test -coverprofile=coverage.out -covermode=atomic ./raftserver/sharding/...
	@cd $(PROJECT_ROOT) && $(GO) tool cover -html=coverage.out -o coverage.html
	@echo "覆盖率报告已生成: coverage.html"

# 代码格式化
.PHONY: fmt
fmt:
	@echo "格式化测试代码..."
	@$(GO) fmt $(TEST_FILES)
	@cd $(PROJECT_ROOT) && $(GO) fmt ./raftserver/sharding/...

# 代码检查
.PHONY: vet
vet:
	@echo "检查测试代码..."
	@cd $(PROJECT_ROOT) && $(GO) vet ./tests/sharding_tests/...
	@cd $(PROJECT_ROOT) && $(GO) vet ./raftserver/sharding/...

# 依赖检查
.PHONY: mod-check
mod-check:
	@echo "检查Go模块依赖..."
	@cd $(PROJECT_ROOT) && $(GO) mod tidy
	@cd $(PROJECT_ROOT) && $(GO) mod verify

# 清理构建文件
.PHONY: clean
clean:
	@echo "清理构建文件..."
	@rm -rf $(BUILD_DIR)
	@rm -f $(PROJECT_ROOT)/coverage.out $(PROJECT_ROOT)/coverage.html

# 完整的测试流程（包括格式化、检查、测试）
.PHONY: full-test
full-test: fmt vet mod-check test
	@echo "完整测试流程完成"

# 持续集成测试
.PHONY: ci
ci: clean full-test benchmark coverage
	@echo "持续集成测试完成"

# 快速测试（仅编译和运行）
.PHONY: quick-test
quick-test: build test

# 调试模式运行测试
.PHONY: debug-test
debug-test: build
	@echo "=== 调试模式运行测试 ==="
	@echo "设置详细输出..."
	@export GO_TEST_VERBOSE=1
	@./$(BUILD_DIR)/test_consistent_hash
	@./$(BUILD_DIR)/test_shard_metadata_only  
	@./$(BUILD_DIR)/test_key_router_simple

# 压力测试
.PHONY: stress-test
stress-test: build
	@echo "=== 运行压力测试 ==="
	@echo "运行长时间测试..."
	@for i in $$(seq 1 10); do \
		echo "第 $$i 轮测试..."; \
		./$(BUILD_DIR)/test_consistent_hash > /dev/null || exit 1; \
		./$(BUILD_DIR)/test_shard_metadata_only > /dev/null || exit 1; \
		./$(BUILD_DIR)/test_key_router_simple > /dev/null || exit 1; \
	done
	@echo "压力测试完成"

# 内存泄漏检测
.PHONY: memcheck
memcheck: build
	@echo "=== 内存泄漏检测 ==="
	@echo "注意：需要安装valgrind"
	@valgrind --leak-check=full --show-leak-kinds=all ./$(BUILD_DIR)/test_consistent_hash
	@valgrind --leak-check=full --show-leak-kinds=all ./$(BUILD_DIR)/test_shard_metadata_only
	@valgrind --leak-check=full --show-leak-kinds=all ./$(BUILD_DIR)/test_key_router_simple

# 显示帮助信息
.PHONY: help
help:
	@echo "ConcordKV 分片功能测试 Makefile"
	@echo ""
	@echo "可用目标："
	@echo "  build         - 编译所有测试"
	@echo "  test          - 运行所有测试"
	@echo "  test-hash     - 运行一致性哈希测试"
	@echo "  test-metadata - 运行分片元数据测试"
	@echo "  test-router   - 运行键路由测试"
	@echo "  benchmark     - 运行性能基准测试"
	@echo "  coverage      - 生成代码覆盖率报告"
	@echo "  fmt           - 格式化代码"
	@echo "  vet           - 代码静态检查"
	@echo "  mod-check     - 检查Go模块依赖"
	@echo "  clean         - 清理构建文件"
	@echo "  full-test     - 完整测试流程"
	@echo "  ci            - 持续集成测试"
	@echo "  quick-test    - 快速测试"
	@echo "  debug-test    - 调试模式测试"
	@echo "  stress-test   - 压力测试"
	@echo "  memcheck      - 内存泄漏检测"
	@echo "  help          - 显示此帮助信息"
	@echo ""
	@echo "示例："
	@echo "  make test           # 运行所有测试"
	@echo "  make test-hash      # 只运行哈希测试"
	@echo "  make benchmark      # 运行性能测试"
	@echo "  make full-test      # 完整测试流程" 