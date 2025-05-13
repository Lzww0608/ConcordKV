#!/bin/bash

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # 无颜色

# 输出标题
print_header() {
    echo -e "${BLUE}====================================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}====================================================${NC}"
}

# 确保测试程序已编译
print_header "编译测试程序"

# 编译红黑树转换测试
echo -e "${YELLOW}编译红黑树转换测试...${NC}"
gcc -o test_hash_rbtree_conversion test_hash_rbtree_conversion.c kvstore_hash.c rbtree_adapter.c -I. -Wall
if [ $? -ne 0 ]; then
    echo -e "${RED}编译失败!${NC}"
    exit 1
fi

# 编译标准测试程序
echo -e "${YELLOW}编译测试程序...${NC}"
gcc -o testcase testcase.c -Wall
if [ $? -ne 0 ]; then
    echo -e "${RED}编译失败!${NC}"
    exit 1
fi

# 运行红黑树转换测试
print_header "运行红黑树转换测试"
./test_hash_rbtree_conversion

# 提示用户如何运行性能测试
print_header "性能测试 (需手动执行)"
echo -e "要手动运行性能测试，请执行以下步骤:"
echo -e "1. 启动ConcordKV服务器 (如果尚未运行)"
echo -e "2. 执行以下命令测试不同的哈希表实现:"
echo -e "   ${GREEN}./testcase -s 127.0.0.1 -p 9096 -m 4${NC} (测试哈希表)"
echo -e "   ${GREEN}./testcase -s 127.0.0.1 -p 9096 -m 7${NC} (测试所有存储引擎)"
echo
echo -e "注: 参数说明"
echo -e "  -s: 服务器IP地址"
echo -e "  -p: 服务器端口"
echo -e "  -m: 测试模式位掩码 (1=数组, 2=红黑树, 4=哈希表)"
echo
echo -e "${YELLOW}建议先运行简单测试:${NC}"
echo -e "${GREEN}./simple_hash_test${NC}"
echo -e "${GREEN}./test_hash_optimized${NC}"

print_header "测试完成"
echo -e "以上测试只检查了基本功能和红黑树转换机制。"
echo -e "性能测试需要启动完整的服务器，并使用testcase程序来执行。" 