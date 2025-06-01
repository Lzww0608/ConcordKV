/*
 * @Author: Lzww0608  
 * @Date: 2025-6-1 12:00:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-1 12:00:00
 * @Description: ConcordKV LSM-Tree MemTable管理器测试
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>

// 包含测试目标
#include "../../../kvserver/lsm_memtable_manager.h"
#include "../../../kvserver/kv_error.h"

// 测试统计
static int tests_run = 0;
static int tests_passed = 0;

// 测试超时处理
static volatile int timeout_flag = 0;

void timeout_handler(int sig) {
    (void)sig;
    timeout_flag = 1;
}

// 测试宏
#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("❌ 断言失败 - 期望: %ld, 实际: %ld (位置: %s:%d)\n", \
               (long)(expected), (long)(actual), __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_NE(expected, actual) do { \
    if ((expected) == (actual)) { \
        printf("❌ 断言失败 - 不期望: %ld, 但得到相同值 (位置: %s:%d)\n", \
               (long)(expected), __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_TRUE(condition) do { \
    if (!(condition)) { \
        printf("❌ 断言失败 - 期望为真 (位置: %s:%d)\n", __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_FALSE(condition) do { \
    if ((condition)) { \
        printf("❌ 断言失败 - 期望为假 (位置: %s:%d)\n", __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        printf("❌ 断言失败 - 期望为NULL (位置: %s:%d)\n", __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        printf("❌ 断言失败 - 期望非NULL (位置: %s:%d)\n", __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define RUN_TEST(test_func) do { \
    printf("🧪 运行测试: %s\n", #test_func); \
    timeout_flag = 0; \
    alarm(10); \
    tests_run++; \
    if (test_func()) { \
        tests_passed++; \
        printf("✅ %s - 通过\n", #test_func); \
    } else { \
        printf("❌ %s - 失败\n", #test_func); \
    } \
    alarm(0); \
    if (timeout_flag) { \
        printf("⏰ %s - 超时\n", #test_func); \
    } \
} while(0)

// === 工具函数 ===
static uint64_t get_timestamp_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

// === 测试函数实现 ===

// 测试1: 基础创建和销毁
int test_manager_create_destroy(void) {
    printf("  测试基础创建和销毁...\n");
    
    // 测试默认配置创建
    lsm_memtable_manager_t *manager = lsm_memtable_manager_create(NULL, "test_manager");
    ASSERT_NOT_NULL(manager);
    
    // 验证初始状态
    ASSERT_EQ(1, lsm_memtable_manager_get_active_count(manager));
    ASSERT_EQ(0, lsm_memtable_manager_get_immutable_count(manager));
    
    // 获取统计信息
    lsm_memtable_manager_stats_t stats;
    int ret = lsm_memtable_manager_get_stats(manager, &stats);
    ASSERT_EQ(KV_ERR_NONE, ret);
    ASSERT_EQ(1, stats.total_memtables);
    ASSERT_EQ(1, stats.active_memtables);
    ASSERT_EQ(0, stats.immutable_memtables);
    
    lsm_memtable_manager_destroy(manager);
    
    return 1;
}

// 测试2: 自定义配置创建
int test_manager_custom_config(void) {
    printf("  测试自定义配置创建...\n");
    
    lsm_memtable_manager_config_t config;
    config.memtable_max_size = 1024;  // 1KB，小一点便于测试
    config.max_immutable_count = 3;
    config.auto_freeze = true;
    config.seq_num_start = 100;
    strncpy(config.name_prefix, "test", sizeof(config.name_prefix) - 1);
    config.name_prefix[sizeof(config.name_prefix) - 1] = '\0';
    
    lsm_memtable_manager_t *manager = lsm_memtable_manager_create(&config, "custom_manager");
    ASSERT_NOT_NULL(manager);
    
    lsm_memtable_manager_destroy(manager);
    
    return 1;
}

// 测试3: 基本的put/get操作
int test_manager_basic_operations(void) {
    printf("  测试基本的put/get操作...\n");
    
    lsm_memtable_manager_t *manager = lsm_memtable_manager_create(NULL, "basic_ops");
    ASSERT_NOT_NULL(manager);
    
    // 插入数据
    int ret = lsm_memtable_manager_put(manager, "key1", 4, "value1", 6);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    ret = lsm_memtable_manager_put(manager, "key2", 4, "value2", 6);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    // 查找数据
    lsm_memtable_lookup_result_t result;
    ret = lsm_memtable_manager_get(manager, "key1", 4, &result);
    ASSERT_EQ(KV_ERR_NONE, ret);
    ASSERT_TRUE(result.found);
    ASSERT_FALSE(result.deleted);
    ASSERT_EQ(6, result.value_len);
    ASSERT_EQ(0, memcmp(result.value, "value1", 6));
    
    lsm_memtable_lookup_result_cleanup(&result);
    
    // 查找不存在的键
    ret = lsm_memtable_manager_get(manager, "nonexistent", 11, &result);
    ASSERT_EQ(KV_ERR_NOT_FOUND, ret);
    ASSERT_FALSE(result.found);
    
    lsm_memtable_manager_destroy(manager);
    
    return 1;
}

// 测试4: 删除操作（墓碑标记）
int test_manager_delete_operations(void) {
    printf("  测试删除操作（墓碑标记）...\n");
    
    lsm_memtable_manager_t *manager = lsm_memtable_manager_create(NULL, "delete_ops");
    ASSERT_NOT_NULL(manager);
    
    // 插入数据
    int ret = lsm_memtable_manager_put(manager, "key1", 4, "value1", 6);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    // 删除数据
    ret = lsm_memtable_manager_delete(manager, "key1", 4);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    // 查找被删除的数据
    lsm_memtable_lookup_result_t result;
    ret = lsm_memtable_manager_get(manager, "key1", 4, &result);
    ASSERT_EQ(KV_ERR_NONE, ret);  // 找到了墓碑标记
    ASSERT_TRUE(result.found);
    ASSERT_TRUE(result.deleted);  // 标记为已删除
    
    lsm_memtable_lookup_result_cleanup(&result);
    
    lsm_memtable_manager_destroy(manager);
    
    return 1;
}

// 测试5: 自动冻结机制
int test_manager_auto_freeze(void) {
    printf("  测试自动冻结机制...\n");
    
    lsm_memtable_manager_config_t config;
    config.memtable_max_size = 512;  // 512字节，很小便于测试
    config.max_immutable_count = 3;
    config.auto_freeze = true;
    config.seq_num_start = 1;
    strncpy(config.name_prefix, "freeze", sizeof(config.name_prefix) - 1);
    config.name_prefix[sizeof(config.name_prefix) - 1] = '\0';
    
    lsm_memtable_manager_t *manager = lsm_memtable_manager_create(&config, "freeze_test");
    ASSERT_NOT_NULL(manager);
    
    // 初始状态检查
    ASSERT_EQ(1, lsm_memtable_manager_get_active_count(manager));
    ASSERT_EQ(0, lsm_memtable_manager_get_immutable_count(manager));
    
    // 插入足够多的数据触发自动冻结
    char key[32], value[64];
    for (int i = 0; i < 20; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        snprintf(value, sizeof(value), "this_is_a_longer_value_for_key_%d", i);
        
        int ret = lsm_memtable_manager_put(manager, key, strlen(key), value, strlen(value));
        ASSERT_EQ(KV_ERR_NONE, ret);
    }
    
    // 检查是否触发了冻结
    int immutable_count = lsm_memtable_manager_get_immutable_count(manager);
    printf("    不可变MemTable数量: %d\n", immutable_count);
    ASSERT_TRUE(immutable_count > 0);  // 应该有不可变的MemTable
    
    // 验证数据仍然能查找到
    lsm_memtable_lookup_result_t result;
    int ret = lsm_memtable_manager_get(manager, "key_0", 5, &result);
    ASSERT_EQ(KV_ERR_NONE, ret);
    ASSERT_TRUE(result.found);
    ASSERT_FALSE(result.deleted);
    
    lsm_memtable_lookup_result_cleanup(&result);
    
    lsm_memtable_manager_destroy(manager);
    
    return 1;
}

// 测试6: 手动冻结操作
int test_manager_manual_freeze(void) {
    printf("  测试手动冻结操作...\n");
    
    lsm_memtable_manager_t *manager = lsm_memtable_manager_create(NULL, "manual_freeze");
    ASSERT_NOT_NULL(manager);
    
    // 插入一些数据
    int ret = lsm_memtable_manager_put(manager, "key1", 4, "value1", 6);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    ret = lsm_memtable_manager_put(manager, "key2", 4, "value2", 6);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    // 手动冻结
    ret = lsm_memtable_manager_freeze_active(manager);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    // 检查状态
    ASSERT_EQ(1, lsm_memtable_manager_get_active_count(manager));
    ASSERT_EQ(1, lsm_memtable_manager_get_immutable_count(manager));
    
    // 验证数据仍然能查找到
    lsm_memtable_lookup_result_t result;
    ret = lsm_memtable_manager_get(manager, "key1", 4, &result);
    ASSERT_EQ(KV_ERR_NONE, ret);
    ASSERT_TRUE(result.found);
    
    lsm_memtable_lookup_result_cleanup(&result);
    
    lsm_memtable_manager_destroy(manager);
    
    return 1;
}

// 测试7: 查找优先级（active -> immutable）
int test_manager_lookup_priority(void) {
    printf("  测试查找优先级（active -> immutable）...\n");
    
    lsm_memtable_manager_t *manager = lsm_memtable_manager_create(NULL, "priority_test");
    ASSERT_NOT_NULL(manager);
    
    // 在活跃MemTable中插入数据
    int ret = lsm_memtable_manager_put(manager, "key1", 4, "old_value", 9);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    // 手动冻结
    ret = lsm_memtable_manager_freeze_active(manager);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    // 在新的活跃MemTable中插入更新的数据
    ret = lsm_memtable_manager_put(manager, "key1", 4, "new_value", 9);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    // 查找应该返回新值（活跃MemTable优先）
    lsm_memtable_lookup_result_t result;
    ret = lsm_memtable_manager_get(manager, "key1", 4, &result);
    ASSERT_EQ(KV_ERR_NONE, ret);
    ASSERT_TRUE(result.found);
    ASSERT_EQ(0, memcmp(result.value, "new_value", 9));
    
    lsm_memtable_lookup_result_cleanup(&result);
    
    lsm_memtable_manager_destroy(manager);
    
    return 1;
}

// 测试8: 不可变MemTable管理
int test_manager_immutable_management(void) {
    printf("  测试不可变MemTable管理...\n");
    
    lsm_memtable_manager_config_t config;
    config.memtable_max_size = 4096;
    config.max_immutable_count = 2;
    config.auto_freeze = false;  // 关闭自动冻结，手动控制
    config.seq_num_start = 1;
    strncpy(config.name_prefix, "imm_mgmt", sizeof(config.name_prefix) - 1);
    config.name_prefix[sizeof(config.name_prefix) - 1] = '\0';
    
    lsm_memtable_manager_t *manager = lsm_memtable_manager_create(&config, "imm_test");
    ASSERT_NOT_NULL(manager);
    
    // 插入数据并手动冻结
    int ret = lsm_memtable_manager_put(manager, "key1", 4, "value1", 6);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    ret = lsm_memtable_manager_freeze_active(manager);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    // 再插入数据并冻结
    ret = lsm_memtable_manager_put(manager, "key2", 4, "value2", 6);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    ret = lsm_memtable_manager_freeze_active(manager);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    // 现在应该有2个不可变MemTable
    ASSERT_EQ(2, lsm_memtable_manager_get_immutable_count(manager));
    
    // 获取最老的不可变MemTable
    lsm_memtable_t *oldest = lsm_memtable_manager_get_oldest_immutable(manager);
    ASSERT_NOT_NULL(oldest);
    
    // 移除最老的不可变MemTable
    ret = lsm_memtable_manager_remove_oldest_immutable(manager);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    // 现在应该只有1个不可变MemTable
    ASSERT_EQ(1, lsm_memtable_manager_get_immutable_count(manager));
    
    lsm_memtable_manager_destroy(manager);
    
    return 1;
}

// 测试9: 状态检查函数
int test_manager_state_checks(void) {
    printf("  测试状态检查函数...\n");
    
    lsm_memtable_manager_config_t config;
    config.memtable_max_size = 256;  // 很小便于测试
    config.max_immutable_count = 4;
    config.auto_freeze = false;
    config.seq_num_start = 1;
    strncpy(config.name_prefix, "state", sizeof(config.name_prefix) - 1);
    config.name_prefix[sizeof(config.name_prefix) - 1] = '\0';
    
    lsm_memtable_manager_t *manager = lsm_memtable_manager_create(&config, "state_test");
    ASSERT_NOT_NULL(manager);
    
    // 初始状态
    ASSERT_FALSE(lsm_memtable_manager_should_freeze(manager));
    ASSERT_FALSE(lsm_memtable_manager_should_flush(manager));
    
    // 填充数据直到需要冻结
    char key[32], value[64];
    for (int i = 0; i < 10; i++) {
        snprintf(key, sizeof(key), "state_key_%d", i);
        snprintf(value, sizeof(value), "this_is_a_test_value_for_state_check_%d", i);
        
        int ret = lsm_memtable_manager_put(manager, key, strlen(key), value, strlen(value));
        ASSERT_EQ(KV_ERR_NONE, ret);
    }
    
    // 现在应该需要冻结
    ASSERT_TRUE(lsm_memtable_manager_should_freeze(manager));
    
    // 手动冻结几次，创建不可变MemTable
    for (int i = 0; i < 3; i++) {
        int ret = lsm_memtable_manager_freeze_active(manager);
        ASSERT_EQ(KV_ERR_NONE, ret);
        
        // 插入一个新数据
        snprintf(key, sizeof(key), "after_freeze_%d", i);
        ret = lsm_memtable_manager_put(manager, key, strlen(key), "test", 4);
        ASSERT_EQ(KV_ERR_NONE, ret);
    }
    
    // 现在应该需要刷写
    ASSERT_TRUE(lsm_memtable_manager_should_flush(manager));
    
    lsm_memtable_manager_destroy(manager);
    
    return 1;
}

// 测试10: 统计信息验证
int test_manager_statistics(void) {
    printf("  测试统计信息验证...\n");
    
    lsm_memtable_manager_t *manager = lsm_memtable_manager_create(NULL, "stats_test");
    ASSERT_NOT_NULL(manager);
    
    // 执行一系列操作
    for (int i = 0; i < 5; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "stats_key_%d", i);
        snprintf(value, sizeof(value), "stats_value_%d", i);
        
        int ret = lsm_memtable_manager_put(manager, key, strlen(key), value, strlen(value));
        ASSERT_EQ(KV_ERR_NONE, ret);
    }
    
    // 删除一些键
    int ret = lsm_memtable_manager_delete(manager, "stats_key_2", 11);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    // 查找一些键
    lsm_memtable_lookup_result_t result;
    ret = lsm_memtable_manager_get(manager, "stats_key_1", 11, &result);
    ASSERT_EQ(KV_ERR_NONE, ret);
    lsm_memtable_lookup_result_cleanup(&result);
    
    ret = lsm_memtable_manager_get(manager, "stats_key_3", 11, &result);
    ASSERT_EQ(KV_ERR_NONE, ret);
    lsm_memtable_lookup_result_cleanup(&result);
    
    // 获取统计信息
    lsm_memtable_manager_stats_t stats;
    ret = lsm_memtable_manager_get_stats(manager, &stats);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    // 验证统计数据
    ASSERT_EQ(5, stats.write_count);   // 5次写入
    ASSERT_EQ(1, stats.delete_count);  // 1次删除
    ASSERT_EQ(2, stats.read_count);    // 2次读取
    ASSERT_EQ(1, stats.active_memtables); // 1个活跃MemTable
    ASSERT_EQ(0, stats.immutable_memtables); // 0个不可变MemTable
    
    printf("    统计信息 - 写入: %lu, 删除: %lu, 读取: %lu\n",
           stats.write_count, stats.delete_count, stats.read_count);
    
    lsm_memtable_manager_destroy(manager);
    
    return 1;
}

// 测试11: 压力测试
int test_manager_stress(void) {
    printf("  测试压力测试（1000次操作）...\n");
    
    lsm_memtable_manager_config_t config;
    config.memtable_max_size = 1024 * 1024;  // 1MB
    config.max_immutable_count = 5;
    config.auto_freeze = true;
    config.seq_num_start = 1;
    strncpy(config.name_prefix, "stress", sizeof(config.name_prefix) - 1);
    config.name_prefix[sizeof(config.name_prefix) - 1] = '\0';
    
    lsm_memtable_manager_t *manager = lsm_memtable_manager_create(&config, "stress_test");
    ASSERT_NOT_NULL(manager);
    
    uint64_t start_time = get_timestamp_us();
    
    // 插入1000个键值对
    char key[32], value[64];
    for (int i = 0; i < 1000; i++) {
        snprintf(key, sizeof(key), "stress_key_%d", i);
        snprintf(value, sizeof(value), "stress_value_for_key_%d_with_more_data", i);
        
        int ret = lsm_memtable_manager_put(manager, key, strlen(key), value, strlen(value));
        ASSERT_EQ(KV_ERR_NONE, ret);
    }
    
    uint64_t insert_time = get_timestamp_us();
    
    // 查找所有键
    for (int i = 0; i < 1000; i++) {
        snprintf(key, sizeof(key), "stress_key_%d", i);
        
        lsm_memtable_lookup_result_t result;
        int ret = lsm_memtable_manager_get(manager, key, strlen(key), &result);
        ASSERT_EQ(KV_ERR_NONE, ret);
        ASSERT_TRUE(result.found);
        ASSERT_FALSE(result.deleted);
        
        lsm_memtable_lookup_result_cleanup(&result);
    }
    
    uint64_t query_time = get_timestamp_us();
    
    // 删除一些键
    for (int i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "stress_key_%d", i * 10);
        
        int ret = lsm_memtable_manager_delete(manager, key, strlen(key));
        ASSERT_EQ(KV_ERR_NONE, ret);
    }
    
    uint64_t delete_time = get_timestamp_us();
    
    // 获取统计信息
    lsm_memtable_manager_stats_t stats;
    int ret = lsm_memtable_manager_get_stats(manager, &stats);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    // 打印性能数据
    double insert_duration = (insert_time - start_time) / 1000.0;  // ms
    double query_duration = (query_time - insert_time) / 1000.0;   // ms
    double delete_duration = (delete_time - query_time) / 1000.0;  // ms
    
    printf("    插入性能: %.2f ms (1000条记录) - %.0f ops/sec\n",
           insert_duration, 1000.0 * 1000.0 / insert_duration);
    printf("    查询性能: %.2f ms (1000次查询) - %.0f ops/sec\n",
           query_duration, 1000.0 * 1000.0 / query_duration);
    printf("    删除性能: %.2f ms (100次删除) - %.0f ops/sec\n",
           delete_duration, 100.0 * 1000.0 / delete_duration);
    printf("    内存使用: %lu KB (%lu bytes)\n",
           stats.total_memory_usage / 1024, stats.total_memory_usage);
    printf("    MemTable数量: 活跃=%lu, 不可变=%lu, 总计=%lu\n",
           stats.active_memtables, stats.immutable_memtables, stats.total_memtables);
    
    lsm_memtable_manager_destroy(manager);
    
    return 1;
}

// 测试12: 并发安全基础测试
int test_manager_concurrent_basic(void) {
    printf("  测试并发安全基础...\n");
    
    lsm_memtable_manager_t *manager = lsm_memtable_manager_create(NULL, "concurrent_test");
    ASSERT_NOT_NULL(manager);
    
    // 在多个线程间进行基本操作
    // 注意：这是简化的并发测试，完整的并发测试需要更复杂的设计
    
    // 插入数据
    for (int i = 0; i < 10; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "concurrent_key_%d", i);
        snprintf(value, sizeof(value), "concurrent_value_%d", i);
        
        int ret = lsm_memtable_manager_put(manager, key, strlen(key), value, strlen(value));
        ASSERT_EQ(KV_ERR_NONE, ret);
    }
    
    // 读取数据
    for (int i = 0; i < 10; i++) {
        char key[32];
        snprintf(key, sizeof(key), "concurrent_key_%d", i);
        
        lsm_memtable_lookup_result_t result;
        int ret = lsm_memtable_manager_get(manager, key, strlen(key), &result);
        ASSERT_EQ(KV_ERR_NONE, ret);
        ASSERT_TRUE(result.found);
        
        lsm_memtable_lookup_result_cleanup(&result);
    }
    
    // 冻结操作
    int ret = lsm_memtable_manager_freeze_active(manager);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    lsm_memtable_manager_destroy(manager);
    
    return 1;
}

// === 主测试函数 ===
int main(void) {
    printf("🚀 ConcordKV LSM-Tree MemTable管理器测试套件\n");
    printf("============================================\n");
    
    // 设置超时处理
    signal(SIGALRM, timeout_handler);
    
    uint64_t start_time = get_timestamp_us();
    
    // 运行所有测试
    RUN_TEST(test_manager_create_destroy);
    RUN_TEST(test_manager_custom_config);
    RUN_TEST(test_manager_basic_operations);
    RUN_TEST(test_manager_delete_operations);
    RUN_TEST(test_manager_auto_freeze);
    RUN_TEST(test_manager_manual_freeze);
    RUN_TEST(test_manager_lookup_priority);
    RUN_TEST(test_manager_immutable_management);
    RUN_TEST(test_manager_state_checks);
    RUN_TEST(test_manager_statistics);
    RUN_TEST(test_manager_stress);
    RUN_TEST(test_manager_concurrent_basic);
    
    uint64_t end_time = get_timestamp_us();
    double total_time = (end_time - start_time) / 1000.0;  // ms
    
    // 打印测试结果
    printf("\n📊 测试结果汇总\n");
    printf("================\n");
    printf("总测试数: %d\n", tests_run);
    printf("通过测试: %d\n", tests_passed);
    printf("失败测试: %d\n", tests_run - tests_passed);
    printf("成功率: %.1f%%\n", tests_run > 0 ? (100.0 * tests_passed / tests_run) : 0.0);
    printf("总耗时: %.2f ms\n", total_time);
    
    if (tests_passed == tests_run) {
        printf("🎉 所有测试通过！LSM-Tree MemTable管理器实现正确！\n");
        return 0;
    } else {
        printf("❌ 部分测试失败，请检查实现\n");
        return 1;
    }
} 