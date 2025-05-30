/*
 * 
 */

/*
 * @Author: Lzww0608  
 * @Date: 2025-5-31 00:08:20
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-5-31 00:08:22
 * @Description: 带详细跟踪的测试程序
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>

// 先测试基本的内存分配
void *kv_store_malloc(size_t size) {
    printf("🔧 kv_store_malloc called with size: %zu\n", size);
    void *ptr = malloc(size);
    printf("🔧 kv_store_malloc returned: %p\n", ptr);
    return ptr;
}

void kv_store_free(void *ptr) {
    printf("🔧 kv_store_free called with ptr: %p\n", ptr);
    free(ptr);
    printf("🔧 kv_store_free completed\n");
}

// 模拟kv_engine_create的开始部分
int main() {
    printf("🔧 跟踪测试开始...\n");
    
    printf("🔧 测试内存分配...\n");
    void *test_ptr = kv_store_malloc(100);
    if (test_ptr) {
        printf("✅ 内存分配成功\n");
        kv_store_free(test_ptr);
        printf("✅ 内存释放成功\n");
    } else {
        printf("❌ 内存分配失败\n");
        return 1;
    }
    
    printf("🔧 测试引擎类型检查...\n");
    int type = 5; // KV_ENGINE_MAX
    printf("🔧 type = %d\n", type);
    
    if (type >= 5) { // KV_ENGINE_MAX
        printf("✅ 正确检测到无效类型\n");
        return 0;
    }
    
    printf("🎉 跟踪测试完成！\n");
    return 0;
} 