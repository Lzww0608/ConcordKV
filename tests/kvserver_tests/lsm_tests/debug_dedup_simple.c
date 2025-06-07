#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>

#include "lsm_tree.h"
#include "kv_memory.h"
#include "kv_error.h"

// 简单的调试程序，专门测试去重逻辑
int main(void) {
    printf("=== 去重逻辑调试 ===\n");
    
    // 初始化内存系统（如果需要）
    // kv_memory_init();
    
    // 创建LSM-Tree
    lsm_tree_config_t config = lsm_tree_default_config("/tmp/dedup_debug");
    config.enable_wal = false;
    config.compaction.enable_background_compaction = false;
    
    lsm_tree_t *tree = lsm_tree_create(&config);
    if (!tree) {
        printf("❌ 创建LSM-Tree失败\n");
        return 1;
    }
    
    if (lsm_tree_open(tree) != KV_ERR_NONE) {
        printf("❌ 打开LSM-Tree失败\n");
        lsm_tree_destroy(tree);
        return 1;
    }
    
    // 创建批量写入器
    lsm_batch_config_t batch_config = lsm_batch_default_config();
    batch_config.enable_deduplication = true;
    
    lsm_batch_writer_t *writer = lsm_batch_writer_create(tree, &batch_config);
    if (!writer) {
        printf("❌ 创建批量写入器失败\n");
        lsm_tree_close(tree);
        lsm_tree_destroy(tree);
        return 1;
    }
    
    printf("✅ 初始化完成\n");
    
    // 测试1：基础去重
    printf("\n=== 测试1：基础去重 ===\n");
    
    // 添加重复键
    lsm_batch_put(writer, "key1", 4, "value1", 6);
    printf("添加 key1 -> value1\n");
    
    lsm_batch_put(writer, "key1", 4, "value2", 6);
    printf("添加 key1 -> value2\n");
    
    lsm_batch_put(writer, "key1", 4, "value3", 6);
    printf("添加 key1 -> value3\n");
    
    // 检查状态
    size_t entry_count, memory_usage;
    lsm_batch_get_status(writer, &entry_count, &memory_usage);
    printf("状态检查: %zu条目, %zu字节\n", entry_count, memory_usage);
    
    // 提交
    lsm_batch_result_t result;
    memset(&result, 0, sizeof(result));
    int ret = lsm_batch_commit(writer, &result);
    printf("提交结果: %d, 提交条目: %zu\n", ret, result.committed_entries);
    
    // 验证结果
    char *value = NULL;
    size_t value_len = 0;
    ret = lsm_tree_get(tree, "key1", 4, &value, &value_len);
    if (ret == KV_ERR_NONE && value) {
        printf("key1 的值: %.*s (期望: value3)\n", (int)value_len, value);
        kv_free(value);
    } else {
        printf("❌ 读取key1失败: %d\n", ret);
    }
    
    // 测试2：PUT/DELETE混合
    printf("\n=== 测试2：PUT/DELETE混合 ===\n");
    
    lsm_batch_put(writer, "key2", 4, "value1", 6);
    printf("添加 key2 -> value1\n");
    
    lsm_batch_delete(writer, "key2", 4);
    printf("删除 key2\n");
    
    lsm_batch_put(writer, "key2", 4, "value2", 6);
    printf("添加 key2 -> value2\n");
    
    // 检查状态
    lsm_batch_get_status(writer, &entry_count, &memory_usage);
    printf("状态检查: %zu条目, %zu字节\n", entry_count, memory_usage);
    
    // 提交
    memset(&result, 0, sizeof(result));
    ret = lsm_batch_commit(writer, &result);
    printf("提交结果: %d, 提交条目: %zu\n", ret, result.committed_entries);
    
    // 验证结果
    value = NULL;
    value_len = 0;
    ret = lsm_tree_get(tree, "key2", 4, &value, &value_len);
    if (ret == KV_ERR_NONE && value) {
        printf("key2 的值: %.*s (期望: value2)\n", (int)value_len, value);
        kv_free(value);
    } else if (ret == KV_ERR_NOT_FOUND) {
        printf("❌ key2 不存在，但期望存在\n");
    } else {
        printf("❌ 读取key2失败: %d\n", ret);
    }
    
    // 清理
    lsm_batch_writer_destroy(writer);
    lsm_tree_close(tree);
    lsm_tree_destroy(tree);
    
    printf("\n=== 调试完成 ===\n");
    return 0;
} 