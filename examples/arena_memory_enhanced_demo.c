/*
 * @Author: Lzww0608  
 * @Date: 2025-6-13 16:26:07
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-13 16:26:10
 * @Description: ConcordKV 增强版Arena内存池使用演示
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

// 包含Arena内存池头文件
#include "../kvserver/kv_memory.h"

// 演示配置
#define DEMO_THREADS 4
#define DEMO_ALLOCATIONS 1000
#define DEMO_DATA_SIZE 256

// 线程演示数据
typedef struct {
    kv_arena_t *arena;
    int thread_id;
    int allocations;
    int success_count;
} demo_thread_data_t;

// 辅助函数：获取当前时间戳（微秒）
static uint64_t get_timestamp_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

// 辅助函数：格式化字节数
static void format_bytes(size_t bytes, char *buffer, size_t buffer_size) {
    const char *units[] = {"B", "KB", "MB", "GB"};
    int unit_index = 0;
    double size = (double)bytes;
    
    while (size >= 1024.0 && unit_index < 3) {
        size /= 1024.0;
        unit_index++;
    }
    
    snprintf(buffer, buffer_size, "%.2f %s", size, units[unit_index]);
}

// 演示1：基础功能展示
void demo_basic_features(void) {
    printf("\n🎯 演示1: 基础功能展示\n");
    printf("=====================================\n");
    
    // 创建默认配置的arena
    kv_arena_t *arena = kv_arena_create_ex(NULL);
    if (!arena) {
        printf("❌ 创建arena失败\n");
        return;
    }
    
    printf("✅ 创建arena成功\n");
    
    // 进行各种大小的分配
    printf("📝 进行不同大小的内存分配...\n");
    
    void *small_ptr = kv_arena_alloc(arena, 64);
    void *medium_ptr = kv_arena_alloc(arena, 1024);
    void *large_ptr = kv_arena_alloc(arena, 8192);
    
    if (small_ptr && medium_ptr && large_ptr) {
        printf("   ✅ 小块分配 (64B): %p\n", small_ptr);
        printf("   ✅ 中块分配 (1KB): %p\n", medium_ptr);
        printf("   ✅ 大块分配 (8KB): %p\n", large_ptr);
        
        // 写入测试数据
        strcpy((char*)small_ptr, "Hello Arena!");
        memset(medium_ptr, 0xAA, 1024);
        memset(large_ptr, 0xBB, 8192);
        
        printf("   ✅ 数据写入成功\n");
    }
    
    // 显示内存使用情况
    size_t memory_usage = kv_arena_memory_usage(arena);
    size_t total_allocated = kv_arena_total_allocated(arena);
    
    char usage_str[64], allocated_str[64];
    format_bytes(memory_usage, usage_str, sizeof(usage_str));
    format_bytes(total_allocated, allocated_str, sizeof(allocated_str));
    
    printf("📊 内存使用情况:\n");
    printf("   总内存使用: %s\n", usage_str);
    printf("   总分配字节: %s\n", allocated_str);
    printf("   内存效率: %.2f%%\n", 
           memory_usage > 0 ? (double)total_allocated / memory_usage * 100 : 0);
    
    kv_arena_destroy(arena);
    printf("✅ arena销毁完成\n");
}

// 演示2：线程安全功能
void *thread_worker(void *arg) {
    demo_thread_data_t *data = (demo_thread_data_t*)arg;
    
    printf("🧵 线程 %d 开始工作...\n", data->thread_id);
    
    for (int i = 0; i < data->allocations; i++) {
        size_t size = (rand() % DEMO_DATA_SIZE) + 1;
        void *ptr = kv_arena_alloc(data->arena, size);
        
        if (ptr) {
            data->success_count++;
            
            // 写入线程ID作为标识
            memset(ptr, data->thread_id, size);
            
            // 模拟一些工作
            if (i % 100 == 0) {
                usleep(10); // 10微秒延迟
            }
        }
    }
    
    printf("🧵 线程 %d 完成，成功分配 %d 次\n", 
           data->thread_id, data->success_count);
    
    return NULL;
}

void demo_thread_safety(void) {
    printf("\n🎯 演示2: 线程安全功能\n");
    printf("=====================================\n");
    
    // 创建线程安全的arena
    kv_arena_config_t config = KV_ARENA_DEFAULT_CONFIG;
    config.thread_mode = KV_ARENA_THREAD_SAFE;
    config.block_size = 8192;
    config.enable_detailed_stats = true;
    
    kv_arena_t *arena = kv_arena_create_ex(&config);
    if (!arena) {
        printf("❌ 创建线程安全arena失败\n");
        return;
    }
    
    printf("✅ 创建线程安全arena成功\n");
    
    // 准备线程数据
    pthread_t threads[DEMO_THREADS];
    demo_thread_data_t thread_data[DEMO_THREADS];
    
    for (int i = 0; i < DEMO_THREADS; i++) {
        thread_data[i].arena = arena;
        thread_data[i].thread_id = i + 1;
        thread_data[i].allocations = DEMO_ALLOCATIONS;
        thread_data[i].success_count = 0;
    }
    
    uint64_t start_time = get_timestamp_us();
    
    // 创建并启动线程
    printf("🚀 启动 %d 个工作线程...\n", DEMO_THREADS);
    for (int i = 0; i < DEMO_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, thread_worker, &thread_data[i]) != 0) {
            printf("❌ 创建线程 %d 失败\n", i);
            return;
        }
    }
    
    // 等待所有线程完成
    for (int i = 0; i < DEMO_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    uint64_t end_time = get_timestamp_us();
    
    // 统计结果
    int total_success = 0;
    for (int i = 0; i < DEMO_THREADS; i++) {
        total_success += thread_data[i].success_count;
    }
    
    printf("📊 并发测试结果:\n");
    printf("   总分配次数: %d\n", total_success);
    printf("   预期分配次数: %d\n", DEMO_THREADS * DEMO_ALLOCATIONS);
    printf("   成功率: %.2f%%\n", 
           (double)total_success / (DEMO_THREADS * DEMO_ALLOCATIONS) * 100);
    printf("   总耗时: %lu μs\n", end_time - start_time);
    printf("   平均每次分配: %.2f μs\n", 
           (double)(end_time - start_time) / total_success);
    
    kv_arena_destroy(arena);
    printf("✅ 线程安全演示完成\n");
}

// 演示3：块缓存和重用功能
void demo_block_cache(void) {
    printf("\n🎯 演示3: 块缓存和重用功能\n");
    printf("=====================================\n");
    
    // 创建支持块缓存的arena
    kv_arena_config_t config = KV_ARENA_DEFAULT_CONFIG;
    config.enable_block_cache = true;
    config.max_cached_blocks = 32;
    config.block_size = 4096;
    config.enable_detailed_stats = true;
    
    kv_arena_t *arena = kv_arena_create_ex(&config);
    if (!arena) {
        printf("❌ 创建支持块缓存的arena失败\n");
        return;
    }
    
    printf("✅ 创建支持块缓存的arena成功\n");
    printf("📝 配置: 块大小=%zu, 最大缓存块=%zu\n", 
           config.block_size, config.max_cached_blocks);
    
    // 第一轮分配：触发块分配
    printf("🔄 第一轮分配 (触发块分配)...\n");
    void *ptrs1[50];
    for (int i = 0; i < 50; i++) {
        ptrs1[i] = kv_arena_alloc(arena, 1000);
    }
    
    kv_arena_stats_t stats1;
    kv_arena_get_stats(arena, &stats1);
    printf("   块分配次数: %lu\n", stats1.block_allocations);
    printf("   块重用次数: %lu\n", stats1.block_reuses);
    
    // 压缩arena以触发块缓存
    printf("🗜️  压缩arena...\n");
    size_t reclaimed = kv_arena_compact(arena);
    char reclaimed_str[64];
    format_bytes(reclaimed, reclaimed_str, sizeof(reclaimed_str));
    printf("   回收内存: %s\n", reclaimed_str);
    
    // 第二轮分配：应该重用缓存的块
    printf("🔄 第二轮分配 (应该重用缓存块)...\n");
    void *ptrs2[50];
    for (int i = 0; i < 50; i++) {
        ptrs2[i] = kv_arena_alloc(arena, 800);
    }
    
    kv_arena_stats_t stats2;
    kv_arena_get_stats(arena, &stats2);
    printf("   块分配次数: %lu (+%lu)\n", 
           stats2.block_allocations, stats2.block_allocations - stats1.block_allocations);
    printf("   块重用次数: %lu (+%lu)\n", 
           stats2.block_reuses, stats2.block_reuses - stats1.block_reuses);
    
    // 显示缓存效果
    if (stats2.block_reuses > stats1.block_reuses) {
        printf("✅ 块缓存功能正常工作！\n");
        printf("📈 缓存命中率: %.2f%%\n", 
               (double)(stats2.block_reuses - stats1.block_reuses) / 
               (stats2.block_allocations + stats2.block_reuses - stats1.block_allocations - stats1.block_reuses) * 100);
    } else {
        printf("⚠️  未检测到块重用\n");
    }
    
    kv_arena_destroy(arena);
    printf("✅ 块缓存演示完成\n");
}

// 演示4：统计增强功能
void demo_enhanced_statistics(void) {
    printf("\n🎯 演示4: 统计增强功能\n");
    printf("=====================================\n");
    
    // 创建支持详细统计的arena
    kv_arena_config_t config = KV_ARENA_DEFAULT_CONFIG;
    config.enable_detailed_stats = true;
    config.block_size = 4096;
    
    kv_arena_t *arena = kv_arena_create_ex(&config);
    if (!arena) {
        printf("❌ 创建支持详细统计的arena失败\n");
        return;
    }
    
    printf("✅ 创建支持详细统计的arena成功\n");
    
    // 进行各种类型的分配
    printf("📝 进行多样化内存分配...\n");
    
    // 小块分配
    for (int i = 0; i < 20; i++) {
        kv_arena_alloc(arena, 16 + i);
    }
    
    // 中等块分配
    for (int i = 0; i < 15; i++) {
        kv_arena_alloc(arena, 256 + i * 10);
    }
    
    // 大块分配
    for (int i = 0; i < 10; i++) {
        kv_arena_alloc(arena, 4096 + i * 100);
    }
    
    // 对齐分配
    kv_arena_alloc_aligned(arena, 100, 16);
    kv_arena_alloc_aligned(arena, 200, 32);
    kv_arena_alloc_aligned(arena, 300, 64);
    
    printf("📊 分配完成，显示详细统计信息:\n");
    
    // 获取并显示统计信息
    kv_arena_stats_t stats;
    kv_arena_get_stats(arena, &stats);
    
    char allocated_str[64], wasted_str[64], peak_str[64], current_str[64];
    format_bytes(stats.total_bytes_allocated, allocated_str, sizeof(allocated_str));
    format_bytes(stats.total_bytes_wasted, wasted_str, sizeof(wasted_str));
    format_bytes(stats.peak_memory_usage, peak_str, sizeof(peak_str));
    format_bytes(stats.current_memory_usage, current_str, sizeof(current_str));
    
    printf("   总分配次数: %lu\n", stats.total_allocations);
    printf("   总分配字节: %s\n", allocated_str);
    printf("   浪费字节数: %s (%.2f%%)\n", wasted_str,
           stats.total_bytes_allocated > 0 ? 
           (double)stats.total_bytes_wasted / stats.total_bytes_allocated * 100 : 0);
    printf("   峰值内存: %s\n", peak_str);
    printf("   当前内存: %s\n", current_str);
    printf("   内存效率: %.2f%%\n",
           stats.peak_memory_usage > 0 ? 
           (double)stats.total_bytes_allocated / stats.peak_memory_usage * 100 : 0);
    
    // 显示热点分析
    printf("\n🔥 分配热点分析:\n");
    kv_arena_print_hotspots(arena);
    
    // 测试统计重置
    printf("\n🔄 重置统计信息...\n");
    kv_arena_reset_stats(arena);
    kv_arena_get_stats(arena, &stats);
    printf("   重置后分配次数: %lu\n", stats.total_allocations);
    
    kv_arena_destroy(arena);
    printf("✅ 统计增强演示完成\n");
}

// 演示5：性能对比测试
void demo_performance_comparison(void) {
    printf("\n🎯 演示5: 性能对比测试\n");
    printf("=====================================\n");
    
    const int test_allocations = 50000;
    
    // 测试增强版arena性能
    printf("⚡ 测试增强版Arena性能...\n");
    
    kv_arena_config_t config = KV_ARENA_DEFAULT_CONFIG;
    config.enable_block_cache = true;
    config.enable_detailed_stats = false; // 关闭统计以获得最佳性能
    config.thread_mode = KV_ARENA_THREAD_UNSAFE; // 单线程测试
    
    kv_arena_t *arena = kv_arena_create_ex(&config);
    
    uint64_t start_time = get_timestamp_us();
    
    for (int i = 0; i < test_allocations; i++) {
        size_t size = (i % 1000) + 1;
        void *ptr = kv_arena_alloc(arena, size);
        if (ptr) {
            // 简单的内存访问
            *(char*)ptr = (char)(i & 0xFF);
        }
    }
    
    uint64_t end_time = get_timestamp_us();
    uint64_t arena_time = end_time - start_time;
    
    size_t arena_memory = kv_arena_memory_usage(arena);
    kv_arena_destroy(arena);
    
    // 测试标准malloc性能
    printf("⚡ 测试标准malloc性能...\n");
    
    start_time = get_timestamp_us();
    
    void **ptrs = malloc(test_allocations * sizeof(void*));
    for (int i = 0; i < test_allocations; i++) {
        size_t size = (i % 1000) + 1;
        ptrs[i] = malloc(size);
        if (ptrs[i]) {
            *(char*)ptrs[i] = (char)(i & 0xFF);
        }
    }
    
    for (int i = 0; i < test_allocations; i++) {
        free(ptrs[i]);
    }
    free(ptrs);
    
    end_time = get_timestamp_us();
    uint64_t malloc_time = end_time - start_time;
    
    // 显示对比结果
    printf("📊 性能对比结果 (%d次分配):\n", test_allocations);
    printf("   增强版Arena: %lu μs\n", arena_time);
    printf("   标准malloc:  %lu μs\n", malloc_time);
    
    if (arena_time < malloc_time) {
        printf("   🚀 Arena性能提升: %.2fx\n", (double)malloc_time / arena_time);
    } else {
        printf("   📉 Arena性能比率: %.2fx\n", (double)arena_time / malloc_time);
    }
    
    char arena_mem_str[64];
    format_bytes(arena_memory, arena_mem_str, sizeof(arena_mem_str));
    printf("   Arena内存使用: %s\n", arena_mem_str);
    
    printf("✅ 性能对比演示完成\n");
}

// 演示6：NUMA优化功能（仅Linux）
void demo_numa_optimization(void) {
    printf("\n🎯 演示6: NUMA优化功能\n");
    printf("=====================================\n");
    
    #ifdef __linux__
    // 检查NUMA支持
    int current_node = kv_arena_get_numa_node();
    if (current_node >= 0) {
        printf("📍 当前NUMA节点: %d\n", current_node);
        
        // 创建NUMA优化的arena
        kv_arena_config_t config = KV_ARENA_DEFAULT_CONFIG;
        config.enable_numa = true;
        config.preferred_numa_node = current_node;
        config.block_size = 8192;
        
        kv_arena_t *arena = kv_arena_create_ex(&config);
        if (arena) {
            printf("✅ 创建NUMA优化arena成功\n");
            
            // 进行一些分配测试
            printf("📝 进行NUMA本地内存分配...\n");
            for (int i = 0; i < 10; i++) {
                void *ptr = kv_arena_alloc(arena, 1024 * (i + 1));
                if (ptr) {
                    printf("   ✅ 分配 %dKB: %p\n", i + 1, ptr);
                    // 写入数据验证可用性
                    memset(ptr, 0xCC, 1024 * (i + 1));
                }
            }
            
            size_t memory_usage = kv_arena_memory_usage(arena);
            char usage_str[64];
            format_bytes(memory_usage, usage_str, sizeof(usage_str));
            printf("📊 NUMA本地内存使用: %s\n", usage_str);
            
            kv_arena_destroy(arena);
            printf("✅ NUMA优化演示完成\n");
        } else {
            printf("❌ 创建NUMA优化arena失败\n");
        }
    } else {
        printf("⚠️  NUMA不可用或未检测到多节点系统\n");
        printf("💡 在多NUMA节点系统上运行可看到更明显效果\n");
    }
    #else
    printf("⚠️  非Linux系统，NUMA优化功能不可用\n");
    printf("💡 在Linux系统上运行可体验NUMA优化功能\n");
    #endif
}

// 主函数
int main(void) {
    printf("🚀 ConcordKV 增强版Arena内存池功能演示\n");
    printf("==========================================\n");
    printf("本演示将展示增强版Arena内存池的各项新功能:\n");
    printf("• 线程安全支持\n");
    printf("• 块缓存和重用机制\n");
    printf("• 统计增强功能\n");
    printf("• 性能优化\n");
    printf("• NUMA优化支持\n");
    printf("==========================================\n");
    
    // 初始化随机数种子
    srand(time(NULL));
    
    // 运行各项演示
    demo_basic_features();
    demo_thread_safety();
    demo_block_cache();
    demo_enhanced_statistics();
    demo_performance_comparison();
    demo_numa_optimization();
    
    printf("\n🎉 所有演示完成！\n");
    printf("==========================================\n");
    printf("💡 增强版Arena内存池主要优势:\n");
    printf("   • 线程安全: 支持多线程并发访问\n");
    printf("   • 高性能: 块缓存减少系统调用开销\n");
    printf("   • 智能统计: 详细的分配热点分析\n");
    printf("   • NUMA优化: 本地节点内存分配\n");
    printf("   • 向后兼容: 完全兼容原有API\n");
    printf("==========================================\n");
    
    return 0;
} 