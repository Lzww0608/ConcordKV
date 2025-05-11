cmake_minimum_required(VERSION 3.10)
project(ConcordKV_Tests)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找GoogleTest
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

# 查找线程库
find_package(Threads REQUIRED)

# 添加kvserver头文件目录
include_directories(../../kvserver)

# 添加测试源文件
add_executable(kvserver_tests
    kv_persist_test.cpp
    kv_concurrency_test.cpp
    kv_transaction_test.cpp
    kv_error_test.cpp
    kvstore_array_test.cpp
    kvstore_hash_test.cpp
    kvstore_rbtree_test.cpp
    main.cpp
)

# 链接kvserver库和测试库
target_link_libraries(kvserver_tests
    ${GTEST_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    # 添加自定义链接选项
    -L${CMAKE_SOURCE_DIR}/../../kvserver
    -lkvstore
)

# 添加测试
enable_testing()
add_test(NAME kvserver_tests COMMAND kvserver_tests) 

# LD_LIBRARY_PATH=~/lzww/ConcordKV/kvserver ./kvserver_tests