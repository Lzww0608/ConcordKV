#define _GNU_SOURCE
#include "../util/util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

// 测试结果统计
static int tests_passed = 0;
static int tests_failed = 0;

// 测试宏
#define TEST_ASSERT(condition, message) \
    do { \
        if (condition) { \
            tests_passed++; \
            printf("✅ PASS: %s\n", message); \
        } else { \
            tests_failed++; \
            printf("❌ FAIL: %s\n", message); \
        } \
    } while(0)

// 测试字符串操作
void test_string_operations() {
    printf("\n=== 测试字符串操作 ===\n");
    
    // 测试字符串复制
    char *str1 = concord_util_strdup("hello world");
    TEST_ASSERT(str1 != NULL && strcmp(str1, "hello world") == 0, "字符串复制");
    free(str1);
    
    // 测试字符串修剪
    char test_str[] = "  hello world  ";
    char *trimmed = concord_util_trim(test_str);
    TEST_ASSERT(strcmp(trimmed, "hello world") == 0, "字符串修剪");
    
    // 测试字符串分割
    char split_str[] = "a,b,c";
    char *parts[10];
    int count = concord_util_split(split_str, ',', parts, 10);
    TEST_ASSERT(count == 3, "字符串分割");
    
    // 测试字符串连接
    char *joined = concord_util_join(parts, count, "|");
    TEST_ASSERT(joined != NULL && strcmp(joined, "a|b|c") == 0, "字符串连接");
    free(joined);
}

// 测试文件操作
void test_file_operations() {
    printf("\n=== 测试文件操作 ===\n");
    
    const char *test_file = "/tmp/simple_test.txt";
    const char *test_data = "Hello, Test!";
    
    // 测试文件写入
    int ret = concord_util_write_file(test_file, test_data, strlen(test_data));
    TEST_ASSERT(ret == 0, "写入文件");
    
    // 测试文件存在检查
    int exists = concord_util_file_exists(test_file);
    TEST_ASSERT(exists == 1, "检查文件存在");
    
    // 测试文件读取
    size_t size;
    char *content = concord_util_read_file(test_file, &size);
    TEST_ASSERT(content != NULL && size == strlen(test_data), "读取文件");
    if (content) free(content);
    
    // 清理测试文件
    unlink(test_file);
}

// 测试时间操作
void test_time_operations() {
    printf("\n=== 测试时间操作 ===\n");
    
    // 测试当前时间
    uint64_t start_time = concord_util_time_now();
    TEST_ASSERT(start_time > 0, "获取当前时间");
    
    // 简单延迟
    usleep(1000); // 1ms
    
    // 测试时间差
    uint64_t elapsed = concord_util_time_elapsed(start_time);
    TEST_ASSERT(elapsed >= 1, "计算时间差");
}

// 测试哈希函数
void test_hash_functions() {
    printf("\n=== 测试哈希函数 ===\n");
    
    const char *test_data = "Hello, Hash!";
    
    // 测试DJB2哈希
    uint32_t hash1 = concord_util_hash_djb2(test_data, strlen(test_data));
    uint32_t hash2 = concord_util_hash_djb2(test_data, strlen(test_data));
    TEST_ASSERT(hash1 == hash2, "DJB2哈希一致性");
    TEST_ASSERT(hash1 != 0, "DJB2哈希非零");
    
    // 测试FNV1a哈希
    uint32_t hash3 = concord_util_hash_fnv1a(test_data, strlen(test_data));
    uint32_t hash4 = concord_util_hash_fnv1a(test_data, strlen(test_data));
    TEST_ASSERT(hash3 == hash4, "FNV1a哈希一致性");
    TEST_ASSERT(hash3 != 0, "FNV1a哈希非零");
}

int main() {
    printf("========================================\n");
    printf("ConcordKV 简化功能测试\n");
    printf("========================================\n");
    
    test_string_operations();
    test_file_operations();
    test_time_operations();
    test_hash_functions();
    
    printf("\n========================================\n");
    printf("测试结果汇总\n");
    printf("========================================\n");
    printf("通过: %d\n", tests_passed);
    printf("失败: %d\n", tests_failed);
    printf("总计: %d\n", tests_passed + tests_failed);
    
    if (tests_failed == 0) {
        printf("\n🎉 所有简化测试通过！\n");
        return 0;
    } else {
        printf("\n⚠️  有 %d 个测试失败\n", tests_failed);
        return 1;
    }
} 