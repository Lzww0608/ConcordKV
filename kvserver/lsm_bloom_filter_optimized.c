/*
 * @Author: Lzww0608  
 * @Date: 2025-6-6 16:19:35
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-6 17:13:06
 * @Description: ConcordKV LSM-Tree 优化布隆过滤器实现
 */

#include "lsm_bloom_filter_optimized.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

// === 错误码定义 ===
#define KV_ERR_NONE         0
#define KV_ERR_PARAM       -1
#define KV_ERR_MEMORY      -2
#define KV_ERR_UNSUPPORTED -3
#define KV_ERR_NOT_FOUND   -4

// === 内存分配器 ===
static void* kv_memory_allocate(size_t size) {
    return malloc(size);
}

static void kv_memory_free(void *ptr) {
    if (ptr) {
        free(ptr);
    }
}



// === 数学常量 ===
#define LN2 0.6931471805599453      // ln(2)
#define LN2_SQUARED 0.4804530139182014  // ln(2)^2

// === 性能优化常量 ===
#define BLOOM_SIMD_ALIGNMENT 32     // SIMD对齐
#define BLOOM_PREFETCH_DISTANCE 64  // 预取距离
#define BLOOM_BATCH_SIZE 64         // 批处理大小

// === FNV哈希常量 ===
#define FNV_OFFSET_BASIS_32 0x811c9dc5
#define FNV_PRIME_32 0x01000193
#define FNV_OFFSET_BASIS_64 0xcbf29ce484222325ULL
#define FNV_PRIME_64 0x100000001b3ULL

// === xxHash常量 ===
#define XXH_PRIME32_1 0x9E3779B1U
#define XXH_PRIME32_2 0x85EBCA77U
#define XXH_PRIME32_3 0xC2B2AE3DU
#define XXH_PRIME32_4 0x27D4EB2FU
#define XXH_PRIME32_5 0x165667B1U

#define XXH_PRIME64_1 0x9E3779B185EBCA87ULL
#define XXH_PRIME64_2 0xC2B2AE3D27D4EB4FULL
#define XXH_PRIME64_3 0x165667B19E3779F9ULL
#define XXH_PRIME64_4 0x85EBCA77C2B2AE63ULL
#define XXH_PRIME64_5 0x27D4EB2F165667C5ULL

// === 内部辅助函数声明 ===
static uint64_t get_timestamp_us(void);
static uint32_t next_power_of_2(uint32_t n);
static bool is_power_of_2(uint32_t n);
static uint32_t rotl32(uint32_t x, int8_t r);
static uint64_t rotl64(uint64_t x, int8_t r);

// === 时间戳获取 ===
static uint64_t get_timestamp_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

// === 数学工具函数 ===
static uint32_t next_power_of_2(uint32_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

static bool is_power_of_2(uint32_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

static uint32_t rotl32(uint32_t x, int8_t r) {
    return (x << r) | (x >> (32 - r));
}

static uint64_t rotl64(uint64_t x, int8_t r) {
    return (x << r) | (x >> (64 - r));
}

// === 哈希函数实现 ===

// MurmurHash3 32位实现
uint32_t bloom_hash_murmur3_32(const void *key, size_t key_len, uint32_t seed) {
    const uint8_t *data = (const uint8_t*)key;
    const int nblocks = key_len / 4;
    
    uint32_t h1 = seed;
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;
    
    // body
    const uint32_t *blocks = (const uint32_t *)(data + nblocks*4);
    
    for(int i = -nblocks; i; i++) {
        uint32_t k1 = blocks[i];
        
        k1 *= c1;
        k1 = rotl32(k1, 15);
        k1 *= c2;
        
        h1 ^= k1;
        h1 = rotl32(h1, 13);
        h1 = h1*5+0xe6546b64;
    }
    
    // tail
    const uint8_t *tail = (const uint8_t*)(data + nblocks*4);
    uint32_t k1 = 0;
    
    switch(key_len & 3) {
        case 3: k1 ^= tail[2] << 16; /* fallthrough */
        case 2: k1 ^= tail[1] << 8;  /* fallthrough */
        case 1: k1 ^= tail[0];
            k1 *= c1; k1 = rotl32(k1, 15); k1 *= c2; h1 ^= k1;
    };
    
    // finalization
    h1 ^= key_len;
    h1 ^= h1 >> 16;
    h1 *= 0x85ebca6b;
    h1 ^= h1 >> 13;
    h1 *= 0xc2b2ae35;
    h1 ^= h1 >> 16;
    
    return h1;
}

// MurmurHash3 64位实现
uint64_t bloom_hash_murmur3_64(const void *key, size_t key_len, uint32_t seed) {
    const uint8_t *data = (const uint8_t*)key;
    const int nblocks = key_len / 16;
    
    uint64_t h1 = seed;
    uint64_t h2 = seed;
    
    const uint64_t c1 = 0x87c37b91114253d5ULL;
    const uint64_t c2 = 0x4cf5ad432745937fULL;
    
    // body
    const uint64_t *blocks = (const uint64_t *)(data);
    
    for(int i = 0; i < nblocks; i++) {
        uint64_t k1 = blocks[i*2+0];
        uint64_t k2 = blocks[i*2+1];
        
        k1 *= c1; k1 = rotl64(k1, 31); k1 *= c2; h1 ^= k1;
        h1 = rotl64(h1, 27); h1 += h2; h1 = h1*5+0x52dce729;
        
        k2 *= c2; k2 = rotl64(k2, 33); k2 *= c1; h2 ^= k2;
        h2 = rotl64(h2, 31); h2 += h1; h2 = h2*5+0x38495ab5;
    }
    
    // tail
    const uint8_t *tail = (const uint8_t*)(data + nblocks*16);
    uint64_t k1 = 0;
    uint64_t k2 = 0;
    
    switch(key_len & 15) {
        case 15: k2 ^= ((uint64_t)tail[14]) << 48; /* fallthrough */
        case 14: k2 ^= ((uint64_t)tail[13]) << 40; /* fallthrough */
        case 13: k2 ^= ((uint64_t)tail[12]) << 32; /* fallthrough */
        case 12: k2 ^= ((uint64_t)tail[11]) << 24; /* fallthrough */
        case 11: k2 ^= ((uint64_t)tail[10]) << 16; /* fallthrough */
        case 10: k2 ^= ((uint64_t)tail[ 9]) << 8;  /* fallthrough */
        case  9: k2 ^= ((uint64_t)tail[ 8]) << 0;
                 k2 *= c2; k2 = rotl64(k2, 33); k2 *= c1; h2 ^= k2;
                 /* fallthrough */
        
        case  8: k1 ^= ((uint64_t)tail[ 7]) << 56; /* fallthrough */
        case  7: k1 ^= ((uint64_t)tail[ 6]) << 48; /* fallthrough */
        case  6: k1 ^= ((uint64_t)tail[ 5]) << 40; /* fallthrough */
        case  5: k1 ^= ((uint64_t)tail[ 4]) << 32; /* fallthrough */
        case  4: k1 ^= ((uint64_t)tail[ 3]) << 24; /* fallthrough */
        case  3: k1 ^= ((uint64_t)tail[ 2]) << 16; /* fallthrough */
        case  2: k1 ^= ((uint64_t)tail[ 1]) << 8;  /* fallthrough */
        case  1: k1 ^= ((uint64_t)tail[ 0]) << 0;
                 k1 *= c1; k1 = rotl64(k1, 31); k1 *= c2; h1 ^= k1;
    };
    
    // finalization
    h1 ^= key_len; h2 ^= key_len;
    h1 += h2; h2 += h1;
    
    h1 ^= h1 >> 33;
    h1 *= 0xff51afd7ed558ccdULL;
    h1 ^= h1 >> 33;
    h1 *= 0xc4ceb9fe1a85ec53ULL;
    h1 ^= h1 >> 33;
    
    h2 ^= h2 >> 33;
    h2 *= 0xff51afd7ed558ccdULL;
    h2 ^= h2 >> 33;
    h2 *= 0xc4ceb9fe1a85ec53ULL;
    h2 ^= h2 >> 33;
    
    h1 += h2; h2 += h1;
    
    return h1; // 返回第一个64位哈希值
}

// FNV-1a 32位实现
uint32_t bloom_hash_fnv1a_32(const void *key, size_t key_len, uint32_t seed) {
    const uint8_t *data = (const uint8_t*)key;
    uint32_t hash = FNV_OFFSET_BASIS_32 ^ seed;
    
    for (size_t i = 0; i < key_len; i++) {
        hash ^= data[i];
        hash *= FNV_PRIME_32;
    }
    
    return hash;
}

// FNV-1a 64位实现
uint64_t bloom_hash_fnv1a_64(const void *key, size_t key_len, uint32_t seed) {
    const uint8_t *data = (const uint8_t*)key;
    uint64_t hash = FNV_OFFSET_BASIS_64 ^ seed;
    
    for (size_t i = 0; i < key_len; i++) {
        hash ^= data[i];
        hash *= FNV_PRIME_64;
    }
    
    return hash;
}

// xxHash 32位实现(简化版)
uint32_t bloom_hash_xxhash_32(const void *key, size_t key_len, uint32_t seed) {
    const uint8_t *data = (const uint8_t*)key;
    uint32_t h32;
    
    if (key_len >= 16) {
        const uint8_t* const end = data + key_len;
        const uint8_t* const limit = end - 16;
        uint32_t v1 = seed + XXH_PRIME32_1 + XXH_PRIME32_2;
        uint32_t v2 = seed + XXH_PRIME32_2;
        uint32_t v3 = seed + 0;
        uint32_t v4 = seed - XXH_PRIME32_1;
        
        do {
            v1 += *(uint32_t*)data * XXH_PRIME32_2; v1 = rotl32(v1, 13); v1 *= XXH_PRIME32_1; data += 4;
            v2 += *(uint32_t*)data * XXH_PRIME32_2; v2 = rotl32(v2, 13); v2 *= XXH_PRIME32_1; data += 4;
            v3 += *(uint32_t*)data * XXH_PRIME32_2; v3 = rotl32(v3, 13); v3 *= XXH_PRIME32_1; data += 4;
            v4 += *(uint32_t*)data * XXH_PRIME32_2; v4 = rotl32(v4, 13); v4 *= XXH_PRIME32_1; data += 4;
        } while (data <= limit);
        
        h32 = rotl32(v1, 1) + rotl32(v2, 7) + rotl32(v3, 12) + rotl32(v4, 18);
    } else {
        h32 = seed + XXH_PRIME32_5;
    }
    
    h32 += (uint32_t) key_len;
    
    while (data + 4 <= (uint8_t*)key + key_len) {
        h32 += *(uint32_t*)data * XXH_PRIME32_3;
        h32 = rotl32(h32, 17) * XXH_PRIME32_4;
        data += 4;
    }
    
    while (data < (uint8_t*)key + key_len) {
        h32 += (*data) * XXH_PRIME32_5;
        h32 = rotl32(h32, 11) * XXH_PRIME32_1;
        data++;
    }
    
    h32 ^= h32 >> 15;
    h32 *= XXH_PRIME32_2;
    h32 ^= h32 >> 13;
    h32 *= XXH_PRIME32_3;
    h32 ^= h32 >> 16;
    
    return h32;
}

// xxHash 64位实现(简化版)
uint64_t bloom_hash_xxhash_64(const void *key, size_t key_len, uint32_t seed) {
    const uint8_t *data = (const uint8_t*)key;
    uint64_t h64;
    
    if (key_len >= 32) {
        const uint8_t* const end = data + key_len;
        const uint8_t* const limit = end - 32;
        uint64_t v1 = seed + XXH_PRIME64_1 + XXH_PRIME64_2;
        uint64_t v2 = seed + XXH_PRIME64_2;
        uint64_t v3 = seed + 0;
        uint64_t v4 = seed - XXH_PRIME64_1;
        
        do {
            v1 += *(uint64_t*)data * XXH_PRIME64_2; v1 = rotl64(v1, 31); v1 *= XXH_PRIME64_1; data += 8;
            v2 += *(uint64_t*)data * XXH_PRIME64_2; v2 = rotl64(v2, 31); v2 *= XXH_PRIME64_1; data += 8;
            v3 += *(uint64_t*)data * XXH_PRIME64_2; v3 = rotl64(v3, 31); v3 *= XXH_PRIME64_1; data += 8;
            v4 += *(uint64_t*)data * XXH_PRIME64_2; v4 = rotl64(v4, 31); v4 *= XXH_PRIME64_1; data += 8;
        } while (data <= limit);
        
        h64 = rotl64(v1, 1) + rotl64(v2, 7) + rotl64(v3, 12) + rotl64(v4, 18);
        
        v1 *= XXH_PRIME64_2; v1 = rotl64(v1, 31); v1 *= XXH_PRIME64_1; h64 ^= v1;
        h64 = h64 * XXH_PRIME64_1 + XXH_PRIME64_4;
        
        v2 *= XXH_PRIME64_2; v2 = rotl64(v2, 31); v2 *= XXH_PRIME64_1; h64 ^= v2;
        h64 = h64 * XXH_PRIME64_1 + XXH_PRIME64_4;
        
        v3 *= XXH_PRIME64_2; v3 = rotl64(v3, 31); v3 *= XXH_PRIME64_1; h64 ^= v3;
        h64 = h64 * XXH_PRIME64_1 + XXH_PRIME64_4;
        
        v4 *= XXH_PRIME64_2; v4 = rotl64(v4, 31); v4 *= XXH_PRIME64_1; h64 ^= v4;
        h64 = h64 * XXH_PRIME64_1 + XXH_PRIME64_4;
    } else {
        h64 = seed + XXH_PRIME64_5;
    }
    
    h64 += (uint64_t) key_len;
    
    while (data + 8 <= (uint8_t*)key + key_len) {
        uint64_t k1 = *(uint64_t*)data;
        k1 *= XXH_PRIME64_2; k1 = rotl64(k1, 31); k1 *= XXH_PRIME64_1;
        h64 ^= k1;
        h64 = rotl64(h64, 27) * XXH_PRIME64_1 + XXH_PRIME64_4;
        data += 8;
    }
    
    if (data + 4 <= (uint8_t*)key + key_len) {
        h64 ^= (uint64_t)(*(uint32_t*)data) * XXH_PRIME64_1;
        h64 = rotl64(h64, 23) * XXH_PRIME64_2 + XXH_PRIME64_3;
        data += 4;
    }
    
    while (data < (uint8_t*)key + key_len) {
        h64 ^= (*data) * XXH_PRIME64_5;
        h64 = rotl64(h64, 11) * XXH_PRIME64_1;
        data++;
    }
    
    h64 ^= h64 >> 33;
    h64 *= XXH_PRIME64_2;
    h64 ^= h64 >> 29;
    h64 *= XXH_PRIME64_3;
    h64 ^= h64 >> 32;
    
    return h64;
}

// === 参数计算函数 ===

bloom_filter_config_t bloom_filter_create_default_config(uint64_t expected_entries, uint32_t false_positive_rate) {
    bloom_filter_config_t config = {0};
    
    // 验证参数范围
    if (expected_entries < BLOOM_MIN_EXPECTED_ENTRIES) {
        expected_entries = BLOOM_MIN_EXPECTED_ENTRIES;
    }
    if (expected_entries > BLOOM_MAX_EXPECTED_ENTRIES) {
        expected_entries = BLOOM_MAX_EXPECTED_ENTRIES;
    }
    
    if (false_positive_rate < BLOOM_MIN_FALSE_POSITIVE) {
        false_positive_rate = BLOOM_MIN_FALSE_POSITIVE;
    }
    if (false_positive_rate > BLOOM_MAX_FALSE_POSITIVE) {
        false_positive_rate = BLOOM_MAX_FALSE_POSITIVE;
    }
    
    config.expected_entries = expected_entries;
    config.false_positive_rate = false_positive_rate;
    config.type = BLOOM_TYPE_AUTO;
    config.hash_type = BLOOM_HASH_HYBRID;
    config.optimization_flags = BLOOM_OPT_AUTO;
    config.max_hash_functions = 16;
    config.block_size = BLOOM_CACHE_LINE_BITS;
    config.enable_statistics = true;
    config.enable_auto_resize = false;
    config.load_factor_threshold = 0.8;
    
    return config;
}

int bloom_filter_calculate_parameters(bloom_parameter_calculator_t *calculator) {
    if (!calculator || calculator->expected_entries == 0 || calculator->target_fpr == 0) {
        return KV_ERR_PARAM;
    }
    
    uint64_t n = calculator->expected_entries;
    double p = (double)calculator->target_fpr / 10000.0; // 转换为小数
    
    // 计算最优位数组大小: m = -n * ln(p) / (ln(2)^2)
    double optimal_m = -(double)n * log(p) / LN2_SQUARED;
    calculator->optimal_bit_count = (size_t)ceil(optimal_m);
    
    // 计算最优哈希函数数量: k = (m/n) * ln(2)
    double optimal_k = (optimal_m / n) * LN2;
    calculator->optimal_hash_count = (uint32_t)round(optimal_k);
    
    // 确保至少有一个哈希函数
    if (calculator->optimal_hash_count == 0) {
        calculator->optimal_hash_count = 1;
    }
    
    // 计算每元素位数
    calculator->bits_per_element = optimal_m / n;
    
    // 计算实际误判率
    double actual_p = pow(1.0 - exp(-optimal_k * n / optimal_m), optimal_k);
    calculator->actual_fpr = actual_p;
    
    // 计算内存需求
    calculator->memory_requirement = (calculator->optimal_bit_count + 7) / 8;
    
    // 性能预测(基于经验公式)
    calculator->insert_ops_per_sec = 1000000.0 / calculator->optimal_hash_count;
    calculator->query_ops_per_sec = calculator->insert_ops_per_sec * 1.2;
    calculator->cache_misses_per_op = (calculator->optimal_hash_count + 7) / 8;
    
    return KV_ERR_NONE;
}

int bloom_filter_optimize_config(bloom_filter_config_t *config, bloom_optimization_flags_t optimization_target) {
    if (!config) {
        return KV_ERR_PARAM;
    }
    
    // 根据优化目标调整配置
    if (optimization_target & BLOOM_OPT_MEMORY) {
        // 内存优化：使用更高的误判率，减少哈希函数数量
        if (config->false_positive_rate < 200) {
            config->false_positive_rate = 200; // 2%
        }
        config->max_hash_functions = 8;
        config->type = BLOOM_TYPE_STANDARD;
    }
    
    if (optimization_target & BLOOM_OPT_SPEED) {
        // 速度优化：使用缓存友好的块布隆过滤器
        config->type = BLOOM_TYPE_BLOCKED;
        config->block_size = BLOOM_CACHE_LINE_BITS;
        config->hash_type = BLOOM_HASH_MURMUR3;
        config->optimization_flags |= BLOOM_OPT_CACHE_FRIENDLY;
    }
    
    if (optimization_target & BLOOM_OPT_BALANCED) {
        // 平衡优化：在内存和速度之间取平衡
        config->false_positive_rate = 100; // 1%
        config->type = BLOOM_TYPE_REGISTER_BLOCKED;
        config->hash_type = BLOOM_HASH_XXHASH;
        config->max_hash_functions = 12;
    }
    
    if (optimization_target & BLOOM_OPT_CACHE_FRIENDLY) {
        // 缓存友好优化
        config->type = BLOOM_TYPE_BLOCKED;
        config->block_size = BLOOM_CACHE_LINE_BITS;
    }
    
    if (optimization_target & BLOOM_OPT_SIMD) {
        // SIMD优化
        config->type = BLOOM_TYPE_REGISTER_BLOCKED;
        config->block_size = BLOOM_REGISTER_SIZE;
    }
    
    if (optimization_target & BLOOM_OPT_AUTO) {
        // 自动优化：根据数据规模选择最优策略
        if (config->expected_entries < 10000) {
            config->type = BLOOM_TYPE_STANDARD;
            config->hash_type = BLOOM_HASH_MURMUR3;
        } else if (config->expected_entries < 1000000) {
            config->type = BLOOM_TYPE_BLOCKED;
            config->hash_type = BLOOM_HASH_XXHASH;
        } else {
            config->type = BLOOM_TYPE_REGISTER_BLOCKED;
            config->hash_type = BLOOM_HASH_XXHASH;
            config->optimization_flags |= BLOOM_OPT_SIMD;
        }
    }
    
    return KV_ERR_NONE;
}

bloom_hash_type_t bloom_hash_auto_select(uint64_t expected_entries, uint32_t hash_count) {
    // 根据数据规模和哈希函数数量选择最优哈希函数
    if (expected_entries < 10000) {
        return BLOOM_HASH_MURMUR3; // 小规模数据，选择质量好的MurmurHash3
    } else if (expected_entries < 1000000) {
        if (hash_count <= 4) {
            return BLOOM_HASH_XXHASH; // 中等规模，少量哈希函数，选择xxHash
        } else {
            return BLOOM_HASH_FNV1A;  // 更多哈希函数，选择更快的FNV
        }
    } else {
        return BLOOM_HASH_FNV1A;     // 大规模数据，优先速度
    }
}

// === 统计函数 ===
static int init_stats(bloom_filter_stats_t *stats) {
    if (!stats) return KV_ERR_PARAM;
    
    memset(stats, 0, sizeof(bloom_filter_stats_t));
    
    if (pthread_rwlock_init(&stats->stats_lock, NULL) != 0) {
        return KV_ERR_MEMORY;
    }
    
    gettimeofday(&stats->last_update, NULL);
    return KV_ERR_NONE;
}

static void destroy_stats(bloom_filter_stats_t *stats) {
    if (stats) {
        pthread_rwlock_destroy(&stats->stats_lock);
    }
}

static void update_stats_insertion(bloom_filter_stats_t *stats) {
    if (!stats) return;
    
    pthread_rwlock_wrlock(&stats->stats_lock);
    stats->total_insertions++;
    gettimeofday(&stats->last_update, NULL);
    pthread_rwlock_unlock(&stats->stats_lock);
}

static void update_stats_query(bloom_filter_stats_t *stats, bool hit) {
    if (!stats) return;
    
    pthread_rwlock_wrlock(&stats->stats_lock);
    stats->total_queries++;
    if (hit) {
        stats->total_hits++;
    } else {
        stats->total_misses++;
    }
    gettimeofday(&stats->last_update, NULL);
    pthread_rwlock_unlock(&stats->stats_lock);
}

// === 内存估算 ===
size_t bloom_filter_estimate_memory_optimized(const bloom_filter_config_t *config) {
    if (!config) return 0;
    
    bloom_parameter_calculator_t calc = {0};
    calc.expected_entries = config->expected_entries;
    calc.target_fpr = config->false_positive_rate;
    
    if (bloom_filter_calculate_parameters(&calc) != KV_ERR_NONE) {
        return 0;
    }
    
    size_t memory = sizeof(optimized_bloom_filter_t);
    memory += calc.memory_requirement; // 位数组
    memory += calc.optimal_hash_count * sizeof(uint32_t); // 哈希种子
    
    // 根据类型增加额外内存
    switch (config->type) {
        case BLOOM_TYPE_BLOCKED: {
            uint32_t block_count = (calc.optimal_bit_count + config->block_size - 1) / config->block_size;
            memory += block_count * sizeof(bloom_block_t);
            break;
        }
        case BLOOM_TYPE_REGISTER_BLOCKED: {
            uint32_t register_count = (calc.optimal_bit_count + 63) / 64;
            memory += register_count * sizeof(uint64_t);
            break;
        }
        case BLOOM_TYPE_COUNTING: {
            memory += calc.optimal_bit_count * sizeof(uint32_t); // 计数器数组
            break;
        }
        default:
            break;
    }
    
    return memory;
}

// === 布隆过滤器实现 ===

optimized_bloom_filter_t* bloom_filter_create_optimized(const bloom_filter_config_t *config) {
    if (!config) return NULL;
    
    optimized_bloom_filter_t *filter = kv_memory_allocate(sizeof(optimized_bloom_filter_t));
    if (!filter) return NULL;
    
    memset(filter, 0, sizeof(optimized_bloom_filter_t));
    memcpy(&filter->config, config, sizeof(bloom_filter_config_t));
    
    // 计算参数
    bloom_parameter_calculator_t calc = {0};
    calc.expected_entries = config->expected_entries;
    calc.target_fpr = config->false_positive_rate;
    
    if (bloom_filter_calculate_parameters(&calc) != KV_ERR_NONE) {
        kv_memory_free(filter);
        return NULL;
    }
    
    filter->bit_count = calc.optimal_bit_count;
    filter->hash_count = calc.optimal_hash_count;
    filter->max_entries = config->expected_entries;
    filter->current_entries = 0;
    
    // 初始化哈希种子
    filter->hash_seeds = kv_memory_allocate(filter->hash_count * sizeof(uint32_t));
    if (!filter->hash_seeds) {
        kv_memory_free(filter);
        return NULL;
    }
    
    srand(time(NULL));
    for (uint32_t i = 0; i < filter->hash_count; i++) {
        filter->hash_seeds[i] = (uint32_t)rand();
    }
    filter->salt = ((uint64_t)rand() << 32) | (uint64_t)rand();
    
    // 根据类型创建过滤器
    switch (config->type) {
        case BLOOM_TYPE_STANDARD:
        case BLOOM_TYPE_AUTO: {
            size_t byte_count = (filter->bit_count + 7) / 8;
            filter->standard.bits = kv_memory_allocate(byte_count);
            if (!filter->standard.bits) {
                kv_memory_free(filter->hash_seeds);
                kv_memory_free(filter);
                return NULL;
            }
            memset(filter->standard.bits, 0, byte_count);
            break;
        }
        
        case BLOOM_TYPE_BLOCKED: {
            uint32_t block_size_bits = config->block_size > 0 ? config->block_size : BLOOM_CACHE_LINE_BITS;
            filter->blocked.block_count = (filter->bit_count + block_size_bits - 1) / block_size_bits;
            
            // 确保是2的幂，便于位运算优化
            uint32_t power_of_2 = 1;
            while (power_of_2 < filter->blocked.block_count) {
                power_of_2 <<= 1;
            }
            filter->blocked.block_count = power_of_2;
            filter->blocked.block_mask = power_of_2 - 1;
            
            filter->blocked.blocks = kv_memory_allocate(
                filter->blocked.block_count * sizeof(bloom_block_t)
            );
            if (!filter->blocked.blocks) {
                kv_memory_free(filter->hash_seeds);
                kv_memory_free(filter);
                return NULL;
            }
            
            size_t block_byte_size = block_size_bits / 8;
            for (uint32_t i = 0; i < filter->blocked.block_count; i++) {
                filter->blocked.blocks[i].bits = kv_memory_allocate(block_byte_size);
                if (!filter->blocked.blocks[i].bits) {
                    // 清理已分配的块
                    for (uint32_t j = 0; j < i; j++) {
                        kv_memory_free(filter->blocked.blocks[j].bits);
                    }
                    kv_memory_free(filter->blocked.blocks);
                    kv_memory_free(filter->hash_seeds);
                    kv_memory_free(filter);
                    return NULL;
                }
                memset(filter->blocked.blocks[i].bits, 0, block_byte_size);
                filter->blocked.blocks[i].size_bits = block_size_bits;
                filter->blocked.blocks[i].hash_count = filter->hash_count - 1; // 第一个哈希用于选择块
                filter->blocked.blocks[i].entry_count = 0;
                filter->blocked.blocks[i].max_entries = config->expected_entries / filter->blocked.block_count;
            }
            break;
        }
        
        case BLOOM_TYPE_REGISTER_BLOCKED: {
            filter->register_blocked.register_count = (filter->bit_count + 63) / 64;
            
            // 确保是2的幂
            uint32_t power_of_2 = 1;
            while (power_of_2 < filter->register_blocked.register_count) {
                power_of_2 <<= 1;
            }
            filter->register_blocked.register_count = power_of_2;
            filter->register_blocked.register_mask = power_of_2 - 1;
            
            filter->register_blocked.registers = kv_memory_allocate(
                filter->register_blocked.register_count * sizeof(uint64_t)
            );
            if (!filter->register_blocked.registers) {
                kv_memory_free(filter->hash_seeds);
                kv_memory_free(filter);
                return NULL;
            }
            memset(filter->register_blocked.registers, 0, 
                   filter->register_blocked.register_count * sizeof(uint64_t));
            break;
        }
        
        case BLOOM_TYPE_COUNTING: {
            filter->counting.counters = kv_memory_allocate(filter->bit_count * sizeof(uint32_t));
            if (!filter->counting.counters) {
                kv_memory_free(filter->hash_seeds);
                kv_memory_free(filter);
                return NULL;
            }
            memset(filter->counting.counters, 0, filter->bit_count * sizeof(uint32_t));
            filter->counting.max_count = UINT32_MAX;
            break;
        }
        
        case BLOOM_TYPE_SCALABLE: {
            filter->scalable.first_layer = kv_memory_allocate(sizeof(scalable_bloom_layer_t));
            if (!filter->scalable.first_layer) {
                kv_memory_free(filter->hash_seeds);
                kv_memory_free(filter);
                return NULL;
            }
            
            size_t byte_count = (filter->bit_count + 7) / 8;
            filter->scalable.first_layer->bits = kv_memory_allocate(byte_count);
            if (!filter->scalable.first_layer->bits) {
                kv_memory_free(filter->scalable.first_layer);
                kv_memory_free(filter->hash_seeds);
                kv_memory_free(filter);
                return NULL;
            }
            
            memset(filter->scalable.first_layer->bits, 0, byte_count);
            filter->scalable.first_layer->bit_count = filter->bit_count;
            filter->scalable.first_layer->hash_count = filter->hash_count;
            filter->scalable.first_layer->max_entries = config->expected_entries;
            filter->scalable.first_layer->current_entries = 0;
            filter->scalable.first_layer->false_positive_rate = config->false_positive_rate;
            filter->scalable.first_layer->next = NULL;
            
            filter->scalable.current_layer = filter->scalable.first_layer;
            filter->scalable.layer_count = 1;
            filter->scalable.growth_factor = 2.0;
            break;
        }
    }
    
    // 初始化统计信息
    if (config->enable_statistics) {
        if (init_stats(&filter->stats) != KV_ERR_NONE) {
            bloom_filter_destroy_optimized(filter);
            return NULL;
        }
    }
    
    // 初始化线程安全
    filter->thread_safe = true;
    if (pthread_rwlock_init(&filter->lock, NULL) != 0) {
        bloom_filter_destroy_optimized(filter);
        return NULL;
    }
    
    // 设置元数据
    filter->creation_time = time(NULL);
    snprintf(filter->name, sizeof(filter->name), "bloom_filter_%lx", filter->salt);
    
    return filter;
}

void bloom_filter_destroy_optimized(optimized_bloom_filter_t *filter) {
    if (!filter) return;
    
    // 销毁不同类型的数据
    switch (filter->config.type) {
        case BLOOM_TYPE_STANDARD:
        case BLOOM_TYPE_AUTO:
            if (filter->standard.bits) {
                kv_memory_free(filter->standard.bits);
            }
            break;
            
        case BLOOM_TYPE_BLOCKED:
            if (filter->blocked.blocks) {
                for (uint32_t i = 0; i < filter->blocked.block_count; i++) {
                    if (filter->blocked.blocks[i].bits) {
                        kv_memory_free(filter->blocked.blocks[i].bits);
                    }
                }
                kv_memory_free(filter->blocked.blocks);
            }
            break;
            
        case BLOOM_TYPE_REGISTER_BLOCKED:
            if (filter->register_blocked.registers) {
                kv_memory_free(filter->register_blocked.registers);
            }
            break;
            
        case BLOOM_TYPE_COUNTING:
            if (filter->counting.counters) {
                kv_memory_free(filter->counting.counters);
            }
            break;
            
        case BLOOM_TYPE_SCALABLE: {
            scalable_bloom_layer_t *layer = filter->scalable.first_layer;
            while (layer) {
                scalable_bloom_layer_t *next = layer->next;
                if (layer->bits) {
                    kv_memory_free(layer->bits);
                }
                kv_memory_free(layer);
                layer = next;
            }
            break;
        }
    }
    
    // 销毁哈希种子
    if (filter->hash_seeds) {
        kv_memory_free(filter->hash_seeds);
    }
    
    // 销毁统计信息
    if (filter->config.enable_statistics) {
        destroy_stats(&filter->stats);
    }
    
    // 销毁锁
    pthread_rwlock_destroy(&filter->lock);
    
    // 销毁过滤器本身
    kv_memory_free(filter);
}

// 内联位操作函数
static inline void set_bit(uint8_t *bits, size_t bit_index) {
    size_t byte_index = bit_index / 8;
    size_t bit_offset = bit_index % 8;
    bits[byte_index] |= (1u << bit_offset);
}

static inline bool get_bit(const uint8_t *bits, size_t bit_index) {
    size_t byte_index = bit_index / 8;
    size_t bit_offset = bit_index % 8;
    return (bits[byte_index] & (1u << bit_offset)) != 0;
}

// 计算哈希值数组
static void compute_hashes(const optimized_bloom_filter_t *filter, const void *key, size_t key_len, uint64_t *hashes) {
    // 使用双哈希技术：h(i) = h1 + i * h2
    uint64_t h1, h2;
    
    switch (filter->config.hash_type) {
        case BLOOM_HASH_MURMUR3:
            h1 = bloom_hash_murmur3_64(key, key_len, filter->hash_seeds[0]);
            h2 = bloom_hash_murmur3_64(key, key_len, filter->hash_seeds[0] ^ 0xAAAAAAAA);
            break;
        case BLOOM_HASH_XXHASH:
            h1 = bloom_hash_xxhash_64(key, key_len, filter->hash_seeds[0]);
            h2 = bloom_hash_xxhash_64(key, key_len, filter->hash_seeds[0] ^ 0xAAAAAAAA);
            break;
        case BLOOM_HASH_FNV1A:
            h1 = bloom_hash_fnv1a_64(key, key_len, filter->hash_seeds[0]);
            h2 = bloom_hash_fnv1a_64(key, key_len, filter->hash_seeds[0] ^ 0xAAAAAAAA);
            break;
        default: // BLOOM_HASH_HYBRID 或其他
            h1 = bloom_hash_murmur3_64(key, key_len, filter->hash_seeds[0]);
            h2 = bloom_hash_xxhash_64(key, key_len, filter->hash_seeds[0] ^ 0xAAAAAAAA);
            break;
    }
    
    for (uint32_t i = 0; i < filter->hash_count; i++) {
        hashes[i] = h1 + i * h2;
    }
}

int bloom_filter_add_optimized(optimized_bloom_filter_t *filter, const void *key, size_t key_len) {
    if (!filter || !key || key_len == 0) {
        return KV_ERR_PARAM;
    }
    
    // 线程安全 - 写锁用于添加操作
    if (filter->thread_safe) {
        pthread_rwlock_wrlock(&filter->lock);
    }
    
    int result = KV_ERR_NONE;
    uint64_t hashes[16]; // 支持最多16个哈希函数
    compute_hashes(filter, key, key_len, hashes);
    
    switch (filter->config.type) {
        case BLOOM_TYPE_STANDARD:
        case BLOOM_TYPE_AUTO: {
            for (uint32_t i = 0; i < filter->hash_count; i++) {
                size_t bit_index = hashes[i] % filter->bit_count;
                set_bit(filter->standard.bits, bit_index);
            }
            break;
        }
        
        case BLOOM_TYPE_BLOCKED: {
            uint32_t block_index = (uint32_t)(hashes[0] & filter->blocked.block_mask);
            bloom_block_t *block = &filter->blocked.blocks[block_index];
            
            for (uint32_t i = 1; i < filter->hash_count; i++) {
                size_t bit_index = hashes[i] % block->size_bits;
                set_bit(block->bits, bit_index);
            }
            block->entry_count++;
            break;
        }
        
        case BLOOM_TYPE_REGISTER_BLOCKED: {
            uint32_t reg_index = (uint32_t)(hashes[0] & filter->register_blocked.register_mask);
            uint64_t mask = 0;
            
            for (uint32_t i = 1; i < filter->hash_count; i++) {
                uint32_t bit_pos = (uint32_t)(hashes[i] % 64);
                mask |= (1ULL << bit_pos);
            }
            
            filter->register_blocked.registers[reg_index] |= mask;
            break;
        }
        
        case BLOOM_TYPE_COUNTING: {
            for (uint32_t i = 0; i < filter->hash_count; i++) {
                size_t index = hashes[i] % filter->bit_count;
                if (filter->counting.counters[index] < filter->counting.max_count) {
                    filter->counting.counters[index]++;
                }
            }
            break;
        }
        
        case BLOOM_TYPE_SCALABLE: {
            scalable_bloom_layer_t *layer = filter->scalable.current_layer;
            
            // 检查当前层是否已满
            if (layer->current_entries >= layer->max_entries) {
                // 创建新层
                scalable_bloom_layer_t *new_layer = kv_memory_allocate(sizeof(scalable_bloom_layer_t));
                if (!new_layer) {
                    result = KV_ERR_MEMORY;
                    break;
                }
                
                // 新层的大小是前一层的2倍
                new_layer->bit_count = layer->bit_count * 2;
                new_layer->hash_count = filter->hash_count;
                new_layer->max_entries = layer->max_entries * 2;
                new_layer->current_entries = 0;
                new_layer->false_positive_rate = layer->false_positive_rate / 2; // 更严格的误判率
                new_layer->next = NULL;
                
                size_t byte_count = (new_layer->bit_count + 7) / 8;
                new_layer->bits = kv_memory_allocate(byte_count);
                if (!new_layer->bits) {
                    kv_memory_free(new_layer);
                    result = KV_ERR_MEMORY;
                    break;
                }
                memset(new_layer->bits, 0, byte_count);
                
                layer->next = new_layer;
                filter->scalable.current_layer = new_layer;
                filter->scalable.layer_count++;
                layer = new_layer;
            }
            
            // 在当前层中插入
            for (uint32_t i = 0; i < layer->hash_count; i++) {
                size_t bit_index = hashes[i] % layer->bit_count;
                set_bit(layer->bits, bit_index);
            }
            layer->current_entries++;
            break;
        }
    }
    
    if (result == KV_ERR_NONE) {
        filter->current_entries++;
        
        // 更新统计信息
        if (filter->config.enable_statistics) {
            update_stats_insertion(&filter->stats);
        }
    }
    
    if (filter->thread_safe) {
        pthread_rwlock_unlock(&filter->lock);
    }
    
    return result;
}

bool bloom_filter_may_contain_optimized(optimized_bloom_filter_t *filter, const void *key, size_t key_len) {
    if (!filter || !key || key_len == 0) {
        return false;
    }
    
    // 线程安全 - 读锁用于查询操作
    if (filter->thread_safe) {
        pthread_rwlock_rdlock(&filter->lock);
    }
    
    bool result = true;
    uint64_t hashes[16];
    compute_hashes(filter, key, key_len, hashes);
    
    switch (filter->config.type) {
        case BLOOM_TYPE_STANDARD:
        case BLOOM_TYPE_AUTO: {
            for (uint32_t i = 0; i < filter->hash_count && result; i++) {
                size_t bit_index = hashes[i] % filter->bit_count;
                if (!get_bit(filter->standard.bits, bit_index)) {
                    result = false;
                }
            }
            break;
        }
        
        case BLOOM_TYPE_BLOCKED: {
            uint32_t block_index = (uint32_t)(hashes[0] & filter->blocked.block_mask);
            bloom_block_t *block = &filter->blocked.blocks[block_index];
            
            for (uint32_t i = 1; i < filter->hash_count && result; i++) {
                size_t bit_index = hashes[i] % block->size_bits;
                if (!get_bit(block->bits, bit_index)) {
                    result = false;
                }
            }
            break;
        }
        
        case BLOOM_TYPE_REGISTER_BLOCKED: {
            uint32_t reg_index = (uint32_t)(hashes[0] & filter->register_blocked.register_mask);
            uint64_t mask = 0;
            
            for (uint32_t i = 1; i < filter->hash_count; i++) {
                uint32_t bit_pos = (uint32_t)(hashes[i] % 64);
                mask |= (1ULL << bit_pos);
            }
            
            result = (filter->register_blocked.registers[reg_index] & mask) == mask;
            break;
        }
        
        case BLOOM_TYPE_COUNTING: {
            for (uint32_t i = 0; i < filter->hash_count && result; i++) {
                size_t index = hashes[i] % filter->bit_count;
                if (filter->counting.counters[index] == 0) {
                    result = false;
                }
            }
            break;
        }
        
        case BLOOM_TYPE_SCALABLE: {
            result = false;
            scalable_bloom_layer_t *layer = filter->scalable.first_layer;
            
            // 在所有层中查找
            while (layer && !result) {
                bool layer_result = true;
                for (uint32_t i = 0; i < layer->hash_count && layer_result; i++) {
                    size_t bit_index = hashes[i] % layer->bit_count;
                    if (!get_bit(layer->bits, bit_index)) {
                        layer_result = false;
                    }
                }
                if (layer_result) {
                    result = true;
                }
                layer = layer->next;
            }
            break;
        }
    }
    
    // 更新统计信息
    if (filter->config.enable_statistics) {
        update_stats_query(&filter->stats, result);
    }
    
    if (filter->thread_safe) {
        pthread_rwlock_unlock(&filter->lock);
    }
    
    return result;
}

// === 批量操作 ===

uint64_t bloom_filter_add_batch_optimized(optimized_bloom_filter_t *filter, 
                                         const void **keys, const size_t *key_lens, uint64_t count) {
    if (!filter || !keys || !key_lens || count == 0) {
        return 0;
    }
    
    uint64_t successful_adds = 0;
    
    // 线程安全 - 写锁用于批量添加
    if (filter->thread_safe) {
        pthread_rwlock_wrlock(&filter->lock);
    }
    
    for (uint64_t i = 0; i < count; i++) {
        if (keys[i] && key_lens[i] > 0) {
            // 直接调用内部函数，避免重复加锁
            uint64_t hashes[16];
            compute_hashes(filter, keys[i], key_lens[i], hashes);
            
            bool added = true;
            
            switch (filter->config.type) {
                case BLOOM_TYPE_STANDARD:
                case BLOOM_TYPE_AUTO: {
                    for (uint32_t j = 0; j < filter->hash_count; j++) {
                        size_t bit_index = hashes[j] % filter->bit_count;
                        set_bit(filter->standard.bits, bit_index);
                    }
                    break;
                }
                
                case BLOOM_TYPE_BLOCKED: {
                    uint32_t block_index = (uint32_t)(hashes[0] & filter->blocked.block_mask);
                    bloom_block_t *block = &filter->blocked.blocks[block_index];
                    
                    for (uint32_t j = 1; j < filter->hash_count; j++) {
                        size_t bit_index = hashes[j] % block->size_bits;
                        set_bit(block->bits, bit_index);
                    }
                    block->entry_count++;
                    break;
                }
                
                case BLOOM_TYPE_REGISTER_BLOCKED: {
                    uint32_t reg_index = (uint32_t)(hashes[0] & filter->register_blocked.register_mask);
                    uint64_t mask = 0;
                    
                    for (uint32_t j = 1; j < filter->hash_count; j++) {
                        uint32_t bit_pos = (uint32_t)(hashes[j] % 64);
                        mask |= (1ULL << bit_pos);
                    }
                    
                    filter->register_blocked.registers[reg_index] |= mask;
                    break;
                }
                
                // 其他类型的批量操作实现...
                default:
                    added = false;
                    break;
            }
            
            if (added) {
                successful_adds++;
                filter->current_entries++;
            }
        }
    }
    
    // 批量更新统计信息
    if (filter->config.enable_statistics && successful_adds > 0) {
        pthread_rwlock_wrlock(&filter->stats.stats_lock);
        filter->stats.total_insertions += successful_adds;
        gettimeofday(&filter->stats.last_update, NULL);
        pthread_rwlock_unlock(&filter->stats.stats_lock);
    }
    
    if (filter->thread_safe) {
        pthread_rwlock_unlock(&filter->lock);
    }
    
    return successful_adds;
}

uint64_t bloom_filter_query_batch_optimized(optimized_bloom_filter_t *filter,
                                           const void **keys, const size_t *key_lens, 
                                           bool *results, uint64_t count) {
    if (!filter || !keys || !key_lens || !results || count == 0) {
        return 0;
    }
    
    uint64_t queries_processed = 0;
    uint64_t total_hits = 0;
    
    // 线程安全 - 读锁用于批量查询
    if (filter->thread_safe) {
        pthread_rwlock_rdlock(&filter->lock);
    }
    
    for (uint64_t i = 0; i < count; i++) {
        if (keys[i] && key_lens[i] > 0) {
            uint64_t hashes[16];
            compute_hashes(filter, keys[i], key_lens[i], hashes);
            
            bool result = true;
            
            switch (filter->config.type) {
                case BLOOM_TYPE_STANDARD:
                case BLOOM_TYPE_AUTO: {
                    for (uint32_t j = 0; j < filter->hash_count && result; j++) {
                        size_t bit_index = hashes[j] % filter->bit_count;
                        if (!get_bit(filter->standard.bits, bit_index)) {
                            result = false;
                        }
                    }
                    break;
                }
                
                case BLOOM_TYPE_BLOCKED: {
                    uint32_t block_index = (uint32_t)(hashes[0] & filter->blocked.block_mask);
                    bloom_block_t *block = &filter->blocked.blocks[block_index];
                    
                    for (uint32_t j = 1; j < filter->hash_count && result; j++) {
                        size_t bit_index = hashes[j] % block->size_bits;
                        if (!get_bit(block->bits, bit_index)) {
                            result = false;
                        }
                    }
                    break;
                }
                
                case BLOOM_TYPE_REGISTER_BLOCKED: {
                    uint32_t reg_index = (uint32_t)(hashes[0] & filter->register_blocked.register_mask);
                    uint64_t mask = 0;
                    
                    for (uint32_t j = 1; j < filter->hash_count; j++) {
                        uint32_t bit_pos = (uint32_t)(hashes[j] % 64);
                        mask |= (1ULL << bit_pos);
                    }
                    
                    result = (filter->register_blocked.registers[reg_index] & mask) == mask;
                    break;
                }
                
                default:
                    result = false;
                    break;
            }
            
            results[i] = result;
            if (result) total_hits++;
            queries_processed++;
        } else {
            results[i] = false;
        }
    }
    
    // 批量更新统计信息
    if (filter->config.enable_statistics && queries_processed > 0) {
        pthread_rwlock_wrlock(&filter->stats.stats_lock);
        filter->stats.total_queries += queries_processed;
        filter->stats.total_hits += total_hits;
        filter->stats.total_misses += (queries_processed - total_hits);
        gettimeofday(&filter->stats.last_update, NULL);
        pthread_rwlock_unlock(&filter->stats.stats_lock);
    }
    
    if (filter->thread_safe) {
        pthread_rwlock_unlock(&filter->lock);
    }
    
    return queries_processed;
}

// === 计数布隆过滤器删除操作 ===

int bloom_filter_remove_optimized(optimized_bloom_filter_t *filter, const void *key, size_t key_len) {
    if (!filter || !key || key_len == 0) {
        return KV_ERR_PARAM;
    }
    
    // 只有计数布隆过滤器支持删除
    if (filter->config.type != BLOOM_TYPE_COUNTING) {
        return KV_ERR_UNSUPPORTED;
    }
    
    // 线程安全 - 写锁用于删除操作
    if (filter->thread_safe) {
        pthread_rwlock_wrlock(&filter->lock);
    }
    
    uint64_t hashes[16];
    compute_hashes(filter, key, key_len, hashes);
    
    // 首先检查是否存在
    bool exists = true;
    for (uint32_t i = 0; i < filter->hash_count && exists; i++) {
        size_t index = hashes[i] % filter->bit_count;
        if (filter->counting.counters[index] == 0) {
            exists = false;
        }
    }
    
    int result = KV_ERR_NONE;
    if (exists) {
        // 递减计数器
        for (uint32_t i = 0; i < filter->hash_count; i++) {
            size_t index = hashes[i] % filter->bit_count;
            if (filter->counting.counters[index] > 0) {
                filter->counting.counters[index]--;
            }
        }
        filter->current_entries--;
    } else {
        result = KV_ERR_NOT_FOUND;
    }
    
    if (filter->thread_safe) {
        pthread_rwlock_unlock(&filter->lock);
    }
    
    return result;
}

// === 统计信息管理 ===

int bloom_filter_get_stats_optimized(optimized_bloom_filter_t *filter, bloom_filter_stats_t *stats) {
    if (!filter || !stats) {
        return KV_ERR_PARAM;
    }
    
    if (!filter->config.enable_statistics) {
        return KV_ERR_UNSUPPORTED;
    }
    
    pthread_rwlock_rdlock(&filter->stats.stats_lock);
    memcpy(stats, &filter->stats, sizeof(bloom_filter_stats_t));
    
    // 计算动态统计信息
    stats->fill_ratio = (double)filter->current_entries / filter->max_entries;
    stats->bits_per_element = (double)filter->bit_count / filter->current_entries;
    
    if (stats->total_queries > 0) {
        stats->actual_fpr = (double)stats->false_positives / stats->total_queries;
    } else {
        stats->actual_fpr = 0.0;
    }
    
    // 计算内存使用量
    stats->memory_usage = sizeof(optimized_bloom_filter_t);
    switch (filter->config.type) {
        case BLOOM_TYPE_STANDARD:
        case BLOOM_TYPE_AUTO:
            stats->memory_usage += (filter->bit_count + 7) / 8;
            break;
        case BLOOM_TYPE_BLOCKED:
            stats->memory_usage += filter->blocked.block_count * 
                                  (sizeof(bloom_block_t) + filter->config.block_size / 8);
            break;
        case BLOOM_TYPE_REGISTER_BLOCKED:
            stats->memory_usage += filter->register_blocked.register_count * sizeof(uint64_t);
            break;
        case BLOOM_TYPE_COUNTING:
            stats->memory_usage += filter->bit_count * sizeof(uint32_t);
            break;
        case BLOOM_TYPE_SCALABLE: {
            scalable_bloom_layer_t *layer = filter->scalable.first_layer;
            while (layer) {
                stats->memory_usage += sizeof(scalable_bloom_layer_t) + 
                                     (layer->bit_count + 7) / 8;
                layer = layer->next;
            }
            break;
        }
    }
    stats->memory_usage += filter->hash_count * sizeof(uint32_t); // 哈希种子
    
    pthread_rwlock_unlock(&filter->stats.stats_lock);
    
    return KV_ERR_NONE;
}

int bloom_filter_reset_stats_optimized(optimized_bloom_filter_t *filter) {
    if (!filter) {
        return KV_ERR_PARAM;
    }
    
    if (!filter->config.enable_statistics) {
        return KV_ERR_UNSUPPORTED;
    }
    
    pthread_rwlock_wrlock(&filter->stats.stats_lock);
    
    filter->stats.total_insertions = 0;
    filter->stats.total_queries = 0;
    filter->stats.total_hits = 0;
    filter->stats.total_misses = 0;
    filter->stats.false_positives = 0;
    filter->stats.true_negatives = 0;
    filter->stats.actual_fpr = 0.0;
    gettimeofday(&filter->stats.last_update, NULL);
    
    pthread_rwlock_unlock(&filter->stats.stats_lock);
    
    return KV_ERR_NONE;
}

// === 序列化和反序列化 ===

typedef struct bloom_filter_header_s {
    uint32_t magic;                     // 魔数
    uint32_t version;                   // 版本号
    bloom_filter_config_t config;      // 配置信息
    size_t bit_count;                   // 位数组大小
    uint32_t hash_count;                // 哈希函数数量
    uint64_t max_entries;               // 最大条目数
    uint64_t current_entries;           // 当前条目数
    uint64_t salt;                      // 盐值
    uint64_t creation_time;             // 创建时间
    uint32_t data_size;                 // 数据大小
    uint32_t checksum;                  // 校验和
} bloom_filter_header_t;

#define BLOOM_FILTER_MAGIC    0x424C4F4D  // "BLOM"
#define BLOOM_FILTER_VERSION  1

static uint32_t calculate_checksum(const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t checksum = 0;
    for (size_t i = 0; i < size; i++) {
        checksum = checksum * 31 + bytes[i];
    }
    return checksum;
}

ssize_t bloom_filter_serialize_optimized(optimized_bloom_filter_t *filter, void *buffer, size_t buffer_size) {
    if (!filter || !buffer) {
        return -1;
    }
    
    // 计算所需大小
    size_t required_size = sizeof(bloom_filter_header_t);
    required_size += filter->hash_count * sizeof(uint32_t); // 哈希种子
    
    size_t data_size = 0;
    switch (filter->config.type) {
        case BLOOM_TYPE_STANDARD:
        case BLOOM_TYPE_AUTO:
            data_size = (filter->bit_count + 7) / 8;
            break;
        case BLOOM_TYPE_BLOCKED:
            data_size = filter->blocked.block_count * filter->config.block_size / 8;
            break;
        case BLOOM_TYPE_REGISTER_BLOCKED:
            data_size = filter->register_blocked.register_count * sizeof(uint64_t);
            break;
        case BLOOM_TYPE_COUNTING:
            data_size = filter->bit_count * sizeof(uint32_t);
            break;
        case BLOOM_TYPE_SCALABLE: {
            scalable_bloom_layer_t *layer = filter->scalable.first_layer;
            while (layer) {
                data_size += (layer->bit_count + 7) / 8;
                layer = layer->next;
            }
            break;
        }
    }
    
    required_size += data_size;
    
    if (buffer_size < required_size) {
        return required_size; // 返回所需大小
    }
    
    // 序列化头部
    bloom_filter_header_t *header = (bloom_filter_header_t *)buffer;
    header->magic = BLOOM_FILTER_MAGIC;
    header->version = BLOOM_FILTER_VERSION;
    memcpy(&header->config, &filter->config, sizeof(bloom_filter_config_t));
    header->bit_count = filter->bit_count;
    header->hash_count = filter->hash_count;
    header->max_entries = filter->max_entries;
    header->current_entries = filter->current_entries;
    header->salt = filter->salt;
    header->creation_time = filter->creation_time;
    header->data_size = data_size;
    
    uint8_t *data_ptr = (uint8_t *)buffer + sizeof(bloom_filter_header_t);
    
    // 序列化哈希种子
    memcpy(data_ptr, filter->hash_seeds, filter->hash_count * sizeof(uint32_t));
    data_ptr += filter->hash_count * sizeof(uint32_t);
    
    // 序列化数据
    switch (filter->config.type) {
        case BLOOM_TYPE_STANDARD:
        case BLOOM_TYPE_AUTO:
            memcpy(data_ptr, filter->standard.bits, data_size);
            break;
        case BLOOM_TYPE_BLOCKED:
            for (uint32_t i = 0; i < filter->blocked.block_count; i++) {
                size_t block_size = filter->config.block_size / 8;
                memcpy(data_ptr, filter->blocked.blocks[i].bits, block_size);
                data_ptr += block_size;
            }
            break;
        case BLOOM_TYPE_REGISTER_BLOCKED:
            memcpy(data_ptr, filter->register_blocked.registers, data_size);
            break;
        case BLOOM_TYPE_COUNTING:
            memcpy(data_ptr, filter->counting.counters, data_size);
            break;
        case BLOOM_TYPE_SCALABLE: {
            scalable_bloom_layer_t *layer = filter->scalable.first_layer;
            while (layer) {
                size_t layer_size = (layer->bit_count + 7) / 8;
                memcpy(data_ptr, layer->bits, layer_size);
                data_ptr += layer_size;
                layer = layer->next;
            }
            break;
        }
    }
    
    // 计算校验和
    header->checksum = calculate_checksum((uint8_t *)buffer + sizeof(bloom_filter_header_t), 
                                        required_size - sizeof(bloom_filter_header_t));
    
    return required_size;
}

optimized_bloom_filter_t* bloom_filter_deserialize_optimized(const void *buffer, size_t buffer_size) {
    if (!buffer || buffer_size < sizeof(bloom_filter_header_t)) {
        return NULL;
    }
    
    const bloom_filter_header_t *header = (const bloom_filter_header_t *)buffer;
    
    // 验证魔数和版本
    if (header->magic != BLOOM_FILTER_MAGIC || header->version != BLOOM_FILTER_VERSION) {
        return NULL;
    }
    
    // 验证大小
    size_t expected_size = sizeof(bloom_filter_header_t) + 
                          header->hash_count * sizeof(uint32_t) + 
                          header->data_size;
    if (buffer_size < expected_size) {
        return NULL;
    }
    
    // 验证校验和
    uint32_t calculated_checksum = calculate_checksum(
        (const uint8_t *)buffer + sizeof(bloom_filter_header_t),
        expected_size - sizeof(bloom_filter_header_t)
    );
    
    if (calculated_checksum != header->checksum) {
        return NULL;
    }
    
    // 创建新的过滤器
    optimized_bloom_filter_t *filter = bloom_filter_create_optimized(&header->config);
    if (!filter) {
        return NULL;
    }
    
    // 恢复基本信息
    filter->bit_count = header->bit_count;
    filter->hash_count = header->hash_count;
    filter->max_entries = header->max_entries;
    filter->current_entries = header->current_entries;
    filter->salt = header->salt;
    filter->creation_time = header->creation_time;
    
    const uint8_t *data_ptr = (const uint8_t *)buffer + sizeof(bloom_filter_header_t);
    
    // 恢复哈希种子
    memcpy(filter->hash_seeds, data_ptr, filter->hash_count * sizeof(uint32_t));
    data_ptr += filter->hash_count * sizeof(uint32_t);
    
    // 恢复数据
    switch (filter->config.type) {
        case BLOOM_TYPE_STANDARD:
        case BLOOM_TYPE_AUTO:
            memcpy(filter->standard.bits, data_ptr, header->data_size);
            break;
        case BLOOM_TYPE_REGISTER_BLOCKED:
            memcpy(filter->register_blocked.registers, data_ptr, header->data_size);
            break;
        case BLOOM_TYPE_COUNTING:
            memcpy(filter->counting.counters, data_ptr, header->data_size);
            break;
        // 其他类型的恢复逻辑...
        default:
            bloom_filter_destroy_optimized(filter);
            return NULL;
    }
    
    return filter;
}

// === 性能基准测试 ===

int bloom_filter_benchmark_optimized(optimized_bloom_filter_t *filter, uint64_t test_data_size) {
    if (!filter || test_data_size == 0) {
        return KV_ERR_PARAM;
    }
    
    printf("🚀 开始布隆过滤器性能基准测试\n");
    printf("==========================================\n");
    printf("过滤器类型: %d\n", filter->config.type);
    printf("哈希函数类型: %d\n", filter->config.hash_type);
    printf("测试数据量: %lu\n", test_data_size);
    printf("位数组大小: %zu bits\n", filter->bit_count);
    printf("哈希函数数量: %u\n", filter->hash_count);
    
    // 生成测试数据
    char **test_keys = kv_memory_allocate(test_data_size * sizeof(char*));
    if (!test_keys) {
        return KV_ERR_MEMORY;
    }
    
    for (uint64_t i = 0; i < test_data_size; i++) {
        test_keys[i] = kv_memory_allocate(32);
        if (!test_keys[i]) {
            // 清理已分配的内存
            for (uint64_t j = 0; j < i; j++) {
                kv_memory_free(test_keys[j]);
            }
            kv_memory_free(test_keys);
            return KV_ERR_MEMORY;
        }
        snprintf(test_keys[i], 32, "benchmark_key_%lu_%lu", i, filter->salt);
    }
    
    // 插入性能测试
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);
    
    for (uint64_t i = 0; i < test_data_size; i++) {
        bloom_filter_add_optimized(filter, test_keys[i], strlen(test_keys[i]));
    }
    
    gettimeofday(&end_time, NULL);
    double insert_time = (end_time.tv_sec - start_time.tv_sec) + 
                        (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    double insert_ops_per_sec = test_data_size / insert_time;
    
    printf("插入性能: %.2f 秒, %.0f ops/sec\n", insert_time, insert_ops_per_sec);
    
    // 查询性能测试(存在的键)
    gettimeofday(&start_time, NULL);
    
    uint64_t hits = 0;
    for (uint64_t i = 0; i < test_data_size; i++) {
        if (bloom_filter_may_contain_optimized(filter, test_keys[i], strlen(test_keys[i]))) {
            hits++;
        }
    }
    
    gettimeofday(&end_time, NULL);
    double query_time = (end_time.tv_sec - start_time.tv_sec) + 
                       (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    double query_ops_per_sec = test_data_size / query_time;
    
    printf("查询性能(存在): %.2f 秒, %.0f ops/sec, 命中率: %.2f%%\n", 
           query_time, query_ops_per_sec, (double)hits * 100.0 / test_data_size);
    
    // 查询性能测试(不存在的键)
    gettimeofday(&start_time, NULL);
    
    uint64_t false_hits = 0;
    for (uint64_t i = 0; i < test_data_size; i++) {
        char false_key[32];
        snprintf(false_key, sizeof(false_key), "false_key_%lu_%lu", i, filter->salt);
        if (bloom_filter_may_contain_optimized(filter, false_key, strlen(false_key))) {
            false_hits++;
        }
    }
    
    gettimeofday(&end_time, NULL);
    double false_query_time = (end_time.tv_sec - start_time.tv_sec) + 
                             (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
    double false_query_ops_per_sec = test_data_size / false_query_time;
    double actual_fpr = (double)false_hits / test_data_size;
    
    printf("查询性能(不存在): %.2f 秒, %.0f ops/sec, 误判率: %.4f%%\n", 
           false_query_time, false_query_ops_per_sec, actual_fpr * 100.0);
    
    // 内存使用情况
    bloom_filter_stats_t stats;
    if (bloom_filter_get_stats_optimized(filter, &stats) == KV_ERR_NONE) {
        printf("内存使用量: %lu bytes (%.2f MB)\n", 
               stats.memory_usage, stats.memory_usage / (1024.0 * 1024.0));
        printf("填充率: %.2f%%\n", stats.fill_ratio * 100.0);
        printf("每元素位数: %.2f bits\n", stats.bits_per_element);
    }
    
    // 清理测试数据
    for (uint64_t i = 0; i < test_data_size; i++) {
        kv_memory_free(test_keys[i]);
    }
    kv_memory_free(test_keys);
    
    printf("==========================================\n");
    printf("✅ 基准测试完成\n");
    
    return KV_ERR_NONE;
}

double bloom_filter_test_false_positive_rate(optimized_bloom_filter_t *filter, uint64_t test_size) {
    if (!filter || test_size == 0) {
        return -1.0;
    }
    
    uint64_t false_positives = 0;
    
    for (uint64_t i = 0; i < test_size; i++) {
        char test_key[32];
        snprintf(test_key, sizeof(test_key), "fpr_test_%lu_%lu", i, filter->salt + 12345);
        
        if (bloom_filter_may_contain_optimized(filter, test_key, strlen(test_key))) {
            false_positives++;
        }
    }
    
    return (double)false_positives / test_size;
}

// 分析内存使用情况
int bloom_filter_analyze_memory_usage(optimized_bloom_filter_t *filter, bool detailed) {
    if (!filter) {
        return KV_ERR_PARAM;
    }
    
    printf("📊 布隆过滤器内存使用分析\n");
    printf("================================\n");
    
    size_t total_memory = sizeof(optimized_bloom_filter_t);
    printf("基础结构: %zu bytes\n", sizeof(optimized_bloom_filter_t));
    
    // 哈希种子
    size_t hash_seed_memory = filter->hash_count * sizeof(uint32_t);
    total_memory += hash_seed_memory;
    printf("哈希种子: %zu bytes (%u seeds)\n", hash_seed_memory, filter->hash_count);
    
    // 数据结构
    size_t data_memory = 0;
    switch (filter->config.type) {
        case BLOOM_TYPE_STANDARD:
        case BLOOM_TYPE_AUTO: {
            data_memory = (filter->bit_count + 7) / 8;
            printf("位数组: %zu bytes (%zu bits)\n", data_memory, filter->bit_count);
            break;
        }
        
        case BLOOM_TYPE_BLOCKED: {
            size_t block_meta = filter->blocked.block_count * sizeof(bloom_block_t);
            size_t block_data = filter->blocked.block_count * filter->config.block_size / 8;
            data_memory = block_meta + block_data;
            printf("块元数据: %zu bytes (%u blocks)\n", block_meta, filter->blocked.block_count);
            printf("块数据: %zu bytes\n", block_data);
            break;
        }
        
        case BLOOM_TYPE_REGISTER_BLOCKED: {
            data_memory = filter->register_blocked.register_count * sizeof(uint64_t);
            printf("寄存器: %zu bytes (%u registers)\n", data_memory, filter->register_blocked.register_count);
            break;
        }
        
        case BLOOM_TYPE_COUNTING: {
            data_memory = filter->bit_count * sizeof(uint32_t);
            printf("计数器: %zu bytes (%zu counters)\n", data_memory, filter->bit_count);
            break;
        }
        
        case BLOOM_TYPE_SCALABLE: {
            scalable_bloom_layer_t *layer = filter->scalable.first_layer;
            uint32_t layer_num = 1;
            while (layer) {
                size_t layer_size = sizeof(scalable_bloom_layer_t) + (layer->bit_count + 7) / 8;
                data_memory += layer_size;
                if (detailed) {
                    printf("层 %u: %zu bytes (%zu bits, %lu/%lu entries)\n", 
                           layer_num, layer_size, layer->bit_count, 
                           layer->current_entries, layer->max_entries);
                }
                layer = layer->next;
                layer_num++;
            }
            if (!detailed) {
                printf("可扩展层: %zu bytes (%u layers)\n", data_memory, filter->scalable.layer_count);
            }
            break;
        }
    }
    
    total_memory += data_memory;
    
    printf("--------------------------------\n");
    printf("总内存: %zu bytes (%.2f KB, %.2f MB)\n", 
           total_memory, total_memory / 1024.0, total_memory / (1024.0 * 1024.0));
    printf("每元素内存: %.2f bytes\n", (double)total_memory / filter->current_entries);
    printf("内存效率: %.2f%% (数据/总计)\n", (double)data_memory * 100.0 / total_memory);
    
    return KV_ERR_NONE;
} 