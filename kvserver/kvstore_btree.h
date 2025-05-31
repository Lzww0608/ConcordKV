/*
 * @Author: Lzww0608  
 * @Date: 2025-5-31 23:10:51
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-1 00:34:05
 * @Description: ConcordKV B+Tree存储引擎头文件
 */
#ifndef __KVSTORE_BTREE_H__
#define __KVSTORE_BTREE_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// B+Tree配置常量
#define BTREE_DEFAULT_ORDER 100         // 默认阶数(每个节点最大键数)
#define BTREE_MIN_ORDER 3               // 最小阶数
#define BTREE_MAX_ORDER 1000            // 最大阶数
#define BTREE_KEY_MAX_LEN 256           // 键最大长度
#define BTREE_VALUE_MAX_LEN 4096        // 值最大长度

// B+Tree节点类型
typedef enum {
    BTREE_NODE_INTERNAL = 0,    // 内部节点
    BTREE_NODE_LEAF = 1         // 叶子节点
} btree_node_type_t;

// B+Tree节点结构
typedef struct btree_node {
    btree_node_type_t type;     // 节点类型(内部节点/叶子节点)
    bool is_leaf;               // 是否为叶子节点(冗余字段，便于判断)
    int key_count;              // 当前键数量
    int max_keys;               // 最大键数量(阶数)
    
    char **keys;                // 键数组
    size_t *key_lens;           // 键长度数组
    
    // 对于叶子节点: values存储实际值
    // 对于内部节点: children存储子节点指针
    union {
        char **values;              // 值数组(叶子节点)
        struct btree_node **children; // 子节点指针数组(内部节点) 
    };
    size_t *value_lens;         // 值长度数组(仅叶子节点使用)
    
    struct btree_node *parent;  // 父节点指针
    struct btree_node *next;    // 下一个叶子节点(仅叶子节点使用，形成链表)
    struct btree_node *prev;    // 上一个叶子节点(仅叶子节点使用，形成链表)
    
    pthread_rwlock_t lock;      // 节点级读写锁
    uint64_t version;           // 版本号(用于并发控制)
} btree_node_t;

// B+Tree主结构
typedef struct btree {
    btree_node_t *root;         // 根节点
    btree_node_t *first_leaf;   // 第一个叶子节点(最左边)
    btree_node_t *last_leaf;    // 最后一个叶子节点(最右边)
    
    int order;                  // B+树的阶数
    size_t total_keys;          // 总键数量
    size_t total_size;          // 总数据大小(字节)
    uint32_t height;            // 树高度
    uint64_t internal_nodes;    // 内部节点数
    uint64_t leaf_nodes;        // 叶子节点数
    
    pthread_rwlock_t tree_lock; // 树级读写锁
    uint64_t version;           // 全局版本号
    
    // 统计信息
    uint64_t read_count;        // 读操作计数
    uint64_t write_count;       // 写操作计数
    uint64_t delete_count;      // 删除操作计数
} btree_t;

// B+Tree迭代器结构
typedef struct btree_iterator {
    btree_t *tree;              // B+Tree指针
    btree_node_t *current_node; // 当前节点
    int current_index;          // 当前键在节点中的索引
    bool valid;                 // 迭代器是否有效
    bool reverse;               // 是否反向迭代
} btree_iterator_t;

// === B+Tree节点操作函数 ===

/**
 * 创建B+Tree节点
 * @param type 节点类型
 * @param order 节点阶数(最大键数)
 * @return 新创建的节点，失败返回NULL
 */
btree_node_t* btree_node_create(btree_node_type_t type, int order);

/**
 * 销毁B+Tree节点(递归销毁所有子节点)
 * @param node 要销毁的节点
 */
void btree_node_destroy(btree_node_t *node);

/**
 * 在节点中查找键的位置
 * @param node 节点指针
 * @param key 要查找的键
 * @param key_len 键长度
 * @param exact_match 是否要求精确匹配
 * @return 键的位置索引，如果未找到返回应该插入的位置
 */
int btree_node_find_key(btree_node_t *node, const char *key, size_t key_len, bool exact_match);

/**
 * 检查节点是否已满
 * @param node 节点指针
 * @return true如果节点已满，false否则
 */
bool btree_node_is_full(btree_node_t *node);

/**
 * 检查节点是否需要合并(键数量过少)
 * @param node 节点指针
 * @return true如果需要合并，false否则
 */
bool btree_node_need_merge(btree_node_t *node);

/**
 * 在节点中插入键值对
 * @param node 节点指针
 * @param index 插入位置
 * @param key 键
 * @param key_len 键长度
 * @param value 值(叶子节点)或子节点指针(内部节点)
 * @param value_len 值长度(仅叶子节点)
 * @return 0成功，负数表示错误
 */
int btree_node_insert_at(btree_node_t *node, int index, const char *key, size_t key_len, 
                        void *value, size_t value_len);

/**
 * 从节点中删除指定位置的键值对
 * @param node 节点指针
 * @param index 删除位置
 * @return 0成功，负数表示错误
 */
int btree_node_remove_at(btree_node_t *node, int index);

/**
 * 节点分裂
 * @param node 要分裂的节点
 * @param parent 父节点
 * @return 新创建的右节点，失败返回NULL
 */
btree_node_t* btree_node_split(btree_node_t *node, btree_node_t *parent);

/**
 * 节点合并
 * @param left 左节点
 * @param right 右节点  
 * @param parent 父节点
 * @param separator_index 分隔符在父节点中的索引
 * @return 0成功，负数表示错误
 */
int btree_node_merge(btree_node_t *left, btree_node_t *right, 
                    btree_node_t *parent, int separator_index);

/**
 * 从兄弟节点借键
 * @param node 需要借键的节点
 * @param sibling 兄弟节点
 * @param parent 父节点
 * @param is_left_sibling 兄弟节点是否在左边
 * @return 0成功，负数表示错误
 */
int btree_node_borrow(btree_node_t *node, btree_node_t *sibling, 
                     btree_node_t *parent, bool is_left_sibling);

/**
 * 验证节点结构的正确性
 * @param node 要验证的节点
 * @return true如果节点结构正确，false否则
 */
bool btree_node_validate(btree_node_t *node);

// === B+Tree主要操作函数 ===

/**
 * 创建B+Tree
 * @param order 树的阶数
 * @return 新创建的B+Tree，失败返回NULL
 */
btree_t* btree_create(int order);

/**
 * 销毁B+Tree
 * @param tree B+Tree指针
 */
void btree_destroy(btree_t *tree);

/**
 * 在B+Tree中搜索键
 * @param tree B+Tree指针
 * @param key 要搜索的键
 * @param key_len 键长度
 * @return 找到的值，未找到返回NULL
 */
char* btree_search(btree_t *tree, const char *key, size_t key_len);

/**
 * 在B+Tree中插入键值对
 * @param tree B+Tree指针
 * @param key 键
 * @param key_len 键长度
 * @param value 值
 * @param value_len 值长度
 * @return 0成功，负数表示错误
 */
int btree_insert(btree_t *tree, const char *key, size_t key_len, 
                const char *value, size_t value_len);

/**
 * 从B+Tree中删除键
 * @param tree B+Tree指针
 * @param key 要删除的键
 * @param key_len 键长度
 * @return 0成功，负数表示错误
 */
int btree_delete(btree_t *tree, const char *key, size_t key_len);

/**
 * 更新B+Tree中的键值对
 * @param tree B+Tree指针
 * @param key 键
 * @param key_len 键长度
 * @param value 新值
 * @param value_len 新值长度
 * @return 0成功，负数表示错误
 */
int btree_update(btree_t *tree, const char *key, size_t key_len,
                const char *value, size_t value_len);

/**
 * 获取B+Tree中的键数量
 * @param tree B+Tree指针
 * @return 键数量
 */
size_t btree_count(btree_t *tree);

/**
 * 范围查询
 * @param tree B+Tree指针
 * @param start_key 起始键
 * @param start_len 起始键长度
 * @param end_key 结束键
 * @param end_len 结束键长度
 * @param include_start 是否包含起始键
 * @param include_end 是否包含结束键
 * @param results 结果数组(输出参数)
 * @param max_results 最大结果数量
 * @return 实际返回的结果数量，负数表示错误
 */
int btree_range_search(btree_t *tree, 
                      const char *start_key, size_t start_len,
                      const char *end_key, size_t end_len,
                      bool include_start, bool include_end,
                      btree_iterator_t **results, size_t max_results);

/**
 * 验证B+Tree结构的正确性
 * @param tree B+Tree指针
 * @return true如果树结构正确，false否则
 */
bool btree_validate(btree_t *tree);

/**
 * 重新平衡B+Tree(如果需要)
 * @param tree B+Tree指针
 * @return 0成功，负数表示错误
 */
int btree_rebalance(btree_t *tree);

// === B+Tree迭代器操作函数 ===

/**
 * 创建B+Tree迭代器
 * @param tree B+Tree指针
 * @param reverse 是否反向迭代
 * @return 新创建的迭代器，失败返回NULL
 */
btree_iterator_t* btree_iterator_create(btree_t *tree, bool reverse);

/**
 * 销毁B+Tree迭代器
 * @param iter 迭代器指针
 */
void btree_iterator_destroy(btree_iterator_t *iter);

/**
 * 迭代器移动到下一个元素
 * @param iter 迭代器指针
 * @return 0成功，负数表示错误或已到末尾
 */
int btree_iterator_next(btree_iterator_t *iter);

/**
 * 迭代器移动到上一个元素
 * @param iter 迭代器指针
 * @return 0成功，负数表示错误或已到开头
 */
int btree_iterator_prev(btree_iterator_t *iter);

/**
 * 迭代器定位到指定键
 * @param iter 迭代器指针
 * @param key 目标键
 * @param key_len 键长度
 */
void btree_iterator_seek(btree_iterator_t *iter, const char *key, size_t key_len);

/**
 * 获取迭代器当前的键值对
 * @param iter 迭代器指针
 * @param key 输出键指针
 * @param key_len 输出键长度
 * @param value 输出值指针
 * @param value_len 输出值长度
 * @return 0成功，负数表示错误
 */
int btree_iterator_get_current(btree_iterator_t *iter, 
                              const char **key, size_t *key_len,
                              const char **value, size_t *value_len);

/**
 * 检查迭代器是否有效
 * @param iter 迭代器指针
 * @return true如果有效，false否则
 */
bool btree_iterator_valid(btree_iterator_t *iter);

// === 工具函数 ===

/**
 * 比较两个键的大小
 * @param key1 键1
 * @param len1 键1长度
 * @param key2 键2
 * @param len2 键2长度
 * @return <0 如果key1<key2, 0 如果相等, >0 如果key1>key2
 */
int btree_key_compare(const char *key1, size_t len1, const char *key2, size_t len2);

/**
 * 复制键
 * @param key 源键
 * @param len 键长度
 * @return 复制的键，失败返回NULL
 */
char* btree_key_copy(const char *key, size_t len);

/**
 * 复制值
 * @param value 源值
 * @param len 值长度
 * @return 复制的值，失败返回NULL
 */
char* btree_value_copy(const char *value, size_t len);

/**
 * 打印B+Tree结构(调试用)
 * @param tree B+Tree指针
 */
void btree_print(btree_t *tree);

/**
 * 获取B+Tree内存使用量
 * @param tree B+Tree指针
 * @return 内存使用量(字节)
 */
size_t btree_memory_usage(btree_t *tree);

// === B+Tree完整算法函数声明 ===

/**
 * 分裂叶子节点
 */
int btree_split_leaf_node(btree_t *tree, btree_node_t *node, 
                         const char *key, size_t key_len,
                         const char *value, size_t value_len);

/**
 * 分裂内部节点  
 */
int btree_split_internal_node(btree_t *tree, btree_node_t *node,
                             const char *key, size_t key_len,
                             btree_node_t *right_child);

/**
 * 向父节点插入键和子节点指针
 */
int btree_insert_to_parent(btree_t *tree, btree_node_t *left, btree_node_t *right,
                          const char *key, size_t key_len);

/**
 * 合并两个节点
 */
int btree_merge_nodes(btree_t *tree, btree_node_t *left, btree_node_t *right,
                     const char *separator_key, size_t separator_key_len);

/**
 * 从兄弟节点借用键
 */
int btree_borrow_from_sibling(btree_t *tree, btree_node_t *node, 
                             btree_node_t *sibling, bool borrow_from_left);

/**
 * 查找节点的左兄弟节点
 */
btree_node_t* btree_find_left_sibling(btree_node_t *node, int *parent_index);

/**
 * 查找节点的右兄弟节点  
 */
btree_node_t* btree_find_right_sibling(btree_node_t *node, int *parent_index);

/**
 * 完整的B+Tree插入实现
 */
int btree_insert_complete(btree_t *tree, const char *key, size_t key_len,
                         const char *value, size_t value_len);

/**
 * 完整的B+Tree删除实现
 */
int btree_delete_complete(btree_t *tree, const char *key, size_t key_len);

/**
 * 递归插入到叶子节点
 */
int btree_insert_recursive(btree_t *tree, btree_node_t *node,
                          const char *key, size_t key_len,
                          const char *value, size_t value_len);

/**
 * 递归删除叶子节点的键
 */
int btree_delete_recursive(btree_t *tree, btree_node_t *node,
                          const char *key, size_t key_len);

/**
 * 修复删除后可能违反B+Tree性质的节点
 */
int btree_fix_after_delete(btree_t *tree, btree_node_t *node);

#ifdef __cplusplus
}
#endif

#endif // __KVSTORE_BTREE_H__ 