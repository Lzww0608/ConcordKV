cmake_minimum_required(VERSION 3.10)
project(LSM_Batch_Write_GTest)

# 设置C++标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置C标准（用于LSM-Tree C代码）
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 查找Google Test
find_package(GTest REQUIRED)
include_directories(${GTEST_INCLUDE_DIRS})

# 查找线程库
find_package(Threads REQUIRED)

# 添加编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2 -Wall -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2 -Wall -Wextra")

# 添加包含目录
include_directories(
    ../../../kvserver           # LSM-Tree头文件目录
    /usr/include/gtest          # GTest头文件目录
)

# 定义LSM-Tree源文件列表
set(LSM_TREE_SOURCES
    ../../../kvserver/lsm_tree.c
    ../../../kvserver/lsm_memtable.c
    ../../../kvserver/lsm_memtable_manager.c
    ../../../kvserver/lsm_sstable.c
    ../../../kvserver/lsm_compaction.c
    ../../../kvserver/kv_memory.c
    ../../../kvserver/kv_error.c
)

# 检查LSM-Tree源文件是否存在
foreach(source_file ${LSM_TREE_SOURCES})
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source_file}")
        message(WARNING "Source file not found: ${source_file}")
    endif()
endforeach()

# 创建LSM-Tree静态库
add_library(lsm_tree_static STATIC ${LSM_TREE_SOURCES})

# 设置LSM-Tree库的编译选项
target_compile_options(lsm_tree_static PRIVATE
    -fPIC
    -pthread
)

# 创建测试可执行文件
add_executable(lsm_batch_write_gtest
    lsm_batch_write_gtest.cpp
    lsm_batch_write_gtest_main.cpp
)

# 链接库
target_link_libraries(lsm_batch_write_gtest
    lsm_tree_static
    ${GTEST_LIBRARIES}
    ${CMAKE_THREAD_LIBS_INIT}
    pthread
    m  # 数学库
    rt # 实时库
)

# 设置可执行文件的编译选项
target_compile_options(lsm_batch_write_gtest PRIVATE
    -DGTEST_HAS_PTHREAD=1
    -pthread
)

# 添加测试
enable_testing()

# 定义各种测试目标
add_test(NAME BasicOperations 
         COMMAND lsm_batch_write_gtest --gtest_filter="LSMBatchWriteTest.Basic*")

add_test(NAME DeduplicationTests 
         COMMAND lsm_batch_write_gtest --gtest_filter="LSMBatchWriteTest.Deduplication*")

add_test(NAME CapacityTests 
         COMMAND lsm_batch_write_gtest --gtest_filter="LSMBatchWriteTest.*Limit")

add_test(NAME PerformanceTests 
         COMMAND lsm_batch_write_gtest --gtest_filter="LSMBatchWriteTest.PerformanceComparison")

add_test(NAME ConcurrencyTests 
         COMMAND lsm_batch_write_gtest --gtest_filter="LSMBatchWriteTest.Concurrent*")

add_test(NAME WALTests 
         COMMAND lsm_batch_write_gtest --gtest_filter="LSMBatchWriteTest.WAL*")

add_test(NAME ErrorHandlingTests 
         COMMAND lsm_batch_write_gtest --gtest_filter="LSMBatchWriteTest.Error*")

add_test(NAME StressTests 
         COMMAND lsm_batch_write_gtest --gtest_filter="LSMBatchWriteTest.StressTest")

add_test(NAME AllTests 
         COMMAND lsm_batch_write_gtest)

# 自定义目标：运行所有测试
add_custom_target(run_all_batch_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --verbose
    DEPENDS lsm_batch_write_gtest
    COMMENT "Running all LSM batch write tests"
)

# 自定义目标：运行基础测试
add_custom_target(run_basic_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --verbose -R "BasicOperations|DeduplicationTests"
    DEPENDS lsm_batch_write_gtest
    COMMENT "Running basic LSM batch write tests"
)

# 自定义目标：运行性能测试
add_custom_target(run_performance_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --verbose -R "PerformanceTests|StressTests"
    DEPENDS lsm_batch_write_gtest
    COMMENT "Running performance LSM batch write tests"
)

# 自定义目标：运行并发测试
add_custom_target(run_concurrency_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --verbose -R "ConcurrencyTests"
    DEPENDS lsm_batch_write_gtest
    COMMENT "Running concurrency LSM batch write tests"
)

# 设置测试环境变量
set_tests_properties(AllTests PROPERTIES
    ENVIRONMENT "GTEST_COLOR=1"
    TIMEOUT 300  # 5分钟超时
)

# 设置压力测试超时
set_tests_properties(StressTests PROPERTIES
    TIMEOUT 600  # 10分钟超时
)

# 输出构建信息
message(STATUS "=== LSM Batch Write GTest Configuration ===")
message(STATUS "CMAKE_CXX_COMPILER: ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_C_COMPILER: ${CMAKE_C_COMPILER}")
message(STATUS "GTest found: ${GTEST_FOUND}")
message(STATUS "GTest libraries: ${GTEST_LIBRARIES}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "CXX flags: ${CMAKE_CXX_FLAGS}")
message(STATUS "C flags: ${CMAKE_C_FLAGS}")
message(STATUS "===========================================") 