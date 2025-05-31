#!/bin/bash

# 编译优化哈希表测试程序
echo "编译优化哈希表测试程序..."
gcc -o test_hash_optimized test_hash_optimized.c -Wall

# 编译简单哈希表测试程序
echo "编译简单哈希表测试程序..."
gcc -o simple_hash_test simple_hash_test.c -Wall

# 编译服务器端测试程序
echo "编译服务器端测试程序..."
gcc -o testcase testcase.c -Wall

# 运行测试
echo "----------------------"
echo "运行简单哈希表测试..."
./simple_hash_test

echo "----------------------"
echo "运行优化哈希表测试..."
./test_hash_optimized

echo "----------------------"
echo "服务器测试 (需手动运行):"
echo "启动KV服务器后，执行以下命令测试："
echo "./testcase -s 127.0.0.1 -p 9096 -m 4"
echo "参数说明："
echo "  -s: 服务器IP地址"
echo "  -p: 服务器端口"
echo "  -m: 测试模式 (1:数组, 2:红黑树, 4:哈希表, 可组合)" 