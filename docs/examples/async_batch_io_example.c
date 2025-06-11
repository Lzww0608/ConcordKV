/*
 * @Author: Lzww0608
 * @Date: 2025-6-11 17:30:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-11 17:30:00
 * @Description: ConcordKV 异步批量I/O功能使用示例
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "../../kvserver/kv_async_batch_io.h"
#include "../../kvserver/kv_io_uring.h"

// 批量完成回调函数
void batch_completion_callback(kv_async_batch_context_t *ctx, 
                               size_t completed_count, size_t failed_count, 
                               void *user_data) {
    printf("批量操作完成:\n");
    printf("  - 成功操作: %zu\n", completed_count);
    printf("  - 失败操作: %zu\n", failed_count);
    printf("  - 用户数据: %s\n", user_data ? (char*)user_data : "None");
}

// 单个操作完成回调函数
void operation_callback(kv_async_batch_operation_t *op, int result, void *user_data) {
    printf("操作完成: 类型=%d, 结果=%d, 用户数据=%s\n", 
           op->op_type, result, user_data ? (char*)user_data : "None");
}

int main(void) {
    printf("=== ConcordKV 异步批量I/O功能使用示例 ===\n\n");
    
    // 1. 创建默认配置
    printf("1. 创建配置...\n");
    kv_async_batch_config_t config = kv_async_batch_default_config();
    
    // 可以根据需要调整配置
    config.max_batch_size = 1000;
    config.timeout_ms = 5000;
    config.max_concurrent_batches = 10;
    
    printf("   配置: max_batch_size=%zu, timeout_ms=%u\n", 
           config.max_batch_size, config.timeout_ms);
    
    // 2. 创建批量操作管理器
    printf("\n2. 创建管理器...\n");
    kv_async_batch_manager_t *manager = kv_async_batch_manager_create(&config);
    if (!manager) {
        printf("❌ 管理器创建失败\n");
        return 1;
    }
    printf("   ✓ 管理器创建成功\n");
    
    // 3. 启动管理器
    printf("\n3. 启动管理器...\n");
    int ret = kv_async_batch_manager_start(manager);
    if (ret != KV_SUCCESS) {
        printf("❌ 管理器启动失败: %d\n", ret);
        kv_async_batch_manager_destroy(manager);
        return 1;
    }
    printf("   ✓ 管理器启动成功\n");
    
    // 4. 创建批量操作上下文
    printf("\n4. 创建批量操作上下文...\n");
    kv_async_batch_context_t *ctx = kv_async_batch_create(manager, 10);
    if (!ctx) {
        printf("❌ 批量操作上下文创建失败\n");
        kv_async_batch_manager_stop(manager);
        kv_async_batch_manager_destroy(manager);
        return 1;
    }
    printf("   ✓ 批量操作上下文创建成功\n");
    
    // 5. 添加批量操作
    printf("\n5. 添加批量操作...\n");
    
    // 添加PUT操作
    for (int i = 0; i < 5; i++) {
        char key[32], value[64];
        snprintf(key, sizeof(key), "user_%d", i);
        snprintf(value, sizeof(value), "{\"id\":%d,\"name\":\"用户%d\"}", i, i);
        
        ret = kv_async_batch_put(ctx, key, strlen(key), value, strlen(value), 
                                 operation_callback, "PUT_DATA");
        if (ret != KV_SUCCESS) {
            printf("   ❌ PUT操作添加失败: key=%s, ret=%d\n", key, ret);
            continue;
        }
        printf("   ✓ PUT操作添加成功: key=%s\n", key);
    }
    
    // 添加GET操作
    for (int i = 0; i < 3; i++) {
        char key[32];
        snprintf(key, sizeof(key), "user_%d", i);
        
        char *value = NULL;
        size_t value_len = 0;
        ret = kv_async_batch_get(ctx, key, strlen(key), &value, &value_len,
                                 operation_callback, "GET_DATA");
        if (ret != KV_SUCCESS) {
            printf("   ❌ GET操作添加失败: key=%s, ret=%d\n", key, ret);
            continue;
        }
        printf("   ✓ GET操作添加成功: key=%s\n", key);
    }
    
    // 添加DELETE操作
    ret = kv_async_batch_delete(ctx, "user_4", 6, operation_callback, "DELETE_DATA");
    if (ret == KV_SUCCESS) {
        printf("   ✓ DELETE操作添加成功: key=user_4\n");
    } else {
        printf("   ❌ DELETE操作添加失败: ret=%d\n", ret);
    }
    
    // 6. 提交批量操作
    printf("\n6. 提交批量操作...\n");
    ret = kv_async_batch_submit(ctx, batch_completion_callback, "BATCH_USER_DATA");
    if (ret != KV_SUCCESS) {
        printf("❌ 批量操作提交失败: %d\n", ret);
    } else {
        printf("   ✓ 批量操作提交成功\n");
    }
    
    // 7. 等待批量操作完成
    printf("\n7. 等待批量操作完成...\n");
    ret = kv_async_batch_wait(ctx, 5000);  // 等待5秒
    if (ret != KV_SUCCESS) {
        printf("❌ 等待批量操作完成失败: %d\n", ret);
    } else {
        printf("   ✓ 批量操作完成\n");
    }
    
    // 8. 获取操作状态和统计信息
    printf("\n8. 获取状态和统计信息...\n");
    kv_async_batch_status_t status = kv_async_batch_get_status(ctx);
    printf("   批量操作状态: %s\n", kv_async_batch_status_name(status));
    
    kv_async_batch_stats_t stats;
    ret = kv_async_batch_get_stats(ctx, &stats);
    if (ret == KV_SUCCESS) {
        printf("   统计信息:\n");
        printf("     - 总操作数: %lu\n", stats.total_operations);
        printf("     - 完成操作数: %lu\n", stats.completed_operations);
        printf("     - 失败操作数: %lu\n", stats.failed_operations);
        printf("     - 总批次数: %lu\n", stats.total_batches);
        printf("     - 平均延迟: %.2f微秒\n", stats.avg_latency_us);
        printf("     - 吞吐量: %.2f ops/sec\n", stats.throughput_ops_per_sec);
    }
    
    // 9. 清理资源
    printf("\n9. 清理资源...\n");
    kv_async_batch_destroy(ctx);
    printf("   ✓ 批量操作上下文已销毁\n");
    
    kv_async_batch_manager_stop(manager);
    printf("   ✓ 管理器已停止\n");
    
    kv_async_batch_manager_destroy(manager);
    printf("   ✓ 管理器已销毁\n");
    
    printf("\n🎉 异步批量I/O功能演示完成！\n");
    
    return 0;
} 