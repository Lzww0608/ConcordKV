/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV storage engine - test_hash_rbtree_conversion.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

// 引入哈希表相关定义
#include "kv_store.h"
#include "rbtree_adapter.h"

// 测试参数
#define BUCKET_TEST_COUNT    3       // 测试桶数量
#define ITEMS_PER_BUCKET     15      // 每个桶的项目数
#define TREEIFY_THRESHOLD    8       // 链表转红黑树阈值
#define UNTREEIFY_THRESHOLD  6       // 红黑树转链表阈值
#define DEBUG_LOG            1       // 是否打印调试日志

// 获取当前时间（毫秒）
long long current_time_millis() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// 创建具有相同哈希值的键
// 通过在键前面加上特定的前缀，确保它们都落在同一个桶中
char *create_collision_key(int bucket_idx, int key_idx) {
    char *key = (char *)malloc(64);
    if (!key) return NULL;
    
    sprintf(key, "bucket%d_key%d", bucket_idx, key_idx);
    return key;
}

// 检查桶是否为红黑树
void check_bucket_state(hashtable_t *hash, int bucket_idx, int expected_count) {
    if (bucket_idx >= hash->max_slots) {
        printf("桶索引 %d 超出范围\n", bucket_idx);
        return;
    }
    
    bucket_t *bucket = &hash->buckets[bucket_idx];
    printf("桶 %d: 大小=%d, 是否为红黑树=%s\n", 
           bucket_idx, bucket->size, 
           bucket->is_tree ? "是" : "否");
    
    if (bucket->size != expected_count) {
        printf("警告: 桶 %d 大小不符合预期: 期望 %d, 实际 %d\n", 
               bucket_idx, expected_count, bucket->size);
    }
    
    // 打印预期状态
    if (expected_count >= TREEIFY_THRESHOLD) {
        printf("预期状态: 应该是红黑树 (节点数 >= %d)\n", TREEIFY_THRESHOLD);
    } else if (expected_count <= UNTREEIFY_THRESHOLD) {
        printf("预期状态: 应该是链表 (节点数 <= %d)\n", UNTREEIFY_THRESHOLD);
    } else {
        printf("预期状态: 保持当前结构状态 (%d < 节点数 < %d)\n", 
               UNTREEIFY_THRESHOLD, TREEIFY_THRESHOLD);
    }
    
    // 验证实际状态
    if (expected_count >= TREEIFY_THRESHOLD && !bucket->is_tree) {
        printf("错误: 应该转换为红黑树但仍为链表\n");
    } else if (expected_count <= UNTREEIFY_THRESHOLD && bucket->is_tree) {
        printf("错误: 应该转换为链表但仍为红黑树\n");
    }
}

// 测试创建一个桶中填充多个项目，观察链表到红黑树的转换
void test_treeify(hashtable_t *hash) {
    printf("\n===== 测试链表转红黑树 =====\n");
    
    // 为了确保键落在同一个桶中，我们使用固定的索引
    int bucket_idx = 0;
    
    // 存储键以便后续删除
    char **keys = (char **)malloc(ITEMS_PER_BUCKET * sizeof(char *));
    if (!keys) {
        printf("内存分配失败\n");
        return;
    }
    
    // 第一阶段：插入TREEIFY_THRESHOLD-1个项目，应该仍然是链表
    printf("\n阶段1: 插入 %d 个项目到桶 %d\n", TREEIFY_THRESHOLD-1, bucket_idx);
    for (int i = 0; i < TREEIFY_THRESHOLD-1; i++) {
        keys[i] = create_collision_key(bucket_idx, i);
        char value[32];
        sprintf(value, "value_%d", i);
        
        kvs_hash_set(hash, keys[i], value);
        printf("  插入: %s -> %s\n", keys[i], value);
    }
    
    // 检查桶状态，应该仍为链表
    check_bucket_state(hash, bucket_idx, TREEIFY_THRESHOLD-1);
    
    // 第二阶段：再插入一个项目，达到阈值，应该转为红黑树
    printf("\n阶段2: 插入第 %d 个项目到桶 %d\n", TREEIFY_THRESHOLD, bucket_idx);
    keys[TREEIFY_THRESHOLD-1] = create_collision_key(bucket_idx, TREEIFY_THRESHOLD-1);
    char value[32];
    sprintf(value, "value_%d", TREEIFY_THRESHOLD-1);
    
    kvs_hash_set(hash, keys[TREEIFY_THRESHOLD-1], value);
    printf("  插入: %s -> %s\n", keys[TREEIFY_THRESHOLD-1], value);
    
    // 检查桶状态，应该转为红黑树
    check_bucket_state(hash, bucket_idx, TREEIFY_THRESHOLD);
    
    // 第三阶段：继续插入更多项目
    printf("\n阶段3: 再插入 %d 个项目到桶 %d\n", 
           ITEMS_PER_BUCKET - TREEIFY_THRESHOLD, bucket_idx);
    for (int i = TREEIFY_THRESHOLD; i < ITEMS_PER_BUCKET; i++) {
        keys[i] = create_collision_key(bucket_idx, i);
        char value[32];
        sprintf(value, "value_%d", i);
        
        kvs_hash_set(hash, keys[i], value);
        printf("  插入: %s -> %s\n", keys[i], value);
    }
    
    // 检查桶状态，应该仍为红黑树
    check_bucket_state(hash, bucket_idx, ITEMS_PER_BUCKET);
    
    // 验证所有值都能正确获取
    printf("\n验证所有键值对...\n");
    int success = 0;
    for (int i = 0; i < ITEMS_PER_BUCKET; i++) {
        char expected[32];
        sprintf(expected, "value_%d", i);
        
        char *actual = kvs_hash_get(hash, keys[i]);
        if (actual && strcmp(actual, expected) == 0) {
            success++;
        } else {
            printf("  验证失败: %s, 期望 %s, 实际 %s\n", 
                   keys[i], expected, actual ? actual : "NULL");
        }
    }
    printf("成功验证 %d/%d 个键值对\n", success, ITEMS_PER_BUCKET);
    
    // 保留这些键以用于后续的untreeify测试
    return;
}

// 测试从红黑树到链表的转换
void test_untreeify(hashtable_t *hash, char **keys) {
    printf("\n===== 测试红黑树转链表 =====\n");
    
    int bucket_idx = 0;
    
    // 第一阶段：删除足够的项目使节点数等于UNTREEIFY_THRESHOLD+1
    int to_delete = ITEMS_PER_BUCKET - (UNTREEIFY_THRESHOLD + 1);
    printf("\n阶段1: 从桶 %d 删除 %d 个项目\n", bucket_idx, to_delete);
    
    for (int i = ITEMS_PER_BUCKET - 1; i >= ITEMS_PER_BUCKET - to_delete; i--) {
        printf("  删除: %s\n", keys[i]);
        kvs_hash_delete(hash, keys[i]);
        free(keys[i]);
        keys[i] = NULL;
    }
    
    // 检查桶状态，应该仍为红黑树
    check_bucket_state(hash, bucket_idx, UNTREEIFY_THRESHOLD + 1);
    
    // 第二阶段：再删除一个项目，使节点数等于UNTREEIFY_THRESHOLD，应该转为链表
    printf("\n阶段2: 从桶 %d 再删除 1 个项目\n", bucket_idx);
    int idx = ITEMS_PER_BUCKET - to_delete - 1;
    printf("  删除: %s\n", keys[idx]);
    kvs_hash_delete(hash, keys[idx]);
    free(keys[idx]);
    keys[idx] = NULL;
    
    // 检查桶状态，应该转为链表
    check_bucket_state(hash, bucket_idx, UNTREEIFY_THRESHOLD);
    
    // 第三阶段：删除剩余项目
    printf("\n阶段3: 删除桶 %d 中剩余的 %d 个项目\n", bucket_idx, UNTREEIFY_THRESHOLD);
    for (int i = 0; i < UNTREEIFY_THRESHOLD; i++) {
        if (keys[i]) {
            printf("  删除: %s\n", keys[i]);
            kvs_hash_delete(hash, keys[i]);
            free(keys[i]);
            keys[i] = NULL;
        }
    }
    
    // 检查桶状态，应该为空
    check_bucket_state(hash, bucket_idx, 0);
    
    free(keys);
}

// 主函数
int main() {
    printf("开始测试哈希表红黑树转换...\n");
    
    // 初始化哈希表
    hashtable_t hash;
    if (kv_store_hash_create(&hash) != 0) {
        printf("创建哈希表失败\n");
        return 1;
    }
    
    // 记录开始时间
    long long start_time = current_time_millis();
    
    // 测试链表转红黑树
    char **keys = (char **)malloc(ITEMS_PER_BUCKET * sizeof(char *));
    if (!keys) {
        printf("内存分配失败\n");
        kv_store_hash_destroy(&hash);
        return 1;
    }
    
    test_treeify(&hash);
    
    // 测试红黑树转链表
    test_untreeify(&hash, keys);
    
    // 销毁哈希表
    kv_store_hash_destroy(&hash);
    
    // 打印总耗时
    printf("\n测试完成! 总耗时: %lld 毫秒\n", current_time_millis() - start_time);
    return 0;
} 