/*
 * @Author: Lzww0608
 * @Date: 2025-6-7 19:20:00
 * @Description: 调试基础功能测试
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

static void cleanup_test_directory(const char *path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", path);
    int result = system(cmd);
    (void)result; // 避免警告
}

static const char* error_code_to_string(int code) {
    switch (code) {
        case 0: return "KV_ERR_NONE";
        case 6: return "KV_ERR_NOT_FOUND";
        case 19: return "KV_ERR_INVALID_PARAM";
        case 20: return "KV_ERR_OUT_OF_MEMORY";
        case 21: return "KV_ERR_IO_ERROR";
        case 24: return "KV_ERR_INVALID_STATE";
        case 25: return "KV_ERR_NOT_IMPLEMENTED";
        default: return "UNKNOWN_ERROR";
    }
}

int main(void) {
    printf("=== 调试基础功能测试 ===\n");
    
    const char *test_dir = "./debug_basic_test";
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
        printf("❌ 打开LSM-Tree失败: %d (%s)\n", ret, error_code_to_string(ret));
        lsm_tree_destroy(tree);
        return 1;
    }
    printf("✅ LSM-Tree创建成功\n");
    
    // 创建批量写入器
    printf("\n2. 创建批量写入器...\n");
    lsm_batch_writer_t *writer = lsm_batch_writer_create(tree, NULL);
    if (!writer) {
        printf("❌ 创建批量写入器失败\n");
        lsm_tree_destroy(tree);
        return 1;
    }
    printf("✅ 批量写入器创建成功\n");
    
    // 添加操作（类似原测试）
    printf("\n3. 添加批量操作...\n");
    
    // 添加PUT操作 (键 0-9)
    for (int i = 0; i < 10; i++) {
        char key[32], value[64];
        snprintf(key, sizeof(key), "batch_key_%03d", i);
        snprintf(value, sizeof(value), "batch_value_%03d", i);
        
        ret = lsm_batch_put(writer, key, strlen(key), value, strlen(value));
        printf("   PUT %s -> %s: %s\n", key, value, 
               ret == KV_ERR_NONE ? "成功" : "失败");
    }
    
    // 添加DELETE操作 (键 5-7)
    for (int i = 5; i < 8; i++) {
        char key[32];
        snprintf(key, sizeof(key), "batch_key_%03d", i);
        
        ret = lsm_batch_delete(writer, key, strlen(key));
        printf("   DELETE %s: %s\n", key, 
               ret == KV_ERR_NONE ? "成功" : "失败");
    }
    
    // 检查批次状态
    printf("\n4. 检查批次状态...\n");
    size_t entry_count, memory_usage;
    ret = lsm_batch_get_status(writer, &entry_count, &memory_usage);
    if (ret == KV_ERR_NONE) {
        printf("   批次状态: %zu条目, %zu字节内存\n", entry_count, memory_usage);
    } else {
        printf("❌ 获取批次状态失败: %d (%s)\n", ret, error_code_to_string(ret));
    }
    
    // 提交批次
    printf("\n5. 提交批次...\n");
    lsm_batch_result_t result;
    memset(&result, 0, sizeof(result));
    ret = lsm_batch_commit(writer, &result);
    
    printf("   批次提交返回码: %d (%s)\n", ret, error_code_to_string(ret));
    printf("   提交结果详情:\n");
    printf("     committed_entries: %zu\n", result.committed_entries);
    printf("     failed_entries: %zu\n", result.failed_entries);
    printf("     first_error_code: %d (%s)\n", result.first_error_code, error_code_to_string(result.first_error_code));
    printf("     first_error_index: %zu\n", result.first_error_index);
    printf("     commit_time_us: %lu\n", result.commit_time_us);
    printf("     wal_writes: %zu\n", result.wal_writes);
    
    if (ret != KV_ERR_NONE) {
        printf("❌ 批次提交失败\n");
        lsm_batch_writer_destroy(writer);
        lsm_tree_destroy(tree);
        return 1;
    }
    printf("✅ 批次提交成功\n");
    
    // 验证数据
    printf("\n6. 详细验证数据完整性...\n");
    bool all_correct = true;
    
    for (int i = 0; i < 10; i++) {
        char key[32];
        snprintf(key, sizeof(key), "batch_key_%03d", i);
        
        char *value = NULL;
        size_t value_len = 0;
        ret = lsm_tree_get(tree, key, strlen(key), &value, &value_len);
        
        printf("   检查 %s: ", key);
        
        if (i >= 5 && i < 8) {
            // 这些键应该被删除
            if (ret == KV_ERR_NOT_FOUND) {
                printf("✅ 正确删除\n");
            } else if (ret == KV_ERR_NONE) {
                printf("❌ 期望删除但仍存在，值=%.*s\n", (int)value_len, value);
                free(value);
                all_correct = false;
            } else {
                printf("❌ 查找出错: %d (%s)\n", ret, error_code_to_string(ret));
                all_correct = false;
            }
        } else {
            // 其他键应该存在
            if (ret == KV_ERR_NONE && value) {
                printf("✅ 正确存在，值=%.*s\n", (int)value_len, value);
                free(value);
            } else if (ret == KV_ERR_NOT_FOUND) {
                printf("❌ 期望存在但未找到\n");
                all_correct = false;
            } else {
                printf("❌ 查找出错: %d (%s)\n", ret, error_code_to_string(ret));
                all_correct = false;
            }
        }
    }
    
    // 清理
    printf("\n7. 清理资源...\n");
    lsm_batch_writer_destroy(writer);
    lsm_tree_destroy(tree);
    cleanup_test_directory(test_dir);
    printf("✅ 清理完成\n");
    
    printf("\n=== 调试基础功能测试完成 ===\n");
    if (all_correct) {
        printf("🎉 所有验证通过！\n");
        return 0;
    } else {
        printf("❌ 部分验证失败\n");
        return 1;
    }
} 