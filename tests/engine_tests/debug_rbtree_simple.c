
/*
 * @Author: Lzww0608  
 * @Date: 2025-5-31 00:06:05
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-5-31 00:06:08
 * @Description: 简单的RBTree测试程序 - 只测试创建和销毁
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../../kvserver/kv_engine_interface.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("🔧 开始简单RBTree测试...\n");
    
    // 初始化错误处理系统
    kv_error_init(KV_LOG_INFO, NULL, 1);
    
    // 测试RBTree引擎创建
    printf("   创建RBTree引擎...\n");
    kv_engine_t *engine = kv_engine_create(KV_ENGINE_RBTREE, NULL);
    if (engine == NULL) {
        printf("❌ 创建RBTree引擎失败\n");
        return 1;
    }
    printf("✅ RBTree引擎创建成功\n");
    
    // 立即销毁，不进行任何操作
    printf("   销毁RBTree引擎...\n");
    int ret = kv_engine_destroy(engine);
    if (ret != KV_ERR_NONE) {
        printf("❌ 销毁RBTree引擎失败，返回值: %d\n", ret);
        return 1;
    }
    printf("✅ RBTree引擎销毁成功\n");
    
    printf("🎉 简单RBTree测试完成\n");
    return 0;
} 