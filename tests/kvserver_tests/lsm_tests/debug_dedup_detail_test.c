/*
 * @Author: Lzww0608
 * @Date: 2025-6-7 20:10:00
 * @Description: 详细调试去重功能测试
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
    printf("=== 详细调试去重功能测试 ===\n");
    
    const char *test_dir = "./debug_dedup_detail_test";
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
    
    // 添加操作（类似原测试）
    printf("\n3. 添加重复键的操作（类似原测试）...\n");
    
    // 模拟原测试的操作序列
    ret = lsm_batch_put(writer, "key1", 4, "value1_v1", 9);
    printf("   PUT key1 -> value1_v1: %s\n", ret == KV_ERR_NONE ? "成功" : "失败");
    
    ret = lsm_batch_put(writer, "key2", 4, "value2_v1", 9);
    printf("   PUT key2 -> value2_v1: %s\n", ret == KV_ERR_NONE ? "成功" : "失败");
    
    ret = lsm_batch_put(writer, "key1", 4, "value1_v2", 9); // 覆盖key1
    printf("   PUT key1 -> value1_v2: %s (覆盖)\n", ret == KV_ERR_NONE ? "成功" : "失败");
    
    ret = lsm_batch_delete(writer, "key2", 4);              // 删除key2
    printf("   DELETE key2: %s\n", ret == KV_ERR_NONE ? "成功" : "失败");
    
    ret = lsm_batch_put(writer, "key1", 4, "value1_v3", 9); // 再次覆盖key1
    printf("   PUT key1 -> value1_v3: %s (再次覆盖)\n", ret == KV_ERR_NONE ? "成功" : "失败");
    
    ret = lsm_batch_put(writer, "key3", 4, "value3_v1", 9);
    printf("   PUT key3 -> value3_v1: %s\n", ret == KV_ERR_NONE ? "成功" : "失败");
    
    // 检查批次状态
    printf("\n4. 检查批次状态...\n");
    size_t entry_count, memory_usage;
    ret = lsm_batch_get_status(writer, &entry_count, &memory_usage);
    if (ret == KV_ERR_NONE) {
        printf("   批次状态: %zu条目, %zu字节内存\n", entry_count, memory_usage);
        printf("   期望去重后: 3条目 (key1->value1_v3, key2->DELETE, key3->value3_v1)\n");
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
    
    // 验证去重结果
    printf("\n6. 详细验证去重结果...\n");
    
    // 检查key1：期望最后的PUT value1_v3
    char *value1 = NULL;
    size_t value1_len = 0;
    ret = lsm_tree_get(tree, "key1", 4, &value1, &value1_len);
    printf("   检查 key1: ");
    if (ret == KV_ERR_NONE && value1) {
        printf("存在，值='%.*s' (长度=%zu)\n", (int)value1_len, value1, value1_len);
        printf("   期望: 'value1_v3' (长度=9)\n");
        if (value1_len == 9 && memcmp(value1, "value1_v3", 9) == 0) {
            printf("   ✅ key1 去重结果正确\n");
        } else {
            printf("   ❌ key1 去重结果错误\n");
            printf("   详细比较:\n");
            printf("     实际值: ");
            for (size_t i = 0; i < value1_len; i++) {
                printf("'%c'(%d) ", value1[i], (int)value1[i]);
            }
            printf("\n");
            printf("     期望值: ");
            for (int i = 0; i < 9; i++) {
                printf("'%c'(%d) ", "value1_v3"[i], (int)"value1_v3"[i]);
            }
            printf("\n");
        }
        free(value1);
    } else {
        printf("不存在或读取失败: %d (%s)\n", ret, error_code_to_string(ret));
    }
    
    // 检查key2：期望被DELETE，不存在
    char *value2 = NULL;
    size_t value2_len = 0;
    ret = lsm_tree_get(tree, "key2", 4, &value2, &value2_len);
    printf("   检查 key2: ");
    if (ret == KV_ERR_NOT_FOUND) {
        printf("不存在 (期望: 被DELETE)\n");
        printf("   ✅ key2 去重结果正确\n");
    } else if (ret == KV_ERR_NONE && value2) {
        printf("存在，值='%.*s' (期望: 不存在)\n", (int)value2_len, value2);
        printf("   ❌ key2 去重结果错误，期望被删除，但仍存在\n");
        free(value2);
    } else {
        printf("读取出现错误: %d (%s)\n", ret, error_code_to_string(ret));
    }
    
    // 检查key3：期望存在value3_v1
    char *value3 = NULL;
    size_t value3_len = 0;
    ret = lsm_tree_get(tree, "key3", 4, &value3, &value3_len);
    printf("   检查 key3: ");
    if (ret == KV_ERR_NONE && value3) {
        printf("存在，值='%.*s' (长度=%zu)\n", (int)value3_len, value3, value3_len);
        printf("   期望: 'value3_v1' (长度=9)\n");
        if (value3_len == 9 && memcmp(value3, "value3_v1", 9) == 0) {
            printf("   ✅ key3 去重结果正确\n");
        } else {
            printf("   ❌ key3 去重结果错误\n");
        }
        free(value3);
    } else {
        printf("不存在或读取失败: %d (%s)\n", ret, error_code_to_string(ret));
    }
    
    // 清理
    printf("\n7. 清理资源...\n");
    lsm_batch_writer_destroy(writer);
    lsm_tree_destroy(tree);
    cleanup_test_directory(test_dir);
    printf("✅ 清理完成\n");
    
    printf("\n=== 详细去重功能调试测试完成 ===\n");
    return 0;
} 