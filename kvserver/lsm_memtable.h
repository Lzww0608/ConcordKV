/*
 * @Author: Lzww0608  
 * @Date: 2025-6-1 12:00:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-2 23:45:12
 * @Description: ConcordKV LSM-Tree MemTable实现
 */
#ifndef __LSM_MEMTABLE_H__
#define __LSM_MEMTABLE_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <pthread.h>
#include "kv_store.h"
#include "kv_error.h"

#ifdef __cplusplus
extern "C" {
#endif

// 前向声明
typedef struct lsm_memtable_s lsm_memtable_t;
typedef struct lsm_memtable_entry_s lsm_memtable_entry_t;

// MemTable条目结构 - 扩展红黑树节点
typedef struct lsm_memtable_entry_s {
    // 红黑树节点基础字段
    unsigned char color;
    struct lsm_memtable_entry_s *left;
    struct lsm_memtable_entry_s *right;
    struct lsm_memtable_entry_s *parent;
    
    // LSM-Tree特有字段
    char *key;              // 键
    size_t key_len;         // 键长度
    char *value;            // 值
    size_t value_len;       // 值长度
    uint64_t seq_num;       // 序列号（用于版本控制）
    uint8_t deleted;        // 删除标记（墓碑标记）
    uint64_t timestamp;     // 时间戳
} lsm_memtable_entry_t;

// MemTable统计信息
typedef struct lsm_memtable_stats_s {
    uint64_t total_entries;     // 总条目数
    uint64_t total_size;        // 总大小（字节）
    uint64_t memory_usage;      // 内存使用量（字节）
    uint64_t active_entries;    // 活跃条目数（未删除）
    uint64_t deleted_entries;   // 已删除条目数
    uint64_t write_count;       // 写操作计数
    uint64_t read_count;        // 读操作计数
    uint64_t delete_count;      // 删除操作计数
} lsm_memtable_stats_t;

// MemTable结构
typedef struct lsm_memtable_s {
    lsm_memtable_entry_t *root;     // 红黑树根节点
    lsm_memtable_entry_t *nil;      // 哨兵节点
    
    size_t size;                    // 当前数据大小（字节）
    size_t max_size;                // 最大大小阈值（字节）
    uint64_t seq_num;               // 当前序列号
    bool is_immutable;              // 是否为不可变表
    
    pthread_rwlock_t lock;          // 读写锁
    lsm_memtable_stats_t stats;     // 统计信息
    
    char name[64];                  // MemTable名称（用于调试）
    uint64_t creation_time;         // 创建时间戳
} lsm_memtable_t;

// MemTable迭代器
typedef struct lsm_memtable_iterator_s {
    lsm_memtable_t *memtable;           // MemTable引用
    lsm_memtable_entry_t *current;      // 当前节点
    bool valid;                         // 迭代器是否有效
    bool include_deleted;               // 是否包含已删除条目
} lsm_memtable_iterator_t;

// === MemTable生命周期管理 ===

/**
 * 创建MemTable
 * @param max_size 最大大小阈值（字节）
 * @param name MemTable名称（可选，用于调试）
 * @return 成功返回MemTable指针，失败返回NULL
 */
lsm_memtable_t* lsm_memtable_create(size_t max_size, const char *name);

/**
 * 销毁MemTable
 * @param memtable MemTable指针
 */
void lsm_memtable_destroy(lsm_memtable_t *memtable);

/**
 * 冻结MemTable为不可变状态
 * @param memtable MemTable指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_memtable_freeze(lsm_memtable_t *memtable);

// === MemTable基础操作 ===

/**
 * 插入或更新键值对
 * @param memtable MemTable指针
 * @param key 键
 * @param key_len 键长度
 * @param value 值
 * @param value_len 值长度
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_memtable_put(lsm_memtable_t *memtable, const char *key, size_t key_len,
                     const char *value, size_t value_len);

/**
 * 查找键值对
 * @param memtable MemTable指针
 * @param key 键
 * @param key_len 键长度
 * @param value 输出值缓冲区
 * @param value_len 输入输出值长度
 * @param seq_num 输出序列号
 * @param deleted 输出删除标记
 * @return 成功返回KV_ERR_NONE，未找到返回KV_ERR_NOT_FOUND，失败返回错误码
 */
int lsm_memtable_get(lsm_memtable_t *memtable, const char *key, size_t key_len,
                     char **value, size_t *value_len, uint64_t *seq_num, bool *deleted);

/**
 * 删除键值对（设置墓碑标记）
 * @param memtable MemTable指针
 * @param key 键
 * @param key_len 键长度
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_memtable_delete(lsm_memtable_t *memtable, const char *key, size_t key_len);

// === MemTable状态查询 ===

/**
 * 检查MemTable是否已满
 * @param memtable MemTable指针
 * @return true表示已满，false表示未满
 */
bool lsm_memtable_is_full(lsm_memtable_t *memtable);

/**
 * 检查MemTable是否为空
 * @param memtable MemTable指针
 * @return true表示为空，false表示非空
 */
bool lsm_memtable_is_empty(lsm_memtable_t *memtable);

/**
 * 获取MemTable当前大小
 * @param memtable MemTable指针
 * @return 当前大小（字节）
 */
size_t lsm_memtable_size(lsm_memtable_t *memtable);

/**
 * 获取MemTable条目数量
 * @param memtable MemTable指针
 * @return 条目数量
 */
uint64_t lsm_memtable_count(lsm_memtable_t *memtable);

/**
 * 获取MemTable统计信息
 * @param memtable MemTable指针
 * @param stats 输出统计信息
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_memtable_get_stats(lsm_memtable_t *memtable, lsm_memtable_stats_t *stats);

// === MemTable迭代器接口 ===

/**
 * 创建MemTable迭代器
 * @param memtable MemTable指针
 * @param include_deleted 是否包含已删除条目
 * @return 成功返回迭代器指针，失败返回NULL
 */
lsm_memtable_iterator_t* lsm_memtable_iterator_create(lsm_memtable_t *memtable, bool include_deleted);

/**
 * 销毁MemTable迭代器
 * @param iter 迭代器指针
 */
void lsm_memtable_iterator_destroy(lsm_memtable_iterator_t *iter);

/**
 * 重置迭代器到开头
 * @param iter 迭代器指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_memtable_iterator_seek_first(lsm_memtable_iterator_t *iter);

/**
 * 迭代器移动到下一个条目
 * @param iter 迭代器指针
 * @return 成功返回KV_ERR_NONE，失败返回错误码
 */
int lsm_memtable_iterator_next(lsm_memtable_iterator_t *iter);

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
int lsm_memtable_iterator_current(lsm_memtable_iterator_t *iter,
                                  const char **key, size_t *key_len,
                                  const char **value, size_t *value_len,
                                  uint64_t *seq_num, bool *deleted);

/**
 * 检查迭代器是否有效
 * @param iter 迭代器指针
 * @return true表示有效，false表示无效
 */
bool lsm_memtable_iterator_valid(lsm_memtable_iterator_t *iter);

// === MemTable内部辅助函数 ===

/**
 * 创建MemTable条目
 * @param key 键
 * @param key_len 键长度
 * @param value 值
 * @param value_len 值长度
 * @param seq_num 序列号
 * @param deleted 删除标记
 * @return 成功返回条目指针，失败返回NULL
 */
lsm_memtable_entry_t* lsm_memtable_entry_create(const char *key, size_t key_len,
                                                 const char *value, size_t value_len,
                                                 uint64_t seq_num, bool deleted);

/**
 * 销毁MemTable条目
 * @param entry 条目指针
 */
void lsm_memtable_entry_destroy(lsm_memtable_entry_t *entry);

/**
 * 比较两个键
 * @param key1 键1
 * @param key1_len 键1长度
 * @param key2 键2
 * @param key2_len 键2长度
 * @return <0表示key1<key2，0表示相等，>0表示key1>key2
 */
int lsm_memtable_key_compare(const char *key1, size_t key1_len,
                             const char *key2, size_t key2_len);

#ifdef __cplusplus
}
#endif

#endif // __LSM_MEMTABLE_H__ 