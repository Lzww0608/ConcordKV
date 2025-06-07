/*
 * @Author: Lzww0608
 * @Date: 2025-6-7 19:40:00
 * @Description: 调试序列号分配问题
 */

#include "lsm_tree.h"
#include "kv_memory.h"
#include "kv_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>
#include <sys/time.h>

static uint64_t get_current_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000ULL + tv.tv_usec;
}

static void cleanup_test_directory(const char *path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", path);
    int result = system(cmd);
    (void)result; // 避免警告
}

int main(void) {
    printf("=== 调试序列号分配问题 ===\n");
    
    const char *test_dir = "./debug_seq_test";
    cleanup_test_directory(test_dir);
    
    // 创建LSM-Tree
    printf("1. 创建LSM-Tree...\n");
    lsm_tree_config_t config = lsm_tree_default_config(test_dir);
    config.enable_wal = false;
    config.compaction.enable_background_compaction = false;
    
    lsm_tree_t *tree = lsm_tree_create(&config);
    if (!tree) {
        printf("❌ 创建LSM-Tree失败\n");
        return 1;
    }
    
    int ret = lsm_tree_open(tree);
    if (ret != KV_ERR_NONE) {
        printf("❌ 打开LSM-Tree失败\n");
        lsm_tree_destroy(tree);
        return 1;
    }
    printf("✅ LSM-Tree创建成功\n");
    
    // 创建批量写入器，启用去重
    printf("\n2. 创建批量写入器(启用去重)...\n");
    lsm_batch_config_t batch_config = lsm_batch_default_config();
    batch_config.enable_deduplication = true;
    batch_config.enable_sorting = true;
    printf("   去重配置: enable_deduplication=%d\n", batch_config.enable_deduplication);
    printf("   排序配置: enable_sorting=%d\n", batch_config.enable_sorting);
    
    lsm_batch_writer_t *writer = lsm_batch_writer_create(tree, &batch_config);
    if (!writer) {
        printf("❌ 创建批量写入器失败\n");
        lsm_tree_destroy(tree);
        return 1;
    }
    printf("✅ 批量写入器创建成功\n");
    
    // 测试序列号分配
    printf("\n3. 测试序列号分配...\n");
    uint64_t seq_nums[10];
    for (int i = 0; i < 10; i++) {
        seq_nums[i] = get_current_time_us();
        if (i > 0) {
            printf("   seq[%d] = %lu, diff = %ld\n", 
                   i, seq_nums[i], (long)(seq_nums[i] - seq_nums[i-1]));
        } else {
            printf("   seq[%d] = %lu\n", i, seq_nums[i]);
        }
        usleep(1); // 微秒级延迟确保序列号不同
    }
    
    // 检查是否有重复序列号
    printf("\n4. 检查序列号重复情况...\n");
    int duplicates = 0;
    for (int i = 0; i < 10; i++) {
        for (int j = i + 1; j < 10; j++) {
            if (seq_nums[i] == seq_nums[j]) {
                printf("   ❌ 重复序列号: seq[%d] = seq[%d] = %lu\n", i, j, seq_nums[i]);
                duplicates++;
            }
        }
    }
    if (duplicates == 0) {
        printf("   ✅ 没有重复序列号\n");
    } else {
        printf("   ❌ 发现 %d 个重复序列号\n", duplicates);
    }
    
    // 添加快速连续的操作来测试去重
    printf("\n5. 添加快速连续的重复键操作...\n");
    
    // 快速连续添加同一个键的多个值
    for (int i = 0; i < 5; i++) {
        char value[32];
        snprintf(value, sizeof(value), "value_v%d", i+1);
        
        ret = lsm_batch_put(writer, "testkey", 7, value, strlen(value));
        printf("   PUT testkey -> %s: %s\n", value, 
               ret == KV_ERR_NONE ? "成功" : "失败");
    }
    
    // 检查批次状态
    printf("\n6. 检查批次状态...\n");
    size_t entry_count, memory_usage;
    ret = lsm_batch_get_status(writer, &entry_count, &memory_usage);
    if (ret == KV_ERR_NONE) {
        printf("   批次状态: %zu条目, %zu字节内存\n", entry_count, memory_usage);
        printf("   期望去重后: 1条目 (只保留最新的testkey值)\n");
    }
    
    // 提交批次
    printf("\n7. 提交批次...\n");
    lsm_batch_result_t result;
    memset(&result, 0, sizeof(result));
    ret = lsm_batch_commit(writer, &result);
    
    printf("   批次提交返回码: %d\n", ret);
    printf("   提交结果详情:\n");
    printf("     committed_entries: %zu\n", result.committed_entries);
    printf("     failed_entries: %zu\n", result.failed_entries);
    printf("     commit_time_us: %lu\n", result.commit_time_us);
    
    if (ret == KV_ERR_NONE) {
        printf("✅ 批次提交成功\n");
        if (result.committed_entries == 1) {
            printf("✅ 去重正确：提交了%zu个条目\n", result.committed_entries);
        } else {
            printf("❌ 去重错误：期望1个条目，实际%zu个条目\n", result.committed_entries);
        }
    } else {
        printf("❌ 批次提交失败\n");
    }
    
    // 验证最终值
    printf("\n8. 验证去重结果...\n");
    char *value = NULL;
    size_t value_len = 0;
    ret = lsm_tree_get(tree, "testkey", 7, &value, &value_len);
    if (ret == KV_ERR_NONE && value) {
        printf("   testkey 的值: %.*s\n", (int)value_len, value);
        printf("   期望: value_v5 (最后一个值)\n");
        if (strncmp(value, "value_v5", 8) == 0) {
            printf("   ✅ 去重结果正确\n");
        } else {
            printf("   ❌ 去重结果错误\n");
        }
        free(value);
    } else {
        printf("   ❌ 读取testkey失败或不存在\n");
    }
    
    // 清理
    printf("\n9. 清理资源...\n");
    lsm_batch_writer_destroy(writer);
    lsm_tree_destroy(tree);
    cleanup_test_directory(test_dir);
    printf("✅ 清理完成\n");
    
    printf("\n=== 序列号调试测试完成 ===\n");
    return 0;
} 