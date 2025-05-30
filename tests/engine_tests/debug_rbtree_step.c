
/*
 * @Author: Lzww0608  
 * @Date: 2025-5-31 00:06:05
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-5-31 00:06:08
 * @Description: 逐步的RBTree测试程序 - 找出问题所在
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../../kvserver/kv_engine_interface.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("🔧 开始逐步RBTree测试...\n");
    
    // 初始化错误处理系统
    kv_error_init(KV_LOG_INFO, NULL, 1);
    
    // 步骤1：创建引擎
    printf("\n🔧 Step 1: 创建RBTree引擎...\n");
    kv_engine_t *engine = kv_engine_create(KV_ENGINE_RBTREE, NULL);
    if (engine == NULL) {
        printf("❌ 创建RBTree引擎失败\n");
        return 1;
    }
    printf("✅ Step 1: RBTree引擎创建成功\n");
    
    // 步骤2：测试第一个SET操作
    printf("\n🔧 Step 2: 测试第一个SET操作...\n");
    int ret = KV_ENGINE_SET(engine, "key1", "value1");
    if (ret != KV_ERR_NONE) {
        printf("❌ Step 2: SET操作失败，返回值: %d\n", ret);
        kv_engine_destroy(engine);
        return 1;
    }
    printf("✅ Step 2: 第一个SET操作成功\n");
    
    // 步骤3：测试第二个SET操作
    printf("\n🔧 Step 3: 测试第二个SET操作...\n");
    ret = KV_ENGINE_SET(engine, "key2", "value2");
    if (ret != KV_ERR_NONE) {
        printf("❌ Step 3: SET操作失败，返回值: %d\n", ret);
        kv_engine_destroy(engine);
        return 1;
    }
    printf("✅ Step 3: 第二个SET操作成功\n");
    
    // 步骤4：测试GET操作
    printf("\n🔧 Step 4: 测试GET操作...\n");
    char *value = KV_ENGINE_GET(engine, "key1");
    if (value == NULL) {
        printf("❌ Step 4: GET操作失败\n");
        kv_engine_destroy(engine);
        return 1;
    }
    printf("✅ Step 4: GET操作成功，值: '%s'\n", value);
    
    // 步骤5：测试COUNT操作
    printf("\n🔧 Step 5: 测试COUNT操作...\n");
    int count = KV_ENGINE_COUNT(engine);
    printf("✅ Step 5: COUNT操作成功，数量: %d\n", count);
    
    // 步骤6：测试DELETE操作
    printf("\n🔧 Step 6: 测试DELETE操作...\n");
    ret = KV_ENGINE_DELETE(engine, "key1");
    if (ret != KV_ERR_NONE) {
        printf("❌ Step 6: DELETE操作失败，返回值: %d\n", ret);
        kv_engine_destroy(engine);
        return 1;
    }
    printf("✅ Step 6: DELETE操作成功\n");
    
    // 步骤7：验证删除
    printf("\n🔧 Step 7: 验证删除...\n");
    value = KV_ENGINE_GET(engine, "key1");
    if (value != NULL) {
        printf("❌ Step 7: 键应该已被删除\n");
        kv_engine_destroy(engine);
        return 1;
    }
    printf("✅ Step 7: 删除验证成功\n");
    
    // 步骤8：销毁引擎
    printf("\n🔧 Step 8: 销毁引擎...\n");
    ret = kv_engine_destroy(engine);
    if (ret != KV_ERR_NONE) {
        printf("❌ Step 8: 销毁引擎失败，返回值: %d\n", ret);
        return 1;
    }
    printf("✅ Step 8: 引擎销毁成功\n");
    
    printf("\n🎉 逐步RBTree测试全部完成\n");
    return 0;
} 