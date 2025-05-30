
/*
 * @Author: Lzww0608  
 * @Date: 2025-5-31 00:05:45
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-5-31 00:05:48
 * @Description: 详细调试工厂函数的测试程序
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../../kvserver/kv_engine_interface.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("🔧 开始详细调试工厂函数...\n");
    
    // 初始化错误处理系统
    printf("🔧 初始化错误处理系统...\n");
    kv_error_init(KV_LOG_INFO, NULL, 1);
    printf("✅ 错误处理系统初始化完成\n");
    
    // 测试步骤1：测试无效类型（KV_ENGINE_MAX）
    printf("\n🔧 Step 1: 测试无效引擎类型 KV_ENGINE_MAX...\n");
    kv_engine_t *engine = kv_engine_create(KV_ENGINE_MAX, NULL);
    if (engine == NULL) {
        printf("✅ Step 1: 正确拒绝了KV_ENGINE_MAX类型\n");
    } else {
        printf("❌ Step 1: 不应该创建KV_ENGINE_MAX类型的引擎\n");
        kv_engine_destroy(engine);
        return 1;
    }
    
    // 测试步骤2：测试Array引擎创建
    printf("\n🔧 Step 2: 测试Array引擎创建...\n");
    engine = kv_engine_create(KV_ENGINE_ARRAY, NULL);
    if (engine == NULL) {
        printf("❌ Step 2: Array引擎创建失败\n");
        return 1;
    }
    printf("✅ Step 2: Array引擎创建成功\n");
    printf("   引擎类型: %d (期望: %d)\n", engine->type, KV_ENGINE_ARRAY);
    printf("   引擎状态: %d (期望: %d)\n", engine->state, KV_ENGINE_STATE_RUNNING);
    printf("   引擎名称: '%s'\n", engine->name);
    printf("   引擎名称长度: %zu\n", strlen(engine->name));
    
    // 检查引擎属性
    if (engine->type != KV_ENGINE_ARRAY) {
        printf("❌ Step 2: 引擎类型不匹配\n");
        kv_engine_destroy(engine);
        return 1;
    }
    
    if (engine->state != KV_ENGINE_STATE_RUNNING) {
        printf("❌ Step 2: 引擎状态不是运行状态\n");
        kv_engine_destroy(engine);
        return 1;
    }
    
    if (strlen(engine->name) == 0) {
        printf("❌ Step 2: 引擎名称为空\n");
        kv_engine_destroy(engine);
        return 1;
    }
    
    printf("✅ Step 2: Array引擎属性验证通过\n");
    
    // 测试步骤3：销毁Array引擎
    printf("\n🔧 Step 3: 销毁Array引擎...\n");
    int ret = kv_engine_destroy(engine);
    if (ret != KV_ERR_NONE) {
        printf("❌ Step 3: Array引擎销毁失败，返回值: %d\n", ret);
        return 1;
    }
    printf("✅ Step 3: Array引擎销毁成功\n");
    
    // 测试步骤4：测试RBTree引擎创建
    printf("\n🔧 Step 4: 测试RBTree引擎创建...\n");
    engine = kv_engine_create(KV_ENGINE_RBTREE, NULL);
    if (engine == NULL) {
        printf("❌ Step 4: RBTree引擎创建失败\n");
        return 1;
    }
    printf("✅ Step 4: RBTree引擎创建成功\n");
    printf("   引擎类型: %d (期望: %d)\n", engine->type, KV_ENGINE_RBTREE);
    printf("   引擎名称: '%s'\n", engine->name);
    
    if (engine->type != KV_ENGINE_RBTREE) {
        printf("❌ Step 4: RBTree引擎类型不匹配\n");
        kv_engine_destroy(engine);
        return 1;
    }
    
    // 销毁RBTree引擎
    ret = kv_engine_destroy(engine);
    if (ret != KV_ERR_NONE) {
        printf("❌ Step 4: RBTree引擎销毁失败\n");
        return 1;
    }
    printf("✅ Step 4: RBTree引擎销毁成功\n");
    
    // 测试步骤5：测试Hash引擎创建
    printf("\n🔧 Step 5: 测试Hash引擎创建...\n");
    engine = kv_engine_create(KV_ENGINE_HASH, NULL);
    if (engine == NULL) {
        printf("❌ Step 5: Hash引擎创建失败\n");
        return 1;
    }
    printf("✅ Step 5: Hash引擎创建成功\n");
    printf("   引擎类型: %d (期望: %d)\n", engine->type, KV_ENGINE_HASH);
    printf("   引擎名称: '%s'\n", engine->name);
    
    if (engine->type != KV_ENGINE_HASH) {
        printf("❌ Step 5: Hash引擎类型不匹配\n");
        kv_engine_destroy(engine);
        return 1;
    }
    
    ret = kv_engine_destroy(engine);
    if (ret != KV_ERR_NONE) {
        printf("❌ Step 5: Hash引擎销毁失败\n");
        return 1;
    }
    printf("✅ Step 5: Hash引擎销毁成功\n");
    
    // 测试步骤6：测试未实现的引擎类型
    printf("\n🔧 Step 6: 测试未实现的引擎类型...\n");
    
    printf("   测试BTree引擎（未实现）...\n");
    engine = kv_engine_create(KV_ENGINE_BTREE, NULL);
    if (engine == NULL) {
        printf("✅ BTree引擎正确返回NULL（未实现）\n");
    } else {
        printf("❌ BTree引擎不应该被创建\n");
        kv_engine_destroy(engine);
        return 1;
    }
    
    printf("   测试LSM引擎（未实现）...\n");
    engine = kv_engine_create(KV_ENGINE_LSM, NULL);
    if (engine == NULL) {
        printf("✅ LSM引擎正确返回NULL（未实现）\n");
    } else {
        printf("❌ LSM引擎不应该被创建\n");
        kv_engine_destroy(engine);
        return 1;
    }
    
    printf("✅ Step 6: 未实现引擎类型测试通过\n");
    
    printf("\n🎉 工厂函数详细测试全部通过！\n");
    return 0;
} 