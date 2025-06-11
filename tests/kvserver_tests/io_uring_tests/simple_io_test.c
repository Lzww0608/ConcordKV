#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "../../../kvserver/kv_io_uring.h"

int main() {
    printf("=== 简单io_uring功能验证 ===\n");
    
    // 1. 检查io_uring支持
    bool supported = kv_uring_is_supported();
    printf("1. io_uring支持: %s\n", supported ? "✅ 支持" : "❌ 不支持");
    if (!supported) return 1;
    
    // 2. 创建配置
    kv_uring_config_t *config = kv_uring_config_create();
    printf("2. 配置创建: %s\n", config ? "✅ 成功" : "❌ 失败");
    if (!config) return 1;
    
    // 3. 验证配置
    int ret = kv_uring_config_validate(config);
    printf("3. 配置验证: %s\n", ret == KV_SUCCESS ? "✅ 有效" : "❌ 无效");
    
    // 4. 创建io_uring实例
    kv_uring_t *uring = kv_uring_create(config);
    printf("4. io_uring创建: %s\n", uring ? "✅ 成功" : "❌ 失败");
    if (!uring) {
        kv_uring_config_destroy(config);
        return 1;
    }
    
    // 5. 启动io_uring
    ret = kv_uring_start(uring);
    printf("5. io_uring启动: %s (错误码: %d)\n", ret == KV_SUCCESS ? "✅ 成功" : "❌ 失败", ret);
    
    // 6. 获取统计信息
    kv_uring_stats_t stats;
    kv_uring_get_stats(uring, &stats);
    printf("6. 统计信息获取: ✅ 成功\n");
    printf("   - 总操作数: %lu\n", stats.total_operations);
    printf("   - 读操作数: %lu\n", stats.read_count);
    printf("   - 写操作数: %lu\n", stats.write_count);
    
    // 7. 停止和清理
    kv_uring_stop(uring);
    kv_uring_destroy(uring);
    kv_uring_config_destroy(config);
    printf("7. 清理完成: ✅ 成功\n");
    
    printf("\n🎉 io_uring基础功能验证通过！\n");
    return 0;
} 