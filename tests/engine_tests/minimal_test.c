
/*
 * @Author: Lzww0608  
 * @Date: 2025-5-31 00:07:27
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-5-31 00:07:30
 * @Description: 最小化测试程序 - 不使用日志记录
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "../../kvserver/kv_engine_interface.h"
#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("🔧 最小化测试开始...\n");
    
    // 不初始化错误处理系统，直接测试
    printf("🔧 测试无效引擎类型...\n");
    kv_engine_t *engine = kv_engine_create(KV_ENGINE_MAX, NULL);
    
    if (engine == NULL) {
        printf("✅ 正确拒绝了无效引擎类型\n");
    } else {
        printf("❌ 应该拒绝无效引擎类型\n");
        return 1;
    }
    
    printf("🔧 测试Array引擎创建...\n");
    engine = kv_engine_create(KV_ENGINE_ARRAY, NULL);
    
    if (engine == NULL) {
        printf("❌ Array引擎创建失败\n");
        return 1;
    }
    
    printf("✅ Array引擎创建成功\n");
    printf("   引擎类型: %d\n", engine->type);
    printf("   引擎状态: %d\n", engine->state);
    
    // 销毁引擎
    printf("🔧 销毁引擎...\n");
    int ret = kv_engine_destroy(engine);
    
    if (ret == 0) {
        printf("✅ 引擎销毁成功\n");
    } else {
        printf("❌ 引擎销毁失败: %d\n", ret);
        return 1;
    }
    
    printf("🎉 最小化测试完成！\n");
    return 0;
} 