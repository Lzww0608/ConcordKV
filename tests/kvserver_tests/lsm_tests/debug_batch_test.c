/*
 * @Author: Lzww0608
 * @Date: 2025-6-4 18:00:00
 * @Description: 简单的批量写入调试测试
 */

#include "lsm_tree.h"
#include "kv_memory.h"
#include "kv_error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

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
    printf("=== LSM-Tree批量写入调试测试 ===\n");
    
    const char *test_dir = "./debug_batch_test";
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
    
    // 测试单次写入
    printf("\n2. 测试单次写入...\n");
    ret = lsm_tree_put(tree, "test_key", 8, "test_value", 10);
    if (ret != KV_ERR_NONE) {
        printf("❌ 单次写入失败: %d (%s)\n", ret, error_code_to_string(ret));
    } else {
        printf("✅ 单次写入成功\n");
        
        // 验证读取
        char *value = NULL;
        size_t value_len = 0;
        ret = lsm_tree_get(tree, "test_key", 8, &value, &value_len);
        if (ret == KV_ERR_NONE && value) {
            printf("✅ 单次读取成功: %.*s\n", (int)value_len, value);
            free(value);
        } else {
            printf("❌ 单次读取失败: %d (%s)\n", ret, error_code_to_string(ret));
        }
    }
    
    // 创建批量写入器
    printf("\n3. 创建批量写入器...\n");
    lsm_batch_config_t batch_config = lsm_batch_default_config();
    printf("   默认配置: max_batch_size=%zu, max_batch_memory=%zu\n", 
           batch_config.max_batch_size, batch_config.max_batch_memory);
    printf("   其他配置: enable_sorting=%d, enable_deduplication=%d, atomic_commit=%d, sync_wal=%d\n",
           batch_config.enable_sorting, batch_config.enable_deduplication, 
           batch_config.atomic_commit, batch_config.sync_wal);
    
    lsm_batch_writer_t *writer = lsm_batch_writer_create(tree, &batch_config);
    if (!writer) {
        printf("❌ 创建批量写入器失败\n");
        lsm_tree_destroy(tree);
        return 1;
    }
    printf("✅ 批量写入器创建成功\n");
    
    // 添加批量操作
    printf("\n4. 添加批量操作...\n");
    for (int i = 0; i < 5; ++i) {
        char key[32], value[64];
        snprintf(key, sizeof(key), "batch_key_%d", i);
        snprintf(value, sizeof(value), "batch_value_%d", i);
        
        ret = lsm_batch_put(writer, key, strlen(key), value, strlen(value));
        if (ret != KV_ERR_NONE) {
            printf("❌ 添加批量操作%d失败: %d (%s)\n", i, ret, error_code_to_string(ret));
            break;
        } else {
            printf("✅ 添加批量操作%d成功: %s -> %s\n", i, key, value);
        }
    }
    
    // 检查批次状态
    printf("\n5. 检查批次状态...\n");
    size_t entry_count, memory_usage;
    ret = lsm_batch_get_status(writer, &entry_count, &memory_usage);
    if (ret == KV_ERR_NONE) {
        printf("✅ 批次状态: %zu条目, %zu字节内存\n", entry_count, memory_usage);
    } else {
        printf("❌ 获取批次状态失败: %d (%s)\n", ret, error_code_to_string(ret));
    }
    
    // 检查LSM-Tree状态
    printf("\n5.1 检查LSM-Tree状态...\n");
    printf("   LSM-Tree is_open: %s\n", tree->is_open ? "true" : "false");
    printf("   MemTable管理器: %p\n", (void*)tree->mem_mgr);
    printf("   压缩调度器: %p\n", (void*)tree->compactor);
    
    // 提交批次
    printf("\n6. 提交批次...\n");
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
    
    if (ret == KV_ERR_NONE) {
        printf("✅ 批次提交成功\n");
    } else {
        printf("❌ 批次提交失败: %d (%s)\n", ret, error_code_to_string(ret));
    }
    
    // 验证批量写入的数据
    printf("\n7. 验证批量写入的数据...\n");
    for (int i = 0; i < 5; ++i) {
        char key[32];
        snprintf(key, sizeof(key), "batch_key_%d", i);
        
        char *value = NULL;
        size_t value_len = 0;
        ret = lsm_tree_get(tree, key, strlen(key), &value, &value_len);
        if (ret == KV_ERR_NONE && value) {
            printf("✅ 读取%s成功: %.*s\n", key, (int)value_len, value);
            free(value);
        } else {
            printf("❌ 读取%s失败: %d (%s)\n", key, ret, error_code_to_string(ret));
        }
    }
    
    // 清理
    printf("\n8. 清理资源...\n");
    lsm_batch_writer_destroy(writer);
    lsm_tree_destroy(tree);
    cleanup_test_directory(test_dir);
    printf("✅ 清理完成\n");
    
    printf("\n=== 调试测试完成 ===\n");
    return 0;
} 