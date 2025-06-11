# ConcordKV io_uring Tests CMakeLists.txt

cmake_minimum_required(VERSION 3.10)
project(ConcordKV_io_uring_Tests)

# 设置C标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -g -O2")

# 检查liburing
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing)

if(NOT LIBURING_FOUND)
    message(FATAL_ERROR "liburing not found. Please install liburing-dev package.")
endif()

# 包含目录
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../kvserver
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../common
    ${LIBURING_INCLUDE_DIRS}
)

# 链接目录
link_directories(${LIBURING_LIBRARY_DIRS})

# 基础测试程序
add_executable(test_io_uring_basic
    test_io_uring_basic.c
    ../../../kvserver/kv_io_uring.c
)

target_link_libraries(test_io_uring_basic
    ${LIBURING_LIBRARIES}
    pthread
    rt
)

# 添加测试
enable_testing()

# 基础功能测试
add_test(
    NAME io_uring_basic_test
    COMMAND test_io_uring_basic
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# 设置测试超时
set_tests_properties(io_uring_basic_test PROPERTIES TIMEOUT 30)

# 安装目标
install(TARGETS test_io_uring_basic
    RUNTIME DESTINATION bin
)

# 自定义目标：运行所有测试
add_custom_target(run_io_uring_tests
    COMMAND ctest --verbose
    DEPENDS test_io_uring_basic
    COMMENT "Running io_uring tests..."
) 