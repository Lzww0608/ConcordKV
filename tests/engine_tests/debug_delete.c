
/*
 * @Author: Lzww0608  
 * @Date: 2025-5-31 00:04:23
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-5-31 00:04:26
 * @Description: 调试delete函数返回值的测试程序
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../../kvserver/kv_engine_interface.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("🔧 开始调试delete函数返回值...\n");
    
    // 初始化错误处理系统
    kv_error_init(KV_LOG_INFO, NULL, 1);
    
    // 测试RBTree引擎
    printf("\n🔧 测试RBTree引擎的delete返回值...\n");
    kv_engine_t *engine = kv_engine_create(KV_ENGINE_RBTREE, NULL);
    if (engine == NULL) {
        printf("❌ 创建RBTree引擎失败\n");
        return 1;
    }
    
    // 先插入一个键
    printf("   插入键 'exist_key'...\n");
    int ret = KV_ENGINE_SET(engine, "exist_key", "exist_value");
    printf("   SET返回值: %d\n", ret);
    
    // 删除存在的键
    printf("   删除存在的键 'exist_key'...\n");
    ret = KV_ENGINE_DELETE(engine, "exist_key");
    printf("   DELETE存在键返回值: %d (期望: %d)\n", ret, KV_ERR_NONE);
    
    // 删除不存在的键
    printf("   删除不存在的键 'nonexistent'...\n");
    ret = KV_ENGINE_DELETE(engine, "nonexistent");
    printf("   DELETE不存在键返回值: %d (期望: %d)\n", ret, KV_ERR_NOT_FOUND);
    
    kv_engine_destroy(engine);
    
    // 测试Hash引擎
    printf("\n🔧 测试Hash引擎的delete返回值...\n");
    engine = kv_engine_create(KV_ENGINE_HASH, NULL);
    if (engine == NULL) {
        printf("❌ 创建Hash引擎失败\n");
        return 1;
    }
    
    // 先插入一个键
    printf("   插入键 'exist_key'...\n");
    ret = KV_ENGINE_SET(engine, "exist_key", "exist_value");
    printf("   SET返回值: %d\n", ret);
    
    // 删除存在的键
    printf("   删除存在的键 'exist_key'...\n");
    ret = KV_ENGINE_DELETE(engine, "exist_key");
    printf("   DELETE存在键返回值: %d (期望: %d)\n", ret, KV_ERR_NONE);
    
    // 删除不存在的键
    printf("   删除不存在的键 'nonexistent'...\n");
    ret = KV_ENGINE_DELETE(engine, "nonexistent");
    printf("   DELETE不存在键返回值: %d (期望: %d)\n", ret, KV_ERR_NOT_FOUND);
    
    kv_engine_destroy(engine);
    
    // 测试Array引擎（对比）
    printf("\n🔧 测试Array引擎的delete返回值（对比）...\n");
    engine = kv_engine_create(KV_ENGINE_ARRAY, NULL);
    if (engine == NULL) {
        printf("❌ 创建Array引擎失败\n");
        return 1;
    }
    
    // 先插入一个键
    printf("   插入键 'exist_key'...\n");
    ret = KV_ENGINE_SET(engine, "exist_key", "exist_value");
    printf("   SET返回值: %d\n", ret);
    
    // 删除存在的键
    printf("   删除存在的键 'exist_key'...\n");
    ret = KV_ENGINE_DELETE(engine, "exist_key");
    printf("   DELETE存在键返回值: %d (期望: %d)\n", ret, KV_ERR_NONE);
    
    // 删除不存在的键
    printf("   删除不存在的键 'nonexistent'...\n");
    ret = KV_ENGINE_DELETE(engine, "nonexistent");
    printf("   DELETE不存在键返回值: %d (期望: %d)\n", ret, KV_ERR_NOT_FOUND);
    
    kv_engine_destroy(engine);
    
    printf("\n🎉 delete函数返回值调试完成\n");
    return 0;
} 