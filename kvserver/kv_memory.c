/*
 * @Author: Lzww0608  
 * @Date: 2025-5-30 22:42:07
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-13 17:33:09
 * @Description: ConcordKV 内存管理模块 - 增强版Arena内存池实现
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "kv_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>

#ifdef __linux__
#include <unistd.h>
#include <sys/syscall.h>
#ifdef HAVE_NUMA
#include <numa.h>
#include <numaif.h>
#endif
#endif

// 全局arena实例（可选）
static kv_arena_t *g_arena = NULL;

// 辅助函数：计算对齐大小
static size_t align_size(size_t size, size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

// 辅助函数：获取当前时间戳（微秒）
static uint64_t get_timestamp_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

// 辅助函数：计算大小分布索引
static int get_size_distribution_index(size_t size) {
    if (size == 0) return 0;
    int index = 0;
    size_t temp = size - 1;
    while (temp > 0 && index < 15) {
        temp >>= 1;
        index++;
    }
    return index;
}

// 辅助函数：计算对齐请求索引
static int get_alignment_index(size_t alignment) {
    switch (alignment) {
        case 1: return 0;
        case 2: return 1;
        case 4: return 2;
        case 8: return 3;
        case 16: return 4;
        case 32: return 5;
        case 64: return 6;
        default: return 7;
    }
}

// 块缓存池管理
static arena_block_cache_t *create_block_cache(size_t capacity) {
    arena_block_cache_t *cache = malloc(sizeof(arena_block_cache_t));
    if (!cache) return NULL;
    
    cache->blocks = calloc(capacity, sizeof(struct arena_block*));
    cache->block_sizes = calloc(capacity, sizeof(size_t));
    if (!cache->blocks || !cache->block_sizes) {
        free(cache->blocks);
        free(cache->block_sizes);
        free(cache);
        return NULL;
    }
    
    cache->count = 0;
    cache->capacity = capacity;
    
    if (pthread_mutex_init(&cache->mutex, NULL) != 0) {
        free(cache->blocks);
        free(cache->block_sizes);
        free(cache);
        return NULL;
    }
    
    return cache;
}

static void destroy_block_cache(arena_block_cache_t *cache) {
    if (!cache) return;
    
    pthread_mutex_lock(&cache->mutex);
    
    // 释放所有缓存的块
    for (size_t i = 0; i < cache->count; i++) {
        if (cache->blocks[i]) {
            free(cache->blocks[i]->data);
            free(cache->blocks[i]);
        }
    }
    
    pthread_mutex_unlock(&cache->mutex);
    pthread_mutex_destroy(&cache->mutex);
    
    free(cache->blocks);
    free(cache->block_sizes);
    free(cache);
}

// 从缓存获取块
static struct arena_block *get_cached_block(arena_block_cache_t *cache, size_t size) {
    if (!cache) return NULL;
    
    pthread_mutex_lock(&cache->mutex);
    
    // 查找合适大小的块
    for (size_t i = 0; i < cache->count; i++) {
        if (cache->block_sizes[i] >= size) {
            struct arena_block *block = cache->blocks[i];
            
            // 移除该块（移动后续元素）
            for (size_t j = i; j < cache->count - 1; j++) {
                cache->blocks[j] = cache->blocks[j + 1];
                cache->block_sizes[j] = cache->block_sizes[j + 1];
            }
            cache->count--;
            
            pthread_mutex_unlock(&cache->mutex);
            return block;
        }
    }
    
    pthread_mutex_unlock(&cache->mutex);
    return NULL;
}

// 将块放入缓存
static bool put_cached_block(arena_block_cache_t *cache, struct arena_block *block) {
    if (!cache || !block || cache->count >= cache->capacity) {
        return false;
    }
    
    pthread_mutex_lock(&cache->mutex);
    
    if (cache->count < cache->capacity) {
        cache->blocks[cache->count] = block;
        cache->block_sizes[cache->count] = block->size;
        cache->count++;
        pthread_mutex_unlock(&cache->mutex);
        return true;
    }
    
    pthread_mutex_unlock(&cache->mutex);
    return false;
}

// 辅助函数：分配新的内存块（增强版）
static struct arena_block *alloc_new_block_ex(kv_arena_t *arena, size_t size) {
    // 简化版：直接分配，移除缓存和NUMA检查以提高性能
    struct arena_block *block = malloc(sizeof(struct arena_block));
    if (!block) return NULL;
    
    block->data = malloc(size);
    if (!block->data) {
        free(block);
        return NULL;
    }
    
    block->size = size;
    block->next = NULL;
    block->alloc_count = 0;
    block->timestamp = 0; // 简化：不记录时间戳以提高性能
    
    return block;
}

// 辅助函数：分配新的内存块（兼容版）
static struct arena_block *alloc_new_block(size_t size) {
    struct arena_block *block = malloc(sizeof(struct arena_block));
    if (!block) return NULL;
    
    block->data = malloc(size);
    if (!block->data) {
        free(block);
        return NULL;
    }
    
    block->size = size;
    block->next = NULL;
    block->alloc_count = 0;
    block->timestamp = 0; // 简化：不记录时间戳以提高性能
    return block;
}

// 线程安全包装宏
#define ARENA_READ_LOCK(arena) \
    do { \
        if ((arena)->thread_mode == KV_ARENA_THREAD_SAFE) { \
            pthread_rwlock_rdlock(&(arena)->rwlock); \
        } \
    } while(0)

#define ARENA_WRITE_LOCK(arena) \
    do { \
        if ((arena)->thread_mode == KV_ARENA_THREAD_SAFE) { \
            pthread_rwlock_wrlock(&(arena)->rwlock); \
        } \
    } while(0)

#define ARENA_UNLOCK(arena) \
    do { \
        if ((arena)->thread_mode == KV_ARENA_THREAD_SAFE) { \
            pthread_rwlock_unlock(&(arena)->rwlock); \
        } \
    } while(0)

// 创建arena内存池（增强版）
kv_arena_t *kv_arena_create_ex(const kv_arena_config_t *config) {
    kv_arena_t *arena = malloc(sizeof(kv_arena_t));
    if (!arena) return NULL;
    
    // 初始化arena结构
    memset(arena, 0, sizeof(kv_arena_t));
    
    // 应用配置
    if (config) {
        arena->block_size = config->block_size > 0 ? config->block_size : KV_ARENA_DEFAULT_BLOCK_SIZE;
        arena->thread_mode = config->thread_mode;
        arena->numa_node = config->enable_numa ? config->preferred_numa_node : -1;
        
        // 创建块缓存
        if (config->enable_block_cache) {
            arena->block_cache = create_block_cache(config->max_cached_blocks);
        }
    } else {
        // 使用默认配置
        arena->block_size = KV_ARENA_DEFAULT_BLOCK_SIZE;
        arena->thread_mode = KV_ARENA_THREAD_UNSAFE;
        arena->numa_node = -1;
        arena->block_cache = create_block_cache(KV_ARENA_MAX_CACHED_BLOCKS);
    }
    
    // 确保最小块大小
    if (arena->block_size < KV_ARENA_MIN_BLOCK_SIZE) {
        arena->block_size = KV_ARENA_MIN_BLOCK_SIZE;
    }
    
    // 初始化线程安全支持
    if (arena->thread_mode == KV_ARENA_THREAD_SAFE) {
        if (pthread_rwlock_init(&arena->rwlock, NULL) != 0) {
            destroy_block_cache(arena->block_cache);
            free(arena);
            return NULL;
        }
    }
    
    // 初始化其他字段
    arena->alloc_ptr = NULL;
    arena->alloc_bytes_remaining = 0;
    arena->blocks = NULL;
    arena->total_allocated = 0;
    arena->total_blocks = 0;
    arena->memory_usage = 0;
    
    // 初始化统计信息
    memset(&arena->stats, 0, sizeof(kv_arena_stats_t));
    
    return arena;
}

// 创建arena内存池（兼容版本）
kv_arena_t *kv_arena_create(size_t block_size) {
    kv_arena_config_t config = KV_ARENA_DEFAULT_CONFIG;
    config.block_size = block_size;
    return kv_arena_create_ex(&config);
}

// 智能块大小选择（根据使用场景优化）
size_t kv_arena_suggest_block_size(const char *usage_hint) {
    if (!usage_hint) {
        return KV_ARENA_DEFAULT_BLOCK_SIZE;
    }
    
    // 小内存场景：内存敏感，优先节省内存
    if (strcmp(usage_hint, "small") == 0 || strcmp(usage_hint, "memory") == 0) {
        return KV_ARENA_SMALL_BLOCK_SIZE;  // 4KB
    }
    
    // 大内存场景：高吞吐量，优先性能
    if (strcmp(usage_hint, "large") == 0 || strcmp(usage_hint, "performance") == 0) {
        return KV_ARENA_LARGE_BLOCK_SIZE;  // 1MB
    }
    
    // 高性能场景：极致性能优化
    if (strcmp(usage_hint, "ultra") == 0 || strcmp(usage_hint, "extreme") == 0) {
        return KV_ARENA_LARGE_BLOCK_SIZE * 4;  // 4MB
    }
    
    // 默认场景：平衡性能和内存
    return KV_ARENA_DEFAULT_BLOCK_SIZE;  // 64KB
}

// 从arena分配内存（增强版）
void *kv_arena_alloc(kv_arena_t *arena, size_t size) {
    if (!arena || size == 0) {
        return NULL;
    }
    
    // 对齐到8字节边界（内联计算以提高性能）
    size_t aligned_size = (size + 7) & ~7;
    
    // 超快速路径：非线程安全模式的优化
    if (arena->thread_mode == KV_ARENA_THREAD_UNSAFE) {
        // 检查当前块是否有足够空间
        if (aligned_size <= arena->alloc_bytes_remaining) {
            char *result = arena->alloc_ptr;
            arena->alloc_ptr += aligned_size;
            arena->alloc_bytes_remaining -= aligned_size;
            
            // 更新统计信息
            arena->total_allocated += aligned_size;
            arena->stats.total_allocations++;
            arena->stats.total_bytes_allocated += aligned_size;
            
            // 更新当前块的分配计数
            if (arena->blocks) {
                arena->blocks->alloc_count++;
            }
            
            return result;
        }
        
        // 需要分配新块
        size_t new_block_size = arena->block_size;
        if (aligned_size > new_block_size) {
            new_block_size = aligned_size;
        }
        
        struct arena_block *new_block = alloc_new_block_ex(arena, new_block_size);
        if (!new_block) {
            return NULL;
        }
        
        // 将新块添加到链表头部
        new_block->next = arena->blocks;
        arena->blocks = new_block;
        arena->total_blocks++;
        arena->memory_usage += new_block_size + sizeof(struct arena_block);
        
        // 更新统计信息
        arena->total_allocated += aligned_size;
        arena->stats.total_allocations++;
        arena->stats.total_bytes_allocated += aligned_size;
        arena->stats.block_allocations++;
        
        // 更新峰值内存使用
        if (arena->memory_usage > arena->stats.peak_memory_usage) {
            arena->stats.peak_memory_usage = arena->memory_usage;
        }
        arena->stats.current_memory_usage = arena->memory_usage;
        
        // 更新分配指针
        arena->alloc_ptr = new_block->data + aligned_size;
        arena->alloc_bytes_remaining = new_block_size - aligned_size;
        new_block->alloc_count++;
        
        return new_block->data;
    }
    
    // 线程安全模式的路径
    // 快速路径：检查当前块是否有足够空间（无锁检查）
    if (aligned_size <= arena->alloc_bytes_remaining) {
        ARENA_WRITE_LOCK(arena);
        
        // 双重检查（防止竞态条件）
        if (aligned_size <= arena->alloc_bytes_remaining) {
            char *result = arena->alloc_ptr;
            arena->alloc_ptr += aligned_size;
            arena->alloc_bytes_remaining -= aligned_size;
            arena->total_allocated += aligned_size;
            
            // 简化统计更新（只更新关键指标）
            arena->stats.total_allocations++;
            arena->stats.total_bytes_allocated += aligned_size;
            
            // 更新当前块的分配计数
            if (arena->blocks) {
                arena->blocks->alloc_count++;
            }
            
            ARENA_UNLOCK(arena);
            return result;
        }
        ARENA_UNLOCK(arena);
    }
    
    // 慢速路径：需要分配新块
    ARENA_WRITE_LOCK(arena);
    
    // 再次检查（可能其他线程已经分配了新块）
    if (aligned_size <= arena->alloc_bytes_remaining) {
        char *result = arena->alloc_ptr;
        arena->alloc_ptr += aligned_size;
        arena->alloc_bytes_remaining -= aligned_size;
        arena->total_allocated += aligned_size;
        
        arena->stats.total_allocations++;
        arena->stats.total_bytes_allocated += aligned_size;
        
        if (arena->blocks) {
            arena->blocks->alloc_count++;
        }
        
        ARENA_UNLOCK(arena);
        return result;
    }
    
    // 当前块空间不足，分配新块
    size_t new_block_size = arena->block_size;
    
    // 如果请求的大小超过默认块大小，使用更大的块
    if (aligned_size > new_block_size) {
        new_block_size = aligned_size;
    }
    
    struct arena_block *new_block = alloc_new_block_ex(arena, new_block_size);
    if (!new_block) {
        ARENA_UNLOCK(arena);
        return NULL;
    }
    
    // 将新块添加到链表头部
    new_block->next = arena->blocks;
    arena->blocks = new_block;
    arena->total_blocks++;
    arena->memory_usage += new_block_size + sizeof(struct arena_block);
    
    // 更新统计信息（简化版）
    arena->stats.total_allocations++;
    arena->stats.total_bytes_allocated += aligned_size;
    arena->stats.block_allocations++;
    
    // 更新峰值内存使用
    if (arena->memory_usage > arena->stats.peak_memory_usage) {
        arena->stats.peak_memory_usage = arena->memory_usage;
    }
    arena->stats.current_memory_usage = arena->memory_usage;
    
    // 更新分配指针
    arena->alloc_ptr = new_block->data + aligned_size;
    arena->alloc_bytes_remaining = new_block_size - aligned_size;
    arena->total_allocated += aligned_size;
    new_block->alloc_count++;
    
    ARENA_UNLOCK(arena);
    return new_block->data;
}

// 从arena分配对齐内存（增强版）
void *kv_arena_alloc_aligned(kv_arena_t *arena, size_t size, size_t alignment) {
    if (!arena || size == 0 || alignment == 0) {
        return NULL;
    }
    
    // 确保alignment是2的幂
    if ((alignment & (alignment - 1)) != 0) {
        return NULL;
    }
    
    ARENA_WRITE_LOCK(arena);
    
    size_t aligned_size = align_size(size, alignment);
    
    // 更新统计信息
    arena->stats.total_allocations++;
    arena->stats.total_bytes_allocated += aligned_size;
    arena->stats.alignment_requests[get_alignment_index(alignment)]++;
    
    // 检查当前块是否有足够空间
    if (arena->alloc_ptr != NULL) {
        // 计算对齐后的指针
        uintptr_t current_ptr = (uintptr_t)arena->alloc_ptr;
        uintptr_t aligned_ptr = align_size(current_ptr, alignment);
        size_t padding = aligned_ptr - current_ptr;
        
        if (padding + aligned_size <= arena->alloc_bytes_remaining) {
            arena->alloc_ptr = (char*)aligned_ptr + aligned_size;
            arena->alloc_bytes_remaining -= (padding + aligned_size);
            arena->total_allocated += (padding + aligned_size);
            arena->stats.total_bytes_wasted += padding;
            
            if (arena->blocks) {
                arena->blocks->alloc_count++;
            }
            
            ARENA_UNLOCK(arena);
            return (void*)aligned_ptr;
        }
    }
    
    // 当前块空间不足，分配新块
    size_t new_block_size = arena->block_size;
    
    // 如果请求的大小超过默认块大小，使用更大的块
    if (aligned_size > new_block_size) {
        new_block_size = aligned_size;
    }
    
    struct arena_block *new_block = alloc_new_block_ex(arena, new_block_size);
    if (!new_block) {
        ARENA_UNLOCK(arena);
        return NULL;
    }
    
    // 将新块添加到链表头部
    new_block->next = arena->blocks;
    arena->blocks = new_block;
    arena->total_blocks++;
    arena->memory_usage += new_block_size + sizeof(struct arena_block);
    
    // 更新峰值内存使用
    if (arena->memory_usage > arena->stats.peak_memory_usage) {
        arena->stats.peak_memory_usage = arena->memory_usage;
    }
    arena->stats.current_memory_usage = arena->memory_usage;
    
    // 计算对齐后的指针
    uintptr_t block_ptr = (uintptr_t)new_block->data;
    uintptr_t aligned_ptr = align_size(block_ptr, alignment);
    size_t padding = aligned_ptr - block_ptr;
    
    // 更新分配指针
    arena->alloc_ptr = (char*)aligned_ptr + aligned_size;
    arena->alloc_bytes_remaining = new_block_size - padding - aligned_size;
    arena->total_allocated += (padding + aligned_size);
    arena->stats.total_bytes_wasted += padding;
    new_block->alloc_count++;
    
    ARENA_UNLOCK(arena);
    return (void*)aligned_ptr;
}

// 获取arena内存使用统计
size_t kv_arena_memory_usage(const kv_arena_t *arena) {
    if (!arena) return 0;
    
    ARENA_READ_LOCK((kv_arena_t*)arena);
    size_t usage = arena->memory_usage;
    ARENA_UNLOCK((kv_arena_t*)arena);
    
    return usage;
}

// 获取arena分配统计
size_t kv_arena_total_allocated(const kv_arena_t *arena) {
    if (!arena) return 0;
    
    ARENA_READ_LOCK((kv_arena_t*)arena);
    size_t allocated = arena->total_allocated;
    ARENA_UNLOCK((kv_arena_t*)arena);
    
    return allocated;
}

// 获取详细统计信息
int kv_arena_get_stats(const kv_arena_t *arena, kv_arena_stats_t *stats) {
    if (!arena || !stats) return -1;
    
    ARENA_READ_LOCK((kv_arena_t*)arena);
    memcpy(stats, &arena->stats, sizeof(kv_arena_stats_t));
    ARENA_UNLOCK((kv_arena_t*)arena);
    
    return 0;
}

// 重置统计信息
void kv_arena_reset_stats(kv_arena_t *arena) {
    if (!arena) return;
    
    ARENA_WRITE_LOCK(arena);
    memset(&arena->stats, 0, sizeof(kv_arena_stats_t));
    ARENA_UNLOCK(arena);
}

// 打印分配热点分析
void kv_arena_print_hotspots(const kv_arena_t *arena) {
    if (!arena) return;
    
    ARENA_READ_LOCK((kv_arena_t*)arena);
    
    printf("\n📊 Arena分配热点分析\n");
    printf("==========================================\n");
    printf("总分配次数: %lu\n", arena->stats.total_allocations);
    printf("总分配字节: %lu\n", arena->stats.total_bytes_allocated);
    printf("浪费字节数: %lu (%.2f%%)\n", 
           arena->stats.total_bytes_wasted,
           arena->stats.total_bytes_allocated > 0 ? 
           (double)arena->stats.total_bytes_wasted / arena->stats.total_bytes_allocated * 100 : 0);
    printf("峰值内存: %lu bytes\n", arena->stats.peak_memory_usage);
    printf("当前内存: %lu bytes\n", arena->stats.current_memory_usage);
    printf("块分配次数: %lu\n", arena->stats.block_allocations);
    printf("块重用次数: %lu\n", arena->stats.block_reuses);
    
    printf("\n分配大小分布:\n");
    for (int i = 0; i < 16; i++) {
        if (arena->stats.size_distribution[i] > 0) {
            printf("  %d-%d bytes: %lu 次\n", 
                   1 << i, (1 << (i + 1)) - 1, 
                   arena->stats.size_distribution[i]);
        }
    }
    
    printf("\n对齐请求分布:\n");
    const char* align_names[] = {"1B", "2B", "4B", "8B", "16B", "32B", "64B", "其他"};
    for (int i = 0; i < 8; i++) {
        if (arena->stats.alignment_requests[i] > 0) {
            printf("  %s对齐: %lu 次\n", align_names[i], arena->stats.alignment_requests[i]);
        }
    }
    
    ARENA_UNLOCK((kv_arena_t*)arena);
}

// 压缩arena（回收未使用的块）
size_t kv_arena_compact(kv_arena_t *arena) {
    if (!arena) return 0;
    
    ARENA_WRITE_LOCK(arena);
    
    size_t reclaimed = 0;
    struct arena_block *current = arena->blocks;
    struct arena_block *prev = NULL;
    
    // 遍历块链表，回收使用率低的块
    while (current) {
        struct arena_block *next = current->next;
        
        // 如果块的分配次数很少且不是当前活跃块，考虑回收
        if (current != arena->blocks && current->alloc_count < 10) {
            // 尝试放入缓存或直接释放
            if (!put_cached_block(arena->block_cache, current)) {
                #if defined(__linux__) && defined(HAVE_NUMA)
                if (arena->numa_node >= 0) {
                    numa_free(current->data, current->size);
                } else {
                    free(current->data);
                }
                #else
                free(current->data);
                #endif
                free(current);
            }
            
            reclaimed += current->size + sizeof(struct arena_block);
            arena->memory_usage -= current->size + sizeof(struct arena_block);
            arena->total_blocks--;
            
            // 更新链表
            if (prev) {
                prev->next = next;
            } else {
                arena->blocks = next;
            }
        } else {
            prev = current;
        }
        
        current = next;
    }
    
    // 更新当前内存使用统计
    arena->stats.current_memory_usage = arena->memory_usage;
    
    ARENA_UNLOCK(arena);
    return reclaimed;
}

// 销毁arena内存池
void kv_arena_destroy(kv_arena_t *arena) {
    if (!arena) return;
    
    // 释放所有内存块
    struct arena_block *current = arena->blocks;
    while (current) {
        struct arena_block *next = current->next;
        
        #if defined(__linux__) && defined(HAVE_NUMA)
        if (arena->numa_node >= 0) {
            numa_free(current->data, current->size);
        } else {
            free(current->data);
        }
        #else
        free(current->data);
        #endif
        
        free(current);
        current = next;
    }
    
    // 销毁块缓存
    destroy_block_cache(arena->block_cache);
    
    // 销毁线程同步对象
    if (arena->thread_mode == KV_ARENA_THREAD_SAFE) {
        pthread_rwlock_destroy(&arena->rwlock);
    }
    
    // 释放arena结构本身
    free(arena);
}

// NUMA支持函数
#if defined(__linux__) && defined(HAVE_NUMA)
int kv_arena_get_numa_node(void) {
    if (numa_available() < 0) {
        return -1;
    }
    return numa_node_of_cpu(sched_getcpu());
}

int kv_arena_set_numa_policy(int node) {
    if (numa_available() < 0 || node < 0) {
        return -1;
    }
    
    struct bitmask *mask = numa_allocate_nodemask();
    if (!mask) return -1;
    
    numa_bitmask_setbit(mask, node);
    numa_set_membind(mask);
    int result = 0;
    numa_free_nodemask(mask);
    
    return result;
}
#else
// NUMA功能不可用时的存根实现
int kv_arena_get_numa_node(void) {
    return -1; // 表示NUMA不可用
}

int kv_arena_set_numa_policy(int node) {
    (void)node; // 避免未使用参数警告
    return -1; // 表示NUMA不可用
}
#endif

// 初始化全局arena
static void init_global_arena(void) {
    if (!g_arena) {
        g_arena = kv_arena_create(KV_ARENA_DEFAULT_BLOCK_SIZE);
    }
}

// 内存分配函数
void *kv_store_malloc(size_t size) {
#ifdef ENABLE_MEM_POOL
    init_global_arena();
    if (g_arena) {
        return kv_arena_alloc(g_arena, size);
    }
    // 如果arena创建失败，回退到标准malloc
    return malloc(size);
#else
    return malloc(size);
#endif
}

// 内存释放函数
void kv_store_free(void *ptr) {
#ifdef ENABLE_MEM_POOL
    // Arena内存池不需要单独释放，在destroy时统一释放
    // 这里保持接口兼容性，但实际不做任何操作
    (void)ptr;
#else
    free(ptr);
#endif
}

// 内存重新分配函数
void *kv_store_realloc(void *ptr, size_t size) {
#ifdef ENABLE_MEM_POOL
    // Arena内存池不支持realloc，分配新内存并复制数据
    if (!ptr) {
        return kv_store_malloc(size);
    }
    
    if (size == 0) {
        kv_store_free(ptr);
        return NULL;
    }
    
    // 分配新内存
    void *new_ptr = kv_store_malloc(size);
    if (!new_ptr) {
        return NULL;
    }
    
    // 注意：这里假设原始大小，实际使用中需要记录分配大小
    // 这是arena内存池的一个局限性
    memcpy(new_ptr, ptr, size);
    kv_store_free(ptr);
    
    return new_ptr;
#else
    return realloc(ptr, size);
#endif
}

// 清理全局arena
void kv_memory_cleanup(void) {
    if (g_arena) {
        kv_arena_destroy(g_arena);
        g_arena = NULL;
    }
} 