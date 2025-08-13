/*
 * @Author: Lzww0608  
 * @Date: 2025-6-1 21:45:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-15 16:40:56
 * @Description: ConcordKV 内存管理模块头文件 - 增强版Arena内存池实现
 */
#ifndef __KV_MEMORY_H__
#define __KV_MEMORY_H__

// 前向声明，避免头文件依赖问题
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// 内存池默认配置
#define KV_ARENA_DEFAULT_BLOCK_SIZE (65536) // 64KB 默认块大小（优化性能）
#define KV_ARENA_LARGE_BLOCK_SIZE (1048576) // 1MB 大块大小（高性能场景）
#define KV_ARENA_SMALL_BLOCK_SIZE (4096)    // 4KB 小块大小（内存敏感场景）
#define KV_ARENA_ALIGNMENT (8)              // 8字节对齐
#define KV_ARENA_MIN_BLOCK_SIZE (1024)      // 最小块大小
#define KV_ARENA_MAX_CACHED_BLOCKS (16)     // 最大缓存块数量
#define KV_ARENA_BLOCK_SIZE_CLASSES (8)     // 块大小分类数量

// Arena配置选项
typedef enum {
    KV_ARENA_THREAD_UNSAFE = 0,  // 非线程安全（默认，高性能）
    KV_ARENA_THREAD_SAFE = 1     // 线程安全
} kv_arena_thread_mode_t;

// 分配统计信息
typedef struct {
    uint64_t total_allocations;     // 总分配次数
    uint64_t total_bytes_allocated; // 总分配字节数
    uint64_t total_bytes_wasted;    // 总浪费字节数（对齐等）
    uint64_t peak_memory_usage;     // 峰值内存使用
    uint64_t current_memory_usage;  // 当前内存使用
    uint64_t block_allocations;     // 块分配次数
    uint64_t block_reuses;          // 块重用次数
    
    // 分配大小分布（热点分析）
    uint64_t size_distribution[16]; // 按2的幂次分布
    uint64_t alignment_requests[8]; // 对齐请求统计
} kv_arena_stats_t;

// 块缓存池
typedef struct arena_block_cache {
    struct arena_block **blocks;    // 缓存的块数组
    size_t *block_sizes;           // 对应的块大小
    size_t count;                  // 当前缓存块数量
    size_t capacity;               // 缓存容量
    pthread_mutex_t mutex;         // 缓存锁
} arena_block_cache_t;

// Arena内存池结构（增强版）
typedef struct kv_arena {
    char *alloc_ptr;         // 当前分配指针
    size_t alloc_bytes_remaining; // 当前块剩余字节数
    
    // 分配的内存块链表
    struct arena_block {
        char *data;
        size_t size;
        struct arena_block *next;
        uint64_t alloc_count;    // 该块的分配次数
        uint64_t timestamp;      // 分配时间戳
    } *blocks;
    
    // 统计信息
    size_t total_allocated;  // 总分配字节数
    size_t total_blocks;     // 总块数
    size_t memory_usage;     // 内存使用量
    
    // 配置
    size_t block_size;       // 块大小
    kv_arena_thread_mode_t thread_mode; // 线程模式
    
    // 线程安全支持
    pthread_rwlock_t rwlock; // 读写锁
    
    // 增强统计
    kv_arena_stats_t stats;  // 详细统计信息
    
    // 块缓存池
    arena_block_cache_t *block_cache; // 块缓存
    
    // NUMA支持
    int numa_node;           // NUMA节点ID (-1表示不指定)
} kv_arena_t;

// Arena配置结构
typedef struct {
    size_t block_size;                    // 块大小
    kv_arena_thread_mode_t thread_mode;   // 线程模式
    bool enable_block_cache;              // 启用块缓存
    size_t max_cached_blocks;             // 最大缓存块数
    bool enable_numa;                     // 启用NUMA优化
    int preferred_numa_node;              // 首选NUMA节点
    bool enable_detailed_stats;           // 启用详细统计
} kv_arena_config_t;

// 默认配置
#define KV_ARENA_DEFAULT_CONFIG { \
    .block_size = KV_ARENA_DEFAULT_BLOCK_SIZE, \
    .thread_mode = KV_ARENA_THREAD_UNSAFE, \
    .enable_block_cache = true, \
    .max_cached_blocks = KV_ARENA_MAX_CACHED_BLOCKS, \
    .enable_numa = false, \
    .preferred_numa_node = -1, \
    .enable_detailed_stats = true \
}

// Arena内存池接口（增强版）

/**
 * 创建arena内存池（增强版）
 * @param config 配置参数，NULL表示使用默认配置
 * @return 成功返回arena指针，失败返回NULL
 */
kv_arena_t *kv_arena_create_ex(const kv_arena_config_t *config);

/**
 * 创建arena内存池（兼容版本）
 * @param block_size 块大小，0表示使用默认值
 * @return 成功返回arena指针，失败返回NULL
 */
kv_arena_t *kv_arena_create(size_t block_size);

/**
 * 智能块大小选择（根据使用场景优化）
 * @param usage_hint 使用场景提示："small"/"default"/"large"/"performance"
 * @return 推荐的块大小
 */
size_t kv_arena_suggest_block_size(const char *usage_hint);

/**
 * 从arena分配内存
 * @param arena arena指针
 * @param size 分配大小
 * @return 成功返回内存指针，失败返回NULL
 */
void *kv_arena_alloc(kv_arena_t *arena, size_t size);

/**
 * 从arena分配对齐内存
 * @param arena arena指针
 * @param size 分配大小
 * @param alignment 对齐字节数
 * @return 成功返回内存指针，失败返回NULL
 */
void *kv_arena_alloc_aligned(kv_arena_t *arena, size_t size, size_t alignment);

/**
 * 获取arena内存使用统计
 * @param arena arena指针
 * @return 内存使用字节数
 */
size_t kv_arena_memory_usage(const kv_arena_t *arena);

/**
 * 获取arena分配统计
 * @param arena arena指针
 * @return 总分配字节数
 */
size_t kv_arena_total_allocated(const kv_arena_t *arena);

/**
 * 获取详细统计信息
 * @param arena arena指针
 * @param stats 输出统计信息
 * @return 成功返回0，失败返回-1
 */
int kv_arena_get_stats(const kv_arena_t *arena, kv_arena_stats_t *stats);

/**
 * 重置统计信息
 * @param arena arena指针
 */
void kv_arena_reset_stats(kv_arena_t *arena);

/**
 * 打印分配热点分析
 * @param arena arena指针
 */
void kv_arena_print_hotspots(const kv_arena_t *arena);

/**
 * 压缩arena（回收未使用的块）
 * @param arena arena指针
 * @return 回收的字节数
 */
size_t kv_arena_compact(kv_arena_t *arena);

/**
 * 销毁arena内存池
 * @param arena arena指针
 */
void kv_arena_destroy(kv_arena_t *arena);

/**
 * 内存分配函数
 * @param size 需要分配的内存大小
 * @return 成功返回内存指针，失败返回NULL
 */
void *kv_store_malloc(size_t size);

/**
 * 内存释放函数
 * @param ptr 需要释放的内存指针
 */
void kv_store_free(void *ptr);

/**
 * 内存重新分配函数
 * @param ptr 原内存指针
 * @param size 新的内存大小
 * @return 成功返回新内存指针，失败返回NULL
 */
void *kv_store_realloc(void *ptr, size_t size);

/**
 * 内存分配并清零函数
 * @param nmemb 元素数量
 * @param size 每个元素的大小
 * @return 成功返回内存指针，失败返回NULL
 */
void *kv_store_calloc(size_t nmemb, size_t size);

// 便于使用的宏定义
#define kv_malloc(size) kv_store_malloc(size)
#define kv_free(ptr) kv_store_free(ptr)
#define kv_realloc(ptr, size) kv_store_realloc(ptr, size)
#define kv_calloc(nmemb, size) kv_store_calloc(nmemb, size)

// Arena宏定义
#define kv_arena_malloc(arena, size) kv_arena_alloc(arena, size)
#define kv_arena_aligned_malloc(arena, size, align) kv_arena_alloc_aligned(arena, size, align)

/**
 * 清理全局arena内存池
 */
void kv_memory_cleanup(void);

// NUMA支持函数
/**
 * 获取当前NUMA节点
 * @return NUMA节点ID，失败返回-1
 */
int kv_arena_get_numa_node(void);

/**
 * 设置NUMA内存策略
 * @param node NUMA节点ID
 * @return 成功返回0，失败返回-1
 */
int kv_arena_set_numa_policy(int node);

#ifdef __cplusplus
}
#endif

#endif /* __KV_MEMORY_H__ */ 