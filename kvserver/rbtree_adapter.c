/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV storage engine - rbtree_adapter.c
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "kv_store.h"
#include "rbtree_adapter.h"

// 定义私有的红黑树节点类型，匹配原始实现
typedef struct _rbtree_node {
    unsigned char color;
    struct _rbtree_node *right;
    struct _rbtree_node *left;
    struct _rbtree_node *parent;
    
    char *key;
    void *value;
} *private_rbtree_node_t;

// 定义私有的红黑树类型，匹配原始实现
typedef struct _rbtree {
    struct _rbtree_node *root;
    struct _rbtree_node *nil;
    int count;
} *private_rbtree_t;

// 红黑树颜色常量
#define RED   1
#define BLACK 2

// 查找节点的包装函数
static rbtree_node_t *adapter_rbtree_search(rbtree_t *tree, char *key) {
    private_rbtree_t priv_tree = (private_rbtree_t)tree;
    private_rbtree_node_t node = priv_tree->root;
    
    while (node != priv_tree->nil) {
        int cmp = strcmp(key, node->key);
        if (cmp < 0) {
            node = node->left;
        } else if (cmp > 0) {
            node = node->right;
        } else {
            return (rbtree_node_t *)node;
        }
    }
    
    return NULL;
}

// 创建红黑树节点的包装函数
static rbtree_node_t *adapter_rbtree_create_node(char *key, char *value) {
    private_rbtree_node_t node = (private_rbtree_node_t)kv_store_malloc(sizeof(struct _rbtree_node));
    if (!node) return NULL;
    
    node->key = strdup(key);
    if (!node->key) {
        kv_store_free(node);
        return NULL;
    }
    
    node->value = value;
    
    return (rbtree_node_t *)node;
}

// 左旋操作
static void _rbtree_left_rotate(private_rbtree_t tree, private_rbtree_node_t x) {
    private_rbtree_node_t y = x->right;
    
    x->right = y->left;
    if (y->left != tree->nil) {
        y->left->parent = x;
    }
    
    y->parent = x->parent;
    if (x->parent == tree->nil) {
        tree->root = y;
    } else if (x == x->parent->left) {
        x->parent->left = y;
    } else {
        x->parent->right = y;
    }
    
    y->left = x;
    x->parent = y;
}

// 右旋操作
static void _rbtree_right_rotate(private_rbtree_t tree, private_rbtree_node_t y) {
    private_rbtree_node_t x = y->left;
    
    y->left = x->right;
    if (x->right != tree->nil) {
        x->right->parent = y;
    }
    
    x->parent = y->parent;
    if (y->parent == tree->nil) {
        tree->root = x;
    } else if (y == y->parent->left) {
        y->parent->left = x;
    } else {
        y->parent->right = x;
    }
    
    x->right = y;
    y->parent = x;
}

// 插入修复
static void _rbtree_insert_fixup(private_rbtree_t tree, private_rbtree_node_t z) {
    while (z->parent->color == RED) {
        if (z->parent == z->parent->parent->left) {
            private_rbtree_node_t y = z->parent->parent->right;
            if (y->color == RED) {
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    _rbtree_left_rotate(tree, z);
                }
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                _rbtree_right_rotate(tree, z->parent->parent);
            }
        } else {
            private_rbtree_node_t y = z->parent->parent->left;
            if (y->color == RED) {
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    _rbtree_right_rotate(tree, z);
                }
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                _rbtree_left_rotate(tree, z->parent->parent);
            }
        }
    }
    tree->root->color = BLACK;
}

// 插入节点的包装函数
static void adapter_rbtree_insert_node(rbtree_t *tree, rbtree_node_t *z_node) {
    private_rbtree_t priv_tree = (private_rbtree_t)tree;
    private_rbtree_node_t z = (private_rbtree_node_t)z_node;
    
    private_rbtree_node_t y = priv_tree->nil;
    private_rbtree_node_t x = priv_tree->root;
    
    while (x != priv_tree->nil) {
        y = x;
        int cmp = strcmp(z->key, x->key);
        if (cmp < 0) {
            x = x->left;
        } else if (cmp > 0) {
            x = x->right;
        } else {
            // 键已存在，更新值
            x->value = z->value;
            kv_store_free(z->key);
            kv_store_free(z);
            return;
        }
    }
    
    z->parent = y;
    if (y == priv_tree->nil) {
        priv_tree->root = z;
    } else {
        int cmp = strcmp(z->key, y->key);
        if (cmp < 0) {
            y->left = z;
        } else {
            y->right = z;
        }
    }
    
    z->left = priv_tree->nil;
    z->right = priv_tree->nil;
    z->color = RED;
    
    _rbtree_insert_fixup(priv_tree, z);
    priv_tree->count++;
}

// 找到红黑树中的最小节点
static private_rbtree_node_t _rbtree_minimum(private_rbtree_t tree, private_rbtree_node_t x) {
    while (x->left != tree->nil) {
        x = x->left;
    }
    return x;
}

// 替换红黑树中的节点
static void _rbtree_transplant(private_rbtree_t tree, private_rbtree_node_t u, private_rbtree_node_t v) {
    if (u->parent == tree->nil) {
        tree->root = v;
    } else if (u == u->parent->left) {
        u->parent->left = v;
    } else {
        u->parent->right = v;
    }
    v->parent = u->parent;
}

// 删除修复
static void _rbtree_delete_fixup(private_rbtree_t tree, private_rbtree_node_t x) {
    while (x != tree->root && x->color == BLACK) {
        if (x == x->parent->left) {
            private_rbtree_node_t w = x->parent->right;
            if (w->color == RED) {
                w->color = BLACK;
                x->parent->color = RED;
                _rbtree_left_rotate(tree, x->parent);
                w = x->parent->right;
            }
            
            if (w->left->color == BLACK && w->right->color == BLACK) {
                w->color = RED;
                x = x->parent;
            } else {
                if (w->right->color == BLACK) {
                    w->left->color = BLACK;
                    w->color = RED;
                    _rbtree_right_rotate(tree, w);
                    w = x->parent->right;
                }
                
                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->right->color = BLACK;
                _rbtree_left_rotate(tree, x->parent);
                x = tree->root;
            }
        } else {
            private_rbtree_node_t w = x->parent->left;
            if (w->color == RED) {
                w->color = BLACK;
                x->parent->color = RED;
                _rbtree_right_rotate(tree, x->parent);
                w = x->parent->left;
            }
            
            if (w->right->color == BLACK && w->left->color == BLACK) {
                w->color = RED;
                x = x->parent;
            } else {
                if (w->left->color == BLACK) {
                    w->right->color = BLACK;
                    w->color = RED;
                    _rbtree_left_rotate(tree, w);
                    w = x->parent->left;
                }
                
                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->left->color = BLACK;
                _rbtree_right_rotate(tree, x->parent);
                x = tree->root;
            }
        }
    }
    
    x->color = BLACK;
}

// 删除节点的包装函数
static rbtree_node_t *adapter_rbtree_delete_node(rbtree_t *tree, rbtree_node_t *z_node) {
    private_rbtree_t priv_tree = (private_rbtree_t)tree;
    private_rbtree_node_t z = (private_rbtree_node_t)z_node;
    
    if (!z || z == priv_tree->nil) return NULL;
    
    private_rbtree_node_t y = z;
    private_rbtree_node_t x;
    unsigned char y_original_color = y->color;
    
    if (z->left == priv_tree->nil) {
        x = z->right;
        _rbtree_transplant(priv_tree, z, z->right);
    } else if (z->right == priv_tree->nil) {
        x = z->left;
        _rbtree_transplant(priv_tree, z, z->left);
    } else {
        y = _rbtree_minimum(priv_tree, z->right);
        y_original_color = y->color;
        x = y->right;
        
        if (y->parent == z) {
            x->parent = y;
        } else {
            _rbtree_transplant(priv_tree, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }
        
        _rbtree_transplant(priv_tree, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }
    
    if (y_original_color == BLACK) {
        _rbtree_delete_fixup(priv_tree, x);
    }
    
    priv_tree->count--;
    
    return (rbtree_node_t *)z;
}

// 创建适配器
rbtree_adapter_t *rbtree_adapter_create() {
    rbtree_adapter_t *adapter = (rbtree_adapter_t *)kv_store_malloc(sizeof(rbtree_adapter_t));
    if (!adapter) return NULL;
    
    // 创建原始红黑树
    adapter->original_tree = (rbtree_t *)kv_store_malloc(sizeof(struct _rbtree));
    if (!adapter->original_tree) {
        kv_store_free(adapter);
        return NULL;
    }
    
    // 初始化原始红黑树
    if (kv_store_rbtree_create(adapter->original_tree) != 0) {
        kv_store_free(adapter->original_tree);
        kv_store_free(adapter);
        return NULL;
    }
    
    // 创建适配器哨兵节点
    adapter->nil = (rbtree_adapter_node_t *)kv_store_malloc(sizeof(rbtree_adapter_node_t));
    if (!adapter->nil) {
        kv_store_rbtree_destroy(adapter->original_tree);
        kv_store_free(adapter->original_tree);
        kv_store_free(adapter);
        return NULL;
    }
    
    // 初始化哨兵节点
    adapter->nil->color = 2; // BLACK
    adapter->nil->left = NULL;
    adapter->nil->right = NULL;
    adapter->nil->parent = NULL;
    adapter->nil->hash_node = NULL;
    
    adapter->root = adapter->nil;
    adapter->count = 0;
    
    return adapter;
}

// 销毁适配器
void rbtree_adapter_destroy(rbtree_adapter_t *adapter) {
    if (!adapter) return;
    
    // 销毁原始红黑树
    if (adapter->original_tree) {
        kv_store_rbtree_destroy(adapter->original_tree);
        kv_store_free(adapter->original_tree);
    }
    
    // 释放哨兵节点
    if (adapter->nil) {
        kv_store_free(adapter->nil);
    }
    
    // 释放适配器
    kv_store_free(adapter);
}

// 在适配器中搜索节点
rbtree_adapter_node_t *rbtree_adapter_search(rbtree_adapter_t *adapter, char *key) {
    if (!adapter || !key) return NULL;
    
    // 使用原始红黑树的搜索功能
    private_rbtree_t priv_tree = (private_rbtree_t)adapter->original_tree;
    private_rbtree_node_t node = (private_rbtree_node_t)adapter_rbtree_search(adapter->original_tree, key);
    
    if (!node || node == priv_tree->nil) {
        return NULL;
    }
    
    // 创建适配器节点
    rbtree_adapter_node_t *adapter_node = (rbtree_adapter_node_t *)kv_store_malloc(sizeof(rbtree_adapter_node_t));
    if (!adapter_node) return NULL;
    
    // 转换哈希节点
    hash_node_t *hash_node = (hash_node_t *)node->value;
    
    // 设置适配器节点属性
    adapter_node->color = node->color;
    adapter_node->hash_node = hash_node;
    
    return adapter_node;
}

// 将哈希节点转换为红黑树节点
static private_rbtree_node_t hash_node_to_rbtree_node(hash_node_t *hash_node) {
    if (!hash_node) return NULL;
    
    // 创建红黑树节点
    private_rbtree_node_t node = (private_rbtree_node_t)adapter_rbtree_create_node(hash_node->key, (char *)hash_node);
    
    return node;
}

// 向适配器中插入节点
void rbtree_adapter_insert(rbtree_adapter_t *adapter, hash_node_t *hash_node) {
    if (!adapter || !hash_node) return;
    
    // 将哈希节点标记为红黑树节点
    hash_node->node_type = NODE_TYPE_TREE;
    
    // 转换为红黑树节点
    private_rbtree_node_t node = hash_node_to_rbtree_node(hash_node);
    if (!node) return;
    
    // 插入到原始红黑树
    adapter_rbtree_insert_node(adapter->original_tree, (rbtree_node_t *)node);
    adapter->count++;
}

// 从适配器中删除节点
hash_node_t *rbtree_adapter_delete(rbtree_adapter_t *adapter, char *key) {
    if (!adapter || !key) return NULL;
    
    // 在原始红黑树中查找节点
    private_rbtree_t priv_tree = (private_rbtree_t)adapter->original_tree;
    private_rbtree_node_t node = (private_rbtree_node_t)adapter_rbtree_search(adapter->original_tree, key);
    
    if (!node || node == priv_tree->nil) {
        return NULL;
    }
    
    // 获取哈希节点
    hash_node_t *hash_node = (hash_node_t *)node->value;
    
    // 从红黑树中删除节点
    private_rbtree_node_t deleted = (private_rbtree_node_t)adapter_rbtree_delete_node(adapter->original_tree, (rbtree_node_t *)node);
    
    // 释放红黑树节点（但不释放哈希节点）
    if (deleted) {
        if (deleted->key) {
            free(deleted->key);
        }
        // 不要释放value，因为它是哈希节点
        kv_store_free(deleted);
    }
    
    adapter->count--;
    
    return hash_node;
}

// 递归收集节点的辅助函数
static void collect_nodes_recursive(private_rbtree_t tree, private_rbtree_node_t node, hash_node_t **nodes, int *count) {
    if (node == tree->nil) return;
    
    // 先遍历左子树
    collect_nodes_recursive(tree, node->left, nodes, count);
    
    // 处理当前节点
    nodes[*count] = (hash_node_t *)node->value;
    (*count)++;
    
    // 再遍历右子树
    collect_nodes_recursive(tree, node->right, nodes, count);
}

// 遍历红黑树收集所有哈希节点 (中序遍历)
void rbtree_adapter_inorder_traversal(rbtree_adapter_t *adapter, hash_node_t **nodes, int *count) {
    if (!adapter || !nodes || !count) return;
    
    // 实现中序遍历
    private_rbtree_t priv_tree = (private_rbtree_t)adapter->original_tree;
    
    // 从根节点开始遍历
    *count = 0;
    collect_nodes_recursive(priv_tree, priv_tree->root, nodes, count);
} 