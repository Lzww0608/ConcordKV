/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV storage engine - test_hash_collision.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "kv_store.h"
#include "rbtree_adapter.h"

// 测试参数
#define BUCKET_COUNT        64      // 桶数量
#define COLLISION_KEYS      1000    // 冲突键数量
#define NORMAL_KEYS         10000   // 普通键数量
#define ITERATIONS          100     // 查询迭代次数
#define DEBUG_OUTPUT        1       // 是否输出调试信息

// 生成随机字符串
void generate_random_string(char *str, int length) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < length - 1; i++) {
        int index = rand() % (sizeof(charset) - 1);
        str[i] = charset[index];
    }
    str[length - 1] = '\0';
}

// 获取当前时间（毫秒）
long long current_time_millis() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// 创建冲突键
char *create_collision_key(int bucket_idx, int key_idx) {
    char *key = (char *)malloc(64);
    if (!key) return NULL;
    
    sprintf(key, "bucket%d_key%d", bucket_idx, key_idx);
    return key;
}

// 创建随机键
char *create_random_key(int idx) {
    char *key = (char *)malloc(64);
    if (!key) return NULL;
    
    sprintf(key, "random_key_%d", idx);
    return key;
}

// 准备测试数据
void prepare_test_data(hashtable_t *hash, char ***collision_keys, char ***normal_keys) {
    // 分配内存
    *collision_keys = (char **)calloc(COLLISION_KEYS, sizeof(char *));
    *normal_keys = (char **)calloc(NORMAL_KEYS, sizeof(char *));
    
    // 创建冲突键
    printf("生成 %d 个冲突键...\n", COLLISION_KEYS);
    int bucket_idx = 0; // 使用一个桶
    
    for (int i = 0; i < COLLISION_KEYS; i++) {
        (*collision_keys)[i] = create_collision_key(bucket_idx, i);
        char value[64];
        sprintf(value, "collision_value_%d", i);
        
        kvs_hash_set(hash, (*collision_keys)[i], value);
        
        if (DEBUG_OUTPUT && i % 100 == 0) {
            printf("  已插入 %d 个冲突键\n", i);
        }
    }
    
    // 创建随机键
    printf("生成 %d 个随机键...\n", NORMAL_KEYS);
    for (int i = 0; i < NORMAL_KEYS; i++) {
        (*normal_keys)[i] = create_random_key(i);
        char value[64];
        sprintf(value, "normal_value_%d", i);
        
        kvs_hash_set(hash, (*normal_keys)[i], value);
        
        if (DEBUG_OUTPUT && i % 1000 == 0) {
            printf("  已插入 %d 个随机键\n", i);
        }
    }
}

// 测试查询性能
void test_query_performance(hashtable_t *hash, char **collision_keys, char **normal_keys) {
    printf("\n===== 测试查询性能 =====\n");
    
    // 准备混合查询序列
    int total_queries = COLLISION_KEYS + NORMAL_KEYS;
    int *query_sequence = (int *)malloc(total_queries * sizeof(int));
    char **all_keys = (char **)malloc(total_queries * sizeof(char *));
    
    // 组合所有键
    for (int i = 0; i < COLLISION_KEYS; i++) {
        all_keys[i] = collision_keys[i];
    }
    for (int i = 0; i < NORMAL_KEYS; i++) {
        all_keys[COLLISION_KEYS + i] = normal_keys[i];
    }
    
    // 生成随机查询序列
    for (int i = 0; i < total_queries; i++) {
        query_sequence[i] = i;
    }
    
    // 打乱顺序
    for (int i = total_queries - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = query_sequence[i];
        query_sequence[i] = query_sequence[j];
        query_sequence[j] = temp;
    }
    
    // 测试冲突键查询性能
    printf("\n测试冲突键查询性能...\n");
    long long start_time = current_time_millis();
    
    int successful_queries = 0;
    
    for (int iter = 0; iter < ITERATIONS; iter++) {
        for (int i = 0; i < COLLISION_KEYS; i++) {
            char *key = collision_keys[i];
            char expected[64];
            sprintf(expected, "collision_value_%d", i);
            
            char *value = kvs_hash_get(hash, key);
            if (value && strcmp(value, expected) == 0) {
                successful_queries++;
            }
        }
    }
    
    long long end_time = current_time_millis();
    long long collision_time = end_time - start_time;
    
    printf("  冲突键查询: %d 次查询, 成功: %d, 耗时: %lld 毫秒\n", 
           COLLISION_KEYS * ITERATIONS, successful_queries, collision_time);
    printf("  平均每次查询: %.6f 毫秒\n", 
           (double)collision_time / (COLLISION_KEYS * ITERATIONS));
    
    // 测试随机键查询性能
    printf("\n测试随机键查询性能...\n");
    start_time = current_time_millis();
    
    successful_queries = 0;
    
    for (int iter = 0; iter < ITERATIONS / 10; iter++) { // 减少迭代次数以平衡测试时间
        for (int i = 0; i < NORMAL_KEYS; i++) {
            char *key = normal_keys[i];
            char expected[64];
            sprintf(expected, "normal_value_%d", i);
            
            char *value = kvs_hash_get(hash, key);
            if (value && strcmp(value, expected) == 0) {
                successful_queries++;
            }
        }
    }
    
    end_time = current_time_millis();
    long long normal_time = end_time - start_time;
    
    printf("  随机键查询: %d 次查询, 成功: %d, 耗时: %lld 毫秒\n", 
           NORMAL_KEYS * (ITERATIONS / 10), successful_queries, normal_time);
    printf("  平均每次查询: %.6f 毫秒\n", 
           (double)normal_time / (NORMAL_KEYS * (ITERATIONS / 10)));
    
    // 对比结果
    printf("\n===== 性能对比 =====\n");
    
    // 计算归一化的性能比较 (单位: 每毫秒的查询数)
    double collision_qps = (double)(COLLISION_KEYS * ITERATIONS) / collision_time;
    double normal_qps = (double)(NORMAL_KEYS * (ITERATIONS / 10)) / normal_time;
    
    printf("冲突键 QPS: %.2f 查询/毫秒\n", collision_qps);
    printf("随机键 QPS: %.2f 查询/毫秒\n", normal_qps);
    
    if (collision_qps > normal_qps * 0.8) {
        printf("结果分析: 冲突键性能良好，红黑树优化有效\n");
    } else {
        printf("结果分析: 冲突键性能较差，红黑树优化可能不够有效\n");
    }
    
    // 释放内存
    free(query_sequence);
    free(all_keys);
}

// 清理测试数据
void cleanup_test_data(char **collision_keys, char **normal_keys) {
    if (collision_keys) {
        for (int i = 0; i < COLLISION_KEYS; i++) {
            if (collision_keys[i]) {
                free(collision_keys[i]);
            }
        }
        free(collision_keys);
    }
    
    if (normal_keys) {
        for (int i = 0; i < NORMAL_KEYS; i++) {
            if (normal_keys[i]) {
                free(normal_keys[i]);
            }
        }
        free(normal_keys);
    }
}

int main() {
    printf("开始哈希冲突性能测试...\n");
    
    // 初始化随机数生成器
    srand(time(NULL));
    
    // 初始化哈希表
    hashtable_t hash;
    if (kv_store_hash_create(&hash) != 0) {
        printf("创建哈希表失败\n");
        return 1;
    }
    
    // 记录开始时间
    long long start_time = current_time_millis();
    
    // 准备测试数据
    char **collision_keys = NULL;
    char **normal_keys = NULL;
    prepare_test_data(&hash, &collision_keys, &normal_keys);
    
    // 测试查询性能
    test_query_performance(&hash, collision_keys, normal_keys);
    
    // 清理资源
    cleanup_test_data(collision_keys, normal_keys);
    kv_store_hash_destroy(&hash);
    
    // 打印总耗时
    printf("\n测试完成! 总耗时: %lld 毫秒\n", current_time_millis() - start_time);
    
    return 0;
} 