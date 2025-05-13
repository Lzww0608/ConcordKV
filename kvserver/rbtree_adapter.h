#ifndef __RBTREE_ADAPTER_H__
#define __RBTREE_ADAPTER_H__

#include "kv_store.h"

// 宏定义
#define MAX_KEY_LEN    128
#define MAX_VALUE_LEN  512
#define ENABLE_POINTER_KEY  1  // 使用指针而不是数组来存储键和值

// 哈希节点结构体
typedef struct hash_node_s {
#if ENABLE_POINTER_KEY
    char *key;
    char *value;
#else
    char key[MAX_KEY_LEN];
    char value[MAX_VALUE_LEN];
#endif
    struct hash_node_s *next;
    unsigned char node_type;  // NODE_TYPE_LIST 或 NODE_TYPE_TREE
} hash_node_t;

// 节点类型标记
#define NODE_TYPE_LIST  0
#define NODE_TYPE_TREE  1

// 红黑树适配器结构体
typedef struct rbtree_adapter_node_s {
    unsigned char color;               // 仅用于适配器内部
    struct rbtree_adapter_node_s *right;
    struct rbtree_adapter_node_s *left;
    struct rbtree_adapter_node_s *parent;
    
    hash_node_t *hash_node;            // 指向哈希节点
} rbtree_adapter_node_t;

// 红黑树适配器
typedef struct rbtree_adapter_s {
    rbtree_adapter_node_t *root;
    rbtree_adapter_node_t *nil;
    int count;
    
    rbtree_t *original_tree;           // 指向原始红黑树
} rbtree_adapter_t;

// 初始化适配器
rbtree_adapter_t *rbtree_adapter_create();

// 销毁适配器
void rbtree_adapter_destroy(rbtree_adapter_t *adapter);

// 在适配器中搜索节点
rbtree_adapter_node_t *rbtree_adapter_search(rbtree_adapter_t *adapter, char *key);

// 向适配器中插入节点
void rbtree_adapter_insert(rbtree_adapter_t *adapter, hash_node_t *hash_node);

// 从适配器中删除节点
hash_node_t *rbtree_adapter_delete(rbtree_adapter_t *adapter, char *key);

// 中序遍历收集所有哈希节点
void rbtree_adapter_inorder_traversal(rbtree_adapter_t *adapter, hash_node_t **nodes, int *count);

// 桶结构体，可以包含链表或红黑树
typedef struct bucket_s {
    union {
        hash_node_t *list;             // 链表头
        rbtree_adapter_t *tree;        // 红黑树适配器
    } data;
    unsigned char is_tree;             // 是否为红黑树
    int size;                          // 当前大小
} bucket_t;

#endif // __RBTREE_ADAPTER_H__ 