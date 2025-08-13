/*
 * @Author: Lzww0608  
 * @Date: 2025-6-1 14:00:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-1 20:23:11
 * @Description: ConcordKV LSM-Tree SSTable 文件格式实现
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "lsm_sstable.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>

// === CRC32校验表 ===
static uint32_t crc32_table[256];
static bool crc32_table_initialized = false;
static pthread_mutex_t crc32_init_lock = PTHREAD_MUTEX_INITIALIZER;

// === 内部辅助函数声明 ===
static void init_crc32_table(void);
static uint32_t murmur3_hash(const void *key, size_t len, uint32_t seed);
static int serialize_entry(char *buffer, size_t buffer_size,
                          const char *key, size_t key_len,
                          const char *value, size_t value_len,
                          uint64_t seq_num, bool deleted);
static int deserialize_entry(const char *buffer, size_t buffer_size,
                            char **key, size_t *key_len,
                            char **value, size_t *value_len,
                            uint64_t *seq_num, bool *deleted);
static int serialize_index_entry(char *buffer, size_t buffer_size,
                                const sstable_index_entry_t *entry);
static int deserialize_index_entry(const char *buffer, size_t buffer_size,
                                  sstable_index_entry_t *entry);

// === CRC32校验实现 ===
static void init_crc32_table(void) {
    const uint32_t polynomial = 0xEDB88320;
    
    for (int i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 8; j > 0; j--) {
            if (crc & 1) {
                crc = (crc >> 1) ^ polynomial;
            } else {
                crc >>= 1;
            }
        }
        crc32_table[i] = crc;
    }
    crc32_table_initialized = true;
}

uint32_t sstable_crc32(const void *data, size_t size) {
    if (!crc32_table_initialized) {
        pthread_mutex_lock(&crc32_init_lock);
        if (!crc32_table_initialized) {
            init_crc32_table();
        }
        pthread_mutex_unlock(&crc32_init_lock);
    }
    
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *bytes = (const uint8_t *)data;
    
    for (size_t i = 0; i < size; i++) {
        crc = crc32_table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }
    
    return crc ^ 0xFFFFFFFF;
}

// === MurmurHash3实现 ===
static uint32_t murmur3_hash(const void *key, size_t len, uint32_t seed) {
    const uint8_t *data = (const uint8_t*)key;
    const int nblocks = len / 4;
    
    uint32_t h1 = seed;
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;
    
    // body
    const uint32_t *blocks = (const uint32_t *)(data + nblocks*4);
    
    for(int i = -nblocks; i; i++) {
        uint32_t k1 = blocks[i];
        
        k1 *= c1;
        k1 = (k1 << 15) | (k1 >> 17);
        k1 *= c2;
        
        h1 ^= k1;
        h1 = (h1 << 13) | (h1 >> 19);
        h1 = h1*5+0xe6546b64;
    }
    
    // tail
    const uint8_t *tail = (const uint8_t*)(data + nblocks*4);
    uint32_t k1 = 0;
    
    switch(len & 3) {
        case 3: k1 ^= tail[2] << 16;
                /* fallthrough */
        case 2: k1 ^= tail[1] << 8;
                /* fallthrough */
        case 1: k1 ^= tail[0];
            k1 *= c1; k1 = (k1 << 15) | (k1 >> 17); k1 *= c2; h1 ^= k1;
    };
    
    // finalization
    h1 ^= len;
    h1 ^= h1 >> 16;
    h1 *= 0x85ebca6b;
    h1 ^= h1 >> 13;
    h1 *= 0xc2b2ae35;
    h1 ^= h1 >> 16;
    
    return h1;
}

// === 序列化/反序列化实现 ===
static int serialize_entry(char *buffer, size_t buffer_size,
                          const char *key, size_t key_len,
                          const char *value, size_t value_len,
                          uint64_t seq_num, bool deleted) {
    // 条目格式：
    // [key_len: 4] [value_len: 4] [seq_num: 8] [deleted: 1] [key: key_len] [value: value_len]
    
    size_t required_size = 4 + 4 + 8 + 1 + key_len + value_len;
    if (buffer_size < required_size) {
        return KV_ERR_PARAM;
    }
    
    char *ptr = buffer;
    
    // 写入key_len
    *(uint32_t*)ptr = (uint32_t)key_len;
    ptr += 4;
    
    // 写入value_len
    *(uint32_t*)ptr = (uint32_t)value_len;
    ptr += 4;
    
    // 写入seq_num
    *(uint64_t*)ptr = seq_num;
    ptr += 8;
    
    // 写入deleted标记
    *ptr = deleted ? 1 : 0;
    ptr += 1;
    
    // 写入key
    if (key_len > 0) {
        memcpy(ptr, key, key_len);
        ptr += key_len;
    }
    
    // 写入value
    if (value_len > 0) {
        memcpy(ptr, value, value_len);
        ptr += value_len;
    }
    
    return (int)(ptr - buffer);
}

static int deserialize_entry(const char *buffer, size_t buffer_size,
                            char **key, size_t *key_len,
                            char **value, size_t *value_len,
                            uint64_t *seq_num, bool *deleted) {
    if (buffer_size < 17) {  // 最小头部大小
        return KV_ERR_PARAM;
    }
    
    const char *ptr = buffer;
    
    // 读取key_len
    uint32_t klen = *(const uint32_t*)ptr;
    ptr += 4;
    
    // 读取value_len
    uint32_t vlen = *(const uint32_t*)ptr;
    ptr += 4;
    
    // 读取seq_num
    *seq_num = *(const uint64_t*)ptr;
    ptr += 8;
    
    // 读取deleted标记
    *deleted = (*ptr != 0);
    ptr += 1;
    
    // 检查缓冲区大小
    if (buffer_size < 17 + klen + vlen) {
        return KV_ERR_PARAM;
    }
    
    // 读取key
    *key_len = klen;
    if (klen > 0) {
        *key = kv_store_malloc(klen + 1);
        if (!*key) {
            return KV_ERR_MEM;
        }
        memcpy(*key, ptr, klen);
        (*key)[klen] = '\0';
        ptr += klen;
    } else {
        *key = NULL;
    }
    
    // 读取value
    *value_len = vlen;
    if (vlen > 0) {
        *value = kv_store_malloc(vlen + 1);
        if (!*value) {
            if (*key) kv_store_free(*key);
            return KV_ERR_MEM;
        }
        memcpy(*value, ptr, vlen);
        (*value)[vlen] = '\0';
        ptr += vlen;
    } else {
        *value = NULL;
    }
    
    return (int)(ptr - buffer);
}

// === 数据块操作实现 ===
sstable_data_block_t* sstable_data_block_create(size_t capacity, sstable_compression_t compression) {
    if (capacity == 0) {
        return NULL;
    }
    
    sstable_data_block_t *block = kv_store_malloc(sizeof(sstable_data_block_t));
    if (!block) {
        KV_ERROR(KV_ERR_MEM, "无法分配数据块内存");
        return NULL;
    }
    
    memset(block, 0, sizeof(sstable_data_block_t));
    
    block->data = kv_store_malloc(capacity);
    if (!block->data) {
        kv_store_free(block);
        KV_ERROR(KV_ERR_MEM, "无法分配数据块缓冲区");
        return NULL;
    }
    
    block->capacity = capacity;
    block->compression = compression;
    block->header.block_type = SSTABLE_BLOCK_DATA;
    
    return block;
}

void sstable_data_block_destroy(sstable_data_block_t *block) {
    if (!block) return;
    
    if (block->data) {
        kv_store_free(block->data);
    }
    
    kv_store_free(block);
}

int sstable_data_block_add_entry(sstable_data_block_t *block,
                                  const char *key, size_t key_len,
                                  const char *value, size_t value_len,
                                  uint64_t seq_num, bool deleted) {
    if (!block || !key) {
        return KV_ERR_PARAM;
    }
    
    // 计算需要的空间
    size_t entry_size = 4 + 4 + 8 + 1 + key_len + value_len;
    
    if (block->size + entry_size > block->capacity) {
        return KV_ERR_PARAM;  // 块已满
    }
    
    // 序列化条目
    int serialized_size = serialize_entry(block->data + block->size, 
                                         block->capacity - block->size,
                                         key, key_len, value, value_len,
                                         seq_num, deleted);
    
    if (serialized_size < 0) {
        return serialized_size;
    }
    
    block->size += serialized_size;
    block->header.entry_count++;
    
    return KV_ERR_NONE;
}

int sstable_data_block_finalize(sstable_data_block_t *block) {
    if (!block) {
        return KV_ERR_PARAM;
    }
    
    // 更新头部信息
    block->header.uncompressed_size = (uint32_t)block->size;
    block->header.compressed_size = (uint32_t)block->size;  // 暂不压缩
    block->header.crc32 = sstable_crc32(block->data, block->size);
    
    // TODO: 实现压缩（LZ4/Snappy）
    
    return KV_ERR_NONE;
}

// === 索引块操作实现 ===
sstable_index_block_t* sstable_index_block_create(uint32_t capacity) {
    if (capacity == 0) {
        return NULL;
    }
    
    sstable_index_block_t *index_block = kv_store_malloc(sizeof(sstable_index_block_t));
    if (!index_block) {
        KV_ERROR(KV_ERR_MEM, "无法分配索引块内存");
        return NULL;
    }
    
    memset(index_block, 0, sizeof(sstable_index_block_t));
    
    index_block->entries = kv_store_malloc(sizeof(sstable_index_entry_t) * capacity);
    if (!index_block->entries) {
        kv_store_free(index_block);
        KV_ERROR(KV_ERR_MEM, "无法分配索引条目数组");
        return NULL;
    }
    
    memset(index_block->entries, 0, sizeof(sstable_index_entry_t) * capacity);
    index_block->capacity = capacity;
    index_block->header.block_type = SSTABLE_BLOCK_INDEX;
    
    return index_block;
}

void sstable_index_block_destroy(sstable_index_block_t *index_block) {
    if (!index_block) return;
    
    // 释放所有键
    for (uint32_t i = 0; i < index_block->entry_count; i++) {
        if (index_block->entries[i].key) {
            kv_store_free(index_block->entries[i].key);
        }
    }
    
    if (index_block->entries) {
        kv_store_free(index_block->entries);
    }
    
    kv_store_free(index_block);
}

int sstable_index_block_add_entry(sstable_index_block_t *index_block,
                                   const char *key, size_t key_len,
                                   uint64_t offset, uint32_t size, uint64_t seq_num) {
    if (!index_block || !key || index_block->entry_count >= index_block->capacity) {
        return KV_ERR_PARAM;
    }
    
    sstable_index_entry_t *entry = &index_block->entries[index_block->entry_count];
    
    // 复制键
    entry->key = kv_store_malloc(key_len + 1);
    if (!entry->key) {
        return KV_ERR_MEM;
    }
    
    memcpy(entry->key, key, key_len);
    entry->key[key_len] = '\0';
    entry->key_len = key_len;
    entry->offset = offset;
    entry->size = size;
    entry->seq_num = seq_num;
    
    index_block->entry_count++;
    
    return KV_ERR_NONE;
}

// === 布隆过滤器实现 ===
sstable_bloom_filter_t* sstable_bloom_filter_create(uint64_t expected_entries, uint32_t false_positive_rate) {
    if (expected_entries == 0 || false_positive_rate == 0) {
        return NULL;
    }
    
    sstable_bloom_filter_t *bloom = kv_store_malloc(sizeof(sstable_bloom_filter_t));
    if (!bloom) {
        KV_ERROR(KV_ERR_MEM, "无法分配布隆过滤器内存");
        return NULL;
    }
    
    // 计算位数组大小和哈希函数个数
    // false_positive_rate 是以万分之一为单位的
    double fp_rate = (double)false_positive_rate / 10000.0;
    
    // 最优位数组大小: m = -n * ln(p) / (ln(2)^2)
    double m = -(double)expected_entries * log(fp_rate) / (log(2) * log(2));
    bloom->bit_count = (size_t)ceil(m);
    
    // 最优哈希函数个数: k = (m / n) * ln(2)
    double k = (m / expected_entries) * log(2);
    bloom->hash_count = (uint32_t)round(k);
    
    // 确保至少有1个哈希函数
    if (bloom->hash_count == 0) {
        bloom->hash_count = 1;
    }
    
    // 分配位数组
    size_t byte_count = (bloom->bit_count + 7) / 8;
    bloom->bits = kv_store_malloc(byte_count);
    if (!bloom->bits) {
        kv_store_free(bloom);
        KV_ERROR(KV_ERR_MEM, "无法分配布隆过滤器位数组");
        return NULL;
    }
    
    memset(bloom->bits, 0, byte_count);
    bloom->false_positive_rate = false_positive_rate;
    
    return bloom;
}

void sstable_bloom_filter_destroy(sstable_bloom_filter_t *bloom) {
    if (!bloom) return;
    
    if (bloom->bits) {
        kv_store_free(bloom->bits);
    }
    
    kv_store_free(bloom);
}

void sstable_bloom_filter_add(sstable_bloom_filter_t *bloom, const char *key, size_t key_len) {
    if (!bloom || !key) return;
    
    for (uint32_t i = 0; i < bloom->hash_count; i++) {
        uint32_t hash = murmur3_hash(key, key_len, i);
        size_t bit_index = hash % bloom->bit_count;
        size_t byte_index = bit_index / 8;
        uint8_t bit_offset = bit_index % 8;
        
        bloom->bits[byte_index] |= (1 << bit_offset);
    }
}

bool sstable_bloom_filter_may_contain(sstable_bloom_filter_t *bloom, const char *key, size_t key_len) {
    if (!bloom || !key) return false;
    
    for (uint32_t i = 0; i < bloom->hash_count; i++) {
        uint32_t hash = murmur3_hash(key, key_len, i);
        size_t bit_index = hash % bloom->bit_count;
        size_t byte_index = bit_index / 8;
        uint8_t bit_offset = bit_index % 8;
        
        if (!(bloom->bits[byte_index] & (1 << bit_offset))) {
            return false;  // 肯定不存在
        }
    }
    
    return true;  // 可能存在
}

// === SSTable写入器实现 ===
sstable_writer_t* sstable_writer_create(const char *filename, sstable_compression_t compression) {
    if (!filename || strlen(filename) == 0) {
        KV_ERROR(KV_ERR_PARAM, "文件名不能为空");
        return NULL;
    }
    
    sstable_writer_t *writer = kv_store_malloc(sizeof(sstable_writer_t));
    if (!writer) {
        KV_ERROR(KV_ERR_MEM, "无法分配写入器内存");
        return NULL;
    }
    
    memset(writer, 0, sizeof(sstable_writer_t));
    
    // 复制文件名
    strncpy(writer->filename, filename, sizeof(writer->filename) - 1);
    writer->filename[sizeof(writer->filename) - 1] = '\0';
    
    // 打开文件
    writer->file = fopen(filename, "wb");
    if (!writer->file) {
        kv_store_free(writer);
        KV_ERROR(KV_ERR_FILE, "无法打开文件 %s: %s", filename, strerror(errno));
        return NULL;
    }
    
    // 初始化组件
    writer->current_block = sstable_data_block_create(SSTABLE_BLOCK_SIZE, compression);
    if (!writer->current_block) {
        fclose(writer->file);
        kv_store_free(writer);
        return NULL;
    }
    
    writer->index_block = sstable_index_block_create(1024);  // 预分配1024个索引条目
    if (!writer->index_block) {
        sstable_data_block_destroy(writer->current_block);
        fclose(writer->file);
        kv_store_free(writer);
        return NULL;
    }
    
    writer->bloom_filter = sstable_bloom_filter_create(10000, 100);  // 1%误判率
    if (!writer->bloom_filter) {
        sstable_index_block_destroy(writer->index_block);
        sstable_data_block_destroy(writer->current_block);
        fclose(writer->file);
        kv_store_free(writer);
        return NULL;
    }
    
    writer->compression = compression;
    writer->min_seq_num = UINT64_MAX;
    writer->max_seq_num = 0;
    
    // 初始化锁
    if (pthread_mutex_init(&writer->lock, NULL) != 0) {
        sstable_bloom_filter_destroy(writer->bloom_filter);
        sstable_index_block_destroy(writer->index_block);
        sstable_data_block_destroy(writer->current_block);
        fclose(writer->file);
        kv_store_free(writer);
        KV_ERROR(KV_ERR_LOCK, "无法初始化写入器锁");
        return NULL;
    }
    
    return writer;
}

void sstable_writer_destroy(sstable_writer_t *writer) {
    if (!writer) return;
    
    if (writer->file) {
        fclose(writer->file);
    }
    
    if (writer->current_block) {
        sstable_data_block_destroy(writer->current_block);
    }
    
    if (writer->index_block) {
        sstable_index_block_destroy(writer->index_block);
    }
    
    if (writer->bloom_filter) {
        sstable_bloom_filter_destroy(writer->bloom_filter);
    }
    
    pthread_mutex_destroy(&writer->lock);
    kv_store_free(writer);
}

int sstable_writer_put(sstable_writer_t *writer,
                       const char *key, size_t key_len,
                       const char *value, size_t value_len,
                       uint64_t seq_num, bool deleted) {
    if (!writer || !key || writer->finalized) {
        return KV_ERR_PARAM;
    }
    
    pthread_mutex_lock(&writer->lock);
    
    // 检查当前块是否需要刷新
    size_t entry_size = 4 + 4 + 8 + 1 + key_len + value_len;
    if (writer->current_block->size + entry_size > writer->current_block->capacity) {
        // 需要刷新当前块
        int ret = sstable_data_block_finalize(writer->current_block);
        if (ret != KV_ERR_NONE) {
            pthread_mutex_unlock(&writer->lock);
            return ret;
        }
        
        // 写入数据块头部
        if (fwrite(&writer->current_block->header, sizeof(sstable_block_header_t), 1, writer->file) != 1) {
            pthread_mutex_unlock(&writer->lock);
            return KV_ERR_FILE;
        }
        
        // 写入数据块内容
        if (fwrite(writer->current_block->data, writer->current_block->size, 1, writer->file) != 1) {
            pthread_mutex_unlock(&writer->lock);
            return KV_ERR_FILE;
        }
        
        // 添加索引条目（每个数据块添加一次索引）
        // 获取第一个键作为索引键
        char *first_key = NULL;
        size_t first_key_len = 0;
        char *dummy_value = NULL;
        size_t dummy_value_len = 0;
        uint64_t dummy_seq_num = 0;
        bool dummy_deleted = false;
        
        if (deserialize_entry(writer->current_block->data, writer->current_block->size,
                            &first_key, &first_key_len, &dummy_value, &dummy_value_len,
                            &dummy_seq_num, &dummy_deleted) > 0) {
            
            ret = sstable_index_block_add_entry(writer->index_block, 
                                               first_key, first_key_len,
                                               writer->file_offset, 
                                               sizeof(sstable_block_header_t) + writer->current_block->size,
                                               dummy_seq_num);
            
            if (first_key) kv_store_free(first_key);
            if (dummy_value) kv_store_free(dummy_value);
            
            if (ret != KV_ERR_NONE) {
                pthread_mutex_unlock(&writer->lock);
                return ret;
            }
        }
        
        // 更新偏移量
        writer->file_offset += sizeof(sstable_block_header_t) + writer->current_block->size;
        writer->stats.block_count++;
        
        // 创建新的数据块
        sstable_data_block_destroy(writer->current_block);
        writer->current_block = sstable_data_block_create(SSTABLE_BLOCK_SIZE, writer->compression);
        if (!writer->current_block) {
            pthread_mutex_unlock(&writer->lock);
            return KV_ERR_MEM;
        }
    }
    
    // 添加条目到当前块
    int ret = sstable_data_block_add_entry(writer->current_block, key, key_len, 
                                          value, value_len, seq_num, deleted);
    if (ret != KV_ERR_NONE) {
        pthread_mutex_unlock(&writer->lock);
        return ret;
    }
    
    // 添加到布隆过滤器
    sstable_bloom_filter_add(writer->bloom_filter, key, key_len);
    
    // 更新统计信息
    writer->entry_count++;
    if (seq_num < writer->min_seq_num) {
        writer->min_seq_num = seq_num;
    }
    if (seq_num > writer->max_seq_num) {
        writer->max_seq_num = seq_num;
    }
    
    pthread_mutex_unlock(&writer->lock);
    return KV_ERR_NONE;
}

int sstable_writer_finalize(sstable_writer_t *writer) {
    if (!writer || writer->finalized) {
        return KV_ERR_PARAM;
    }
    
    pthread_mutex_lock(&writer->lock);
    
    // 刷新最后一个数据块
    if (writer->current_block->header.entry_count > 0) {
        int ret = sstable_data_block_finalize(writer->current_block);
        if (ret != KV_ERR_NONE) {
            pthread_mutex_unlock(&writer->lock);
            return ret;
        }
        
        // 为最后一个数据块添加索引条目
        char *first_key = NULL;
        size_t first_key_len = 0;
        char *dummy_value = NULL;
        size_t dummy_value_len = 0;
        uint64_t dummy_seq_num = 0;
        bool dummy_deleted = false;
        
        if (deserialize_entry(writer->current_block->data, writer->current_block->size,
                            &first_key, &first_key_len, &dummy_value, &dummy_value_len,
                            &dummy_seq_num, &dummy_deleted) > 0) {
            
            ret = sstable_index_block_add_entry(writer->index_block, 
                                               first_key, first_key_len,
                                               writer->file_offset, 
                                               sizeof(sstable_block_header_t) + writer->current_block->size,
                                               dummy_seq_num);
            
            if (first_key) kv_store_free(first_key);
            if (dummy_value) kv_store_free(dummy_value);
            
            if (ret != KV_ERR_NONE) {
                pthread_mutex_unlock(&writer->lock);
                return ret;
            }
        }
        
        // 写入数据块
        if (fwrite(&writer->current_block->header, sizeof(sstable_block_header_t), 1, writer->file) != 1 ||
            fwrite(writer->current_block->data, writer->current_block->size, 1, writer->file) != 1) {
            pthread_mutex_unlock(&writer->lock);
            return KV_ERR_FILE;
        }
        
        writer->file_offset += sizeof(sstable_block_header_t) + writer->current_block->size;
        writer->stats.block_count++;
    }
    
    // 写入索引块
    uint64_t index_offset = writer->file_offset;
    
    // 计算序列化后的索引块大小
    size_t serialized_size = 0;
    for (uint32_t i = 0; i < writer->index_block->entry_count; i++) {
        sstable_index_entry_t *entry = &writer->index_block->entries[i];
        serialized_size += 4 + 8 + 4 + 8 + entry->key_len;  // key_len + offset + size + seq_num + key
    }
    
    // 处理空索引块的情况
    if (serialized_size == 0) {
        serialized_size = 1;  // 至少分配1字节
    }
    
    // 分配序列化缓冲区
    char *index_buffer = kv_store_malloc(serialized_size);
    if (!index_buffer) {
        pthread_mutex_unlock(&writer->lock);
        return KV_ERR_MEM;
    }
    
    // 序列化所有索引条目
    size_t buffer_offset = 0;
    for (uint32_t i = 0; i < writer->index_block->entry_count; i++) {
        sstable_index_entry_t *entry = &writer->index_block->entries[i];
        int bytes_written = serialize_index_entry(index_buffer + buffer_offset, 
                                                 serialized_size - buffer_offset, entry);
        if (bytes_written < 0) {
            kv_store_free(index_buffer);
            pthread_mutex_unlock(&writer->lock);
            return KV_ERR_MEM;
        }
        buffer_offset += bytes_written;
    }
    
    // 如果没有索引条目，设置实际大小为0
    if (writer->index_block->entry_count == 0) {
        buffer_offset = 0;
    }
    
    sstable_block_header_t index_header = {
        .block_type = SSTABLE_BLOCK_INDEX,
        .entry_count = writer->index_block->entry_count,
        .uncompressed_size = (uint32_t)buffer_offset,
        .compressed_size = (uint32_t)buffer_offset,
        .crc32 = sstable_crc32(index_buffer, buffer_offset)
    };
    
    if (fwrite(&index_header, sizeof(sstable_block_header_t), 1, writer->file) != 1 ||
        (buffer_offset > 0 && fwrite(index_buffer, buffer_offset, 1, writer->file) != 1)) {
        kv_store_free(index_buffer);
        pthread_mutex_unlock(&writer->lock);
        return KV_ERR_FILE;
    }
    
    kv_store_free(index_buffer);
    writer->file_offset += sizeof(sstable_block_header_t) + buffer_offset;
    
    // 写入布隆过滤器
    uint64_t bloom_offset = writer->file_offset;
    size_t bloom_size = (writer->bloom_filter->bit_count + 7) / 8;
    sstable_block_header_t bloom_header = {
        .block_type = SSTABLE_BLOCK_BLOOM,
        .entry_count = writer->bloom_filter->hash_count,
        .uncompressed_size = (uint32_t)bloom_size,
        .compressed_size = (uint32_t)bloom_size,
        .crc32 = sstable_crc32(writer->bloom_filter->bits, bloom_size),
        .reserved = (uint32_t)writer->bloom_filter->bit_count  // 保存原始bit_count
    };
    
    if (fwrite(&bloom_header, sizeof(sstable_block_header_t), 1, writer->file) != 1 ||
        fwrite(writer->bloom_filter->bits, bloom_size, 1, writer->file) != 1) {
        pthread_mutex_unlock(&writer->lock);
        return KV_ERR_FILE;
    }
    
    writer->file_offset += sizeof(sstable_block_header_t) + bloom_size;
    
    // 写入Footer
    sstable_footer_t footer = {
        .magic_number = SSTABLE_MAGIC_NUMBER,
        .version = SSTABLE_VERSION,
        .index_offset = index_offset,
        .index_size = (uint32_t)(sizeof(sstable_block_header_t) + index_header.uncompressed_size),
        .bloom_offset = bloom_offset,
        .bloom_size = (uint32_t)(sizeof(sstable_block_header_t) + bloom_size),
        .min_seq_num = writer->min_seq_num,
        .max_seq_num = writer->max_seq_num,
        .entry_count = writer->entry_count,
        .crc32 = 0  // 先设置为0
    };
    
    // 计算除CRC32字段外的所有字段的CRC32
    footer.crc32 = sstable_crc32(&footer, sizeof(sstable_footer_t) - sizeof(uint32_t));
    
    if (fwrite(&footer, sizeof(sstable_footer_t), 1, writer->file) != 1) {
        pthread_mutex_unlock(&writer->lock);
        return KV_ERR_FILE;
    }
    
    // 刷新文件缓冲区
    if (fflush(writer->file) != 0) {
        pthread_mutex_unlock(&writer->lock);
        return KV_ERR_FILE;
    }
    
    // 更新统计信息
    writer->stats.total_entries = writer->entry_count;
    writer->stats.file_size = writer->file_offset + sizeof(sstable_footer_t);
    writer->stats.compression = writer->compression;
    
    writer->finalized = true;
    
    pthread_mutex_unlock(&writer->lock);
    return KV_ERR_NONE;
}

// === 工具函数实现 ===
int sstable_format_filename(char *buffer, size_t buffer_size, 
                            const char *dir, int level, uint64_t file_id) {
    if (!buffer || !dir) {
        return KV_ERR_PARAM;
    }
    
    int ret = snprintf(buffer, buffer_size, "%s/level-%d-%06lu.sst", dir, level, file_id);
    if (ret < 0 || (size_t)ret >= buffer_size) {
        return KV_ERR_PARAM;
    }
    
    return KV_ERR_NONE;
}

// === 从MemTable创建SSTable ===
int sstable_create_from_memtable(lsm_memtable_t *memtable, 
                                 const char *filename,
                                 sstable_compression_t compression) {
    if (!memtable || !filename) {
        return KV_ERR_PARAM;
    }
    
    // 创建写入器
    sstable_writer_t *writer = sstable_writer_create(filename, compression);
    if (!writer) {
        return KV_ERR_MEM;
    }
    
    // 创建MemTable迭代器
    lsm_memtable_iterator_t *iter = lsm_memtable_iterator_create(memtable, true);
    if (!iter) {
        sstable_writer_destroy(writer);
        return KV_ERR_MEM;
    }
    
    // 遍历MemTable并写入SSTable
    int ret = lsm_memtable_iterator_seek_first(iter);
    while (ret == KV_ERR_NONE && lsm_memtable_iterator_valid(iter)) {
        const char *key, *value;
        size_t key_len, value_len;
        uint64_t seq_num;
        bool deleted;
        
        ret = lsm_memtable_iterator_current(iter, &key, &key_len, &value, &value_len, 
                                          &seq_num, &deleted);
        if (ret != KV_ERR_NONE) {
            break;
        }
        
        ret = sstable_writer_put(writer, key, key_len, value, value_len, seq_num, deleted);
        if (ret != KV_ERR_NONE) {
            break;
        }
        
        ret = lsm_memtable_iterator_next(iter);
    }
    
    // 完成写入
    if (ret == KV_ERR_NONE) {
        ret = sstable_writer_finalize(writer);
    }
    
    // 清理资源
    lsm_memtable_iterator_destroy(iter);
    sstable_writer_destroy(writer);
    
    return ret;
}

// === SSTable读取器实现 ===
sstable_reader_t* sstable_reader_create(const char *filename) {
    if (!filename) {
        KV_ERROR(KV_ERR_PARAM, "文件名不能为空");
        return NULL;
    }
    
    sstable_reader_t *reader = kv_store_malloc(sizeof(sstable_reader_t));
    if (!reader) {
        KV_ERROR(KV_ERR_MEM, "无法分配读取器内存");
        return NULL;
    }
    
    memset(reader, 0, sizeof(sstable_reader_t));
    
    // 复制文件名
    strncpy(reader->filename, filename, sizeof(reader->filename) - 1);
    reader->filename[sizeof(reader->filename) - 1] = '\0';
    
    // 打开文件进行读取
    reader->file = fopen(filename, "rb");
    if (!reader->file) {
        kv_store_free(reader);
        KV_ERROR(KV_ERR_FILE, "无法打开文件 %s: %s", filename, strerror(errno));
        return NULL;
    }
    
    // 读取Footer
    if (fseek(reader->file, -sizeof(sstable_footer_t), SEEK_END) != 0) {
        fclose(reader->file);
        kv_store_free(reader);
        KV_ERROR(KV_ERR_FILE, "无法定位到Footer位置");
        return NULL;
    }
    
    if (fread(&reader->footer, sizeof(sstable_footer_t), 1, reader->file) != 1) {
        fclose(reader->file);
        kv_store_free(reader);
        KV_ERROR(KV_ERR_FILE, "无法读取Footer");
        return NULL;
    }
    
    // 验证Footer
    uint32_t footer_crc = sstable_crc32(&reader->footer, sizeof(sstable_footer_t) - sizeof(uint32_t));
    
    // 添加调试信息
    if (reader->footer.crc32 != footer_crc) {
        fclose(reader->file);
        kv_store_free(reader);
        KV_ERROR(KV_ERR_FILE, "Footer CRC32不匹配 - 期望: 0x%08X, 实际: 0x%08X", 
                footer_crc, reader->footer.crc32);
        return NULL;
    }
    
    if (reader->footer.magic_number != SSTABLE_MAGIC_NUMBER) {
        fclose(reader->file);
        kv_store_free(reader);
        KV_ERROR(KV_ERR_FILE, "Magic Number不匹配 - 期望: 0x%08X, 实际: 0x%08X", 
                SSTABLE_MAGIC_NUMBER, reader->footer.magic_number);
        return NULL;
    }
    
    if (reader->footer.version != SSTABLE_VERSION) {
        fclose(reader->file);
        kv_store_free(reader);
        KV_ERROR(KV_ERR_FILE, "版本号不匹配 - 期望: %u, 实际: %u", 
                SSTABLE_VERSION, reader->footer.version);
        return NULL;
    }
    
    // 读取索引块
    if (fseek(reader->file, reader->footer.index_offset, SEEK_SET) != 0) {
        fclose(reader->file);
        kv_store_free(reader);
        KV_ERROR(KV_ERR_FILE, "无法定位到索引块");
        return NULL;
    }
    
    sstable_block_header_t index_header;
    if (fread(&index_header, sizeof(sstable_block_header_t), 1, reader->file) != 1) {
        fclose(reader->file);
        kv_store_free(reader);
        KV_ERROR(KV_ERR_FILE, "无法读取索引块头");
        return NULL;
    }
    
    reader->index_block = sstable_index_block_create(index_header.entry_count);
    if (!reader->index_block) {
        fclose(reader->file);
        kv_store_free(reader);
        return NULL;
    }
    
    // 读取序列化的索引数据
    char *index_buffer = kv_store_malloc(index_header.uncompressed_size);
    if (!index_buffer) {
        sstable_index_block_destroy(reader->index_block);
        fclose(reader->file);
        kv_store_free(reader);
        return NULL;
    }
    
    if (fread(index_buffer, index_header.uncompressed_size, 1, reader->file) != 1) {
        kv_store_free(index_buffer);
        sstable_index_block_destroy(reader->index_block);
        fclose(reader->file);
        kv_store_free(reader);
        KV_ERROR(KV_ERR_FILE, "无法读取索引数据");
        return NULL;
    }
    
    // 验证索引块校验和
    uint32_t expected_crc = sstable_crc32(index_buffer, index_header.uncompressed_size);
    if (index_header.crc32 != expected_crc) {
        kv_store_free(index_buffer);
        sstable_index_block_destroy(reader->index_block);
        fclose(reader->file);
        kv_store_free(reader);
        KV_ERROR(KV_ERR_FILE, "索引块校验和错误");
        return NULL;
    }
    
    // 反序列化索引条目
    size_t buffer_offset = 0;
    for (uint32_t i = 0; i < index_header.entry_count; i++) {
        sstable_index_entry_t *entry = &reader->index_block->entries[i];
        int bytes_read = deserialize_index_entry(index_buffer + buffer_offset,
                                                index_header.uncompressed_size - buffer_offset,
                                                entry);
        if (bytes_read < 0) {
            // 清理已分配的键
            for (uint32_t j = 0; j < i; j++) {
                if (reader->index_block->entries[j].key) {
                    kv_store_free(reader->index_block->entries[j].key);
                }
            }
            kv_store_free(index_buffer);
            sstable_index_block_destroy(reader->index_block);
            fclose(reader->file);
            kv_store_free(reader);
            KV_ERROR(KV_ERR_FILE, "无法反序列化索引条目");
            return NULL;
        }
        buffer_offset += bytes_read;
    }
    
    kv_store_free(index_buffer);
    reader->index_block->entry_count = index_header.entry_count;
    
    // 读取布隆过滤器
    if (fseek(reader->file, reader->footer.bloom_offset, SEEK_SET) != 0) {
        sstable_index_block_destroy(reader->index_block);
        fclose(reader->file);
        kv_store_free(reader);
        KV_ERROR(KV_ERR_FILE, "无法定位到布隆过滤器");
        return NULL;
    }
    
    sstable_block_header_t bloom_header;
    if (fread(&bloom_header, sizeof(sstable_block_header_t), 1, reader->file) != 1) {
        sstable_index_block_destroy(reader->index_block);
        fclose(reader->file);
        kv_store_free(reader);
        KV_ERROR(KV_ERR_FILE, "无法读取布隆过滤器头");
        return NULL;
    }
    
    reader->bloom_filter = kv_store_malloc(sizeof(sstable_bloom_filter_t));
    if (!reader->bloom_filter) {
        sstable_index_block_destroy(reader->index_block);
        fclose(reader->file);
        kv_store_free(reader);
        return NULL;
    }
    
    reader->bloom_filter->bit_count = bloom_header.reserved;  // 从reserved字段读取原始bit_count
    reader->bloom_filter->hash_count = bloom_header.entry_count;
    reader->bloom_filter->bits = kv_store_malloc(bloom_header.uncompressed_size);
    if (!reader->bloom_filter->bits) {
        kv_store_free(reader->bloom_filter);
        sstable_index_block_destroy(reader->index_block);
        fclose(reader->file);
        kv_store_free(reader);
        return NULL;
    }
    
    if (fread(reader->bloom_filter->bits, bloom_header.uncompressed_size, 1, reader->file) != 1) {
        kv_store_free(reader->bloom_filter->bits);
        kv_store_free(reader->bloom_filter);
        sstable_index_block_destroy(reader->index_block);
        fclose(reader->file);
        kv_store_free(reader);
        KV_ERROR(KV_ERR_FILE, "无法读取布隆过滤器数据");
        return NULL;
    }
    
    // 初始化读写锁
    if (pthread_rwlock_init(&reader->lock, NULL) != 0) {
        kv_store_free(reader->bloom_filter->bits);
        kv_store_free(reader->bloom_filter);
        sstable_index_block_destroy(reader->index_block);
        fclose(reader->file);
        kv_store_free(reader);
        KV_ERROR(KV_ERR_LOCK, "无法初始化读取器锁");
        return NULL;
    }
    
    // 初始化统计信息
    reader->stats.total_entries = reader->footer.entry_count;
    reader->stats.file_size = ftell(reader->file) + sizeof(sstable_footer_t);
    reader->stats.compression = SSTABLE_COMPRESSION_NONE;  // 暂时没有压缩
    
    return reader;
}

void sstable_reader_destroy(sstable_reader_t *reader) {
    if (!reader) return;
    
    if (reader->file) {
        fclose(reader->file);
    }
    
    if (reader->index_block) {
        sstable_index_block_destroy(reader->index_block);
    }
    
    if (reader->bloom_filter) {
        if (reader->bloom_filter->bits) {
            kv_store_free(reader->bloom_filter->bits);
        }
        kv_store_free(reader->bloom_filter);
    }
    
    pthread_rwlock_destroy(&reader->lock);
    kv_store_free(reader);
}

int sstable_reader_get(sstable_reader_t *reader,
                       const char *key, size_t key_len,
                       char **value, size_t *value_len,
                       uint64_t *seq_num, bool *deleted) {
    if (!reader || !key || key_len == 0) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_rdlock(&reader->lock);
    
    // 首先检查布隆过滤器
    if (!sstable_bloom_filter_may_contain(reader->bloom_filter, key, key_len)) {
        pthread_rwlock_unlock(&reader->lock);
        return KV_ERR_NOT_FOUND;  // 肯定不存在
    }
    
    // 在索引中查找合适的数据块
    int block_index = -1;
    for (uint32_t i = 0; i < reader->index_block->entry_count; i++) {
        sstable_index_entry_t *entry = &reader->index_block->entries[i];
        int cmp = lsm_memtable_key_compare(key, key_len, entry->key, entry->key_len);
        if (cmp <= 0) {
            block_index = i;
            break;
        }
    }
    
    if (block_index == -1) {
        // 键比所有索引键都大，可能在最后一个块中
        if (reader->index_block->entry_count > 0) {
            block_index = reader->index_block->entry_count - 1;
        } else {
            pthread_rwlock_unlock(&reader->lock);
            return KV_ERR_NOT_FOUND;
        }
    }
    
    // 读取数据块
    sstable_index_entry_t *index_entry = &reader->index_block->entries[block_index];
    
    if (fseek(reader->file, index_entry->offset, SEEK_SET) != 0) {
        pthread_rwlock_unlock(&reader->lock);
        return KV_ERR_FILE;
    }
    
    sstable_block_header_t block_header;
    if (fread(&block_header, sizeof(sstable_block_header_t), 1, reader->file) != 1) {
        pthread_rwlock_unlock(&reader->lock);
        return KV_ERR_FILE;
    }
    
    char *block_data = kv_store_malloc(block_header.uncompressed_size);
    if (!block_data) {
        pthread_rwlock_unlock(&reader->lock);
        return KV_ERR_MEM;
    }
    
    if (fread(block_data, block_header.uncompressed_size, 1, reader->file) != 1) {
        kv_store_free(block_data);
        pthread_rwlock_unlock(&reader->lock);
        return KV_ERR_FILE;
    }
    
    // 验证数据块校验和
    uint32_t expected_crc = sstable_crc32(block_data, block_header.uncompressed_size);
    if (block_header.crc32 != expected_crc) {
        kv_store_free(block_data);
        pthread_rwlock_unlock(&reader->lock);
        return KV_ERR_FILE;
    }
    
    // 在数据块中查找键
    size_t offset = 0;
    for (uint32_t i = 0; i < block_header.entry_count; i++) {
        char *entry_key, *entry_value;
        size_t entry_key_len, entry_value_len;
        uint64_t entry_seq_num;
        bool entry_deleted;
        
        int bytes_read = deserialize_entry(block_data + offset, 
                                         block_header.uncompressed_size - offset,
                                         &entry_key, &entry_key_len,
                                         &entry_value, &entry_value_len,
                                         &entry_seq_num, &entry_deleted);
        
        if (bytes_read < 0) {
            kv_store_free(block_data);
            pthread_rwlock_unlock(&reader->lock);
            return KV_ERR_FILE;
        }
        
        int cmp = lsm_memtable_key_compare(key, key_len, entry_key, entry_key_len);
        
        if (cmp == 0) {
            // 找到了键
            if (value && entry_value) {
                *value = kv_store_malloc(entry_value_len + 1);
                if (*value) {
                    memcpy(*value, entry_value, entry_value_len);
                    (*value)[entry_value_len] = '\0';
                }
            }
            if (value_len) *value_len = entry_value_len;
            if (seq_num) *seq_num = entry_seq_num;
            if (deleted) *deleted = entry_deleted;
            
            // 清理临时内存
            if (entry_key) kv_store_free(entry_key);
            if (entry_value) kv_store_free(entry_value);
            kv_store_free(block_data);
            pthread_rwlock_unlock(&reader->lock);
            return KV_ERR_NONE;
        }
        
        // 清理当前条目的内存
        if (entry_key) kv_store_free(entry_key);
        if (entry_value) kv_store_free(entry_value);
        
        if (cmp < 0) {
            // 键比当前条目小，不会在后面找到
            break;
        }
        
        offset += bytes_read;
    }
    
    kv_store_free(block_data);
    pthread_rwlock_unlock(&reader->lock);
    return KV_ERR_NOT_FOUND;
}

bool sstable_reader_may_contain(sstable_reader_t *reader, const char *key, size_t key_len) {
    if (!reader || !key) return false;
    
    pthread_rwlock_rdlock(&reader->lock);
    bool result = sstable_bloom_filter_may_contain(reader->bloom_filter, key, key_len);
    pthread_rwlock_unlock(&reader->lock);
    
    return result;
}

int sstable_reader_get_stats(sstable_reader_t *reader, sstable_stats_t *stats) {
    if (!reader || !stats) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_rdlock(&reader->lock);
    *stats = reader->stats;
    pthread_rwlock_unlock(&reader->lock);
    
    return KV_ERR_NONE;
}

// === SSTable迭代器实现 ===
sstable_iterator_t* sstable_iterator_create(sstable_reader_t *reader, bool include_deleted) {
    if (!reader) {
        return NULL;
    }
    
    sstable_iterator_t *iter = kv_store_malloc(sizeof(sstable_iterator_t));
    if (!iter) {
        KV_ERROR(KV_ERR_MEM, "无法分配迭代器内存");
        return NULL;
    }
    
    iter->reader = reader;
    iter->block_index = 0;
    iter->entry_index = 0;
    iter->current_block = NULL;
    iter->valid = false;
    iter->include_deleted = include_deleted;
    
    return iter;
}

void sstable_iterator_destroy(sstable_iterator_t *iter) {
    if (!iter) return;
    
    if (iter->current_block) {
        sstable_data_block_destroy(iter->current_block);
    }
    
    kv_store_free(iter);
}

int sstable_iterator_seek_first(sstable_iterator_t *iter) {
    if (!iter || !iter->reader) {
        return KV_ERR_PARAM;
    }
    
    iter->block_index = 0;
    iter->entry_index = 0;
    
    if (iter->current_block) {
        sstable_data_block_destroy(iter->current_block);
        iter->current_block = NULL;
    }
    
    if (iter->reader->index_block->entry_count == 0) {
        iter->valid = false;
        return KV_ERR_NONE;
    }
    
    // 加载第一个数据块
    // TODO: 实现数据块加载逻辑
    iter->valid = true;
    
    return KV_ERR_NONE;
}

int sstable_iterator_next(sstable_iterator_t *iter) {
    if (!iter || !iter->valid) {
        return KV_ERR_PARAM;
    }
    
    // TODO: 实现迭代器移动逻辑
    iter->entry_index++;
    
    return KV_ERR_NONE;
}

bool sstable_iterator_valid(sstable_iterator_t *iter) {
    if (!iter) return false;
    return iter->valid;
}

int sstable_iterator_current(sstable_iterator_t *iter,
                             const char **key, size_t *key_len,
                             const char **value, size_t *value_len,
                             uint64_t *seq_num, bool *deleted) {
    if (!iter || !iter->valid) {
        return KV_ERR_PARAM;
    }
    
    // TODO: 实现当前条目获取逻辑
    (void)key; (void)key_len; (void)value; (void)value_len; (void)seq_num; (void)deleted;
    
    return KV_ERR_NONE;
}

// === 索引条目序列化函数实现 ===
static int serialize_index_entry(char *buffer, size_t buffer_size,
                                const sstable_index_entry_t *entry) {
    // 索引条目格式：
    // [key_len: 4] [offset: 8] [size: 4] [seq_num: 8] [key: key_len]
    
    size_t required_size = 4 + 8 + 4 + 8 + entry->key_len;
    if (buffer_size < required_size) {
        return KV_ERR_PARAM;
    }
    
    char *ptr = buffer;
    
    // 写入key_len
    *(uint32_t*)ptr = (uint32_t)entry->key_len;
    ptr += 4;
    
    // 写入offset
    *(uint64_t*)ptr = entry->offset;
    ptr += 8;
    
    // 写入size
    *(uint32_t*)ptr = entry->size;
    ptr += 4;
    
    // 写入seq_num
    *(uint64_t*)ptr = entry->seq_num;
    ptr += 8;
    
    // 写入key
    if (entry->key_len > 0 && entry->key) {
        memcpy(ptr, entry->key, entry->key_len);
        ptr += entry->key_len;
    }
    
    return (int)(ptr - buffer);
}

static int deserialize_index_entry(const char *buffer, size_t buffer_size,
                                  sstable_index_entry_t *entry) {
    if (buffer_size < 24) {  // 最小头部大小
        return KV_ERR_PARAM;
    }
    
    const char *ptr = buffer;
    
    // 读取key_len
    uint32_t key_len = *(const uint32_t*)ptr;
    ptr += 4;
    
    // 读取offset
    uint64_t offset = *(const uint64_t*)ptr;
    ptr += 8;
    
    // 读取size
    uint32_t size = *(const uint32_t*)ptr;
    ptr += 4;
    
    // 读取seq_num
    uint64_t seq_num = *(const uint64_t*)ptr;
    ptr += 8;
    
    // 检查缓冲区大小
    if (buffer_size < 24 + key_len) {
        return KV_ERR_PARAM;
    }
    
    // 读取key
    char *key = NULL;
    if (key_len > 0) {
        key = kv_store_malloc(key_len + 1);
        if (!key) {
            return KV_ERR_MEM;
        }
        memcpy(key, ptr, key_len);
        key[key_len] = '\0';
        ptr += key_len;
    }
    
    // 设置条目字段
    entry->key = key;
    entry->key_len = key_len;
    entry->offset = offset;
    entry->size = size;
    entry->seq_num = seq_num;
    
    return (int)(ptr - buffer);
}
