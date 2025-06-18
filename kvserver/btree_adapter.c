/*
 * @Author: Lzww0608  
 * @Date: 2025-5-31 23:35:03
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-18 21:46:06
 * @Description: ConcordKV B+Tree存储引擎适配器实现
 */
#define _GNU_SOURCE     // 启用扩展函数
#define _POSIX_C_SOURCE 200809L  // 启用POSIX扩展

#include "btree_adapter.h"
#include "kv_error.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

// === 辅助函数 ===

/**
 * 在B+Tree中搜索键(内部实现)
 */
static char* btree_search_internal(btree_t *tree, const char *key, size_t key_len) {
    if (!tree || !key || !tree->root) {
        return NULL;
    }
    
    btree_node_t *current = tree->root;
    
    // 从根节点开始向下搜索，直到叶子节点
    while (!current->is_leaf) {
        int index = 0;
        
        // 找到正确的子节点位置
        while (index < current->key_count && 
               btree_key_compare(key, key_len, current->keys[index], current->key_lens[index]) >= 0) {
            index++;
        }
        
        // 移动到正确的子节点
        current = current->children[index];
        
        if (!current) {
            return NULL;  // 树结构异常
        }
    }
    
    // 在叶子节点中查找精确匹配
    int index = btree_node_find_key(current, key, key_len, true);
    if (index >= 0) {
        // 找到键，返回值的副本
        return btree_value_copy(current->values[index], current->value_lens[index]);
    }
    
    return NULL;  // 未找到
}

// === B+Tree适配器接口实现 ===

/**
 * 设置键值对 - 使用完整的B+Tree算法
 */
int kvs_btree_set(btree_t *btree, char *key, char *value) {
    if (!btree || !key || !value) {
        return KV_ERR_PARAM;
    }
    
    size_t key_len = strlen(key);
    size_t value_len = strlen(value);
    
    // 检查长度限制
    if (key_len > BTREE_KEY_MAX_LEN || value_len > BTREE_VALUE_MAX_LEN) {
        return KV_ERR_PARAM;
    }
    
    // 获取写锁
    pthread_rwlock_wrlock(&btree->tree_lock);
    
    int ret = btree_insert_complete(btree, key, key_len, value, value_len);
    
    // 释放锁
    pthread_rwlock_unlock(&btree->tree_lock);
    
    return ret;
}

/**
 * 获取键对应的值 - 保持不变，使用现有的搜索实现
 */
char* kvs_btree_get(btree_t *btree, char *key) {
    if (!btree || !key) {
        return NULL;
    }
    
    size_t key_len = strlen(key);
    
    // 获取读锁
    pthread_rwlock_rdlock(&btree->tree_lock);
    
    char *result = btree_search_internal(btree, key, key_len);
    
    if (result) {
        btree->read_count++;
    }
    
    // 释放锁
    pthread_rwlock_unlock(&btree->tree_lock);
    
    return result;
}

/**
 * 删除键值对 - 使用完整的B+Tree算法
 */
int kvs_btree_delete(btree_t *btree, char *key) {
    if (!btree || !key) {
        return KV_ERR_PARAM;
    }
    
    size_t key_len = strlen(key);
    
    // 获取写锁
    pthread_rwlock_wrlock(&btree->tree_lock);
    
    int ret = btree_delete_complete(btree, key, key_len);
    
    // 释放锁
    pthread_rwlock_unlock(&btree->tree_lock);
    
    return ret;
}

/**
 * 修改键对应的值 - 使用完整的B+Tree算法
 */
int kvs_btree_modify(btree_t *btree, char *key, char *value) {
    if (!btree || !key || !value) {
        return KV_ERR_PARAM;
    }
    
    size_t key_len = strlen(key);
    size_t value_len = strlen(value);
    
    // 检查长度限制
    if (key_len > BTREE_KEY_MAX_LEN || value_len > BTREE_VALUE_MAX_LEN) {
        return KV_ERR_PARAM;
    }
    
    // 获取写锁
    pthread_rwlock_wrlock(&btree->tree_lock);
    
    // 先检查键是否存在
    char *existing_value = btree_search_internal(btree, key, key_len);
    if (!existing_value) {
        pthread_rwlock_unlock(&btree->tree_lock);
        return 1;  // 键不存在，返回正数
    }
    
    free(existing_value);  // 释放临时值
    
    // 更新值（完整算法会自动处理键已存在的情况）
    int ret = btree_insert_complete(btree, key, key_len, value, value_len);
    
    // 释放锁
    pthread_rwlock_unlock(&btree->tree_lock);
    
    return ret == KV_ERR_NONE ? 0 : ret;
}

/**
 * 获取键数量
 */
int kvs_btree_count(btree_t *btree) {
    if (!btree) {
        return 0;
    }
    
    // 获取读锁
    pthread_rwlock_rdlock(&btree->tree_lock);
    
    int count = (int)btree->total_keys;
    
    // 释放锁
    pthread_rwlock_unlock(&btree->tree_lock);
    
    return count;
}

/**
 * 创建B+Tree存储引擎
 */
btree_t* kv_store_btree_create(int order) {
    if (order <= 0) {
        order = BTREE_DEFAULT_ORDER;
    }
    
    return btree_create(order);
}

/**
 * 销毁B+Tree存储引擎
 */
void kv_store_btree_destroy(btree_t *btree) {
    if (btree) {
        btree_destroy(btree);
    }
} 