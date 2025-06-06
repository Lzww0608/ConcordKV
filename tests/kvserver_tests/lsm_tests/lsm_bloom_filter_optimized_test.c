/*
 * @Author: Lzww0608  
 * @Date: 2025-6-6 16:25:33
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-6 16:25:36
 * @Description: ConcordKV LSM-Tree 优化布隆过滤器测试
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <math.h>

#include "../../../kvserver/lsm_bloom_filter_optimized.h"
#include "../../../kvserver/kv_error.h"

// === 测试框架宏 ===
#define EXPECT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("❌ 断言失败 - 期望: %ld, 实际: %ld (位置: %s:%d)\n", \
               (long)(expected), (long)(actual), __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define EXPECT_NE(not_expected, actual) do { \
    if ((not_expected) == (actual)) { \
        printf("❌ 断言失败 - 不期望: %ld, 实际: %ld (位置: %s:%d)\n", \
               (long)(not_expected), (long)(actual), __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define EXPECT_TRUE(condition) do { \
    if (!(condition)) { \
        printf("❌ 断言失败 - 期望为真 (位置: %s:%d)\n", __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define EXPECT_FALSE(condition) do { \
    if (condition) { \
        printf("❌ 断言失败 - 期望为假 (位置: %s:%d)\n", __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define EXPECT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        printf("❌ 断言失败 - 期望非NULL (位置: %s:%d)\n", __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define EXPECT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        printf("❌ 断言失败 - 期望为NULL (位置: %s:%d)\n", __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define EXPECT_NEAR(expected, actual, tolerance) do { \
    double diff = fabs((double)(expected) - (double)(actual)); \
    if (diff > (tolerance)) { \
        printf("❌ 断言失败 - 期望: %f, 实际: %f, 差异: %f > %f (位置: %s:%d)\n", \
               (double)(expected), (double)(actual), diff, (double)(tolerance), __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define RUN_TEST(test_func) do { \
    printf("🧪 运行测试: %s\n", #test_func); \
    timeout_flag = 0; \
    alarm(30); \
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

// === 全局变量 ===
static int tests_run = 0;
static int tests_passed = 0;
static volatile int timeout_flag = 0;

// === 超时处理 ===
static void timeout_handler(int sig) {
    timeout_flag = 1;
}

// === 工具函数 ===
static uint64_t get_timestamp_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static void generate_test_key(char *buffer, size_t buffer_size, int index) {
    snprintf(buffer, buffer_size, "test_key_%d", index);
}

static double calculate_false_positive_rate(int false_positives, int total_tests) {
    if (total_tests == 0) return 0.0;
    return (double)false_positives / total_tests;
}

// === 测试函数实现 ===

// 测试1: 参数计算器基础功能
int test_parameter_calculator_basic(void) {
    printf("  测试参数计算器基础功能...\n");
    
    bloom_parameter_calculator_t calc = {0};
    calc.expected_entries = 10000;
    calc.target_fpr = 100; // 1%
    
    int ret = bloom_filter_calculate_parameters(&calc);
    EXPECT_EQ(KV_ERR_NONE, ret);
    
    // 验证计算结果的合理性
    EXPECT_TRUE(calc.optimal_bit_count > 0);
    EXPECT_TRUE(calc.optimal_hash_count > 0);
    EXPECT_TRUE(calc.optimal_hash_count <= 16); // 合理的哈希函数数量
    EXPECT_TRUE(calc.bits_per_element > 0);
    EXPECT_TRUE(calc.actual_fpr > 0 && calc.actual_fpr < 1.0);
    EXPECT_TRUE(calc.memory_requirement > 0);
    
    printf("    计算结果:\n");
    printf("      位数组大小: %zu bits\n", calc.optimal_bit_count);
    printf("      哈希函数数量: %u\n", calc.optimal_hash_count);
    printf("      每元素位数: %.2f\n", calc.bits_per_element);
    printf("      实际误判率: %.4f%%\n", calc.actual_fpr * 100);
    printf("      内存需求: %zu bytes\n", calc.memory_requirement);
    
    return 1;
}

// 测试2: 配置优化功能
int test_config_optimization(void) {
    printf("  测试配置优化功能...\n");
    
    // 测试内存优化
    bloom_filter_config_t config = bloom_filter_create_default_config(10000, 50);
    int ret = bloom_filter_optimize_config(&config, BLOOM_OPT_MEMORY);
    EXPECT_EQ(KV_ERR_NONE, ret);
    EXPECT_TRUE(config.false_positive_rate >= 200); // 内存优化应该增加误判率
    EXPECT_TRUE(config.max_hash_functions <= 8);    // 减少哈希函数数量
    
    printf("    内存优化结果: FPR=%u, MaxHash=%u, Type=%d\n", 
           config.false_positive_rate, config.max_hash_functions, config.type);
    
    // 测试速度优化
    config = bloom_filter_create_default_config(10000, 100);
    ret = bloom_filter_optimize_config(&config, BLOOM_OPT_SPEED);
    EXPECT_EQ(KV_ERR_NONE, ret);
    EXPECT_EQ(BLOOM_TYPE_BLOCKED, config.type);
    EXPECT_EQ(BLOOM_HASH_MURMUR3, config.hash_type);
    
    printf("    速度优化结果: Type=%d, HashType=%d, BlockSize=%u\n", 
           config.type, config.hash_type, config.block_size);
    
    // 测试平衡优化
    config = bloom_filter_create_default_config(100000, 50);
    ret = bloom_filter_optimize_config(&config, BLOOM_OPT_BALANCED);
    EXPECT_EQ(KV_ERR_NONE, ret);
    EXPECT_EQ(100, config.false_positive_rate); // 1%
    EXPECT_EQ(BLOOM_TYPE_REGISTER_BLOCKED, config.type);
    
    printf("    平衡优化结果: FPR=%u, Type=%d, MaxHash=%u\n", 
           config.false_positive_rate, config.type, config.max_hash_functions);
    
    return 1;
}

// 测试3: 哈希函数性能比较
int test_hash_function_performance(void) {
    printf("  测试哈希函数性能比较...\n");
    
    const char *test_keys[] = {
        "short", "medium_length_key", "very_long_test_key_for_hashing_performance_evaluation",
        "numeric123", "special!@#$%", "unicode测试", "mixed_Case_Key_123"
    };
    const int key_count = sizeof(test_keys) / sizeof(test_keys[0]);
    const int iterations = 10000;
    
    // 测试每种哈希函数
    uint64_t start_time, end_time;
    double duration;
    
    // MurmurHash3 32位
    start_time = get_timestamp_us();
    for (int iter = 0; iter < iterations; iter++) {
        for (int i = 0; i < key_count; i++) {
            uint32_t hash = bloom_hash_murmur3_32(test_keys[i], strlen(test_keys[i]), iter);
            (void)hash; // 防止编译器优化
        }
    }
    end_time = get_timestamp_us();
    duration = (end_time - start_time) / 1000.0;
    double murmur3_32_ops = (iterations * key_count * 1000.0) / duration;
    printf("    MurmurHash3-32: %.2f ms, %.0f ops/sec\n", duration, murmur3_32_ops);
    
    // MurmurHash3 64位
    start_time = get_timestamp_us();
    for (int iter = 0; iter < iterations; iter++) {
        for (int i = 0; i < key_count; i++) {
            uint64_t hash = bloom_hash_murmur3_64(test_keys[i], strlen(test_keys[i]), iter);
            (void)hash;
        }
    }
    end_time = get_timestamp_us();
    duration = (end_time - start_time) / 1000.0;
    double murmur3_64_ops = (iterations * key_count * 1000.0) / duration;
    printf("    MurmurHash3-64: %.2f ms, %.0f ops/sec\n", duration, murmur3_64_ops);
    
    // FNV-1a 32位
    start_time = get_timestamp_us();
    for (int iter = 0; iter < iterations; iter++) {
        for (int i = 0; i < key_count; i++) {
            uint32_t hash = bloom_hash_fnv1a_32(test_keys[i], strlen(test_keys[i]), iter);
            (void)hash;
        }
    }
    end_time = get_timestamp_us();
    duration = (end_time - start_time) / 1000.0;
    double fnv1a_32_ops = (iterations * key_count * 1000.0) / duration;
    printf("    FNV-1a-32: %.2f ms, %.0f ops/sec\n", duration, fnv1a_32_ops);
    
    // xxHash 32位
    start_time = get_timestamp_us();
    for (int iter = 0; iter < iterations; iter++) {
        for (int i = 0; i < key_count; i++) {
            uint32_t hash = bloom_hash_xxhash_32(test_keys[i], strlen(test_keys[i]), iter);
            (void)hash;
        }
    }
    end_time = get_timestamp_us();
    duration = (end_time - start_time) / 1000.0;
    double xxhash_32_ops = (iterations * key_count * 1000.0) / duration;
    printf("    xxHash-32: %.2f ms, %.0f ops/sec\n", duration, xxhash_32_ops);
    
    // 验证哈希函数产生不同的值
    uint32_t hash1 = bloom_hash_murmur3_32("test", 4, 0);
    uint32_t hash2 = bloom_hash_murmur3_32("test", 4, 1);
    EXPECT_NE(hash1, hash2); // 不同种子应产生不同哈希
    
    uint32_t hash3 = bloom_hash_murmur3_32("test1", 5, 0);
    uint32_t hash4 = bloom_hash_murmur3_32("test2", 5, 0);
    EXPECT_NE(hash3, hash4); // 不同内容应产生不同哈希
    
    return 1;
}

// 测试4: 哈希函数质量测试
int test_hash_function_quality(void) {
    printf("  测试哈希函数质量...\n");
    
    const int test_size = 10000;
    const int bucket_count = 1000;
    int buckets[bucket_count];
    
    // 测试MurmurHash3的分布质量
    memset(buckets, 0, sizeof(buckets));
    for (int i = 0; i < test_size; i++) {
        char key[32];
        snprintf(key, sizeof(key), "quality_test_%d", i);
        uint32_t hash = bloom_hash_murmur3_32(key, strlen(key), 0);
        buckets[hash % bucket_count]++;
    }
    
    // 计算分布的方差
    double mean = (double)test_size / bucket_count;
    double variance = 0.0;
    for (int i = 0; i < bucket_count; i++) {
        double diff = buckets[i] - mean;
        variance += diff * diff;
    }
    variance /= bucket_count;
    double std_dev = sqrt(variance);
    
    printf("    MurmurHash3分布统计:\n");
    printf("      期望每桶: %.2f\n", mean);
    printf("      方差: %.2f\n", variance);
    printf("      标准差: %.2f\n", std_dev);
    printf("      变异系数: %.4f\n", std_dev / mean);
    
    // 好的哈希函数的变异系数应该较小(通常<0.1)
    // 由于测试样本相对较小，放宽到0.4以允许一定的分布不均匀
    EXPECT_TRUE(std_dev / mean < 0.4);
    
    return 1;
}

// 测试5: 自动哈希选择功能
int test_hash_auto_selection(void) {
    printf("  测试自动哈希选择功能...\n");
    
    // 小规模数据
    bloom_hash_type_t hash_type = bloom_hash_auto_select(1000, 4);
    EXPECT_EQ(BLOOM_HASH_MURMUR3, hash_type);
    printf("    小规模(1K): %d\n", hash_type);
    
    // 中等规模数据，少量哈希函数
    hash_type = bloom_hash_auto_select(100000, 4);
    EXPECT_EQ(BLOOM_HASH_XXHASH, hash_type);
    printf("    中等规模(100K, 少哈希): %d\n", hash_type);
    
    // 中等规模数据，多量哈希函数
    hash_type = bloom_hash_auto_select(100000, 8);
    EXPECT_EQ(BLOOM_HASH_FNV1A, hash_type);
    printf("    中等规模(100K, 多哈希): %d\n", hash_type);
    
    // 大规模数据
    hash_type = bloom_hash_auto_select(10000000, 6);
    EXPECT_EQ(BLOOM_HASH_FNV1A, hash_type);
    printf("    大规模(10M): %d\n", hash_type);
    
    return 1;
}

// 测试6: 内存估算准确性
int test_memory_estimation(void) {
    printf("  测试内存估算准确性...\n");
    
    // 标准布隆过滤器
    bloom_filter_config_t config = bloom_filter_create_default_config(10000, 100);
    config.type = BLOOM_TYPE_STANDARD;
    
    size_t estimated = bloom_filter_estimate_memory_optimized(&config);
    EXPECT_TRUE(estimated > 0);
    
    printf("    标准过滤器估算内存: %zu bytes\n", estimated);
    
    // 块布隆过滤器
    config.type = BLOOM_TYPE_BLOCKED;
    size_t blocked_estimated = bloom_filter_estimate_memory_optimized(&config);
    EXPECT_TRUE(blocked_estimated > estimated); // 块过滤器应该使用更多内存
    
    printf("    块过滤器估算内存: %zu bytes\n", blocked_estimated);
    
    // 寄存器块布隆过滤器
    config.type = BLOOM_TYPE_REGISTER_BLOCKED;
    size_t register_estimated = bloom_filter_estimate_memory_optimized(&config);
    EXPECT_TRUE(register_estimated > 0);
    
    printf("    寄存器块过滤器估算内存: %zu bytes\n", register_estimated);
    
    // 计数布隆过滤器
    config.type = BLOOM_TYPE_COUNTING;
    size_t counting_estimated = bloom_filter_estimate_memory_optimized(&config);
    EXPECT_TRUE(counting_estimated > blocked_estimated); // 计数过滤器应该使用最多内存
    
    printf("    计数过滤器估算内存: %zu bytes\n", counting_estimated);
    
    return 1;
}

// 测试7: 参数边界条件测试
int test_parameter_boundary_conditions(void) {
    printf("  测试参数边界条件...\n");
    
    bloom_parameter_calculator_t calc;
    
    // 测试最小值
    calc.expected_entries = BLOOM_MIN_EXPECTED_ENTRIES;
    calc.target_fpr = BLOOM_MIN_FALSE_POSITIVE;
    int ret = bloom_filter_calculate_parameters(&calc);
    EXPECT_EQ(KV_ERR_NONE, ret);
    EXPECT_TRUE(calc.optimal_bit_count > 0);
    EXPECT_TRUE(calc.optimal_hash_count > 0);
    
    printf("    最小参数 - 位数组: %zu, 哈希数: %u\n", 
           calc.optimal_bit_count, calc.optimal_hash_count);
    
    // 测试最大值
    calc.expected_entries = BLOOM_MAX_EXPECTED_ENTRIES;
    calc.target_fpr = BLOOM_MAX_FALSE_POSITIVE;
    ret = bloom_filter_calculate_parameters(&calc);
    EXPECT_EQ(KV_ERR_NONE, ret);
    EXPECT_TRUE(calc.optimal_bit_count > 0);
    EXPECT_TRUE(calc.optimal_hash_count > 0);
    
    printf("    最大参数 - 位数组: %zu, 哈希数: %u\n", 
           calc.optimal_bit_count, calc.optimal_hash_count);
    
    // 测试无效参数
    calc.expected_entries = 0;
    calc.target_fpr = 100;
    ret = bloom_filter_calculate_parameters(&calc);
    EXPECT_NE(KV_ERR_NONE, ret);
    
    calc.expected_entries = 1000;
    calc.target_fpr = 0;
    ret = bloom_filter_calculate_parameters(&calc);
    EXPECT_NE(KV_ERR_NONE, ret);
    
    return 1;
}

// 测试8: 配置默认值检查
int test_default_config_validation(void) {
    printf("  测试配置默认值检查...\n");
    
    bloom_filter_config_t config = bloom_filter_create_default_config(10000, 100);
    
    // 验证默认值
    EXPECT_EQ(10000, config.expected_entries);
    EXPECT_EQ(100, config.false_positive_rate);
    EXPECT_EQ(BLOOM_TYPE_AUTO, config.type);
    EXPECT_EQ(BLOOM_HASH_HYBRID, config.hash_type);
    EXPECT_EQ(BLOOM_OPT_AUTO, config.optimization_flags);
    EXPECT_TRUE(config.enable_statistics);
    EXPECT_FALSE(config.enable_auto_resize);
    EXPECT_EQ(0.8, config.load_factor_threshold);
    
    printf("    默认配置验证通过\n");
    
    // 测试边界值自动调整
    config = bloom_filter_create_default_config(50, 0); // 低于最小值
    EXPECT_EQ(BLOOM_MIN_EXPECTED_ENTRIES, config.expected_entries);
    EXPECT_EQ(BLOOM_MIN_FALSE_POSITIVE, config.false_positive_rate);
    
    config = bloom_filter_create_default_config(1000000000, 10000); // 高于最大值
    EXPECT_EQ(BLOOM_MAX_EXPECTED_ENTRIES, config.expected_entries);
    EXPECT_EQ(BLOOM_MAX_FALSE_POSITIVE, config.false_positive_rate);
    
    printf("    边界值自动调整验证通过\n");
    
    return 1;
}

// 测试9: 哈希碰撞率测试
int test_hash_collision_rate(void) {
    printf("  测试哈希碰撞率...\n");
    
    const int test_count = 100000;
    const uint32_t seed = 12345;
    
    // 使用集合来检测碰撞
    uint32_t *hashes = malloc(test_count * sizeof(uint32_t));
    EXPECT_NOT_NULL(hashes);
    
    // 生成哈希值
    for (int i = 0; i < test_count; i++) {
        char key[32];
        snprintf(key, sizeof(key), "collision_test_%d", i);
        hashes[i] = bloom_hash_murmur3_32(key, strlen(key), seed);
    }
    
    // 检查碰撞
    int collisions = 0;
    for (int i = 0; i < test_count; i++) {
        for (int j = i + 1; j < test_count; j++) {
            if (hashes[i] == hashes[j]) {
                collisions++;
            }
        }
    }
    
    double collision_rate = (double)collisions / test_count;
    printf("    哈希碰撞率: %.6f%% (%d/%d)\n", collision_rate * 100, collisions, test_count);
    
    // 期望碰撞率应该很低
    EXPECT_TRUE(collision_rate < 0.001); // 小于0.1%
    
    free(hashes);
    return 1;
}

// 测试10: 性能基准对比
int test_performance_benchmark(void) {
    printf("  测试性能基准对比...\n");
    
    const int test_iterations = 50000;
    const char* test_key = "performance_benchmark_test_key_with_reasonable_length";
    const size_t key_len = strlen(test_key);
    
    struct {
        const char *name;
        uint64_t (*hash_func)(const void*, size_t, uint32_t);
        double ops_per_sec;
    } hash_functions[] = {
        {"MurmurHash3-64", bloom_hash_murmur3_64, 0},
        {"FNV-1a-64", bloom_hash_fnv1a_64, 0},
        {"xxHash-64", bloom_hash_xxhash_64, 0}
    };
    const int func_count = sizeof(hash_functions) / sizeof(hash_functions[0]);
    
    printf("    性能基准测试结果:\n");
    
    for (int f = 0; f < func_count; f++) {
        uint64_t start_time = get_timestamp_us();
        
        for (int i = 0; i < test_iterations; i++) {
            uint64_t hash = hash_functions[f].hash_func(test_key, key_len, i);
            (void)hash; // 防止优化
        }
        
        uint64_t end_time = get_timestamp_us();
        double duration_ms = (end_time - start_time) / 1000.0;
        hash_functions[f].ops_per_sec = (test_iterations * 1000.0) / duration_ms;
        
        printf("      %s: %.2f ms, %.0f ops/sec\n", 
               hash_functions[f].name, duration_ms, hash_functions[f].ops_per_sec);
    }
    
    // 验证所有哈希函数都有合理的性能
    for (int f = 0; f < func_count; f++) {
        EXPECT_TRUE(hash_functions[f].ops_per_sec > 100000); // 至少10万ops/sec
    }
    
    return 1;
}

// === 主测试函数 ===
int main(void) {
    printf("🔬 开始 LSM-Tree 优化布隆过滤器测试\n");
    printf("=====================================\n");
    
    // 设置超时处理
    signal(SIGALRM, timeout_handler);
    
    // 运行所有测试
    RUN_TEST(test_parameter_calculator_basic);
    RUN_TEST(test_config_optimization);
    RUN_TEST(test_hash_function_performance);
    RUN_TEST(test_hash_function_quality);
    RUN_TEST(test_hash_auto_selection);
    RUN_TEST(test_memory_estimation);
    RUN_TEST(test_parameter_boundary_conditions);
    RUN_TEST(test_default_config_validation);
    RUN_TEST(test_hash_collision_rate);
    RUN_TEST(test_performance_benchmark);
    
    // 输出测试结果
    printf("\n📊 测试结果总结\n");
    printf("===============\n");
    printf("总测试数: %d\n", tests_run);
    printf("通过测试: %d\n", tests_passed);
    printf("失败测试: %d\n", tests_run - tests_passed);
    printf("成功率: %.1f%%\n", tests_run > 0 ? (100.0 * tests_passed / tests_run) : 0.0);
    
    if (tests_passed == tests_run) {
        printf("🎉 所有测试通过！\n");
        return 0;
    } else {
        printf("❌ 有测试失败！\n");
        return 1;
    }
} 