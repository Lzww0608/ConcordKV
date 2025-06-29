#!/bin/bash

# ConcordKV Phase 5.2.3 故障转移和恢复测试运行脚本
# Author: Lzww0608
# Date: 2025-6-28

set -e

echo "=== ConcordKV Phase 5.2.3 故障转移和恢复测试 ==="
echo "开始时间: $(date)"
echo

# 设置测试环境
export GO111MODULE=on
export GOPATH=""

# 检查Go版本
echo "Go版本信息:"
go version
echo

# 检查项目结构
echo "检查项目结构..."
if [ ! -d "../../raftserver" ]; then
    echo "错误: 找不到raftserver目录"
    exit 1
fi

if [ ! -f "../../raftserver/go.mod" ]; then
    echo "错误: 找不到raftserver/go.mod"
    exit 1
fi

echo "项目结构检查通过"
echo

# 清理旧的依赖
echo "清理依赖..."
go clean -modcache 2>/dev/null || true
rm -f go.sum 2>/dev/null || true

# 初始化模块
echo "初始化Go模块..."
go mod tidy

echo "下载依赖..."
go mod download

# 运行测试
echo "=== 开始运行故障转移和恢复测试 ==="
echo

echo "1. 运行简化故障转移流程测试..."
go test -v -run TestSimpleFailoverFlow -timeout 30s

echo
echo "2. 运行DC故障检测器基础测试..."
go test -v -run TestDCFailureDetectorBasic -timeout 30s

echo
echo "3. 运行一致性恢复器基础测试..."
go test -v -run TestConsistencyRecoveryBasic -timeout 30s

echo
echo "4. 运行集成组件测试..."
go test -v -run TestIntegratedComponents -timeout 60s

echo
echo "5. 运行网络分区场景测试..."
go test -v -run TestNetworkPartitionScenario -timeout 30s

echo
echo "6. 运行完整集成测试..."
go test -v -run TestFailoverRecoveryIntegration -timeout 120s

echo
echo "7. 运行性能基准测试..."
go test -v -bench=BenchmarkFailoverPerformance -benchtime=5s -timeout 60s

echo
echo "=== 测试完成 ==="
echo "结束时间: $(date)"

# 生成测试报告
echo
echo "=== 测试报告 ==="
echo "测试项目: ConcordKV Phase 5.2.3 故障转移和恢复"
echo "测试组件:"
echo "  - DC级别故障检测器"
echo "  - 数据一致性恢复器"
echo "  - 故障转移协调器"
echo "  - 集成测试场景"
echo
echo "测试覆盖:"
echo "  - 基础功能测试"
echo "  - 网络分区场景"
echo "  - 故障转移协调"
echo "  - 数据一致性恢复"
echo "  - 性能基准测试"
echo
echo "详细日志请查看上述输出" 