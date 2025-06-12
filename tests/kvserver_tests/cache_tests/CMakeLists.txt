cmake_minimum_required(VERSION 3.10)
project(CachePoliciesTest)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置C标准
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g -O2")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g -O2 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L")

# 查找必要的包
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

# 查找GTest
find_package(GTest REQUIRED)
if(NOT GTest_FOUND)
    # 如果系统没有安装GTest，尝试使用FetchContent
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/03597a01ee50ed33e9fd7188a1e9cd6a8d6b0f0.zip
    )
    FetchContent_MakeAvailable(googletest)
endif()

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../kvserver
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common
)

# 源文件
set(CACHE_SOURCES
    ../../../kvserver/kv_cache.c
)

set(TEST_SOURCES
    test_cache_policies.cpp
)

# 创建测试可执行文件
add_executable(test_cache_policies
    ${TEST_SOURCES}
    ${CACHE_SOURCES}
)

# 链接库
target_link_libraries(test_cache_policies
    ${GTEST_LIBRARIES}
    ${GTEST_MAIN_LIBRARIES}
    Threads::Threads
    m  # 数学库
)

# 如果使用FetchContent下载的GTest
if(TARGET gtest)
    target_link_libraries(test_cache_policies
        gtest
        gtest_main
        Threads::Threads
        m
    )
endif()

# 添加测试
enable_testing()
add_test(NAME CachePoliciesTest COMMAND test_cache_policies)

# 设置测试属性
set_tests_properties(CachePoliciesTest PROPERTIES
    TIMEOUT 300  # 5分钟超时
)

# 添加自定义目标来运行测试
add_custom_target(run_cache_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --verbose
    DEPENDS test_cache_policies
    COMMENT "Running cache policies tests"
)

# 打印配置信息
message(STATUS "Cache Policies Test Configuration:")
message(STATUS "  CMAKE_C_COMPILER: ${CMAKE_C_COMPILER}")
message(STATUS "  CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
message(STATUS "  CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message(STATUS "  GTest found: ${GTest_FOUND}") 