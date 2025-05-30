
/*
 * @Author: Lzww0608  
 * @Date: 2025-5-31 00:06:41
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-5-31 00:06:43
 * @Description: 简化的调试测试程序
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../../kvserver/kv_engine_interface.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("🔧 开始调试测试...\n");
    
    // 初始化错误处理系统
    printf("🔧 初始化错误处理系统...\n");
    kv_error_init(KV_LOG_INFO, NULL, 1);
    printf("✅ 错误处理系统初始化完成\n");
    
    // 测试创建Array引擎
    printf("🔧 尝试创建Array引擎...\n");
    kv_engine_t *engine = kv_engine_create(KV_ENGINE_ARRAY, NULL);
    
    if (engine == NULL) {
        printf("❌ Array引擎创建失败\n");
        return 1;
    }
    
    printf("✅ Array引擎创建成功\n");
    printf("   引擎类型: %d\n", engine->type);
    printf("   引擎状态: %d\n", engine->state);
    printf("   引擎名称: %s\n", engine->name);
    
    // 测试基本操作
    printf("🔧 测试基本SET操作...\n");
    int ret = KV_ENGINE_SET(engine, "test_key", "test_value");
    printf("   SET结果: %d\n", ret);
    
    if (ret == KV_ERR_NONE) {
        printf("🔧 测试基本GET操作...\n");
        char *value = KV_ENGINE_GET(engine, "test_key");
        if (value) {
            printf("✅ GET成功: %s\n", value);
        } else {
            printf("❌ GET失败\n");
        }
    }
    
    // 销毁引擎
    printf("🔧 销毁引擎...\n");
    ret = kv_engine_destroy(engine);
    printf("   销毁结果: %d\n", ret);
    
    printf("🎉 调试测试完成\n");
    return 0;
} 