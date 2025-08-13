/*
 * @Author: Lzww0608  
 * @Date: 2025-6-1 14:00:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-1 20:23:16
 * @Description: ConcordKV LSM-Tree SSTable 文件格式实现
 */
#ifndef __LSM_SSTABLE_H__
#define __LSM_SSTABLE_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <pthread.h>
#include "lsm_memtable.h"
#include "kv_store.h"
#include "kv_error.h"

#ifdef __cplusplus
extern "C" {
#endif

// === SSTable文件格式常量 ===
#define SSTABLE_MAGIC_NUMBER        0x53535441  // "SSTA"
#define SSTABLE_VERSION             1
#define SSTABLE_BLOCK_SIZE          4096        // 4KB数据块
#define SSTABLE_INDEX_INTERVAL      1           // 每个数据块添加一个索引条目
#define SSTABLE_MAX_FILENAME        256
#define SSTABLE_FOOTER_SIZE         64          // Footer固定大小

// 压缩类型
typedef enum {
    SSTABLE_COMPRESSION_NONE = 0,
    SSTABLE_COMPRESSION_LZ4  = 1,
    SSTABLE_COMPRESSION_SNAPPY = 2
} sstable_compression_t;

// 块类型
typedef enum {
    SSTABLE_BLOCK_DATA   = 0,
    SSTABLE_BLOCK_INDEX  = 1,
    SSTABLE_BLOCK_BLOOM  = 2,
    SSTABLE_BLOCK_FOOTER = 3
} sstable_block_type_t;

// === SSTable数据结构 ===

// 数据块头部
typedef struct sstable_block_header_s {
    uint32_t block_type;        // 块类型
    uint32_t compressed_size;   // 压缩后大小
    uint32_t uncompressed_size; // 压缩前大小
    uint32_t crc32;             // CRC32校验和
    uint32_t entry_count;       // 条目数量
    uint32_t reserved;          // 保留字段
} __attribute__((packed)) sstable_block_header_t;

// 数据块结构
typedef struct sstable_data_block_s {
    sstable_block_header_t header;
    char *data;                 // 数据内容
    size_t capacity;            // 容量
    size_t size;                // 当前大小
    sstable_compression_t compression;
} sstable_data_block_t;

// 索引条目结构
typedef struct sstable_index_entry_s {
    char *key;                  // 键
    size_t key_len;             // 键长度
    uint64_t offset;            // 数据块在文件中的偏移
    uint32_t size;              // 数据块大小
    uint64_t seq_num;           // 序列号
} sstable_index_entry_t;

// 索引块结构
typedef struct sstable_index_block_s {
    sstable_block_header_t header;
    sstable_index_entry_t *entries;
    uint32_t entry_count;
    uint32_t capacity;
} sstable_index_block_t;

// 布隆过滤器结构
typedef struct sstable_bloom_filter_s {
    uint8_t *bits;              // 位数组
    size_t bit_count;           // 位数组大小
    uint32_t hash_count;        // 哈希函数个数
    uint32_t false_positive_rate; // 误判率(以万分之一为单位)
} sstable_bloom_filter_t;

// SSTable文件尾部
typedef struct sstable_footer_s {
    uint32_t magic_number;      // 魔数
    uint32_t version;           // 版本号
    uint64_t index_offset;      // 索引块偏移
    uint32_t index_size;        // 索引块大小
    uint64_t bloom_offset;      // 布隆过滤器偏移
    uint32_t bloom_size;        // 布隆过滤器大小
    uint64_t min_seq_num;       // 最小序列号
    uint64_t max_seq_num;       // 最大序列号
    uint64_t entry_count;       // 总条目数
    uint32_t crc32;             // Footer CRC32
} __attribute__((packed)) sstable_footer_t;

// SSTable统计信息
typedef struct sstable_stats_s {
    uint64_t total_entries;     // 总条目数
    uint64_t file_size;         // 文件大小
    uint64_t data_size;         // 数据大小
    uint64_t index_size;        // 索引大小
    uint64_t bloom_size;        // 布隆过滤器大小
    uint32_t block_count;       // 数据块数量
    sstable_compression_t compression;
    double compression_ratio;   // 压缩比
} sstable_stats_t;

// === SSTable写入器 ===
typedef struct sstable_writer_s {
    FILE *file;                 // 文件句柄
    char filename[SSTABLE_MAX_FILENAME];
    
    sstable_data_block_t *current_block;   // 当前数据块
    sstable_index_block_t *index_block;    // 索引块
    sstable_bloom_filter_t *bloom_filter;  // 布隆过滤器
    
    uint64_t file_offset;       // 当前文件偏移
    uint64_t entry_count;       // 已写入条目数
    uint64_t min_seq_num;       // 最小序列号
    uint64_t max_seq_num;       // 最大序列号
    
    sstable_compression_t compression;
    bool finalized;             // 是否已完成
    
    sstable_stats_t stats;      // 统计信息
    pthread_mutex_t lock;       // 写入锁
} sstable_writer_t;

// === SSTable读取器 ===
typedef struct sstable_reader_s {
    FILE *file;                 // 文件句柄
    char filename[SSTABLE_MAX_FILENAME];
    
    sstable_footer_t footer;    // 文件尾部信息
    sstable_index_block_t *index_block;    // 索引块
    sstable_bloom_filter_t *bloom_filter;  // 布隆过滤器
    
    sstable_stats_t stats;      // 统计信息
    pthread_rwlock_t lock;      // 读写锁
} sstable_reader_t;

// === SSTable迭代器 ===
typedef struct sstable_iterator_s {
    sstable_reader_t *reader;   // SSTable读取器
    uint32_t block_index;       // 当前块索引
    uint32_t entry_index;       // 当前条目索引
    sstable_data_block_t *current_block;   // 当前数据块
    
    bool valid;                 // 迭代器有效性
    bool include_deleted;       // 是否包含已删除条目
} sstable_iterator_t;

// === SSTable写入器API ===

/**
 * 创建SSTable写入器
 * @param filename 文件路径
 * @param compression 压缩类型
 * @return 成功返回写入器指针，失败返回NULL
 */
sstable_writer_t* sstable_writer_create(const char *filename, sstable_compression_t compression);

/**
 * 销毁SSTable写入器
 * @param writer 写入器指针
 */
void sstable_writer_destroy(sstable_writer_t *writer);

/**
 * 写入键值对到SSTable
 * @param writer 写入器指针
 * @param key 键
 * @param key_len 键长度
 * @param value 值
 * @param value_len 值长度
 * @param seq_num 序列号
 * @param deleted 是否已删除
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int sstable_writer_put(sstable_writer_t *writer,
                       const char *key, size_t key_len,
                       const char *value, size_t value_len,
                       uint64_t seq_num, bool deleted);

/**
 * 完成SSTable写入并刷新到磁盘
 * @param writer 写入器指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int sstable_writer_finalize(sstable_writer_t *writer);

/**
 * 从MemTable创建SSTable
 * @param memtable MemTable指针
 * @param filename 输出文件路径
 * @param compression 压缩类型
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int sstable_create_from_memtable(lsm_memtable_t *memtable, 
                                 const char *filename,
                                 sstable_compression_t compression);

// === SSTable读取器API ===

/**
 * 创建SSTable读取器
 * @param filename 文件路径
 * @return 成功返回读取器指针，失败返回NULL
 */
sstable_reader_t* sstable_reader_create(const char *filename);

/**
 * 销毁SSTable读取器
 * @param reader 读取器指针
 */
void sstable_reader_destroy(sstable_reader_t *reader);

/**
 * 从SSTable查找键值对
 * @param reader 读取器指针
 * @param key 键
 * @param key_len 键长度
 * @param value 输出值缓冲区
 * @param value_len 输入输出值长度
 * @param seq_num 输出序列号
 * @param deleted 输出删除标记
 * @return 成功返回KV_ERR_NONE，未找到返回KV_ERR_NOT_FOUND，失败返回错误码
 */
int sstable_reader_get(sstable_reader_t *reader,
                       const char *key, size_t key_len,
                       char **value, size_t *value_len,
                       uint64_t *seq_num, bool *deleted);

/**
 * 检查键是否可能存在于SSTable（布隆过滤器）
 * @param reader 读取器指针
 * @param key 键
 * @param key_len 键长度
 * @return true表示可能存在，false表示肯定不存在
 */
bool sstable_reader_may_contain(sstable_reader_t *reader, const char *key, size_t key_len);

/**
 * 获取SSTable统计信息
 * @param reader 读取器指针
 * @param stats 输出统计信息
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int sstable_reader_get_stats(sstable_reader_t *reader, sstable_stats_t *stats);

// === SSTable迭代器API ===

/**
 * 创建SSTable迭代器
 * @param reader 读取器指针
 * @param include_deleted 是否包含已删除条目
 * @return 成功返回迭代器指针，失败返回NULL
 */
sstable_iterator_t* sstable_iterator_create(sstable_reader_t *reader, bool include_deleted);

/**
 * 销毁SSTable迭代器
 * @param iter 迭代器指针
 */
void sstable_iterator_destroy(sstable_iterator_t *iter);

/**
 * 移动迭代器到第一个条目
 * @param iter 迭代器指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int sstable_iterator_seek_first(sstable_iterator_t *iter);

/**
 * 移动迭代器到指定键
 * @param iter 迭代器指针
 * @param key 键
 * @param key_len 键长度
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int sstable_iterator_seek(sstable_iterator_t *iter, const char *key, size_t key_len);

/**
 * 移动迭代器到下一个条目
 * @param iter 迭代器指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int sstable_iterator_next(sstable_iterator_t *iter);

/**
 * 获取迭代器当前条目
 * @param iter 迭代器指针
 * @param key 输出键
 * @param key_len 输出键长度
 * @param value 输出值
 * @param value_len 输出值长度
 * @param seq_num 输出序列号
 * @param deleted 输出删除标记
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int sstable_iterator_current(sstable_iterator_t *iter,
                             const char **key, size_t *key_len,
                             const char **value, size_t *value_len,
                             uint64_t *seq_num, bool *deleted);

/**
 * 检查迭代器是否有效
 * @param iter 迭代器指针
 * @return true表示有效，false表示无效
 */
bool sstable_iterator_valid(sstable_iterator_t *iter);

// === 数据块操作API ===

/**
 * 创建数据块
 * @param capacity 容量
 * @param compression 压缩类型
 * @return 成功返回数据块指针，失败返回NULL
 */
sstable_data_block_t* sstable_data_block_create(size_t capacity, sstable_compression_t compression);

/**
 * 销毁数据块
 * @param block 数据块指针
 */
void sstable_data_block_destroy(sstable_data_block_t *block);

/**
 * 向数据块添加条目
 * @param block 数据块指针
 * @param key 键
 * @param key_len 键长度
 * @param value 值
 * @param value_len 值长度
 * @param seq_num 序列号
 * @param deleted 是否已删除
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int sstable_data_block_add_entry(sstable_data_block_t *block,
                                  const char *key, size_t key_len,
                                  const char *value, size_t value_len,
                                  uint64_t seq_num, bool deleted);

/**
 * 完成数据块并压缩
 * @param block 数据块指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int sstable_data_block_finalize(sstable_data_block_t *block);

// === 索引块操作API ===

/**
 * 创建索引块
 * @param capacity 容量
 * @return 成功返回索引块指针，失败返回NULL
 */
sstable_index_block_t* sstable_index_block_create(uint32_t capacity);

/**
 * 销毁索引块
 * @param index_block 索引块指针
 */
void sstable_index_block_destroy(sstable_index_block_t *index_block);

/**
 * 向索引块添加条目
 * @param index_block 索引块指针
 * @param key 键
 * @param key_len 键长度
 * @param offset 偏移
 * @param size 大小
 * @param seq_num 序列号
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int sstable_index_block_add_entry(sstable_index_block_t *index_block,
                                   const char *key, size_t key_len,
                                   uint64_t offset, uint32_t size, uint64_t seq_num);

// === 布隆过滤器API ===

/**
 * 创建布隆过滤器
 * @param expected_entries 预期条目数
 * @param false_positive_rate 误判率(以万分之一为单位)
 * @return 成功返回布隆过滤器指针，失败返回NULL
 */
sstable_bloom_filter_t* sstable_bloom_filter_create(uint64_t expected_entries, uint32_t false_positive_rate);

/**
 * 销毁布隆过滤器
 * @param bloom 布隆过滤器指针
 */
void sstable_bloom_filter_destroy(sstable_bloom_filter_t *bloom);

/**
 * 向布隆过滤器添加键
 * @param bloom 布隆过滤器指针
 * @param key 键
 * @param key_len 键长度
 */
void sstable_bloom_filter_add(sstable_bloom_filter_t *bloom, const char *key, size_t key_len);

/**
 * 检查键是否可能存在于布隆过滤器
 * @param bloom 布隆过滤器指针
 * @param key 键
 * @param key_len 键长度
 * @return true表示可能存在，false表示肯定不存在
 */
bool sstable_bloom_filter_may_contain(sstable_bloom_filter_t *bloom, const char *key, size_t key_len);

// === 工具函数 ===

/**
 * 计算CRC32校验和
 * @param data 数据
 * @param size 数据大小
 * @return CRC32值
 */
uint32_t sstable_crc32(const void *data, size_t size);

/**
 * 格式化SSTable文件名
 * @param buffer 缓冲区
 * @param buffer_size 缓冲区大小
 * @param dir 目录
 * @param level 层级
 * @param file_id 文件ID
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int sstable_format_filename(char *buffer, size_t buffer_size, 
                            const char *dir, int level, uint64_t file_id);

#ifdef __cplusplus
}
#endif

#endif // __LSM_SSTABLE_H__ 