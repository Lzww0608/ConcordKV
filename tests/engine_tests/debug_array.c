
/*
 * @Author: Lzww0608  
 * @Date: 2025-5-31 00:03:29
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-5-31 00:03:38
 * @Description: 专门调试Array引擎delete问题的程序
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../../kvserver/kv_engine_interface.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("🔧 开始调试Array引擎delete问题...\n");
    
    // 初始化错误处理系统
    kv_error_init(KV_LOG_INFO, NULL, 1);
    
    // 测试Array引擎
    printf("\n🔧 测试Array引擎详细流程...\n");
    kv_engine_t *engine = kv_engine_create(KV_ENGINE_ARRAY, NULL);
    if (engine == NULL) {
        printf("❌ 创建Array引擎失败\n");
        return 1;
    }
    
    // 检查初始状态
    printf("   检查初始状态...\n");
    char *value = KV_ENGINE_GET(engine, "nonexistent");
    printf("   GET不存在键返回值: %p\n", (void*)value);
    
    // 插入一个键
    printf("   插入键 'test_key'...\n");
    int ret = KV_ENGINE_SET(engine, "test_key", "test_value");
    printf("   SET返回值: %d\n", ret);
    
    // 验证插入成功
    printf("   验证插入成功...\n");
    value = KV_ENGINE_GET(engine, "test_key");
    printf("   GET存在键返回值: %p\n", (void*)value);
    if (value) {
        printf("   GET存在键内容: '%s'\n", value);
    }
    
    // 尝试GET不存在的键
    printf("   GET不存在的键...\n");
    value = KV_ENGINE_GET(engine, "definitely_not_exist");
    printf("   GET不存在键返回值: %p\n", (void*)value);
    
    // 删除存在的键
    printf("   删除存在的键 'test_key'...\n");
    ret = KV_ENGINE_DELETE(engine, "test_key");
    printf("   DELETE存在键返回值: %d (期望: 0)\n", ret);
    
    // 验证删除成功
    printf("   验证删除成功...\n");
    value = KV_ENGINE_GET(engine, "test_key");
    printf("   GET已删除键返回值: %p\n", (void*)value);
    
    // 删除不存在的键
    printf("   删除不存在的键 'nonexistent'...\n");
    ret = KV_ENGINE_DELETE(engine, "nonexistent");
    printf("   DELETE不存在键返回值: %d (期望: 6)\n", ret);
    
    kv_engine_destroy(engine);
    
    printf("\n🎉 Array引擎delete调试完成\n");
    return 0;
} 