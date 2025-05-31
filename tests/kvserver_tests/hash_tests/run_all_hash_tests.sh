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

# 设置工作目录
cd "$(dirname "$0")"

print_header "ConcordKV 哈希表优化测试套件"

# 编译所有测试程序
print_header "1. 编译测试程序"

echo -e "${YELLOW}编译简单哈希表测试...${NC}"
gcc -o simple_hash_test simple_hash_test.c -Wall

echo -e "${YELLOW}编译优化哈希表测试...${NC}"
gcc -o test_hash_optimized test_hash_optimized.c -Wall

echo -e "${YELLOW}编译红黑树转换测试...${NC}"
gcc -o test_hash_rbtree_conversion test_hash_rbtree_conversion.c kvstore_hash.c rbtree_adapter.c -I. -Wall

echo -e "${YELLOW}编译哈希冲突性能测试...${NC}"
gcc -o test_hash_collision test_hash_collision.c kvstore_hash.c rbtree_adapter.c -I. -Wall

echo -e "${YELLOW}编译服务器测试程序...${NC}"
gcc -o testcase testcase.c -Wall

# 运行基本功能测试
print_header "2. 运行基本功能测试"
./simple_hash_test

# 运行优化版测试
print_header "3. 运行优化版测试"
./test_hash_optimized

# 运行红黑树转换测试
print_header "4. 运行红黑树转换测试"

echo -e "${YELLOW}是否运行红黑树转换测试？(y/n)${NC}"
read -r answer
if [[ "$answer" =~ ^[Yy]$ ]]; then
    ./test_hash_rbtree_conversion
else
    echo -e "跳过红黑树转换测试"
fi

# 运行冲突性能测试
print_header "5. 运行哈希冲突性能测试"

echo -e "${YELLOW}是否运行哈希冲突性能测试？(y/n)${NC}"
read -r answer
if [[ "$answer" =~ ^[Yy]$ ]]; then
    ./test_hash_collision
else
    echo -e "跳过哈希冲突性能测试"
fi

# 提示服务器端测试
print_header "6. 服务器端测试"
echo -e "要运行服务器端测试，请确保 ConcordKV 服务器已启动，然后执行："
echo -e "${GREEN}./testcase -s 127.0.0.1 -p 9096 -m 4${NC}"
echo -e ""
echo -e "参数说明:"
echo -e "  -s: 服务器IP地址"
echo -e "  -p: 服务器端口"
echo -e "  -m: 测试模式 (1:数组, 2:红黑树, 4:哈希表, 可组合)"

# 测试总结
print_header "测试总结"
echo -e "已完成以下测试:"
echo -e "1. 简单哈希表测试 ✓"
echo -e "2. 优化哈希表测试 ✓"

if [[ "$answer" =~ ^[Yy]$ ]]; then
    echo -e "3. 红黑树转换测试 ✓"
    echo -e "4. 哈希冲突性能测试 ✓"
else
    echo -e "3. 红黑树转换测试 (已跳过)"
    echo -e "4. 哈希冲突性能测试 (已跳过)"
fi

echo -e "5. 服务器端测试 (需手动运行)"
echo -e ""
echo -e "${GREEN}测试完成！${NC}" 