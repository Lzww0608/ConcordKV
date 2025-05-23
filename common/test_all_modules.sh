#!/bin/bash

echo "========================================="
echo "ConcordKV Common 模块功能测试"
echo "========================================="

# 设置颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 测试结果统计
PASSED=0
FAILED=0

# 测试函数
test_module() {
    local module_name=$1
    local test_command=$2
    
    echo -e "\n${YELLOW}测试 $module_name 模块...${NC}"
    
    if eval "$test_command"; then
        echo -e "${GREEN}✅ $module_name 模块测试通过${NC}"
        ((PASSED++))
    else
        echo -e "${RED}❌ $module_name 模块测试失败${NC}"
        ((FAILED++))
    fi
}

# 进入common目录
cd "$(dirname "$0")"

echo "当前目录: $(pwd)"
echo "开始测试各个模块..."

# 1. 测试 metrics 模块
test_module "Metrics" "cd metrics && ls -la metrics_demo && echo '启动HTTP服务器测试...' && timeout 5 ./metrics_demo > /dev/null 2>&1 &; sleep 2; curl -s http://localhost:8080/metrics | head -5 | grep -q 'active_connections'; result=$?; pkill metrics_demo 2>/dev/null; exit $result"

# 2. 测试 testing 模块  
test_module "Testing" "cd testing && ls -la testing_demo && echo '运行测试工具演示...' && timeout 15 ./testing_demo > /tmp/testing_output.log 2>&1; grep -q '基准测试报告' /tmp/testing_output.log"

# 3. 测试 util 模块的timer功能
test_module "Util-Timer" "cd util && ls -la timer_demo && echo '测试定时器功能...' && timeout 5 ./timer_demo > /tmp/timer_output.log 2>&1; grep -q '定时器1触发' /tmp/timer_output.log"

# 4. 检查数据文件生成
test_module "数据生成" "ls -la /tmp/concord_test_dataset.txt && wc -l /tmp/concord_test_dataset.txt | grep -q '1007'"

# 5. 检查配置模块结构
test_module "Config结构" "cd config && ls -la config.h config.c parser.h parser.c"

# 6. 检查文档完整性
test_module "文档" "cd doc && ls -la *.md && wc -l *.md | grep -v 'total' | head -3"

echo ""
echo "========================================="
echo "测试结果汇总"
echo "========================================="
echo -e "通过: ${GREEN}$PASSED${NC}"
echo -e "失败: ${RED}$FAILED${NC}"
echo -e "总计: $((PASSED + FAILED))"

if [ $FAILED -eq 0 ]; then
    echo -e "\n${GREEN}🎉 所有测试通过！ConcordKV Common 模块功能正常${NC}"
    exit 0
else
    echo -e "\n${RED}⚠️  有 $FAILED 个测试失败，请检查相关模块${NC}"
    exit 1
fi 