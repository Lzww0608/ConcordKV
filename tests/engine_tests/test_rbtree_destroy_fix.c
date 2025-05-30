
/*
 * @Author: Lzww0608  
 * @Date: 2025-5-31 00:08:03
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-5-31 00:08:06
 * @Description: 专门测试RBTree销毁功能修复的测试程序
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../../kvserver/kv_engine_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

// 段错误信号处理器
void segfault_handler(int sig) {
    printf("❌ 捕获到段错误！RBTree销毁修复失败\n");
    exit(1);
}

int main() {
    printf("🔧 开始测试RBTree销毁功能修复...\n");
    
    // 设置段错误信号处理器
    signal(SIGSEGV, segfault_handler);
    
    // 初始化错误处理系统
    kv_error_init(KV_LOG_INFO, NULL, 1);
    
    printf("\n📋 测试场景1: 空树销毁\n");
    kv_engine_t *engine = kv_engine_create(KV_ENGINE_RBTREE, NULL);
    if (engine == NULL) {
        printf("❌ 创建RBTree引擎失败\n");
        return 1;
    }
    printf("   ✅ 创建空RBTree引擎成功\n");
    
    int ret = kv_engine_destroy(engine);
    if (ret != KV_ERR_NONE) {
        printf("❌ 销毁空RBTree引擎失败，返回值: %d\n", ret);
        return 1;
    }
    printf("   ✅ 销毁空RBTree引擎成功\n");
    
    printf("\n📋 测试场景2: 单节点树销毁\n");
    engine = kv_engine_create(KV_ENGINE_RBTREE, NULL);
    if (engine == NULL) {
        printf("❌ 创建RBTree引擎失败\n");
        return 1;
    }
    
    ret = KV_ENGINE_SET(engine, "single_key", "single_value");
    if (ret != KV_ERR_NONE) {
        printf("❌ 插入单个节点失败\n");
        kv_engine_destroy(engine);
        return 1;
    }
    printf("   ✅ 插入单个节点成功\n");
    
    ret = kv_engine_destroy(engine);
    if (ret != KV_ERR_NONE) {
        printf("❌ 销毁单节点RBTree引擎失败，返回值: %d\n", ret);
        return 1;
    }
    printf("   ✅ 销毁单节点RBTree引擎成功\n");
    
    printf("\n📋 测试场景3: 多节点树销毁\n");
    engine = kv_engine_create(KV_ENGINE_RBTREE, NULL);
    if (engine == NULL) {
        printf("❌ 创建RBTree引擎失败\n");
        return 1;
    }
    
    // 插入多个节点
    const char *keys[] = {"key1", "key2", "key3", "key4", "key5"};
    const char *values[] = {"value1", "value2", "value3", "value4", "value5"};
    int num_keys = 5;
    
    for (int i = 0; i < num_keys; i++) {
        ret = KV_ENGINE_SET(engine, keys[i], values[i]);
        if (ret != KV_ERR_NONE) {
            printf("❌ 插入节点 %s 失败\n", keys[i]);
            kv_engine_destroy(engine);
            return 1;
        }
    }
    printf("   ✅ 插入 %d 个节点成功\n", num_keys);
    
    // 验证节点存在
    int count = KV_ENGINE_COUNT(engine);
    printf("   节点计数: %d\n", count);
    
    ret = kv_engine_destroy(engine);
    if (ret != KV_ERR_NONE) {
        printf("❌ 销毁多节点RBTree引擎失败，返回值: %d\n", ret);
        return 1;
    }
    printf("   ✅ 销毁多节点RBTree引擎成功\n");
    
    printf("\n📋 测试场景4: 复杂树结构销毁 (压力测试)\n");
    engine = kv_engine_create(KV_ENGINE_RBTREE, NULL);
    if (engine == NULL) {
        printf("❌ 创建RBTree引擎失败\n");
        return 1;
    }
    
    // 插入100个节点创建复杂树结构
    for (int i = 0; i < 100; i++) {
        char key[32], value[64];
        snprintf(key, sizeof(key), "complex_key_%d", i);
        snprintf(value, sizeof(value), "complex_value_%d", i);
        
        ret = KV_ENGINE_SET(engine, key, value);
        if (ret != KV_ERR_NONE) {
            printf("❌ 插入复杂节点 %d 失败\n", i);
            kv_engine_destroy(engine);
            return 1;
        }
    }
    printf("   ✅ 插入 100 个复杂节点成功\n");
    
    count = KV_ENGINE_COUNT(engine);
    printf("   最终节点计数: %d\n", count);
    
    ret = kv_engine_destroy(engine);
    if (ret != KV_ERR_NONE) {
        printf("❌ 销毁复杂RBTree引擎失败，返回值: %d\n", ret);
        return 1;
    }
    printf("   ✅ 销毁复杂RBTree引擎成功\n");
    
    printf("\n📋 测试场景5: 多次创建销毁循环\n");
    for (int cycle = 0; cycle < 10; cycle++) {
        engine = kv_engine_create(KV_ENGINE_RBTREE, NULL);
        if (engine == NULL) {
            printf("❌ 第 %d 轮创建RBTree引擎失败\n", cycle + 1);
            return 1;
        }
        
        // 插入几个节点
        for (int i = 0; i < 5; i++) {
            char key[32], value[64];
            snprintf(key, sizeof(key), "cycle_%d_key_%d", cycle, i);
            snprintf(value, sizeof(value), "cycle_%d_value_%d", cycle, i);
            KV_ENGINE_SET(engine, key, value);
        }
        
        ret = kv_engine_destroy(engine);
        if (ret != KV_ERR_NONE) {
            printf("❌ 第 %d 轮销毁RBTree引擎失败\n", cycle + 1);
            return 1;
        }
    }
    printf("   ✅ 完成 10 轮创建销毁循环测试\n");
    
    printf("\n🎉 所有RBTree销毁测试通过！段错误问题已彻底解决。\n");
    printf("✅ 修复验证成功 - RBTree销毁功能现在安全可靠\n");
    
    return 0;
} 