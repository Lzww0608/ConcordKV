
/*
 * @Author: Lzww0608  
 * @Date: 2025-5-31 00:07:46
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-5-31 00:07:48
 * @Description: 逐步调试测试程序
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../../kvserver/kv_engine_interface.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("🔧 Step 1: 开始测试...\n");
    
    // 初始化错误处理系统
    printf("🔧 Step 2: 初始化错误处理系统...\n");
    kv_error_init(KV_LOG_INFO, NULL, 1);
    printf("✅ Step 2: 错误处理系统初始化完成\n");
    
    // 测试无效类型
    printf("🔧 Step 3: 测试无效引擎类型...\n");
    kv_engine_t *engine = kv_engine_create(KV_ENGINE_MAX, NULL);
    if (engine == NULL) {
        printf("✅ Step 3: 正确拒绝了无效引擎类型\n");
    } else {
        printf("❌ Step 3: 应该拒绝无效引擎类型\n");
        return 1;
    }
    
    // 测试Array引擎
    printf("🔧 Step 4: 测试Array引擎创建...\n");
    engine = kv_engine_create(KV_ENGINE_ARRAY, NULL);
    if (engine == NULL) {
        printf("❌ Step 4: Array引擎创建失败\n");
        return 1;
    }
    printf("✅ Step 4: Array引擎创建成功\n");
    
    printf("🔧 Step 5: 销毁Array引擎...\n");
    int ret = kv_engine_destroy(engine);
    if (ret == KV_ERR_NONE) {
        printf("✅ Step 5: Array引擎销毁成功\n");
    } else {
        printf("❌ Step 5: Array引擎销毁失败\n");
        return 1;
    }
    
    // 测试RBTree引擎
    printf("🔧 Step 6: 测试RBTree引擎创建...\n");
    engine = kv_engine_create(KV_ENGINE_RBTREE, NULL);
    if (engine == NULL) {
        printf("❌ Step 6: RBTree引擎创建失败\n");
        return 1;
    }
    printf("✅ Step 6: RBTree引擎创建成功\n");
    
    printf("🔧 Step 7: 销毁RBTree引擎...\n");
    ret = kv_engine_destroy(engine);
    if (ret == KV_ERR_NONE) {
        printf("✅ Step 7: RBTree引擎销毁成功\n");
    } else {
        printf("❌ Step 7: RBTree引擎销毁失败\n");
        return 1;
    }
    
    printf("🎉 所有步骤测试完成！\n");
    return 0;
} 