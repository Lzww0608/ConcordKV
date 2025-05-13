#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 定义红黑树节点颜色
#define RED   1
#define BLACK 2

// 节点结构
typedef struct rbtree_node_s {
    unsigned char color;
    struct rbtree_node_s *left;
    struct rbtree_node_s *right;
    struct rbtree_node_s *parent;
    char *key;
    char *value;
} rbtree_node_t;

// 红黑树结构
typedef struct rbtree_s {
    rbtree_node_t *root;
    rbtree_node_t *nil;  // 哨兵节点
    int count;
} rbtree_t;

// 函数声明
rbtree_t *create_rbtree();
void destroy_rbtree(rbtree_t *tree);
void rbtree_insert(rbtree_t *tree, char *key, char *value);
char *rbtree_search(rbtree_t *tree, char *key);
void rbtree_delete(rbtree_t *tree, char *key);
void print_rbtree(rbtree_t *tree, rbtree_node_t *node, int depth);

// 创建红黑树
rbtree_t *create_rbtree() {
    rbtree_t *tree = (rbtree_t *)malloc(sizeof(rbtree_t));
    if (!tree) return NULL;
    
    // 创建哨兵节点
    tree->nil = (rbtree_node_t *)malloc(sizeof(rbtree_node_t));
    if (!tree->nil) {
        free(tree);
        return NULL;
    }
    
    tree->nil->color = BLACK;
    tree->nil->left = NULL;
    tree->nil->right = NULL;
    tree->nil->parent = NULL;
    tree->nil->key = NULL;
    tree->nil->value = NULL;
    
    tree->root = tree->nil;
    tree->count = 0;
    
    return tree;
}

// 左旋操作
void left_rotate(rbtree_t *tree, rbtree_node_t *x) {
    rbtree_node_t *y = x->right;
    
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
void right_rotate(rbtree_t *tree, rbtree_node_t *y) {
    rbtree_node_t *x = y->left;
    
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
void insert_fixup(rbtree_t *tree, rbtree_node_t *z) {
    while (z->parent->color == RED) {
        if (z->parent == z->parent->parent->left) {
            rbtree_node_t *y = z->parent->parent->right;
            if (y->color == RED) {
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    left_rotate(tree, z);
                }
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                right_rotate(tree, z->parent->parent);
            }
        } else {
            rbtree_node_t *y = z->parent->parent->left;
            if (y->color == RED) {
                z->parent->color = BLACK;
                y->color = BLACK;
                z->parent->parent->color = RED;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    right_rotate(tree, z);
                }
                z->parent->color = BLACK;
                z->parent->parent->color = RED;
                left_rotate(tree, z->parent->parent);
            }
        }
    }
    tree->root->color = BLACK;
}

// 插入节点
void rbtree_insert(rbtree_t *tree, char *key, char *value) {
    rbtree_node_t *z = (rbtree_node_t *)malloc(sizeof(rbtree_node_t));
    if (!z) return;
    
    z->key = strdup(key);
    z->value = strdup(value);
    if (!z->key || !z->value) {
        if (z->key) free(z->key);
        if (z->value) free(z->value);
        free(z);
        return;
    }
    
    rbtree_node_t *y = tree->nil;
    rbtree_node_t *x = tree->root;
    
    while (x != tree->nil) {
        y = x;
        int cmp = strcmp(z->key, x->key);
        if (cmp < 0) {
            x = x->left;
        } else if (cmp > 0) {
            x = x->right;
        } else {
            // 键已存在，更新值
            free(x->value);
            x->value = z->value;
            free(z->key);
            free(z);
            return;
        }
    }
    
    z->parent = y;
    if (y == tree->nil) {
        tree->root = z;
    } else {
        int cmp = strcmp(z->key, y->key);
        if (cmp < 0) {
            y->left = z;
        } else {
            y->right = z;
        }
    }
    
    z->left = tree->nil;
    z->right = tree->nil;
    z->color = RED;
    
    insert_fixup(tree, z);
    tree->count++;
}

// 查找节点
char *rbtree_search(rbtree_t *tree, char *key) {
    rbtree_node_t *x = tree->root;
    
    while (x != tree->nil) {
        int cmp = strcmp(key, x->key);
        if (cmp < 0) {
            x = x->left;
        } else if (cmp > 0) {
            x = x->right;
        } else {
            return x->value;
        }
    }
    
    return NULL;
}

// 找到最小节点
rbtree_node_t *tree_minimum(rbtree_t *tree, rbtree_node_t *x) {
    while (x->left != tree->nil) {
        x = x->left;
    }
    return x;
}

// 销毁红黑树
void destroy_rbtree(rbtree_t *tree) {
    // 这里应该有清理所有节点的代码
    // 为简洁起见，省略
    if (tree) {
        if (tree->nil) free(tree->nil);
        free(tree);
    }
}

// 打印红黑树
void print_rbtree(rbtree_t *tree, rbtree_node_t *node, int depth) {
    if (node == tree->nil) return;
    
    // 打印右子树
    print_rbtree(tree, node->right, depth + 1);
    
    // 打印当前节点
    for (int i = 0; i < depth; i++) {
        printf("    ");
    }
    printf("%s (%s): %s\n", node->color == RED ? "红" : "黑", node->key, node->value);
    
    // 打印左子树
    print_rbtree(tree, node->left, depth + 1);
}

int main() {
    // 创建红黑树
    rbtree_t *tree = create_rbtree();
    if (!tree) {
        printf("创建红黑树失败\n");
        return 1;
    }
    
    printf("插入测试数据...\n");
    
    // 插入一些键值对
    rbtree_insert(tree, "apple", "苹果");
    rbtree_insert(tree, "banana", "香蕉");
    rbtree_insert(tree, "cherry", "樱桃");
    rbtree_insert(tree, "date", "枣");
    rbtree_insert(tree, "elderberry", "接骨木果");
    rbtree_insert(tree, "fig", "无花果");
    rbtree_insert(tree, "grape", "葡萄");
    rbtree_insert(tree, "honeydew", "蜜瓜");
    
    // 打印树结构
    printf("\n红黑树结构:\n");
    print_rbtree(tree, tree->root, 0);
    
    // 查找测试
    printf("\n查找测试:\n");
    char *val = rbtree_search(tree, "cherry");
    if (val) {
        printf("找到 cherry: %s\n", val);
    } else {
        printf("未找到 cherry\n");
    }
    
    val = rbtree_search(tree, "watermelon");
    if (val) {
        printf("找到 watermelon: %s\n", val);
    } else {
        printf("未找到 watermelon\n");
    }
    
    // 清理
    printf("\n销毁红黑树...\n");
    destroy_rbtree(tree);
    
    printf("测试完成!\n");
    return 0;
} 