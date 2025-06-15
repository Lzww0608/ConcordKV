/*
 * @Author: Lzww0608  
 * @Date: 2025-5-31 23:42:03
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-15 16:35:47
 * @Description: ConcordKV B+Tree存储引擎实现
 */
#define _GNU_SOURCE     // 启用扩展函数
#define _POSIX_C_SOURCE 200809L  // 启用POSIX扩展

#include "kvstore_btree.h"
#include "kv_error.h"
#include "kv_memory.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>

// === 工具函数实现 ===

/**
 * 比较两个键的大小
 */
int btree_key_compare(const char *key1, size_t len1, const char *key2, size_t len2) {
    if (!key1 || !key2) {
        return key1 ? 1 : (key2 ? -1 : 0);
    }
    
    size_t min_len = len1 < len2 ? len1 : len2;
    int cmp = memcmp(key1, key2, min_len);
    
    if (cmp != 0) {
        return cmp;
    }
    
    // 前缀相同，比较长度
    if (len1 < len2) return -1;
    if (len1 > len2) return 1;
    return 0;
}

/**
 * 复制键
 */
char* btree_key_copy(const char *key, size_t len) {
    if (!key || len == 0) return NULL;
    
    char *copy = (char*)kv_store_malloc(len + 1);
    if (!copy) {
        KV_ERROR(KV_ERR_MEM, "Failed to allocate memory for key copy");
        return NULL;
    }
    
    memcpy(copy, key, len);
    copy[len] = '\0';  // 确保字符串结尾
    return copy;
}

/**
 * 复制值
 */
char* btree_value_copy(const char *value, size_t len) {
    if (!value || len == 0) return NULL;
    
    char *copy = (char*)kv_store_malloc(len + 1);
    if (!copy) {
        KV_ERROR(KV_ERR_MEM, "Failed to allocate memory for value copy");
        return NULL;
    }
    
    memcpy(copy, value, len);
    copy[len] = '\0';  // 确保字符串结尾
    return copy;
}

// === B+Tree节点操作函数实现 ===

/**
 * 创建B+Tree节点
 */
btree_node_t* btree_node_create(btree_node_type_t type, int order) {
    if (order < BTREE_MIN_ORDER || order > BTREE_MAX_ORDER) {
        KV_ERROR(KV_ERR_PARAM, "Invalid B+Tree order: %d", order);
        return NULL;
    }
    
    btree_node_t *node = (btree_node_t*)kv_store_calloc(1, sizeof(btree_node_t));
    if (!node) {
        KV_ERROR(KV_ERR_MEM, "Failed to allocate memory for B+Tree node");
        return NULL;
    }
    
    // 初始化节点基本信息
    node->type = type;
    node->is_leaf = (type == BTREE_NODE_LEAF);
    node->key_count = 0;
    node->max_keys = order;
    node->parent = NULL;
    node->next = NULL;
    node->prev = NULL;
    node->version = 0;
    
    // 分配键数组
    node->keys = (char**)kv_store_calloc(order, sizeof(char*));
    if (!node->keys) {
        KV_ERROR(KV_ERR_MEM, "Failed to allocate memory for keys array");
        kv_store_free(node);
        return NULL;
    }
    
    // 分配键长度数组
    node->key_lens = (size_t*)kv_store_calloc(order, sizeof(size_t));
    if (!node->key_lens) {
        KV_ERROR(KV_ERR_MEM, "Failed to allocate memory for key lengths array");
        kv_store_free(node->keys);
        kv_store_free(node);
        return NULL;
    }
    
    if (node->is_leaf) {
        // 叶子节点：分配值数组和值长度数组
        node->values = (char**)kv_store_calloc(order, sizeof(char*));
        if (!node->values) {
            KV_ERROR(KV_ERR_MEM, "Failed to allocate memory for values array");
            kv_store_free(node->key_lens);
            kv_store_free(node->keys);
            kv_store_free(node);
            return NULL;
        }
        
        node->value_lens = (size_t*)kv_store_calloc(order, sizeof(size_t));
        if (!node->value_lens) {
            KV_ERROR(KV_ERR_MEM, "Failed to allocate memory for value lengths array");
            kv_store_free(node->values);
            kv_store_free(node->key_lens);
            kv_store_free(node->keys);
            kv_store_free(node);
            return NULL;
        }
    } else {
        // 内部节点：分配子节点指针数组(需要order+1个子节点)
        node->children = (btree_node_t**)kv_store_calloc(order + 1, sizeof(btree_node_t*));
        if (!node->children) {
            KV_ERROR(KV_ERR_MEM, "Failed to allocate memory for children array");
            kv_store_free(node->key_lens);
            kv_store_free(node->keys);
            kv_store_free(node);
            return NULL;
        }
        node->value_lens = NULL;  // 内部节点不使用值长度数组
    }
    
    // 初始化读写锁
    if (pthread_rwlock_init(&node->lock, NULL) != 0) {
        KV_ERROR(KV_ERR_SYS, "Failed to initialize node rwlock");
        if (node->is_leaf) {
            kv_store_free(node->value_lens);
            kv_store_free(node->values);
        } else {
            kv_store_free(node->children);
        }
        kv_store_free(node->key_lens);
        kv_store_free(node->keys);
        kv_store_free(node);
        return NULL;
    }
    
    return node;
}

/**
 * 销毁B+Tree节点(递归销毁所有子节点)
 */
void btree_node_destroy(btree_node_t *node) {
    if (!node) return;
    
    // 销毁所有键和值
    for (int i = 0; i < node->key_count; i++) {
        if (node->keys[i]) {
            kv_store_free(node->keys[i]);
        }
        
        if (node->is_leaf && node->values[i]) {
            kv_store_free(node->values[i]);
        }
    }
    
    // 递归销毁子节点
    if (!node->is_leaf && node->children) {
        for (int i = 0; i <= node->key_count; i++) {
            if (node->children[i]) {
                btree_node_destroy(node->children[i]);
            }
        }
        kv_store_free(node->children);
    }
    
    // 释放数组内存
    if (node->keys) kv_store_free(node->keys);
    if (node->key_lens) kv_store_free(node->key_lens);
    if (node->is_leaf) {
        if (node->values) kv_store_free(node->values);
        if (node->value_lens) kv_store_free(node->value_lens);
    }
    
    // 销毁锁
    pthread_rwlock_destroy(&node->lock);
    
    // 释放节点内存
    kv_store_free(node);
}

/**
 * 在节点中查找键的位置
 */
int btree_node_find_key(btree_node_t *node, const char *key, size_t key_len, bool exact_match) {
    if (!node || !key) return -1;
    
    int left = 0;
    int right = node->key_count - 1;
    int result = node->key_count;  // 默认插入位置为末尾
    
    // 二分查找
    while (left <= right) {
        int mid = left + (right - left) / 2;
        int cmp = btree_key_compare(key, key_len, node->keys[mid], node->key_lens[mid]);
        
        if (cmp == 0) {
            // 找到精确匹配
            return mid;
        } else if (cmp < 0) {
            // 目标键小于中间键
            result = mid;
            right = mid - 1;
        } else {
            // 目标键大于中间键
            left = mid + 1;
        }
    }
    
    // 如果要求精确匹配但未找到
    if (exact_match) {
        return -1;
    }
    
    // 返回插入位置
    return result;
}

/**
 * 检查节点是否已满
 */
bool btree_node_is_full(btree_node_t *node) {
    if (!node) return false;
    return node->key_count >= node->max_keys;
}

/**
 * 检查节点是否需要合并(键数量过少)
 */
bool btree_node_need_merge(btree_node_t *node) {
    if (!node) return false;
    
    // 根节点的特殊情况
    if (!node->parent) {
        return node->key_count == 0;
    }
    
    // 非根节点的最小键数量为 ceil(max_keys/2) - 1
    int min_keys = (node->max_keys + 1) / 2 - 1;
    return node->key_count < min_keys;
}

/**
 * 在节点中插入键值对
 */
int btree_node_insert_at(btree_node_t *node, int index, const char *key, size_t key_len, 
                        void *value, size_t value_len) {
    if (!node || !key || index < 0 || index > node->key_count) {
        return KV_ERR_PARAM;
    }
    
    if (btree_node_is_full(node)) {
        return KV_ERR_BUSY;
    }
    
    // 向右移动现有元素为新元素腾出空间
    for (int i = node->key_count; i > index; i--) {
        node->keys[i] = node->keys[i-1];
        node->key_lens[i] = node->key_lens[i-1];
        
        if (node->is_leaf) {
            node->values[i] = node->values[i-1];
            node->value_lens[i] = node->value_lens[i-1];
        } else {
            node->children[i+1] = node->children[i];
        }
    }
    
    // 插入新的键
    node->keys[index] = btree_key_copy(key, key_len);
    if (!node->keys[index]) {
        return KV_ERR_MEM;
    }
    node->key_lens[index] = key_len;
    
    if (node->is_leaf) {
        // 叶子节点：插入值
        if (value && value_len > 0) {
            node->values[index] = btree_value_copy((const char*)value, value_len);
            if (!node->values[index]) {
                free(node->keys[index]);
                return KV_ERR_MEM;
            }
            node->value_lens[index] = value_len;
        } else {
            node->values[index] = NULL;
            node->value_lens[index] = 0;
        }
    } else {
        // 内部节点：设置子节点指针
        node->children[index+1] = (btree_node_t*)value;
        if (value) {
            ((btree_node_t*)value)->parent = node;
        }
    }
    
    node->key_count++;
    node->version++;
    
    return KV_ERR_NONE;
}

/**
 * 从节点中删除指定位置的键值对
 */
int btree_node_remove_at(btree_node_t *node, int index) {
    if (!node || index < 0 || index >= node->key_count) {
        return KV_ERR_PARAM;
    }
    
    // 释放要删除的键和值
    if (node->keys[index]) {
        free(node->keys[index]);
    }
    
    if (node->is_leaf && node->values[index]) {
        free(node->values[index]);
    }
    
    // 向左移动剩余元素
    for (int i = index; i < node->key_count - 1; i++) {
        node->keys[i] = node->keys[i+1];
        node->key_lens[i] = node->key_lens[i+1];
        
        if (node->is_leaf) {
            node->values[i] = node->values[i+1];
            node->value_lens[i] = node->value_lens[i+1];
        } else {
            node->children[i+1] = node->children[i+2];
        }
    }
    
    // 清理最后一个元素
    node->keys[node->key_count-1] = NULL;
    node->key_lens[node->key_count-1] = 0;
    
    if (node->is_leaf) {
        node->values[node->key_count-1] = NULL;
        node->value_lens[node->key_count-1] = 0;
    } else {
        node->children[node->key_count] = NULL;
    }
    
    node->key_count--;
    node->version++;
    
    return KV_ERR_NONE;
}

/**
 * 验证节点结构的正确性
 */
bool btree_node_validate(btree_node_t *node) {
    if (!node) return false;
    
    // 检查基本属性
    if (node->key_count < 0 || node->key_count > node->max_keys) {
        return false;
    }
    
    if (node->max_keys < BTREE_MIN_ORDER || node->max_keys > BTREE_MAX_ORDER) {
        return false;
    }
    
    // 检查键的顺序
    for (int i = 1; i < node->key_count; i++) {
        if (btree_key_compare(node->keys[i-1], node->key_lens[i-1], 
                             node->keys[i], node->key_lens[i]) >= 0) {
            return false;  // 键不是严格递增的
        }
    }
    
    // 检查指针的一致性
    if (!node->is_leaf) {
        // 内部节点应该有 key_count + 1 个子节点
        for (int i = 0; i <= node->key_count; i++) {
            if (!node->children[i]) {
                return false;  // 缺少子节点
            }
            if (node->children[i]->parent != node) {
                return false;  // 父指针不一致
            }
        }
    }
    
    return true;
}

// === B+Tree主要操作函数实现 ===

/**
 * 创建B+Tree
 */
btree_t* btree_create(int order) {
    if (order < BTREE_MIN_ORDER || order > BTREE_MAX_ORDER) {
        KV_ERROR(KV_ERR_PARAM, "Invalid B+Tree order: %d", order);
        return NULL;
    }
    
    btree_t *tree = (btree_t*)kv_store_calloc(1, sizeof(btree_t));
    if (!tree) {
        KV_ERROR(KV_ERR_MEM, "Failed to allocate memory for B+Tree");
        return NULL;
    }
    
    // 创建根节点(初始为叶子节点)
    tree->root = btree_node_create(BTREE_NODE_LEAF, order);
    if (!tree->root) {
        kv_store_free(tree);
        return NULL;
    }
    
    // 初始化树属性
    tree->first_leaf = tree->root;
    tree->last_leaf = tree->root;
    tree->order = order;
    tree->total_keys = 0;
    tree->total_size = 0;
    tree->height = 1;
    tree->internal_nodes = 0;
    tree->leaf_nodes = 1;
    tree->version = 0;
    tree->read_count = 0;
    tree->write_count = 0;
    tree->delete_count = 0;
    
    // 初始化树级锁
    if (pthread_rwlock_init(&tree->tree_lock, NULL) != 0) {
        KV_ERROR(KV_ERR_SYS, "Failed to initialize tree rwlock");
        btree_node_destroy(tree->root);
        kv_store_free(tree);
        return NULL;
    }
    
    return tree;
}

/**
 * 销毁B+Tree
 */
void btree_destroy(btree_t *tree) {
    if (!tree) return;
    
    // 递归销毁所有节点
    if (tree->root) {
        btree_node_destroy(tree->root);
    }
    
    // 销毁锁
    pthread_rwlock_destroy(&tree->tree_lock);
    
    // 释放树结构
    kv_store_free(tree);
}

/**
 * 获取B+Tree中的键数量
 */
size_t btree_count(btree_t *tree) {
    if (!tree) return 0;
    return tree->total_keys;
}

/**
 * 获取B+Tree内存使用量
 */
size_t btree_memory_usage(btree_t *tree) {
    if (!tree) return 0;
    
    // 基础结构大小
    size_t usage = sizeof(btree_t);
    
    // 估算节点内存使用(简化计算)
    usage += tree->leaf_nodes * sizeof(btree_node_t);
    usage += tree->internal_nodes * sizeof(btree_node_t);
    
    // 估算键值数据大小
    usage += tree->total_size;
    
    return usage;
}

/**
 * 打印B+Tree结构(调试用)
 */
void btree_print(btree_t *tree) {
    if (!tree || !tree->root) {
        printf("Empty B+Tree\n");
        return;
    }
    
    printf("B+Tree (order=%d, keys=%zu, height=%u):\n", 
           tree->order, tree->total_keys, tree->height);
    
    // 简单的根节点信息输出
    btree_node_t *root = tree->root;
    printf("Root node: %s, keys=%d\n", 
           root->is_leaf ? "Leaf" : "Internal", root->key_count);
    
    if (root->key_count > 0) {
        printf("First key: %.*s\n", (int)root->key_lens[0], root->keys[0]);
        if (root->key_count > 1) {
            printf("Last key: %.*s\n", 
                   (int)root->key_lens[root->key_count-1], 
                   root->keys[root->key_count-1]);
        }
    }
}

// === 完整B+Tree算法实现 ===

/**
 * 查找节点的左兄弟节点
 */
btree_node_t* btree_find_left_sibling(btree_node_t *node, int *parent_index) {
    if (!node || !node->parent) {
        return NULL;
    }
    
    btree_node_t *parent = node->parent;
    
    // 找到节点在父节点中的位置
    for (int i = 0; i <= parent->key_count; i++) {
        if (parent->children[i] == node) {
            if (parent_index) *parent_index = i;
            if (i > 0) {
                return parent->children[i-1];  // 返回左兄弟
            }
            break;
        }
    }
    
    return NULL;  // 没有左兄弟
}

/**
 * 查找节点的右兄弟节点
 */
btree_node_t* btree_find_right_sibling(btree_node_t *node, int *parent_index) {
    if (!node || !node->parent) {
        return NULL;
    }
    
    btree_node_t *parent = node->parent;
    
    // 找到节点在父节点中的位置
    for (int i = 0; i <= parent->key_count; i++) {
        if (parent->children[i] == node) {
            if (parent_index) *parent_index = i;
            if (i < parent->key_count) {
                return parent->children[i+1];  // 返回右兄弟
            }
            break;
        }
    }
    
    return NULL;  // 没有右兄弟
}

/**
 * 分裂叶子节点
 */
int btree_split_leaf_node(btree_t *tree, btree_node_t *node, 
                         const char *key, size_t key_len,
                         const char *value, size_t value_len) {
    if (!tree || !node || !node->is_leaf) {
        return KV_ERR_PARAM;
    }
    
    // 创建新的右节点
    btree_node_t *right = btree_node_create(BTREE_NODE_LEAF, node->max_keys);
    if (!right) {
        return KV_ERR_MEM;
    }
    
    // 计算分裂点（中点）
    int split_point = (node->max_keys + 1) / 2;
    int total_keys = node->key_count + 1;  // 包括要插入的新键
    
    // 创建临时数组存储所有键值（包括新插入的）
    char **temp_keys = (char**)malloc(total_keys * sizeof(char*));
    size_t *temp_key_lens = (size_t*)malloc(total_keys * sizeof(size_t));
    char **temp_values = (char**)malloc(total_keys * sizeof(char*));
    size_t *temp_value_lens = (size_t*)malloc(total_keys * sizeof(size_t));
    
    if (!temp_keys || !temp_key_lens || !temp_values || !temp_value_lens) {
        free(temp_keys);
        free(temp_key_lens); 
        free(temp_values);
        free(temp_value_lens);
        btree_node_destroy(right);
        return KV_ERR_MEM;
    }
    
    // 找到新键的插入位置
    int insert_pos = btree_node_find_key(node, key, key_len, false);
    
    // 构建排序后的临时数组
    int temp_idx = 0;
    
    // 复制插入位置之前的键
    for (int i = 0; i < insert_pos && i < node->key_count; i++) {
        temp_keys[temp_idx] = node->keys[i];
        temp_key_lens[temp_idx] = node->key_lens[i];
        temp_values[temp_idx] = node->values[i];
        temp_value_lens[temp_idx] = node->value_lens[i];
        temp_idx++;
    }
    
    // 插入新键值
    temp_keys[temp_idx] = btree_key_copy(key, key_len);
    temp_key_lens[temp_idx] = key_len;
    temp_values[temp_idx] = btree_value_copy(value, value_len);
    temp_value_lens[temp_idx] = value_len;
    
    if (!temp_keys[temp_idx] || !temp_values[temp_idx]) {
        // 清理已分配的内存
        for (int j = 0; j < temp_idx; j++) {
            // 注意：不要释放原节点的键值，它们会在后面重新分配
        }
        if (temp_keys[temp_idx]) free(temp_keys[temp_idx]);
        if (temp_values[temp_idx]) free(temp_values[temp_idx]);
        free(temp_keys);
        free(temp_key_lens);
        free(temp_values);
        free(temp_value_lens);
        btree_node_destroy(right);
        return KV_ERR_MEM;
    }
    temp_idx++;
    
    // 复制插入位置之后的键
    for (int i = insert_pos; i < node->key_count; i++) {
        temp_keys[temp_idx] = node->keys[i];
        temp_key_lens[temp_idx] = node->key_lens[i];
        temp_values[temp_idx] = node->values[i];
        temp_value_lens[temp_idx] = node->value_lens[i];
        temp_idx++;
    }
    
    // 清空原节点的指针（但不释放内存，因为要重新分配）
    for (int i = 0; i < node->key_count; i++) {
        node->keys[i] = NULL;
        node->values[i] = NULL;
    }
    node->key_count = 0;
    
    // 重新分配键值到左右节点
    // 左节点保留前split_point个键
    for (int i = 0; i < split_point; i++) {
        node->keys[i] = temp_keys[i];
        node->key_lens[i] = temp_key_lens[i];
        node->values[i] = temp_values[i];
        node->value_lens[i] = temp_value_lens[i];
        node->key_count++;
    }
    
    // 右节点保存剩余的键
    for (int i = split_point; i < total_keys; i++) {
        int right_idx = i - split_point;
        right->keys[right_idx] = temp_keys[i];
        right->key_lens[right_idx] = temp_key_lens[i];
        right->values[right_idx] = temp_values[i];
        right->value_lens[right_idx] = temp_value_lens[i];
        right->key_count++;
    }
    
    // 更新叶子节点链表
    right->next = node->next;
    if (node->next) {
        node->next->prev = right;
    } else {
        tree->last_leaf = right;  // 更新最后一个叶子节点
    }
    node->next = right;
    right->prev = node;
    
    // 更新树统计信息
    tree->leaf_nodes++;
    tree->total_keys++;
    tree->version++;
    
    // 清理临时数组
    free(temp_keys);
    free(temp_key_lens);
    free(temp_values);
    free(temp_value_lens);
    
    // 向父节点插入分隔键
    char *separator_key = btree_key_copy(right->keys[0], right->key_lens[0]);
    if (!separator_key) {
        return KV_ERR_MEM;
    }
    
    int ret = btree_insert_to_parent(tree, node, right, separator_key, right->key_lens[0]);
    free(separator_key);
    
    return ret;
}

/**
 * 分裂内部节点
 */
int btree_split_internal_node(btree_t *tree, btree_node_t *node,
                             const char *key, size_t key_len,
                             btree_node_t *right_child) {
    if (!tree || !node || node->is_leaf) {
        return KV_ERR_PARAM;
    }
    
    // 创建新的右节点
    btree_node_t *right = btree_node_create(BTREE_NODE_INTERNAL, node->max_keys);
    if (!right) {
        return KV_ERR_MEM;
    }
    
    // 计算分裂点
    int split_point = node->max_keys / 2;
    int total_keys = node->key_count + 1;
    
    // 创建临时数组
    char **temp_keys = (char**)malloc(total_keys * sizeof(char*));
    size_t *temp_key_lens = (size_t*)malloc(total_keys * sizeof(size_t));
    btree_node_t **temp_children = (btree_node_t**)malloc((total_keys + 1) * sizeof(btree_node_t*));
    
    if (!temp_keys || !temp_key_lens || !temp_children) {
        free(temp_keys);
        free(temp_key_lens);
        free(temp_children);
        btree_node_destroy(right);
        return KV_ERR_MEM;
    }
    
    // 找到插入位置
    int insert_pos = btree_node_find_key(node, key, key_len, false);
    
    // 构建临时数组
    int temp_idx = 0;
    
    // 复制插入位置之前的键和子节点
    for (int i = 0; i < insert_pos && i < node->key_count; i++) {
        temp_keys[temp_idx] = node->keys[i];
        temp_key_lens[temp_idx] = node->key_lens[i];
        temp_children[temp_idx] = node->children[i];
        temp_idx++;
    }
    
    // 复制插入位置的左子节点
    temp_children[temp_idx] = node->children[insert_pos];
    
    // 插入新键和右子节点
    temp_keys[temp_idx] = btree_key_copy(key, key_len);
    temp_key_lens[temp_idx] = key_len;
    temp_children[temp_idx + 1] = right_child;
    temp_idx++;
    
    // 复制插入位置之后的键和子节点
    for (int i = insert_pos; i < node->key_count; i++) {
        temp_keys[temp_idx] = node->keys[i];
        temp_key_lens[temp_idx] = node->key_lens[i];
        temp_children[temp_idx + 1] = node->children[i + 1];
        temp_idx++;
    }
    
    // 清空原节点
    for (int i = 0; i < node->key_count; i++) {
        node->keys[i] = NULL;
        node->children[i] = NULL;
    }
    node->children[node->key_count] = NULL;
    node->key_count = 0;
    
    // 重新分配键和子节点到左节点
    for (int i = 0; i < split_point; i++) {
        node->keys[i] = temp_keys[i];
        node->key_lens[i] = temp_key_lens[i];
        node->children[i] = temp_children[i];
        if (temp_children[i]) {
            temp_children[i]->parent = node;
        }
        node->key_count++;
    }
    node->children[split_point] = temp_children[split_point];
    if (temp_children[split_point]) {
        temp_children[split_point]->parent = node;
    }
    
    // 分隔键（不复制到右节点，而是提升到父节点）
    char *separator_key = temp_keys[split_point];
    size_t separator_key_len = temp_key_lens[split_point];
    
    // 右节点获得剩余的键和子节点
    for (int i = split_point + 1; i < total_keys; i++) {
        int right_idx = i - split_point - 1;
        right->keys[right_idx] = temp_keys[i];
        right->key_lens[right_idx] = temp_key_lens[i];
        right->children[right_idx] = temp_children[i];
        if (temp_children[i]) {
            temp_children[i]->parent = right;
        }
        right->key_count++;
    }
    right->children[right->key_count] = temp_children[total_keys];
    if (temp_children[total_keys]) {
        temp_children[total_keys]->parent = right;
    }
    
    // 更新树统计信息
    tree->internal_nodes++;
    tree->version++;
    
    // 清理临时数组
    free(temp_keys);
    free(temp_key_lens);
    free(temp_children);
    
    // 向父节点插入分隔键
    int ret = btree_insert_to_parent(tree, node, right, separator_key, separator_key_len);
    free(separator_key);
    
    return ret;
}

/**
 * 向父节点插入键和子节点指针
 */
int btree_insert_to_parent(btree_t *tree, btree_node_t *left, btree_node_t *right,
                          const char *key, size_t key_len) {
    if (!tree || !left || !right) {
        return KV_ERR_PARAM;
    }
    
    // 如果左节点是根节点，创建新的根节点
    if (!left->parent) {
        btree_node_t *new_root = btree_node_create(BTREE_NODE_INTERNAL, left->max_keys);
        if (!new_root) {
            return KV_ERR_MEM;
        }
        
        // 设置新根节点
        new_root->keys[0] = btree_key_copy(key, key_len);
        new_root->key_lens[0] = key_len;
        new_root->children[0] = left;
        new_root->children[1] = right;
        new_root->key_count = 1;
        
        left->parent = new_root;
        right->parent = new_root;
        
        tree->root = new_root;
        tree->height++;
        tree->internal_nodes++;
        
        return KV_ERR_NONE;
    }
    
    btree_node_t *parent = left->parent;
    right->parent = parent;
    
    // 如果父节点有空间，直接插入
    if (!btree_node_is_full(parent)) {
        int insert_pos = btree_node_find_key(parent, key, key_len, false);
        
        // 向右移动现有元素
        for (int i = parent->key_count; i > insert_pos; i--) {
            parent->keys[i] = parent->keys[i-1];
            parent->key_lens[i] = parent->key_lens[i-1];
            parent->children[i+1] = parent->children[i];
        }
        
        // 插入新键和子节点
        parent->keys[insert_pos] = btree_key_copy(key, key_len);
        parent->key_lens[insert_pos] = key_len;
        parent->children[insert_pos + 1] = right;
        parent->key_count++;
        
        return KV_ERR_NONE;
    }
    
    // 父节点已满，需要分裂
    return btree_split_internal_node(tree, parent, key, key_len, right);
}

/**
 * 完整的B+Tree插入实现
 */
int btree_insert_complete(btree_t *tree, const char *key, size_t key_len,
                         const char *value, size_t value_len) {
    if (!tree || !key || !value) {
        return KV_ERR_PARAM;
    }
    
    return btree_insert_recursive(tree, tree->root, key, key_len, value, value_len);
}

/**
 * 递归插入到叶子节点
 */
int btree_insert_recursive(btree_t *tree, btree_node_t *node,
                          const char *key, size_t key_len,
                          const char *value, size_t value_len) {
    if (!tree || !node) {
        return KV_ERR_PARAM;
    }
    
    if (node->is_leaf) {
        // 检查键是否已存在
        int index = btree_node_find_key(node, key, key_len, true);
        if (index >= 0) {
            // 键已存在，更新值
            if (node->values[index]) {
                free(node->values[index]);
            }
            node->values[index] = btree_value_copy(value, value_len);
            node->value_lens[index] = value_len;
            return node->values[index] ? KV_ERR_NONE : KV_ERR_MEM;
        }
        
        // 如果叶子节点有空间，直接插入
        if (!btree_node_is_full(node)) {
            int insert_pos = btree_node_find_key(node, key, key_len, false);
            int ret = btree_node_insert_at(node, insert_pos, key, key_len, (void*)value, value_len);
            if (ret == KV_ERR_NONE) {
                tree->total_keys++;
                tree->write_count++;
            }
            return ret;
        }
        
        // 叶子节点已满，需要分裂
        return btree_split_leaf_node(tree, node, key, key_len, value, value_len);
    } else {
        // 内部节点，找到合适的子节点
        int index = 0;
        
        // 找到正确的子节点位置
        while (index < node->key_count && 
               btree_key_compare(key, key_len, node->keys[index], node->key_lens[index]) >= 0) {
            index++;
        }
        
        // 递归到正确的子节点
        return btree_insert_recursive(tree, node->children[index], 
                                    key, key_len, value, value_len);
    }
}

/**
 * 从兄弟节点借用键
 */
int btree_borrow_from_sibling(btree_t *tree, btree_node_t *node, 
                             btree_node_t *sibling, bool borrow_from_left) {
    if (!tree || !node || !sibling || !node->parent) {
        return KV_ERR_PARAM;
    }
    
    btree_node_t *parent = node->parent;
    int node_index = -1;
    
    // 找到节点在父节点中的位置
    for (int i = 0; i <= parent->key_count; i++) {
        if (parent->children[i] == node) {
            node_index = i;
            break;
        }
    }
    
    if (node_index == -1) {
        return KV_ERR_SYS;  // 找不到节点位置，树结构异常
    }
    
    if (borrow_from_left) {
        int sibling_index = node_index - 1;
        
        if (node->is_leaf) {
            // 叶子节点从左兄弟借用
            char *borrowed_key = sibling->keys[sibling->key_count - 1];
            size_t borrowed_key_len = sibling->key_lens[sibling->key_count - 1];
            char *borrowed_value = sibling->values[sibling->key_count - 1];
            size_t borrowed_value_len = sibling->value_lens[sibling->key_count - 1];
            
            // 在当前节点前端插入借用的键值
            for (int i = node->key_count; i > 0; i--) {
                node->keys[i] = node->keys[i-1];
                node->key_lens[i] = node->key_lens[i-1];
                node->values[i] = node->values[i-1];
                node->value_lens[i] = node->value_lens[i-1];
            }
            
            node->keys[0] = borrowed_key;
            node->key_lens[0] = borrowed_key_len;
            node->values[0] = borrowed_value;
            node->value_lens[0] = borrowed_value_len;
            node->key_count++;
            
            // 从兄弟节点移除
            sibling->keys[sibling->key_count - 1] = NULL;
            sibling->values[sibling->key_count - 1] = NULL;
            sibling->key_count--;
            
            // 更新父节点的分隔键
            if (parent->keys[sibling_index]) {
                free(parent->keys[sibling_index]);
            }
            parent->keys[sibling_index] = btree_key_copy(node->keys[0], node->key_lens[0]);
            parent->key_lens[sibling_index] = node->key_lens[0];
            
        } else {
            // 内部节点从左兄弟借用
            return KV_ERR_NOT_SUPPORTED;
        }
    } else {
        int sibling_index = node_index + 1;
        
        if (node->is_leaf) {
            // 叶子节点从右兄弟借用
            char *borrowed_key = sibling->keys[0];
            size_t borrowed_key_len = sibling->key_lens[0];
            char *borrowed_value = sibling->values[0];
            size_t borrowed_value_len = sibling->value_lens[0];
            
            // 在当前节点末尾添加借用的键值
            node->keys[node->key_count] = borrowed_key;
            node->key_lens[node->key_count] = borrowed_key_len;
            node->values[node->key_count] = borrowed_value;
            node->value_lens[node->key_count] = borrowed_value_len;
            node->key_count++;
            
            // 从兄弟节点移除并左移
            for (int i = 0; i < sibling->key_count - 1; i++) {
                sibling->keys[i] = sibling->keys[i+1];
                sibling->key_lens[i] = sibling->key_lens[i+1];
                sibling->values[i] = sibling->values[i+1];
                sibling->value_lens[i] = sibling->value_lens[i+1];
            }
            sibling->keys[sibling->key_count - 1] = NULL;
            sibling->values[sibling->key_count - 1] = NULL;
            sibling->key_count--;
            
            // 更新父节点的分隔键
            if (parent->keys[node_index]) {
                free(parent->keys[node_index]);
            }
            parent->keys[node_index] = btree_key_copy(sibling->keys[0], sibling->key_lens[0]);
            parent->key_lens[node_index] = sibling->key_lens[0];
        } else {
            // 内部节点从右兄弟借用
            return KV_ERR_NOT_SUPPORTED;
        }
    }
    
    return KV_ERR_NONE;
}

/**
 * 合并两个节点
 */
int btree_merge_nodes(btree_t *tree, btree_node_t *left, btree_node_t *right,
                     const char *separator_key, size_t separator_key_len) {
    if (!tree || !left || !right) {
        return KV_ERR_PARAM;
    }
    
    if (left->is_leaf) {
        // 合并叶子节点
        for (int i = 0; i < right->key_count; i++) {
            left->keys[left->key_count + i] = right->keys[i];
            left->key_lens[left->key_count + i] = right->key_lens[i];
            left->values[left->key_count + i] = right->values[i];
            left->value_lens[left->key_count + i] = right->value_lens[i];
            
            // 防止在销毁right时重复释放
            right->keys[i] = NULL;
            right->values[i] = NULL;
        }
        
        left->key_count += right->key_count;
        
        // 更新叶子节点链表
        left->next = right->next;
        if (right->next) {
            right->next->prev = left;
        } else {
            tree->last_leaf = left;
        }
        
    } else {
        // 合并内部节点
        // 首先添加分隔键
        left->keys[left->key_count] = btree_key_copy(separator_key, separator_key_len);
        left->key_lens[left->key_count] = separator_key_len;
        left->key_count++;
        
        // 然后合并右节点的键和子节点
        for (int i = 0; i < right->key_count; i++) {
            left->keys[left->key_count + i] = right->keys[i];
            left->key_lens[left->key_count + i] = right->key_lens[i];
            left->children[left->key_count + i] = right->children[i];
            
            if (right->children[i]) {
                right->children[i]->parent = left;
            }
            
            right->keys[i] = NULL;
            right->children[i] = NULL;
        }
        
        // 最后一个子节点
        left->children[left->key_count + right->key_count] = right->children[right->key_count];
        if (right->children[right->key_count]) {
            right->children[right->key_count]->parent = left;
        }
        right->children[right->key_count] = NULL;
        
        left->key_count += right->key_count;
    }
    
    // 销毁右节点
    right->key_count = 0;  // 防止重复释放
    btree_node_destroy(right);
    
    // 更新树统计信息
    if (left->is_leaf) {
        tree->leaf_nodes--;
    } else {
        tree->internal_nodes--;
    }
    
    return KV_ERR_NONE;
}

/**
 * 修复删除后可能违反B+Tree性质的节点
 */
int btree_fix_after_delete(btree_t *tree, btree_node_t *node) {
    if (!tree || !node) {
        return KV_ERR_PARAM;
    }
    
    // 根节点的特殊处理
    if (!node->parent) {
        // 如果根节点是叶子节点且为空，或者是内部节点但只有一个子节点
        if ((node->is_leaf && node->key_count == 0) || 
            (!node->is_leaf && node->key_count == 0 && node->children[0])) {
            
            if (!node->is_leaf) {
                // 内部节点只有一个子节点，提升子节点为新根
                tree->root = node->children[0];
                tree->root->parent = NULL;
                tree->height--;
                tree->internal_nodes--;
                
                node->children[0] = NULL;  // 防止级联销毁
                btree_node_destroy(node);
            }
        }
        return KV_ERR_NONE;
    }
    
    // 检查是否需要修复
    if (!btree_node_need_merge(node)) {
        return KV_ERR_NONE;  // 不需要修复
    }
    
    int parent_index;
    btree_node_t *left_sibling = btree_find_left_sibling(node, &parent_index);
    btree_node_t *right_sibling = btree_find_right_sibling(node, NULL);
    
    // 尝试从兄弟节点借用
    if (left_sibling && left_sibling->key_count > (left_sibling->max_keys + 1) / 2) {
        return btree_borrow_from_sibling(tree, node, left_sibling, true);
    }
    
    if (right_sibling && right_sibling->key_count > (right_sibling->max_keys + 1) / 2) {
        return btree_borrow_from_sibling(tree, node, right_sibling, false);
    }
    
    // 无法借用，需要合并
    btree_node_t *parent = node->parent;
    
    if (left_sibling) {
        // 与左兄弟合并
        char *separator_key = parent->keys[parent_index - 1];
        size_t separator_key_len = parent->key_lens[parent_index - 1];
        
        int ret = btree_merge_nodes(tree, left_sibling, node, separator_key, separator_key_len);
        if (ret != KV_ERR_NONE) {
            return ret;
        }
        
        // 从父节点移除分隔键
        btree_node_remove_at(parent, parent_index - 1);
        
    } else if (right_sibling) {
        // 与右兄弟合并
        char *separator_key = parent->keys[parent_index];
        size_t separator_key_len = parent->key_lens[parent_index];
        
        int ret = btree_merge_nodes(tree, node, right_sibling, separator_key, separator_key_len);
        if (ret != KV_ERR_NONE) {
            return ret;
        }
        
        // 从父节点移除分隔键
        btree_node_remove_at(parent, parent_index);
    }
    
    // 递归修复父节点
    return btree_fix_after_delete(tree, parent);
}

/**
 * 完整的B+Tree删除实现
 */
int btree_delete_complete(btree_t *tree, const char *key, size_t key_len) {
    if (!tree || !key) {
        return KV_ERR_PARAM;
    }
    
    return btree_delete_recursive(tree, tree->root, key, key_len);
}

/**
 * 递归删除叶子节点的键
 */
int btree_delete_recursive(btree_t *tree, btree_node_t *node,
                          const char *key, size_t key_len) {
    if (!tree || !node) {
        return KV_ERR_PARAM;
    }
    
    if (node->is_leaf) {
        // 在叶子节点中查找键
        int index = btree_node_find_key(node, key, key_len, true);
        if (index < 0) {
            return KV_ERR_NOT_FOUND;  // 键不存在
        }
        
        // 删除键值对
        int ret = btree_node_remove_at(node, index);
        if (ret == KV_ERR_NONE) {
            tree->total_keys--;
            tree->delete_count++;
            
            // 修复可能违反B+Tree性质的节点
            return btree_fix_after_delete(tree, node);
        }
        
        return ret;
    } else {
        // 内部节点，找到包含该键的子节点
        int index = btree_node_find_key(node, key, key_len, false);
        
        if (index >= node->key_count) {
            return btree_delete_recursive(tree, node->children[node->key_count], key, key_len);
        } else {
            int cmp = btree_key_compare(key, key_len, node->keys[index], node->key_lens[index]);
            if (cmp < 0) {
                return btree_delete_recursive(tree, node->children[index], key, key_len);
            } else {
                return btree_delete_recursive(tree, node->children[index + 1], key, key_len);
            }
        }
    }
} 