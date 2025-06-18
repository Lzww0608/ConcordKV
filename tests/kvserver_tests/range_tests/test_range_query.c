/*
 * @Author: Lzww0608  
 * @Date: 2025-6-18 22:10:19
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-18 22:10:20
 * @Description: ConcordKV 范围查询和前缀扫描测试
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>
#include <errno.h>

// 包含必要的头文件
#include "../../../kvserver/kv_range_query.h"
#include "../../../kvserver/kv_engine_interface.h"
#include "../../../kvserver/btree_adapter.h"
#include "../../../kvserver/kv_memory.h"
#include "../../../kvserver/kv_error.h"

// 测试配置
#define TEST_TIMEOUT_SECONDS 10
#define MAX_TEST_KEYS 100
#define TEST_KEY_PREFIX "test_key_"
#define TEST_VALUE_PREFIX "test_value_"

// 全局测试状态
static int total_tests = 0;
static int passed_tests = 0;
static int failed_tests = 0;

// 测试断言宏
#define TEST_ASSERT(condition, message) do { \
    total_tests++; \
    if (condition) { \
        passed_tests++; \
        printf("[PASS] %s\n", message); \
    } else { \
        failed_tests++; \
        printf("[FAIL] %s\n", message); \
    } \
} while(0)

// 简单测试函数 - 验证接口可用性
static void test_basic_functionality(void) {
    printf("\n=== 测试基础功能 ===\n");
    
    // 测试默认选项创建
    kv_range_options_t options = kv_range_options_default();
    TEST_ASSERT(options.limit == 1000, "默认选项创建");
    TEST_ASSERT(options.timeout_ms == 5000, "默认超时设置");
    
    // 测试前缀配置创建
    kv_prefix_config_t config = kv_prefix_config_default();
    TEST_ASSERT(config.case_sensitive == true, "默认前缀配置");
    
    // 测试工具函数
    bool match = kv_key_match_prefix("test_key_001", "test_key_", true);
    TEST_ASSERT(match == true, "前缀匹配功能");
    
    match = kv_key_match_prefix("other_key_001", "test_key_", true);
    TEST_ASSERT(match == false, "前缀不匹配检测");
    
    // 测试键比较
    int cmp = kv_key_compare("key001", "key002");
    TEST_ASSERT(cmp < 0, "键比较功能");
    
    // 测试前缀下一个键生成
    char *next_key = kv_prefix_next_key("test_");
    TEST_ASSERT(next_key != NULL, "前缀下一个键生成");
    if (next_key) {
        TEST_ASSERT(strcmp(next_key, "test_") > 0, "下一个键大于原前缀");
        free(next_key);
    }
}

// 主测试函数
int main(void) {
    printf("=== ConcordKV 范围查询和前缀扫描测试开始 ===\n");
    
    // 运行基础功能测试
    test_basic_functionality();
    
    // 输出测试结果
    printf("\n=== 测试结果汇总 ===\n");
    printf("总测试数: %d\n", total_tests);
    printf("通过: %d\n", passed_tests);
    printf("失败: %d\n", failed_tests);
    printf("成功率: %.1f%%\n", 
           total_tests > 0 ? (passed_tests * 100.0 / total_tests) : 0);
    
    if (failed_tests > 0) {
        printf("\n[ERROR] 有测试失败，请检查实现\n");
        return 1;
    }
    
    printf("\n[SUCCESS] 所有测试通过！\n");
    return 0;
}
