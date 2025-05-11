#!/bin/bash

set -e

# 创建build目录
mkdir -p build
cd build

# 检查是否安装了GTest
if ! pkg-config --exists gtest; then
    echo "Error: Google Test framework not found. Please install it first."
    echo "On Ubuntu/Debian: sudo apt-get install libgtest-dev"
    echo "On CentOS/RHEL: sudo yum install gtest-devel"
    exit 1
fi

# 运行CMake
cmake ..

# 编译测试
make

# 运行测试
echo "Running tests..."
./kvserver_tests

echo "All tests completed." 

# LD_LIBRARY_PATH=~/lzww/ConcordKV/kvserver ./kvserver_tests