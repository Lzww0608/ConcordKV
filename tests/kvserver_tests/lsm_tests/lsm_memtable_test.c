/*
 * @Author: Lzww0608  
 * @Date: 2025-12-19 15:30:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-12-19 15:30:00
 * @Description: ConcordKV LSM-Tree MemTable 基础测试
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>

#include "../../../kvserver/lsm_memtable.h"
#include "../../../kvserver/kv_error.h"

// 测试结果统计
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// 测试超时设置
#define TEST_TIMEOUT_SECONDS 10

// 测试宏定义
#define TEST_ASSERT(condition, message) do { \
    tests_run++; \
    if (condition) { \
        tests_passed++; \
        printf("✓ PASS: %s\n", message); \
    } else { \
        tests_failed++; \
        printf("✗ FAIL: %s\n", message); \
    } \
} while(0)

#define TEST_ASSERT_NOT_NULL(ptr, message) \
    TEST_ASSERT((ptr) != NULL, message)

#define TEST_ASSERT_NULL(ptr, message) \
    TEST_ASSERT((ptr) == NULL, message)

#define TEST_ASSERT_EQ(expected, actual, message) \
    TEST_ASSERT((expected) == (actual), message)

#define TEST_ASSERT_STR_EQ(expected, actual, message) \
    TEST_ASSERT(strcmp((expected), (actual)) == 0, message)

#define TEST_ASSERT_ERR(expected_err, actual_err, message) \
    TEST_ASSERT((expected_err) == (actual_err), message)

// 获取当前时间戳（毫秒）
static double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// 测试函数声明
static void test_memtable_create_destroy(void);
static void test_memtable_basic_operations(void);
static void test_memtable_delete_tombstone(void);
static void test_memtable_size_limits(void);
static void test_memtable_immutable(void);
static void test_memtable_statistics(void);
static void test_memtable_stress(void);
static void print_test_summary(void);

// === 测试用例实现 ===

/**
 * 测试MemTable创建和销毁
 */
static void test_memtable_create_destroy(void) {
    printf("\n=== 测试MemTable创建和销毁 ===\n");
    
    // 测试正常创建
    lsm_memtable_t *memtable = lsm_memtable_create(1024 * 1024, "test_memtable");
    TEST_ASSERT_NOT_NULL(memtable, "MemTable创建成功");
    
    if (memtable) {
        TEST_ASSERT_EQ(0, lsm_memtable_size(memtable), "新创建的MemTable大小为0");
        TEST_ASSERT_EQ(0, lsm_memtable_count(memtable), "新创建的MemTable条目数为0");
        TEST_ASSERT(lsm_memtable_is_empty(memtable), "新创建的MemTable为空");
        TEST_ASSERT(!lsm_memtable_is_full(memtable), "新创建的MemTable未满");
        
        lsm_memtable_destroy(memtable);
        printf("✓ MemTable销毁成功\n");
    }
    
    // 测试默认大小创建
    lsm_memtable_t *memtable_default = lsm_memtable_create(0, NULL);
    TEST_ASSERT_NOT_NULL(memtable_default, "默认大小MemTable创建成功");
    
    if (memtable_default) {
        lsm_memtable_destroy(memtable_default);
    }
}

/**
 * 测试MemTable基本操作
 */
static void test_memtable_basic_operations(void) {
    printf("\n=== 测试MemTable基本操作 ===\n");
    
    lsm_memtable_t *memtable = lsm_memtable_create(1024 * 1024, "basic_test");
    TEST_ASSERT_NOT_NULL(memtable, "MemTable创建成功");
    
    if (!memtable) return;
    
    // 测试插入操作
    int ret = lsm_memtable_put(memtable, "key1", 4, "value1", 6);
    TEST_ASSERT_ERR(KV_ERR_NONE, ret, "插入键值对成功");
    
    ret = lsm_memtable_put(memtable, "key2", 4, "value2", 6);
    TEST_ASSERT_ERR(KV_ERR_NONE, ret, "插入第二个键值对成功");
    
    // 测试查找操作
    char *value = NULL;
    size_t value_len = 0;
    uint64_t seq_num = 0;
    bool deleted = false;
    
    ret = lsm_memtable_get(memtable, "key1", 4, &value, &value_len, &seq_num, &deleted);
    TEST_ASSERT_ERR(KV_ERR_NONE, ret, "查找key1成功");
    if (value) {
        TEST_ASSERT_STR_EQ("value1", value, "key1的值正确");
        TEST_ASSERT_EQ(6, value_len, "key1的值长度正确");
        TEST_ASSERT(!deleted, "key1未被删除");
        free(value);
        value = NULL;
    }
    
    // 测试更新操作
    ret = lsm_memtable_put(memtable, "key1", 4, "new_value1", 10);
    TEST_ASSERT_ERR(KV_ERR_NONE, ret, "更新key1成功");
    
    ret = lsm_memtable_get(memtable, "key1", 4, &value, &value_len, &seq_num, &deleted);
    TEST_ASSERT_ERR(KV_ERR_NONE, ret, "查找更新后的key1成功");
    if (value) {
        TEST_ASSERT_STR_EQ("new_value1", value, "key1的新值正确");
        TEST_ASSERT_EQ(10, value_len, "key1的新值长度正确");
        free(value);
        value = NULL;
    }
    
    // 测试查找不存在的键
    ret = lsm_memtable_get(memtable, "nonexistent", 11, &value, &value_len, &seq_num, &deleted);
    TEST_ASSERT_ERR(KV_ERR_NOT_FOUND, ret, "查找不存在的键返回NOT_FOUND");
    
    // 测试条目计数
    TEST_ASSERT_EQ(2, lsm_memtable_count(memtable), "MemTable条目数正确");
    TEST_ASSERT(!lsm_memtable_is_empty(memtable), "MemTable不为空");
    
    lsm_memtable_destroy(memtable);
}

/**
 * 测试MemTable删除和墓碑标记
 */
static void test_memtable_delete_tombstone(void) {
    printf("\n=== 测试MemTable删除和墓碑标记 ===\n");
    
    lsm_memtable_t *memtable = lsm_memtable_create(1024 * 1024, "delete_test");
    TEST_ASSERT_NOT_NULL(memtable, "MemTable创建成功");
    
    if (!memtable) return;
    
    // 插入测试数据
    lsm_memtable_put(memtable, "key1", 4, "value1", 6);
    lsm_memtable_put(memtable, "key2", 4, "value2", 6);
    
    // 删除存在的键
    int ret = lsm_memtable_delete(memtable, "key1", 4);
    TEST_ASSERT_ERR(KV_ERR_NONE, ret, "删除存在的键成功");
    
    // 查找被删除的键，应该能找到但标记为删除
    char *value = NULL;
    size_t value_len = 0;
    uint64_t seq_num = 0;
    bool deleted = false;
    
    ret = lsm_memtable_get(memtable, "key1", 4, &value, &value_len, &seq_num, &deleted);
    TEST_ASSERT_ERR(KV_ERR_NONE, ret, "能找到被删除的键");
    TEST_ASSERT(deleted, "键被标记为删除");
    if (value) {
        free(value);
    }
    
    // 删除不存在的键（应该创建墓碑标记）
    ret = lsm_memtable_delete(memtable, "nonexistent", 11);
    TEST_ASSERT_ERR(KV_ERR_NONE, ret, "删除不存在的键成功（创建墓碑）");
    
    // 验证墓碑标记
    ret = lsm_memtable_get(memtable, "nonexistent", 11, &value, &value_len, &seq_num, &deleted);
    TEST_ASSERT_ERR(KV_ERR_NONE, ret, "能找到墓碑标记");
    TEST_ASSERT(deleted, "墓碑标记为删除状态");
    if (value) {
        free(value);
    }
    
    // 验证未删除的键仍然存在
    ret = lsm_memtable_get(memtable, "key2", 4, &value, &value_len, &seq_num, &deleted);
    TEST_ASSERT_ERR(KV_ERR_NONE, ret, "未删除的键仍存在");
    TEST_ASSERT(!deleted, "未删除的键状态正确");
    if (value) {
        TEST_ASSERT_STR_EQ("value2", value, "未删除键的值正确");
        free(value);
    }
    
    lsm_memtable_destroy(memtable);
}

/**
 * 测试MemTable大小限制
 */
static void test_memtable_size_limits(void) {
    printf("\n=== 测试MemTable大小限制 ===\n");
    
    // 创建一个小的MemTable
    lsm_memtable_t *memtable = lsm_memtable_create(1024, "size_test");
    TEST_ASSERT_NOT_NULL(memtable, "小容量MemTable创建成功");
    
    if (!memtable) return;
    
    // 插入数据直到接近满
    char key[32], value[256];
    int count = 0;
    
    while (!lsm_memtable_is_full(memtable) && count < 100) {
        snprintf(key, sizeof(key), "key_%04d", count);
        snprintf(value, sizeof(value), "value_%04d_with_some_padding_data", count);
        
        int ret = lsm_memtable_put(memtable, key, strlen(key), value, strlen(value));
        if (ret != KV_ERR_NONE) break;
        
        count++;
    }
    
    printf("插入了 %d 个条目，MemTable大小: %zu 字节\n", count, lsm_memtable_size(memtable));
    TEST_ASSERT(count > 0, "成功插入了一些数据");
    TEST_ASSERT(lsm_memtable_is_full(memtable), "MemTable达到大小限制");
    
    lsm_memtable_destroy(memtable);
}

/**
 * 测试MemTable不可变状态
 */
static void test_memtable_immutable(void) {
    printf("\n=== 测试MemTable不可变状态 ===\n");
    
    lsm_memtable_t *memtable = lsm_memtable_create(1024 * 1024, "immutable_test");
    TEST_ASSERT_NOT_NULL(memtable, "MemTable创建成功");
    
    if (!memtable) return;
    
    // 插入一些数据
    lsm_memtable_put(memtable, "key1", 4, "value1", 6);
    lsm_memtable_put(memtable, "key2", 4, "value2", 6);
    
    // 冻结MemTable
    int ret = lsm_memtable_freeze(memtable);
    TEST_ASSERT_ERR(KV_ERR_NONE, ret, "MemTable冻结成功");
    
    // 尝试插入新数据（应该失败）
    ret = lsm_memtable_put(memtable, "key3", 4, "value3", 6);
    TEST_ASSERT_ERR(KV_ERR_NOT_SUPPORTED, ret, "冻结后插入操作被拒绝");
    
    // 尝试删除数据（应该失败）
    ret = lsm_memtable_delete(memtable, "key1", 4);
    TEST_ASSERT_ERR(KV_ERR_NOT_SUPPORTED, ret, "冻结后删除操作被拒绝");
    
    // 读取操作应该仍然可用
    char *value = NULL;
    size_t value_len = 0;
    uint64_t seq_num = 0;
    bool deleted = false;
    
    ret = lsm_memtable_get(memtable, "key1", 4, &value, &value_len, &seq_num, &deleted);
    TEST_ASSERT_ERR(KV_ERR_NONE, ret, "冻结后读取操作仍可用");
    if (value) {
        TEST_ASSERT_STR_EQ("value1", value, "冻结后读取的值正确");
        free(value);
    }
    
    lsm_memtable_destroy(memtable);
}

/**
 * 测试MemTable统计信息
 */
static void test_memtable_statistics(void) {
    printf("\n=== 测试MemTable统计信息 ===\n");
    
    lsm_memtable_t *memtable = lsm_memtable_create(1024 * 1024, "stats_test");
    TEST_ASSERT_NOT_NULL(memtable, "MemTable创建成功");
    
    if (!memtable) return;
    
    lsm_memtable_stats_t stats;
    int ret = lsm_memtable_get_stats(memtable, &stats);
    TEST_ASSERT_ERR(KV_ERR_NONE, ret, "获取统计信息成功");
    
    // 初始统计信息
    TEST_ASSERT_EQ(0, stats.total_entries, "初始条目数为0");
    TEST_ASSERT_EQ(0, stats.active_entries, "初始活跃条目数为0");
    TEST_ASSERT_EQ(0, stats.deleted_entries, "初始删除条目数为0");
    TEST_ASSERT_EQ(0, stats.write_count, "初始写操作数为0");
    TEST_ASSERT_EQ(0, stats.read_count, "初始读操作数为0");
    TEST_ASSERT_EQ(0, stats.delete_count, "初始删除操作数为0");
    
    // 执行一些操作
    lsm_memtable_put(memtable, "key1", 4, "value1", 6);
    lsm_memtable_put(memtable, "key2", 4, "value2", 6);
    
    char *value = NULL;
    size_t value_len = 0;
    uint64_t seq_num = 0;
    bool deleted = false;
    lsm_memtable_get(memtable, "key1", 4, &value, &value_len, &seq_num, &deleted);
    if (value) free(value);
    
    lsm_memtable_delete(memtable, "key1", 4);
    
    // 检查更新后的统计信息
    ret = lsm_memtable_get_stats(memtable, &stats);
    TEST_ASSERT_ERR(KV_ERR_NONE, ret, "获取更新后统计信息成功");
    
    TEST_ASSERT_EQ(2, stats.total_entries, "总条目数正确");
    TEST_ASSERT_EQ(1, stats.active_entries, "活跃条目数正确");
    TEST_ASSERT_EQ(1, stats.deleted_entries, "删除条目数正确");
    TEST_ASSERT_EQ(2, stats.write_count, "写操作数正确");
    TEST_ASSERT_EQ(1, stats.read_count, "读操作数正确");
    TEST_ASSERT_EQ(1, stats.delete_count, "删除操作数正确");
    
    printf("统计信息: 总条目=%lu, 活跃=%lu, 删除=%lu\n", 
           stats.total_entries, stats.active_entries, stats.deleted_entries);
    printf("操作计数: 写=%lu, 读=%lu, 删除=%lu\n", 
           stats.write_count, stats.read_count, stats.delete_count);
    
    lsm_memtable_destroy(memtable);
}

/**
 * 测试MemTable压力测试
 */
static void test_memtable_stress(void) {
    printf("\n=== 测试MemTable压力测试 ===\n");
    
    lsm_memtable_t *memtable = lsm_memtable_create(16 * 1024 * 1024, "stress_test");
    TEST_ASSERT_NOT_NULL(memtable, "压力测试MemTable创建成功");
    
    if (!memtable) return;
    
    const int test_count = 1000;
    char key[32], value[128];
    double start_time = get_time_ms();
    
    // 插入测试
    for (int i = 0; i < test_count; i++) {
        snprintf(key, sizeof(key), "stress_key_%06d", i);
        snprintf(value, sizeof(value), "stress_value_%06d_with_additional_data", i);
        
        int ret = lsm_memtable_put(memtable, key, strlen(key), value, strlen(value));
        if (ret != KV_ERR_NONE) {
            printf("插入失败在索引 %d, 错误码: %d\n", i, ret);
            break;
        }
    }
    
    double insert_time = get_time_ms() - start_time;
    uint64_t count = lsm_memtable_count(memtable);
    TEST_ASSERT(count > 0, "压力测试插入了数据");
    
    printf("插入 %lu 条记录，耗时 %.2f ms (%.0f ops/sec)\n", 
           count, insert_time, count * 1000.0 / insert_time);
    
    // 随机查找测试
    start_time = get_time_ms();
    int found_count = 0;
    
    for (int i = 0; i < test_count; i++) {
        snprintf(key, sizeof(key), "stress_key_%06d", i);
        
        char *found_value = NULL;
        size_t found_len = 0;
        uint64_t seq_num = 0;
        bool deleted = false;
        
        int ret = lsm_memtable_get(memtable, key, strlen(key), 
                                  &found_value, &found_len, &seq_num, &deleted);
        if (ret == KV_ERR_NONE) {
            found_count++;
            if (found_value) free(found_value);
        }
    }
    
    double search_time = get_time_ms() - start_time;
    TEST_ASSERT_EQ(test_count, found_count, "所有键都能找到");
    
    printf("查找 %d 条记录，耗时 %.2f ms (%.0f ops/sec)\n", 
           found_count, search_time, found_count * 1000.0 / search_time);
    
    // 内存使用统计
    size_t memory_usage = lsm_memtable_size(memtable);
    printf("内存使用: %zu 字节 (%.2f MB)\n", 
           memory_usage, memory_usage / 1024.0 / 1024.0);
    
    lsm_memtable_destroy(memtable);
}

/**
 * 打印测试总结
 */
static void print_test_summary(void) {
    printf("\n==================================================\n");
    printf("LSM-Tree MemTable 测试总结\n");
    printf("==================================================\n");
    printf("总测试数: %d\n", tests_run);
    printf("通过数量: %d\n", tests_passed);
    printf("失败数量: %d\n", tests_failed);
    printf("成功率: %.1f%%\n", tests_run > 0 ? (tests_passed * 100.0 / tests_run) : 0.0);
    
    if (tests_failed == 0) {
        printf("\n🎉 所有测试通过！LSM-Tree MemTable 实现质量良好。\n");
    } else {
        printf("\n⚠️  有 %d 个测试失败，需要检查实现。\n", tests_failed);
    }
}

/**
 * 超时处理函数
 */
static void timeout_handler(int sig) {
    (void)sig;  // 避免未使用参数警告
    printf("\n❌ 测试超时！可能存在死锁或无限循环。\n");
    exit(1);
}

/**
 * 主函数
 */
int main(void) {
    printf("开始 LSM-Tree MemTable 基础测试...\n");
    
    // 设置超时处理
    signal(SIGALRM, timeout_handler);
    alarm(TEST_TIMEOUT_SECONDS);
    
    double start_time = get_time_ms();
    
    // 运行所有测试
    test_memtable_create_destroy();
    test_memtable_basic_operations();
    test_memtable_delete_tombstone();
    test_memtable_size_limits();
    test_memtable_immutable();
    test_memtable_statistics();
    test_memtable_stress();
    
    double total_time = get_time_ms() - start_time;
    
    // 取消超时
    alarm(0);
    
    printf("\n总测试时间: %.2f ms\n", total_time);
    print_test_summary();
    
    return tests_failed == 0 ? 0 : 1;
} 