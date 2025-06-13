/*
 * @Author: Lzww0608  
 * @Date: 2025-6-13 16:02:54
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-13 16:02:57
 * @Description: ConcordKV Arena内存池使用演示
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "../kvserver/kv_memory.h"

// 获取时间（微秒）
static uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

// 演示基础使用
void demo_basic_usage(void) {
    printf("🔹 基础使用演示\n");
    printf("=====================================\n");
    
    // 创建arena
    kv_arena_t *arena = kv_arena_create(8192);  // 8KB块大小
    if (!arena) {
        printf("❌ 创建arena失败\n");
        return;
    }
    
    printf("✅ 创建arena成功，块大小: 8192 bytes\n");
    
    // 分配不同大小的内存
    void *ptr1 = kv_arena_alloc(arena, 256);
    void *ptr2 = kv_arena_alloc(arena, 512);
    void *ptr3 = kv_arena_alloc(arena, 1024);
    
    printf("✅ 分配内存: 256, 512, 1024 bytes\n");
    printf("   指针地址: %p, %p, %p\n", ptr1, ptr2, ptr3);
    
    // 写入测试数据
    strcpy((char*)ptr1, "Hello Arena!");
    memset(ptr2, 0xAB, 512);
    memset(ptr3, 0xCD, 1024);
    
    printf("✅ 写入测试数据成功\n");
    printf("   字符串: %s\n", (char*)ptr1);
    printf("   数据验证: ptr2[0]=0x%02X, ptr3[0]=0x%02X\n", 
           ((unsigned char*)ptr2)[0], ((unsigned char*)ptr3)[0]);
    
    // 显示统计信息
    printf("📊 统计信息:\n");
    printf("   内存使用: %zu bytes\n", kv_arena_memory_usage(arena));
    printf("   总分配: %zu bytes\n", kv_arena_total_allocated(arena));
    
    kv_arena_destroy(arena);
    printf("✅ 清理完成\n\n");
}

// 演示对齐分配
void demo_aligned_allocation(void) {
    printf("🔹 对齐分配演示\n");
    printf("=====================================\n");
    
    kv_arena_t *arena = kv_arena_create(4096);
    if (!arena) {
        printf("❌ 创建arena失败\n");
        return;
    }
    
    // 分配不同对齐要求的内存
    void *ptr_8 = kv_arena_alloc_aligned(arena, 100, 8);
    void *ptr_16 = kv_arena_alloc_aligned(arena, 100, 16);
    void *ptr_64 = kv_arena_alloc_aligned(arena, 100, 64);
    void *ptr_256 = kv_arena_alloc_aligned(arena, 100, 256);
    
    printf("✅ 对齐分配结果:\n");
    printf("   8字节对齐:   %p (对齐: %s)\n", 
           ptr_8, ((uintptr_t)ptr_8 % 8 == 0) ? "✓" : "✗");
    printf("   16字节对齐:  %p (对齐: %s)\n", 
           ptr_16, ((uintptr_t)ptr_16 % 16 == 0) ? "✓" : "✗");
    printf("   64字节对齐:  %p (对齐: %s)\n", 
           ptr_64, ((uintptr_t)ptr_64 % 64 == 0) ? "✓" : "✗");
    printf("   256字节对齐: %p (对齐: %s)\n", 
           ptr_256, ((uintptr_t)ptr_256 % 256 == 0) ? "✓" : "✗");
    
    kv_arena_destroy(arena);
    printf("✅ 对齐分配演示完成\n\n");
}

// 演示多块分配
void demo_multiple_blocks(void) {
    printf("🔹 多块分配演示\n");
    printf("=====================================\n");
    
    // 创建小块arena以触发多块分配
    kv_arena_t *arena = kv_arena_create(1024);
    if (!arena) {
        printf("❌ 创建arena失败\n");
        return;
    }
    
    printf("创建小块arena (1024 bytes) 来演示多块分配\n");
    
    // 分配多个块
    void *ptrs[8];
    for (int i = 0; i < 8; i++) {
        ptrs[i] = kv_arena_alloc(arena, 200);  // 每次分配200字节
        printf("分配 #%d: %p\n", i + 1, ptrs[i]);
        
        if (ptrs[i]) {
            // 写入标识数据
            sprintf((char*)ptrs[i], "Block_%d", i);
        }
    }
    
    printf("\n验证数据完整性:\n");
    for (int i = 0; i < 8; i++) {
        if (ptrs[i]) {
            printf("Block %d: %s\n", i + 1, (char*)ptrs[i]);
        }
    }
    
    printf("\n📊 多块统计:\n");
    printf("   内存使用: %zu bytes\n", kv_arena_memory_usage(arena));
    printf("   总分配: %zu bytes\n", kv_arena_total_allocated(arena));
    
    kv_arena_destroy(arena);
    printf("✅ 多块分配演示完成\n\n");
}

// 演示性能对比
void demo_performance_comparison(void) {
    printf("🔹 性能对比演示\n");
    printf("=====================================\n");
    
    const int num_allocs = 50000;
    const size_t alloc_size = 64;
    
    // Arena性能测试
    kv_arena_t *arena = kv_arena_create(64 * 1024);  // 64KB块
    
    uint64_t start_time = get_time_us();
    for (int i = 0; i < num_allocs; i++) {
        void *ptr = kv_arena_alloc(arena, alloc_size);
        if (ptr) {
            // 简单写入以模拟使用
            *(int*)ptr = i;
        }
    }
    uint64_t arena_time = get_time_us() - start_time;
    
    // 标准malloc性能测试
    start_time = get_time_us();
    void **malloc_ptrs = malloc(num_allocs * sizeof(void*));
    for (int i = 0; i < num_allocs; i++) {
        malloc_ptrs[i] = malloc(alloc_size);
        if (malloc_ptrs[i]) {
            *(int*)malloc_ptrs[i] = i;
        }
    }
    uint64_t malloc_time = get_time_us() - start_time;
    
    // 清理malloc分配的内存
    for (int i = 0; i < num_allocs; i++) {
        if (malloc_ptrs[i]) {
            free(malloc_ptrs[i]);
        }
    }
    free(malloc_ptrs);
    
    printf("性能测试结果 (%d 次 %zu 字节分配):\n", num_allocs, alloc_size);
    printf("   Arena时间:  %lu μs\n", arena_time);
    printf("   Malloc时间: %lu μs\n", malloc_time);
    printf("   性能提升:   %.2fx\n", (double)malloc_time / arena_time);
    
    printf("\nArena内存效率:\n");
    printf("   内存使用: %zu bytes\n", kv_arena_memory_usage(arena));
    printf("   总分配:   %zu bytes\n", kv_arena_total_allocated(arena));
    printf("   效率:     %.2f%%\n", 
           (double)kv_arena_total_allocated(arena) / kv_arena_memory_usage(arena) * 100);
    
    kv_arena_destroy(arena);
    printf("✅ 性能对比演示完成\n\n");
}

// 演示实际使用场景
void demo_real_world_usage(void) {
    printf("🔹 实际场景演示 - 简单KV存储\n");
    printf("=====================================\n");
    
    // 模拟简单的键值对存储
    typedef struct {
        char *key;
        char *value;
        size_t key_len;
        size_t value_len;
    } kv_pair_t;
    
    kv_arena_t *arena = kv_arena_create(16384);  // 16KB块
    
    // 分配键值对数组
    const int num_pairs = 100;
    kv_pair_t *pairs = (kv_pair_t*)kv_arena_alloc(arena, num_pairs * sizeof(kv_pair_t));
    
    printf("分配 %d 个键值对结构\n", num_pairs);
    
    // 填充数据
    for (int i = 0; i < num_pairs; i++) {
        // 分配键
        char key_buf[32];
        snprintf(key_buf, sizeof(key_buf), "key_%03d", i);
        pairs[i].key_len = strlen(key_buf) + 1;
        pairs[i].key = (char*)kv_arena_alloc(arena, pairs[i].key_len);
        strcpy(pairs[i].key, key_buf);
        
        // 分配值
        char value_buf[64];
        snprintf(value_buf, sizeof(value_buf), "value_for_key_%03d_with_data", i);
        pairs[i].value_len = strlen(value_buf) + 1;
        pairs[i].value = (char*)kv_arena_alloc(arena, pairs[i].value_len);
        strcpy(pairs[i].value, value_buf);
    }
    
    printf("填充数据完成\n");
    
    // 验证前几个和后几个
    printf("\n前5个键值对:\n");
    for (int i = 0; i < 5; i++) {
        printf("  %s -> %s\n", pairs[i].key, pairs[i].value);
    }
    
    printf("\n后5个键值对:\n");
    for (int i = num_pairs - 5; i < num_pairs; i++) {
        printf("  %s -> %s\n", pairs[i].key, pairs[i].value);
    }
    
    printf("\n📊 存储统计:\n");
    printf("   键值对数量: %d\n", num_pairs);
    printf("   内存使用:   %zu bytes\n", kv_arena_memory_usage(arena));
    printf("   总分配:     %zu bytes\n", kv_arena_total_allocated(arena));
    printf("   平均每对:   %.2f bytes\n", 
           (double)kv_arena_total_allocated(arena) / num_pairs);
    
    kv_arena_destroy(arena);
    printf("✅ 实际场景演示完成\n\n");
}

int main(void) {
    printf("🚀 ConcordKV Arena内存池演示程序\n");
    printf("==========================================\n\n");
    
    // 运行各种演示
    demo_basic_usage();
    demo_aligned_allocation();
    demo_multiple_blocks();
    demo_performance_comparison();
    demo_real_world_usage();
    
    printf("🎉 所有演示完成！\n");
    printf("Arena内存池提供了高性能、内存高效的分配策略，\n");
    printf("特别适合批量分配和生命周期相同的内存使用场景。\n");
    
    return 0;
} 