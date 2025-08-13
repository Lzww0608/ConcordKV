/*
 * @Author: Lzww0608  
 * @Date: 2025-6-1 11:00:15
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-1 14:01:04
 * @Description: ConcordKV LSM-Tree MemTable 实现
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "lsm_memtable.h"
#include "kv_store.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>

// 红黑树颜色定义
#define LSM_RB_RED   1
#define LSM_RB_BLACK 2

// 默认MemTable最大大小：4MB
#define LSM_MEMTABLE_DEFAULT_SIZE (4 * 1024 * 1024)

// === 内部辅助函数声明 ===
static void lsm_memtable_left_rotate(lsm_memtable_t *memtable, lsm_memtable_entry_t *x);
static void lsm_memtable_right_rotate(lsm_memtable_t *memtable, lsm_memtable_entry_t *y);
static void lsm_memtable_insert_fixup(lsm_memtable_t *memtable, lsm_memtable_entry_t *z);
static lsm_memtable_entry_t* lsm_memtable_search_node(lsm_memtable_t *memtable, const char *key, size_t key_len);
static void lsm_memtable_insert_node(lsm_memtable_t *memtable, lsm_memtable_entry_t *z);
static void lsm_memtable_destroy_recursive(lsm_memtable_t *memtable, lsm_memtable_entry_t *node);
static uint64_t lsm_memtable_get_timestamp(void);

// === 键比较函数实现 ===
int lsm_memtable_key_compare(const char *key1, size_t key1_len, 
                             const char *key2, size_t key2_len) {
    if (!key1 || !key2) {
        return key1 ? 1 : (key2 ? -1 : 0);
    }
    
    size_t min_len = key1_len < key2_len ? key1_len : key2_len;
    int result = memcmp(key1, key2, min_len);
    
    if (result == 0) {
        if (key1_len < key2_len) return -1;
        if (key1_len > key2_len) return 1;
        return 0;
    }
    
    return result;
}

// === MemTable条目管理 ===
lsm_memtable_entry_t* lsm_memtable_entry_create(const char *key, size_t key_len,
                                                 const char *value, size_t value_len,
                                                 uint64_t seq_num, bool deleted) {
    if (!key) return NULL;
    
    lsm_memtable_entry_t *entry = kv_store_malloc(sizeof(lsm_memtable_entry_t));
    if (!entry) return NULL;
    
    // 分配并复制键
    entry->key = kv_store_malloc(key_len + 1);
    if (!entry->key) {
        kv_store_free(entry);
        return NULL;
    }
    memcpy(entry->key, key, key_len);
    entry->key[key_len] = '\0';
    entry->key_len = key_len;
    
    // 分配并复制值（删除时值可能为NULL）
    if (value && value_len > 0) {
        entry->value = kv_store_malloc(value_len + 1);
        if (!entry->value) {
            kv_store_free(entry->key);
            kv_store_free(entry);
            return NULL;
        }
        memcpy(entry->value, value, value_len);
        entry->value[value_len] = '\0';
        entry->value_len = value_len;
    } else {
        entry->value = NULL;
        entry->value_len = 0;
    }
    
    // 初始化红黑树节点字段
    entry->color = LSM_RB_RED;
    entry->left = NULL;
    entry->right = NULL;
    entry->parent = NULL;
    
    // 设置LSM-Tree特有字段
    entry->seq_num = seq_num;
    entry->deleted = deleted ? 1 : 0;
    entry->timestamp = lsm_memtable_get_timestamp();
    
    return entry;
}

void lsm_memtable_entry_destroy(lsm_memtable_entry_t *entry) {
    if (!entry) return;
    
    if (entry->key) {
        kv_store_free(entry->key);
    }
    if (entry->value) {
        kv_store_free(entry->value);
    }
    kv_store_free(entry);
}

// === MemTable生命周期管理 ===
lsm_memtable_t* lsm_memtable_create(size_t max_size, const char *name) {
    lsm_memtable_t *memtable = kv_store_malloc(sizeof(lsm_memtable_t));
    if (!memtable) return NULL;
    
    // 创建哨兵节点
    memtable->nil = kv_store_malloc(sizeof(lsm_memtable_entry_t));
    if (!memtable->nil) {
        kv_store_free(memtable);
        return NULL;
    }
    
    // 初始化哨兵节点
    memset(memtable->nil, 0, sizeof(lsm_memtable_entry_t));
    memtable->nil->color = LSM_RB_BLACK;
    memtable->nil->left = memtable->nil;
    memtable->nil->right = memtable->nil;
    memtable->nil->parent = memtable->nil;
    
    // 初始化MemTable
    memtable->root = memtable->nil;
    memtable->size = 0;
    memtable->max_size = max_size > 0 ? max_size : LSM_MEMTABLE_DEFAULT_SIZE;
    memtable->seq_num = 1;
    memtable->is_immutable = false;
    memtable->creation_time = lsm_memtable_get_timestamp();
    
    // 初始化读写锁
    if (pthread_rwlock_init(&memtable->lock, NULL) != 0) {
        kv_store_free(memtable->nil);
        kv_store_free(memtable);
        return NULL;
    }
    
    // 设置名称
    if (name) {
        strncpy(memtable->name, name, sizeof(memtable->name) - 1);
        memtable->name[sizeof(memtable->name) - 1] = '\0';
    } else {
        snprintf(memtable->name, sizeof(memtable->name), "memtable_%lu", 
                memtable->creation_time);
    }
    
    // 初始化统计信息
    memset(&memtable->stats, 0, sizeof(memtable->stats));
    
    return memtable;
}

void lsm_memtable_destroy(lsm_memtable_t *memtable) {
    if (!memtable) return;
    
    // 获取写锁
    pthread_rwlock_wrlock(&memtable->lock);
    
    // 递归销毁所有节点
    if (memtable->root != memtable->nil) {
        lsm_memtable_destroy_recursive(memtable, memtable->root);
    }
    
    // 销毁哨兵节点
    kv_store_free(memtable->nil);
    
    // 释放锁并销毁
    pthread_rwlock_unlock(&memtable->lock);
    pthread_rwlock_destroy(&memtable->lock);
    
    // 销毁MemTable
    kv_store_free(memtable);
}

int lsm_memtable_freeze(lsm_memtable_t *memtable) {
    if (!memtable) return KV_ERR_PARAM;
    
    pthread_rwlock_wrlock(&memtable->lock);
    memtable->is_immutable = true;
    pthread_rwlock_unlock(&memtable->lock);
    
    return KV_ERR_NONE;
}

// === MemTable基础操作 ===
int lsm_memtable_put(lsm_memtable_t *memtable, const char *key, size_t key_len,
                     const char *value, size_t value_len) {
    if (!memtable || !key || key_len == 0) return KV_ERR_PARAM;
    
    pthread_rwlock_wrlock(&memtable->lock);
    
    // 检查是否为不可变表
    if (memtable->is_immutable) {
        pthread_rwlock_unlock(&memtable->lock);
        return KV_ERR_NOT_SUPPORTED;
    }
    
    // 查找是否已存在
    lsm_memtable_entry_t *existing = lsm_memtable_search_node(memtable, key, key_len);
    
    if (existing) {
        // 更新现有条目
        char *new_value = NULL;
        if (value && value_len > 0) {
            new_value = kv_store_malloc(value_len + 1);
            if (!new_value) {
                pthread_rwlock_unlock(&memtable->lock);
                return KV_ERR_MEM;
            }
            memcpy(new_value, value, value_len);
            new_value[value_len] = '\0';
        }
        
        // 更新大小统计
        if (existing->value) {
            memtable->size -= existing->value_len;
            kv_store_free(existing->value);
        }
        if (new_value) {
            memtable->size += value_len;
        }
        
        // 更新条目
        existing->value = new_value;
        existing->value_len = value_len;
        existing->seq_num = ++memtable->seq_num;
        existing->deleted = 0;
        existing->timestamp = lsm_memtable_get_timestamp();
        
        memtable->stats.write_count++;
        
    } else {
        // 创建新条目
        lsm_memtable_entry_t *entry = lsm_memtable_entry_create(
            key, key_len, value, value_len, ++memtable->seq_num, false);
        
        if (!entry) {
            pthread_rwlock_unlock(&memtable->lock);
            return KV_ERR_MEM;
        }
        
        // 插入红黑树
        lsm_memtable_insert_node(memtable, entry);
        
        // 更新统计信息
        memtable->size += key_len + value_len + sizeof(lsm_memtable_entry_t);
        memtable->stats.total_entries++;
        memtable->stats.active_entries++;
        memtable->stats.write_count++;
    }
    
    // 更新内存使用统计
    memtable->stats.total_size = memtable->size;
    memtable->stats.memory_usage = memtable->size;
    
    pthread_rwlock_unlock(&memtable->lock);
    return KV_ERR_NONE;
}

int lsm_memtable_get(lsm_memtable_t *memtable, const char *key, size_t key_len,
                     char **value, size_t *value_len, uint64_t *seq_num, bool *deleted) {
    if (!memtable || !key || key_len == 0) return KV_ERR_PARAM;
    
    pthread_rwlock_rdlock(&memtable->lock);
    
    lsm_memtable_entry_t *entry = lsm_memtable_search_node(memtable, key, key_len);
    
    if (!entry) {
        pthread_rwlock_unlock(&memtable->lock);
        return KV_ERR_NOT_FOUND;
    }
    
    // 设置输出参数
    if (value && entry->value) {
        *value = kv_store_malloc(entry->value_len + 1);
        if (*value) {
            memcpy(*value, entry->value, entry->value_len);
            (*value)[entry->value_len] = '\0';
        }
    }
    if (value_len) *value_len = entry->value_len;
    if (seq_num) *seq_num = entry->seq_num;
    if (deleted) *deleted = entry->deleted != 0;
    
    memtable->stats.read_count++;
    
    pthread_rwlock_unlock(&memtable->lock);
    return KV_ERR_NONE;
}

int lsm_memtable_delete(lsm_memtable_t *memtable, const char *key, size_t key_len) {
    if (!memtable || !key || key_len == 0) return KV_ERR_PARAM;
    
    pthread_rwlock_wrlock(&memtable->lock);
    
    // 检查是否为不可变表
    if (memtable->is_immutable) {
        pthread_rwlock_unlock(&memtable->lock);
        return KV_ERR_NOT_SUPPORTED;
    }
    
    lsm_memtable_entry_t *entry = lsm_memtable_search_node(memtable, key, key_len);
    
    if (!entry) {
        // 键不存在，插入墓碑标记
        lsm_memtable_entry_t *tombstone = lsm_memtable_entry_create(
            key, key_len, NULL, 0, ++memtable->seq_num, true);
        
        if (!tombstone) {
            pthread_rwlock_unlock(&memtable->lock);
            return KV_ERR_MEM;
        }
        
        lsm_memtable_insert_node(memtable, tombstone);
        
        // 更新统计信息
        memtable->size += key_len + sizeof(lsm_memtable_entry_t);
        memtable->stats.total_entries++;
        memtable->stats.deleted_entries++;
        
    } else {
        // 标记现有条目为已删除
        if (!entry->deleted) {
            memtable->stats.active_entries--;
            memtable->stats.deleted_entries++;
        }
        
        entry->deleted = 1;
        entry->seq_num = ++memtable->seq_num;
        entry->timestamp = lsm_memtable_get_timestamp();
        
        // 释放值的内存（保留键用于墓碑标记）
        if (entry->value) {
            memtable->size -= entry->value_len;
            kv_store_free(entry->value);
            entry->value = NULL;
            entry->value_len = 0;
        }
    }
    
    memtable->stats.delete_count++;
    memtable->stats.total_size = memtable->size;
    memtable->stats.memory_usage = memtable->size;
    
    pthread_rwlock_unlock(&memtable->lock);
    return KV_ERR_NONE;
}

// === MemTable状态查询 ===
bool lsm_memtable_is_full(lsm_memtable_t *memtable) {
    if (!memtable) return false;
    
    pthread_rwlock_rdlock(&memtable->lock);
    bool is_full = memtable->size >= memtable->max_size;
    pthread_rwlock_unlock(&memtable->lock);
    
    return is_full;
}

bool lsm_memtable_is_empty(lsm_memtable_t *memtable) {
    if (!memtable) return true;
    
    pthread_rwlock_rdlock(&memtable->lock);
    bool is_empty = (memtable->root == memtable->nil);
    pthread_rwlock_unlock(&memtable->lock);
    
    return is_empty;
}

size_t lsm_memtable_size(lsm_memtable_t *memtable) {
    if (!memtable) return 0;
    
    pthread_rwlock_rdlock(&memtable->lock);
    size_t size = memtable->size;
    pthread_rwlock_unlock(&memtable->lock);
    
    return size;
}

uint64_t lsm_memtable_count(lsm_memtable_t *memtable) {
    if (!memtable) return 0;
    
    pthread_rwlock_rdlock(&memtable->lock);
    uint64_t count = memtable->stats.total_entries;
    pthread_rwlock_unlock(&memtable->lock);
    
    return count;
}

int lsm_memtable_get_stats(lsm_memtable_t *memtable, lsm_memtable_stats_t *stats) {
    if (!memtable || !stats) return KV_ERR_PARAM;
    
    pthread_rwlock_rdlock(&memtable->lock);
    *stats = memtable->stats;
    pthread_rwlock_unlock(&memtable->lock);
    
    return KV_ERR_NONE;
}

// === 红黑树内部实现 ===
static void lsm_memtable_left_rotate(lsm_memtable_t *memtable, lsm_memtable_entry_t *x) {
    lsm_memtable_entry_t *y = x->right;
    
    x->right = y->left;
    if (y->left != memtable->nil) {
        y->left->parent = x;
    }
    
    y->parent = x->parent;
    if (x->parent == memtable->nil) {
        memtable->root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    
    y->left = x;
    x->parent = y;
}

static void lsm_memtable_right_rotate(lsm_memtable_t *memtable, lsm_memtable_entry_t *y) {
    lsm_memtable_entry_t *x = y->left;
    
    y->left = x->right;
    if (x->right != memtable->nil) {
        x->right->parent = y;
    }
    
    x->parent = y->parent;
    if (y->parent == memtable->nil) {
        memtable->root = x;
    } else if (y == y->parent->right) {
        y->parent->right = x;
    } else {
        y->parent->left = x;
    }
    
    x->right = y;
    y->parent = x;
}

static void lsm_memtable_insert_fixup(lsm_memtable_t *memtable, lsm_memtable_entry_t *z) {
    while (z->parent->color == LSM_RB_RED) {
        if (z->parent == z->parent->parent->left) {
            lsm_memtable_entry_t *y = z->parent->parent->right;
            if (y->color == LSM_RB_RED) {
                z->parent->color = LSM_RB_BLACK;
                y->color = LSM_RB_BLACK;
                z->parent->parent->color = LSM_RB_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    lsm_memtable_left_rotate(memtable, z);
                }
                z->parent->color = LSM_RB_BLACK;
                z->parent->parent->color = LSM_RB_RED;
                lsm_memtable_right_rotate(memtable, z->parent->parent);
            }
        } else {
            lsm_memtable_entry_t *y = z->parent->parent->left;
            if (y->color == LSM_RB_RED) {
                z->parent->color = LSM_RB_BLACK;
                y->color = LSM_RB_BLACK;
                z->parent->parent->color = LSM_RB_RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    lsm_memtable_right_rotate(memtable, z);
                }
                z->parent->color = LSM_RB_BLACK;
                z->parent->parent->color = LSM_RB_RED;
                lsm_memtable_left_rotate(memtable, z->parent->parent);
            }
        }
    }
    memtable->root->color = LSM_RB_BLACK;
}

static lsm_memtable_entry_t* lsm_memtable_search_node(lsm_memtable_t *memtable, 
                                                      const char *key, size_t key_len) {
    lsm_memtable_entry_t *x = memtable->root;
    
    while (x != memtable->nil) {
        int cmp = lsm_memtable_key_compare(key, key_len, x->key, x->key_len);
        if (cmp < 0) {
            x = x->left;
        } else if (cmp > 0) {
            x = x->right;
        } else {
            return x;
        }
    }
    
    return NULL;
}

static void lsm_memtable_insert_node(lsm_memtable_t *memtable, lsm_memtable_entry_t *z) {
    lsm_memtable_entry_t *y = memtable->nil;
    lsm_memtable_entry_t *x = memtable->root;
    
    while (x != memtable->nil) {
        y = x;
        int cmp = lsm_memtable_key_compare(z->key, z->key_len, x->key, x->key_len);
        if (cmp < 0) {
            x = x->left;
        } else {
            x = x->right;
        }
    }
    
    z->parent = y;
    if (y == memtable->nil) {
        memtable->root = z;
    } else {
        int cmp = lsm_memtable_key_compare(z->key, z->key_len, y->key, y->key_len);
        if (cmp < 0) {
            y->left = z;
        } else {
            y->right = z;
        }
    }
    
    z->left = memtable->nil;
    z->right = memtable->nil;
    z->color = LSM_RB_RED;
    
    lsm_memtable_insert_fixup(memtable, z);
}

static void lsm_memtable_destroy_recursive(lsm_memtable_t *memtable, lsm_memtable_entry_t *node) {
    if (node == memtable->nil) return;
    
    lsm_memtable_destroy_recursive(memtable, node->left);
    lsm_memtable_destroy_recursive(memtable, node->right);
    lsm_memtable_entry_destroy(node);
}

static uint64_t lsm_memtable_get_timestamp(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

// === MemTable迭代器实现 ===

// 查找最小节点
static lsm_memtable_entry_t* lsm_memtable_minimum(lsm_memtable_t *memtable, lsm_memtable_entry_t *x) {
    while (x->left != memtable->nil) {
        x = x->left;
    }
    return x;
}

// 查找后继节点
static lsm_memtable_entry_t* lsm_memtable_successor(lsm_memtable_t *memtable, lsm_memtable_entry_t *x) {
    if (x->right != memtable->nil) {
        return lsm_memtable_minimum(memtable, x->right);
    }
    
    lsm_memtable_entry_t *y = x->parent;
    while (y != memtable->nil && x == y->right) {
        x = y;
        y = y->parent;
    }
    return y;
}

lsm_memtable_iterator_t* lsm_memtable_iterator_create(lsm_memtable_t *memtable, bool include_deleted) {
    if (!memtable) {
        return NULL;
    }
    
    lsm_memtable_iterator_t *iter = kv_store_malloc(sizeof(lsm_memtable_iterator_t));
    if (!iter) {
        KV_ERROR(KV_ERR_MEM, "无法分配迭代器内存");
        return NULL;
    }
    
    iter->memtable = memtable;
    iter->current = memtable->nil;
    iter->include_deleted = include_deleted;
    iter->valid = false;
    
    return iter;
}

void lsm_memtable_iterator_destroy(lsm_memtable_iterator_t *iter) {
    if (!iter) return;
    kv_store_free(iter);
}

int lsm_memtable_iterator_seek_first(lsm_memtable_iterator_t *iter) {
    if (!iter || !iter->memtable) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_rdlock(&iter->memtable->lock);
    
    if (iter->memtable->root == iter->memtable->nil) {
        iter->current = iter->memtable->nil;
        iter->valid = false;
    } else {
        // 找到最小节点
        iter->current = lsm_memtable_minimum(iter->memtable, iter->memtable->root);
        iter->valid = (iter->current != iter->memtable->nil);
        
        // 如果不包含已删除条目，跳过删除的条目
        if (iter->valid && !iter->include_deleted && iter->current->deleted) {
            pthread_rwlock_unlock(&iter->memtable->lock);
            return lsm_memtable_iterator_next(iter);
        }
    }
    
    pthread_rwlock_unlock(&iter->memtable->lock);
    
    return KV_ERR_NONE;
}

int lsm_memtable_iterator_next(lsm_memtable_iterator_t *iter) {
    if (!iter || !iter->memtable || !iter->valid) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_rdlock(&iter->memtable->lock);
    
    do {
        iter->current = lsm_memtable_successor(iter->memtable, iter->current);
        iter->valid = (iter->current != iter->memtable->nil);
        
        // 如果包含已删除条目或当前条目未删除，则停止
        if (!iter->valid || iter->include_deleted || !iter->current->deleted) {
            break;
        }
    } while (iter->valid);
    
    pthread_rwlock_unlock(&iter->memtable->lock);
    
    return KV_ERR_NONE;
}

bool lsm_memtable_iterator_valid(lsm_memtable_iterator_t *iter) {
    if (!iter) return false;
    return iter->valid;
}

int lsm_memtable_iterator_current(lsm_memtable_iterator_t *iter,
                                  const char **key, size_t *key_len,
                                  const char **value, size_t *value_len,
                                  uint64_t *seq_num, bool *deleted) {
    if (!iter || !iter->valid || !key || !key_len || !value || !value_len || !seq_num || !deleted) {
        return KV_ERR_PARAM;
    }
    
    pthread_rwlock_rdlock(&iter->memtable->lock);
    
    if (iter->current == iter->memtable->nil) {
        pthread_rwlock_unlock(&iter->memtable->lock);
        return KV_ERR_PARAM;
    }
    
    *key = iter->current->key;
    *key_len = iter->current->key_len;
    *value = iter->current->value;
    *value_len = iter->current->value_len;
    *seq_num = iter->current->seq_num;
    *deleted = iter->current->deleted;
    
    pthread_rwlock_unlock(&iter->memtable->lock);
    
    return KV_ERR_NONE;
} 