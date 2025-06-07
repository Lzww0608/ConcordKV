#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>

#include "lsm_tree.h"
#include "kv_memory.h"
#include "kv_error.h"

// 详细的去重调试程序
int main(void) {
    printf("=== 详细去重调试 ===\n");
    
    // 创建LSM-Tree
    lsm_tree_config_t config = lsm_tree_default_config("/tmp/detailed_dedup_debug");
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
    batch_config.max_batch_size = 100;
    
    lsm_batch_writer_t *writer = lsm_batch_writer_create(tree, &batch_config);
    if (!writer) {
        printf("❌ 创建批量写入器失败\n");
        lsm_tree_close(tree);
        lsm_tree_destroy(tree);
        return 1;
    }
    
    printf("✅ 初始化完成\n");
    
    // 精确复制GTest的DeduplicationFeature测试
    printf("\n=== 详细去重过程 ===\n");
    
    const char *key = "dedup_key";
    const char *value1 = "value_1";
    const char *value2 = "value_2";
    const char *value3 = "value_3_final";
    
    // 添加第一个值
    printf("1. 添加 %s -> %s\n", key, value1);
    int ret = lsm_batch_put(writer, key, strlen(key), value1, strlen(value1));
    printf("   结果: %s\n", (ret == KV_ERR_NONE) ? "成功" : "失败");
    
    size_t entry_count, memory_usage;
    lsm_batch_get_status(writer, &entry_count, &memory_usage);
    printf("   状态: %zu条目, %zu字节\n", entry_count, memory_usage);
    
    // 添加第二个值
    printf("\n2. 添加 %s -> %s\n", key, value2);
    ret = lsm_batch_put(writer, key, strlen(key), value2, strlen(value2));
    printf("   结果: %s\n", (ret == KV_ERR_NONE) ? "成功" : "失败");
    
    lsm_batch_get_status(writer, &entry_count, &memory_usage);
    printf("   状态: %zu条目, %zu字节\n", entry_count, memory_usage);
    
    // 添加第三个值
    printf("\n3. 添加 %s -> %s\n", key, value3);
    ret = lsm_batch_put(writer, key, strlen(key), value3, strlen(value3));
    printf("   结果: %s\n", (ret == KV_ERR_NONE) ? "成功" : "失败");
    
    lsm_batch_get_status(writer, &entry_count, &memory_usage);
    printf("   状态: %zu条目, %zu字节\n", entry_count, memory_usage);
    
    // 提交前的最终状态
    printf("\n4. 提交前最终状态:\n");
    lsm_batch_get_status(writer, &entry_count, &memory_usage);
    printf("   条目数: %zu (期望: 1)\n", entry_count);
    printf("   内存: %zu字节\n", memory_usage);
    
    // 提交
    printf("\n5. 提交批次...\n");
    lsm_batch_result_t result;
    memset(&result, 0, sizeof(result));
    ret = lsm_batch_commit(writer, &result);
    printf("   提交结果: %d\n", ret);
    printf("   提交条目: %zu (期望: 1)\n", result.committed_entries);
    printf("   失败条目: %zu\n", result.failed_entries);
    printf("   提交时间: %lu微秒\n", result.commit_time_us);
    
    // 验证最终结果
    printf("\n6. 验证最终结果:\n");
    char *value = NULL;
    size_t value_len = 0;
    ret = lsm_tree_get(tree, key, strlen(key), &value, &value_len);
    if (ret == KV_ERR_NONE && value) {
        printf("   实际值: %.*s\n", (int)value_len, value);
        printf("   期望值: %s\n", value3);
        printf("   匹配: %s\n", (strncmp(value, value3, value_len) == 0 && strlen(value3) == value_len) ? "✅" : "❌");
        kv_free(value);
    } else {
        printf("   ❌ 读取失败: %d\n", ret);
    }
    
    // 清理
    lsm_batch_writer_destroy(writer);
    lsm_tree_close(tree);
    lsm_tree_destroy(tree);
    
    printf("\n=== 调试完成 ===\n");
    return 0;
} 