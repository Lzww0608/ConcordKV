/*
 * @Author: Lzww0608
 * @Date: 2025-6-4 20:00:00
 * @Description: 去重功能调试测试
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

// 函数声明
int test_basic_deduplication(void);
int test_put_delete_deduplication(void);

int main(void) {
    int ret1 = test_basic_deduplication();
    int ret2 = test_put_delete_deduplication();
    
    if (ret1 == 0 && ret2 == 0) {
        printf("\n🎉 所有去重测试通过！\n");
        return 0;
    } else {
        printf("\n❌ 去重测试失败\n");
        return -1;
    }
}

// 基础去重测试
int test_basic_deduplication(void) {
    printf("=== LSM-Tree去重功能调试测试 ===\n");
    
    const char *test_dir = "./debug_dedup_test";
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
    lsm_batch_config_t batch_config = lsm_batch_default_config();
    batch_config.enable_deduplication = true;
    printf("   去重配置: enable_deduplication=%d\n", batch_config.enable_deduplication);
    
    lsm_batch_writer_t *writer = lsm_batch_writer_create(tree, &batch_config);
    if (!writer) {
        printf("❌ 创建批量写入器失败\n");
        lsm_tree_destroy(tree);
        return 1;
    }
    printf("✅ 批量写入器创建成功\n");
    
    // 添加重复键的操作
    printf("\n3. 添加重复键的操作...\n");
    
    // 添加 key1 -> value1
    ret = lsm_batch_put(writer, "key1", 4, "value1", 6);
    printf("   添加 key1 -> value1: %s\n", ret == KV_ERR_NONE ? "成功" : "失败");
    
    // 添加 key1 -> value2 (重复键)
    ret = lsm_batch_put(writer, "key1", 4, "value2", 6);
    printf("   添加 key1 -> value2: %s\n", ret == KV_ERR_NONE ? "成功" : "失败");
    
    // 添加 key1 -> value3 (重复键)
    ret = lsm_batch_put(writer, "key1", 4, "value3", 6);
    printf("   添加 key1 -> value3: %s\n", ret == KV_ERR_NONE ? "成功" : "失败");
    
    // 添加 key2 -> value4 (不同键)
    ret = lsm_batch_put(writer, "key2", 4, "value4", 6);
    printf("   添加 key2 -> value4: %s\n", ret == KV_ERR_NONE ? "成功" : "失败");
    
    // 检查批次状态
    printf("\n4. 检查批次状态...\n");
    size_t entry_count, memory_usage;
    ret = lsm_batch_get_status(writer, &entry_count, &memory_usage);
    if (ret == KV_ERR_NONE) {
        printf("   提交前批次状态: %zu条目, %zu字节内存\n", entry_count, memory_usage);
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
    
    if (ret == KV_ERR_NONE) {
        printf("✅ 批次提交成功\n");
        printf("   期望: 去重后应该只有2个条目 (key1->value3, key2->value4)\n");
        printf("   实际: 提交了%zu个条目\n", result.committed_entries);
    } else {
        printf("❌ 批次提交失败: %d (%s)\n", ret, error_code_to_string(ret));
    }
    
    // 验证去重结果
    printf("\n6. 验证去重结果...\n");
    
    // 检查 key1 的值（应该是最后一个值 value3）
    char *value = NULL;
    size_t value_len = 0;
    ret = lsm_tree_get(tree, "key1", 4, &value, &value_len);
    if (ret == KV_ERR_NONE && value) {
        printf("   key1 的值: %.*s (期望: value3)\n", (int)value_len, value);
        if (strncmp(value, "value3", 6) == 0) {
            printf("   ✅ key1 去重正确\n");
        } else {
            printf("   ❌ key1 去重错误，期望value3，实际%.*s\n", (int)value_len, value);
        }
        free(value);
    } else {
        printf("   ❌ 读取key1失败: %d (%s)\n", ret, error_code_to_string(ret));
    }
    
    // 检查 key2 的值
    value = NULL;
    value_len = 0;
    ret = lsm_tree_get(tree, "key2", 4, &value, &value_len);
    if (ret == KV_ERR_NONE && value) {
        printf("   key2 的值: %.*s (期望: value4)\n", (int)value_len, value);
        if (strncmp(value, "value4", 6) == 0) {
            printf("   ✅ key2 正确\n");
        } else {
            printf("   ❌ key2 错误，期望value4，实际%.*s\n", (int)value_len, value);
        }
        free(value);
    } else {
        printf("   ❌ 读取key2失败: %d (%s)\n", ret, error_code_to_string(ret));
    }
    
    // 清理
    printf("\n7. 清理资源...\n");
    lsm_batch_writer_destroy(writer);
    lsm_tree_destroy(tree);
    cleanup_test_directory(test_dir);
    printf("✅ 清理完成\n");
    
    printf("\n=== 去重调试测试完成 ===\n");
    return 0;
}

// 测试PUT/DELETE混合去重功能
int test_put_delete_deduplication() {
    printf("\n=== PUT/DELETE混合去重测试 ===\n");
    
    // 1. 创建LSM-Tree
    printf("1. 创建LSM-Tree...\n");
    lsm_tree_config_t config = lsm_tree_default_config("/tmp/lsm_test_putdel");
    lsm_tree_t *tree = lsm_tree_create(&config);
    if (!tree) {
        printf("❌ LSM-Tree创建失败\n");
        return -1;
    }
    
    if (lsm_tree_open(tree) != KV_ERR_NONE) {
        printf("❌ LSM-Tree打开失败\n");
        lsm_tree_destroy(tree);
        return -1;
    }
    
    printf("✅ LSM-Tree创建成功\n");
    
    // 2. 创建批量写入器
    printf("\n2. 创建批量写入器...\n");
    lsm_batch_config_t batch_config = lsm_batch_default_config();
    batch_config.enable_deduplication = true;
    printf("   去重配置: enable_deduplication=%d\n", batch_config.enable_deduplication);
    
    lsm_batch_writer_t *writer = lsm_batch_writer_create(tree, &batch_config);
    if (!writer) {
        printf("❌ 批量写入器创建失败\n");
        lsm_tree_close(tree);
        lsm_tree_destroy(tree);
        return -1;
    }
    
    printf("✅ 批量写入器创建成功\n");
    
    // 3. 添加PUT/DELETE混合操作
    printf("\n3. 添加PUT/DELETE混合操作...\n");
    
    // 添加：PUT key1 -> value1
    int ret = lsm_batch_put(writer, "key1", 4, "value1", 6);
    printf("   PUT key1 -> value1: %s\n", ret == KV_ERR_NONE ? "成功" : "失败");
    
    // 添加：DELETE key1
    ret = lsm_batch_delete(writer, "key1", 4);
    printf("   DELETE key1: %s\n", ret == KV_ERR_NONE ? "成功" : "失败");
    
    // 添加：PUT key1 -> value2
    ret = lsm_batch_put(writer, "key1", 4, "value2", 6);
    printf("   PUT key1 -> value2: %s\n", ret == KV_ERR_NONE ? "成功" : "失败");
    
    // 添加：PUT key2 -> value3
    ret = lsm_batch_put(writer, "key2", 4, "value3", 6);
    printf("   PUT key2 -> value3: %s\n", ret == KV_ERR_NONE ? "成功" : "失败");
    
    // 添加：DELETE key2
    ret = lsm_batch_delete(writer, "key2", 4);
    printf("   DELETE key2: %s\n", ret == KV_ERR_NONE ? "成功" : "失败");
    
    // 4. 检查批次状态
    printf("\n4. 检查批次状态...\n");
    size_t entry_count, memory_usage;
    lsm_batch_get_status(writer, &entry_count, &memory_usage);
    printf("   提交前批次状态: %zu条目, %zu字节内存\n", entry_count, memory_usage);
    
    // 5. 提交批次
    printf("\n5. 提交批次...\n");
    lsm_batch_result_t result;
    memset(&result, 0, sizeof(result));
    
    ret = lsm_batch_commit(writer, &result);
    printf("   批次提交返回码: %d (%s)\n", ret, ret == KV_ERR_NONE ? "KV_ERR_NONE" : "错误");
    
    if (ret == KV_ERR_NONE) {
        printf("   提交结果详情:\n");
        printf("     committed_entries: %zu\n", result.committed_entries);
        printf("     failed_entries: %zu\n", result.failed_entries);
        printf("     first_error_code: %d (%s)\n", result.first_error_code, 
               result.first_error_code == KV_ERR_NONE ? "KV_ERR_NONE" : "错误");
        printf("     first_error_index: %zu\n", result.first_error_index);
        printf("     commit_time_us: %lu\n", result.commit_time_us);
        printf("     wal_writes: %zu\n", result.wal_writes);
        printf("✅ 批次提交成功\n");
        printf("   期望: 去重后应该只有2个条目 (key1->value2, key2->DELETE)\n");
        printf("   实际: 提交了%zu个条目\n", result.committed_entries);
    } else {
        printf("❌ 批次提交失败\n");
    }
    
    // 6. 验证去重结果
    printf("\n6. 验证去重结果...\n");
    
    // 检查key1：期望最后的PUT value2
    char *value1 = NULL;
    size_t value1_len = 0;
    ret = lsm_tree_get(tree, "key1", 4, &value1, &value1_len);
    if (ret == KV_ERR_NONE && value1) {
        printf("   key1 的值: %.*s (期望: value2)\n", (int)value1_len, value1);
        if (value1_len == 6 && memcmp(value1, "value2", 6) == 0) {
            printf("   ✅ key1 去重正确\n");
        } else {
            printf("   ❌ key1 去重错误，期望value2，实际%.*s\n", (int)value1_len, value1);
        }
        kv_free(value1);
    } else {
        printf("   ❌ key1 读取失败或不存在\n");
    }
    
    // 检查key2：期望被DELETE，不存在
    char *value2 = NULL;
    size_t value2_len = 0;
    ret = lsm_tree_get(tree, "key2", 4, &value2, &value2_len);
    if (ret == KV_ERR_NOT_FOUND) {
        printf("   key2 不存在 (期望: 被DELETE)\n");
        printf("   ✅ key2 去重正确\n");
    } else if (ret == KV_ERR_NONE && value2) {
        printf("   key2 的值: %.*s (期望: 不存在)\n", (int)value2_len, value2);
        printf("   ❌ key2 去重错误，期望被删除，但仍存在\n");
        kv_free(value2);
    } else {
        printf("   ❌ key2 读取出现错误: %d\n", ret);
    }
    
    // 7. 清理资源
    printf("\n7. 清理资源...\n");
    lsm_batch_writer_destroy(writer);
    lsm_tree_close(tree);
    lsm_tree_destroy(tree);
    printf("✅ 清理完成\n");
    
    printf("\n=== PUT/DELETE混合去重测试完成 ===\n");
    return 0;
} 