/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV storage engine - kvstore_hash.c
 */
// Zipper

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>

#include "kv_store.h"
#include "rbtree_adapter.h"

#define MAX_KEY_LEN	128
#define MAX_VALUE_LEN	512


#define MAX_TABLE_SIZE	102400

#define ENABLE_POINTER_KEY	1

// 宏定义
#define TREEIFY_THRESHOLD    8     // 链表转为红黑树的阈值
#define UNTREEIFY_THRESHOLD  6     // 红黑树转为链表的阈值
#define USE_RBTREE           1     // 是否启用红黑树优化

#define INITIAL_CAPACITY     1024  // 初始容量
#define LOAD_FACTOR         0.75f  // 负载因子
#define RESIZE_FACTOR       2      // 扩容倍数

// 全局哈希表
hashtable_t Hash;

// 声明适配器函数
extern void rbtree_adapter_inorder_traversal(rbtree_adapter_t *adapter, hash_node_t **nodes, int *count);

// 前向声明内部函数
static int _hash(char *key, int size);
static float _load_factor(hashtable_t *hash);
static hash_node_t *_create_node(char *key, char *value);
static void _rbtree_inorder_traversal(rbtree_adapter_t *adapter, hash_node_t **nodes, int *count);
static void _treeify_bucket(bucket_t *bucket);
static void _untreeify_bucket(bucket_t *bucket);
static void _check_capacity(hashtable_t *hash);
static int _rehash(hashtable_t *hash, int new_size);
int init_hashtable(hashtable_t *hash);
void dest_hashtable(hashtable_t *hash);
int put_kv_hashtable(hashtable_t *hash, char *key, char *value);
char *get_kv_hashtable(hashtable_t *hash, char *key);
int count_kv_hashtable(hashtable_t *hash);
int delete_kv_hashtable(hashtable_t *hash, char *key);
int exist_kv_hashtable(hashtable_t *hash, char *key);

// 新的哈希函数 - 使用FNV-1a算法
// 比简单的加法哈希更好地分布键
static uint32_t _hash_fnv1a(const char *key) {
    if (!key) return 0;
    
    // FNV-1a哈希算法参数
    const uint32_t FNV_PRIME = 16777619;
    const uint32_t FNV_OFFSET_BASIS = 2166136261;
    
    uint32_t hash = FNV_OFFSET_BASIS;
    
    for (const unsigned char *p = (const unsigned char *)key; *p; p++) {
        hash ^= *p;
        hash *= FNV_PRIME;
    }
    
    return hash;
}

// 使用新的哈希函数计算索引
static int _hash(char *key, int size) {
	if (!key) return -1;
    return _hash_fnv1a(key) % size;
}

// 计算负载因子
static float _load_factor(hashtable_t *hash) {
    if (!hash || hash->max_slots == 0) return 0.0f;
    return (float)hash->count / hash->max_slots;
}

// 创建哈希节点
static hash_node_t *_create_node(char *key, char *value) {
	hash_node_t *node = (hash_node_t*)kv_store_malloc(sizeof(hash_node_t));
	if (!node) return NULL;

#if ENABLE_POINTER_KEY
	node->key = kv_store_malloc(strlen(key) + 1);
	if (!node->key) {
		kv_store_free(node);
		return NULL;
	}
	strcpy(node->key, key);

	node->value = kv_store_malloc(strlen(value) + 1);
	if (!node->value) {
		kv_store_free(node->key);
		kv_store_free(node);
		return NULL;
	}
	strcpy(node->value, value);
#else
	strncpy(node->key, key, MAX_KEY_LEN);
	strncpy(node->value, value, MAX_VALUE_LEN);
#endif

	node->next = NULL;
    node->node_type = NODE_TYPE_LIST;

	return node;
}

// 中序遍历红黑树，收集所有节点
static void _rbtree_inorder_traversal(rbtree_adapter_t *adapter, hash_node_t **nodes, int *count) {
    rbtree_adapter_inorder_traversal(adapter, nodes, count);
}

// 将桶中的链表转换为红黑树
static void _treeify_bucket(bucket_t *bucket) {
    if (!bucket || bucket->is_tree) return;
    
    // 创建红黑树适配器
    rbtree_adapter_t *adapter = rbtree_adapter_create();
    if (!adapter) return;
    
    // 将链表节点插入到红黑树
    hash_node_t *node = bucket->data.list;
    hash_node_t *next;
    
    while (node) {
        next = node->next;
        node->next = NULL;
        
        // 插入到红黑树适配器
        rbtree_adapter_insert(adapter, node);
        
        node = next;
    }
    
    // 更新桶
    bucket->data.tree = adapter;
    bucket->is_tree = 1;
}

// 将桶中的红黑树转换回链表
static void _untreeify_bucket(bucket_t *bucket) {
    if (!bucket || !bucket->is_tree) return;
    
    rbtree_adapter_t *adapter = bucket->data.tree;
    
    // 收集红黑树中的所有节点
    hash_node_t **nodes = (hash_node_t**)kv_store_malloc(sizeof(hash_node_t*) * bucket->size);
    if (!nodes) return;
    
    int count = 0;
    _rbtree_inorder_traversal(adapter, nodes, &count);
    
    // 构建链表
    hash_node_t *list = NULL;
    
    for (int i = 0; i < count; i++) {
        nodes[i]->node_type = NODE_TYPE_LIST;
        nodes[i]->next = list;
        list = nodes[i];
    }
    
    // 更新桶
    bucket->data.list = list;
    bucket->is_tree = 0;
    
    // 释放内存
    kv_store_free(nodes);
    rbtree_adapter_destroy(adapter);
}

// 检查是否需要扩容
static void _check_capacity(hashtable_t *hash) {
    if (!hash) return;
    
    if (_load_factor(hash) > LOAD_FACTOR) {
        // 扩容为原来的两倍
        int new_size = hash->max_slots * RESIZE_FACTOR;
        _rehash(hash, new_size);
    }
}

// 重新哈希所有键值对
static int _rehash(hashtable_t *hash, int new_size) {
    if (!hash || new_size <= 0) return -1;
    
    // 创建新的桶数组
    bucket_t *new_buckets = (bucket_t*)kv_store_malloc(sizeof(bucket_t) * new_size);
    if (!new_buckets) return -1;
    
    // 初始化新桶
    for (int i = 0; i < new_size; i++) {
        new_buckets[i].data.list = NULL;
        new_buckets[i].is_tree = 0;
        new_buckets[i].size = 0;
    }
    
    // 保存旧的桶数组和大小
    bucket_t *old_buckets = hash->buckets;
    int old_size = hash->max_slots;
    
    // 更新哈希表结构
    hash->buckets = new_buckets;
    hash->max_slots = new_size;
    hash->count = 0;
    
    // 将所有数据从旧桶重新哈希到新桶
    for (int i = 0; i < old_size; i++) {
        bucket_t *bucket = &old_buckets[i];
        
        if (bucket->is_tree) {
            // 从红黑树中提取节点
            rbtree_adapter_t *adapter = bucket->data.tree;
            
            // 收集红黑树中的所有节点
            hash_node_t **nodes = (hash_node_t**)kv_store_malloc(sizeof(hash_node_t*) * bucket->size);
            if (!nodes) continue;
            
            int count = 0;
            _rbtree_inorder_traversal(adapter, nodes, &count);
            
            // 重新插入所有节点
            for (int j = 0; j < count; j++) {
                hash_node_t *node = nodes[j];
                put_kv_hashtable(hash, node->key, node->value);
                
                // 释放旧节点
#if ENABLE_POINTER_KEY
                if (node->key) {
                    kv_store_free(node->key);
                }
                if (node->value) {
                    kv_store_free(node->value);
                }
#endif
                kv_store_free(node);
            }
            
            kv_store_free(nodes);
            
            // 销毁红黑树适配器
            rbtree_adapter_destroy(adapter);
        } else {
            // 处理链表
            hash_node_t *node = bucket->data.list;
            
            while (node) {
                hash_node_t *next = node->next;
                
                // 重新插入节点
                put_kv_hashtable(hash, node->key, node->value);
                
                // 释放旧节点
#if ENABLE_POINTER_KEY
                if (node->key) {
                    kv_store_free(node->key);
                }
                if (node->value) {
                    kv_store_free(node->value);
                }
#endif
                kv_store_free(node);
                
                node = next;
            }
        }
    }
    
    // 释放旧的桶数组
    kv_store_free(old_buckets);
    
    return 0;
}

// 初始化哈希表
int init_hashtable(hashtable_t *hash) {
	if (!hash) return -1;

    hash->max_slots = INITIAL_CAPACITY;
	hash->count = 0; 
    
    hash->buckets = (bucket_t*)kv_store_malloc(sizeof(bucket_t) * hash->max_slots);
    if (!hash->buckets) return -1;
    
    // 初始化所有桶
    for (int i = 0; i < hash->max_slots; i++) {
        hash->buckets[i].data.list = NULL;
        hash->buckets[i].is_tree = 0;
        hash->buckets[i].size = 0;
    }

	return 0;
}

// 销毁哈希表
void dest_hashtable(hashtable_t *hash) {
	if (!hash) return;

    for (int i = 0; i < hash->max_slots; i++) {
        bucket_t *bucket = &hash->buckets[i];
        
        if (bucket->is_tree) {
            // 销毁红黑树
            rbtree_adapter_destroy(bucket->data.tree);
        } else {
            // 释放链表节点
            hash_node_t *node = bucket->data.list;
            hash_node_t *next;
            
            while (node) {
                next = node->next;
                
#if ENABLE_POINTER_KEY
                if (node->key) {
                    kv_store_free(node->key);
                }
                if (node->value) {
                    kv_store_free(node->value);
                }
#endif
                kv_store_free(node);
                
                node = next;
            }
        }
    }
    
    kv_store_free(hash->buckets);
    hash->buckets = NULL;
    hash->max_slots = 0;
    hash->count = 0;
}

// 向哈希表中插入键值对
int put_kv_hashtable(hashtable_t *hash, char *key, char *value) {
	if (!hash || !key || !value) return -1;

    // 检查是否需要扩容
    _check_capacity(hash);

    int idx = _hash(key, hash->max_slots);
    bucket_t *bucket = &hash->buckets[idx];

    // 红黑树情况
    if (bucket->is_tree) {
        // 在红黑树中查找节点
        rbtree_adapter_t *adapter = bucket->data.tree;
        rbtree_adapter_node_t *found = rbtree_adapter_search(adapter, key);
        
        if (found) {
            // 更新现有节点的值
#if ENABLE_POINTER_KEY
            kv_store_free(found->hash_node->value);
            found->hash_node->value = kv_store_malloc(strlen(value) + 1);
            if (!found->hash_node->value) return -1;
            strcpy(found->hash_node->value, value);
#else
            strncpy(found->hash_node->value, value, MAX_VALUE_LEN);
#endif
            kv_store_free(found); // 释放搜索结果
            return 1; // 已存在
        } else {
            // 创建新节点并插入红黑树
            hash_node_t *hash_node = _create_node(key, value);
            if (!hash_node) return -1;
            hash_node->node_type = NODE_TYPE_TREE;
            
            // 插入到红黑树
            rbtree_adapter_insert(adapter, hash_node);
            
            bucket->size++;
            hash->count++;
            return 0;
        }
    } else {
        // 链表情况
        hash_node_t *prev = NULL;
        hash_node_t *current = bucket->data.list;
        
        // 查找节点
        while (current) {
#if ENABLE_POINTER_KEY
            if (strcmp(current->key, key) == 0) {
#else
            if (strncmp(current->key, key, MAX_KEY_LEN) == 0) {
#endif
                // 更新现有节点的值
#if ENABLE_POINTER_KEY
                kv_store_free(current->value);
                current->value = kv_store_malloc(strlen(value) + 1);
                if (!current->value) return -1;
                strcpy(current->value, value);
#else
                strncpy(current->value, value, MAX_VALUE_LEN);
#endif
                return 1; // 已存在
            }
            
            prev = current;
            current = current->next;
        }
        
        // 创建新节点
	hash_node_t *new_node = _create_node(key, value);
        if (!new_node) return -1;
        
        // 插入到链表头部
        new_node->next = bucket->data.list;
        bucket->data.list = new_node;
        
        bucket->size++;
        hash->count++;
        
        // 检查是否需要转换为红黑树
#if USE_RBTREE
        if (bucket->size >= TREEIFY_THRESHOLD) {
            _treeify_bucket(bucket);
        }
#endif

	return 0;
    }
}

// 从哈希表中获取值
char *get_kv_hashtable(hashtable_t *hash, char *key) {
	if (!hash || !key) return NULL;

    int idx = _hash(key, hash->max_slots);
    bucket_t *bucket = &hash->buckets[idx];
    
    if (bucket->is_tree) {
        // 红黑树情况
        rbtree_adapter_t *adapter = bucket->data.tree;
        rbtree_adapter_node_t *found = rbtree_adapter_search(adapter, key);
        
        if (found) {
            char *value = found->hash_node->value;
            kv_store_free(found); // 释放搜索结果
            return value;
        }
    } else {
        // 链表情况
        hash_node_t *current = bucket->data.list;
        
        while (current) {
#if ENABLE_POINTER_KEY
            if (strcmp(current->key, key) == 0) {
#else
            if (strncmp(current->key, key, MAX_KEY_LEN) == 0) {
#endif
                return current->value;
            }
            
            current = current->next;
        }
    }

	return NULL;
}

// 统计哈希表中的键值对数量
int count_kv_hashtable(hashtable_t *hash) {
    return hash ? hash->count : 0;
}

// 从哈希表中删除键值对
int delete_kv_hashtable(hashtable_t *hash, char *key) {
    if (!hash || !key) return -1;
    
    int idx = _hash(key, hash->max_slots);
    bucket_t *bucket = &hash->buckets[idx];
    
    if (bucket->is_tree) {
        // 红黑树情况
        rbtree_adapter_t *adapter = bucket->data.tree;
        hash_node_t *deleted = rbtree_adapter_delete(adapter, key);
        
        if (deleted) {
            // 释放哈希节点
#if ENABLE_POINTER_KEY
            if (deleted->key) {
                kv_store_free(deleted->key);
            }
            if (deleted->value) {
                kv_store_free(deleted->value);
            }
#endif
            kv_store_free(deleted);
            
            bucket->size--;
            hash->count--;
            
            // 检查是否需要转换回链表
#if USE_RBTREE
            if (bucket->size <= UNTREEIFY_THRESHOLD) {
                _untreeify_bucket(bucket);
            }
#endif

		return 0;
	}
    } else {
        // 链表情况
        hash_node_t *prev = NULL;
        hash_node_t *current = bucket->data.list;
        
        while (current) {
#if ENABLE_POINTER_KEY
            if (strcmp(current->key, key) == 0) {
#else
            if (strncmp(current->key, key, MAX_KEY_LEN) == 0) {
#endif
                // 从链表中删除节点
                if (prev) {
                    prev->next = current->next;
                } else {
                    bucket->data.list = current->next;
                }
                
                // 释放节点内存
#if ENABLE_POINTER_KEY
                if (current->key) {
                    kv_store_free(current->key);
	}
                if (current->value) {
                    kv_store_free(current->value);
	}
#endif
                kv_store_free(current);
                
                bucket->size--;
                hash->count--;

	return 0;
}

            prev = current;
            current = current->next;
        }
    }
    
    return -1; // 键不存在
}

// 检查键是否存在
int exist_kv_hashtable(hashtable_t *hash, char *key) {
    return get_kv_hashtable(hash, key) != NULL;
}

// 对外接口: 创建哈希表
int kv_store_hash_create(hashtable_t *hash) {
	return init_hashtable(hash);
}

// 对外接口: 销毁哈希表
void kv_store_hash_destroy(hashtable_t *hash) {
    dest_hashtable(hash);
}

// 对外接口: 设置键值对
int kvs_hash_set(hashtable_t *hash, char *key, char *value) {
	return put_kv_hashtable(hash, key, value);
}

// 对外接口: 获取值
char *kvs_hash_get(hashtable_t *hash, char *key) {
	return get_kv_hashtable(hash, key);
}

// 对外接口: 删除键值对
int kvs_hash_delete(hashtable_t *hash, char *key) {
	return delete_kv_hashtable(hash, key);
}

// 对外接口: 修改键值对
int kvs_hash_modify(hashtable_t *hash, char *key, char *value) {
    // 修改操作与设置操作相同，只是返回值不同
    int result = put_kv_hashtable(hash, key, value);
    return result == 1 ? 0 : -1; // 0表示成功，-1表示失败
}

// 对外接口: 统计键值对数量
int kvs_hash_count(hashtable_t *hash) {
    return count_kv_hashtable(hash);
}




