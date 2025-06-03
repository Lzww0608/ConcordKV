/*
 * @Author: Lzww0608  
 * @Date: 2025-5-30 22:42:07
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-3 21:50:57
 * @Description: ConcordKV 内存管理模块
 */
#include "kv_store.h"
#include <stdlib.h>

// 内存分配函数
void *kv_store_malloc(size_t size) {
#if ENABLE_MEM_POOL
    // 如果启用内存池，在这里添加内存池逻辑
    // 目前直接使用标准malloc
    return malloc(size);
#else
    return malloc(size);
#endif
}

// 内存释放函数
void kv_store_free(void *ptr) {
#if ENABLE_MEM_POOL
    // 如果启用内存池，在这里添加内存池逻辑
    // 目前直接使用标准free
    free(ptr);
#else
    free(ptr);
#endif
}

// 内存重新分配函数
void *kv_store_realloc(void *ptr, size_t size) {
#if ENABLE_MEM_POOL
    // 如果启用内存池，在这里添加内存池逻辑
    // 目前直接使用标准realloc
    return realloc(ptr, size);
#else
    return realloc(ptr, size);
#endif
} 