#include <gtest/gtest.h>
#include <string>
#include <cstring>
#include <vector>
#include <algorithm>
#include <random>

// 定义红黑树数据结构
#define RED  1
#define BLACK 2

typedef struct rbtree_node {
    unsigned char color;
    struct rbtree_node *right;
    struct rbtree_node *left;
    struct rbtree_node *parent;
    char *key;
    void *value;
} rbtree_node;

typedef struct _rbtree {
    rbtree_node *root;
    rbtree_node *nil;  // Sentinel node for leaves and root parent
    int count;
} rbtree_t;

// 声明内存分配和释放函数(不要重复定义)
extern void *kv_store_malloc(size_t size);
extern void kv_store_free(void *ptr);

// 实现红黑树操作函数
rbtree_node *create_node(const char *key, const char *value) {
    rbtree_node *node = (rbtree_node*)malloc(sizeof(rbtree_node));
    if (!node) return NULL;
    
    node->key = strdup(key);
    node->value = value ? strdup(value) : NULL;
    
    if (!node->key || (value && !node->value)) {
        if (node->key) free(node->key);
        if (node->value) free(node->value);
        free(node);
        return NULL;
    }
    
    return node;
}

// 左旋转
void left_rotate(rbtree_t *tree, rbtree_node *x) {
    rbtree_node *y = x->right;
    
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

// 右旋转
void right_rotate(rbtree_t *tree, rbtree_node *y) {
    rbtree_node *x = y->left;
    
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
void insert_fixup(rbtree_t *tree, rbtree_node *z) {
    while (z->parent->color == RED) {
        if (z->parent == z->parent->parent->left) {
            rbtree_node *y = z->parent->parent->right;
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
            rbtree_node *y = z->parent->parent->left;
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

// 查找节点
rbtree_node *search(rbtree_t *tree, const char *key) {
    rbtree_node *current = tree->root;
    while (current != tree->nil) {
        int cmp = strcmp(key, current->key);
        if (cmp == 0) {
            return current;
        } else if (cmp < 0) {
            current = current->left;
        } else {
            current = current->right;
        }
    }
    return NULL;
}

// 查找最小节点
rbtree_node *minimum(rbtree_t *tree, rbtree_node *x) {
    while (x->left != tree->nil) {
        x = x->left;
    }
    return x;
}

// 查找后继节点
rbtree_node *successor(rbtree_t *tree, rbtree_node *x) {
    if (x->right != tree->nil) {
        return minimum(tree, x->right);
    }
    
    rbtree_node *y = x->parent;
    while (y != tree->nil && x == y->right) {
        x = y;
        y = y->parent;
    }
    return y;
}

// 删除修复
void delete_fixup(rbtree_t *tree, rbtree_node *x) {
    while (x != tree->root && x->color == BLACK) {
        if (x == x->parent->left) {
            rbtree_node *w = x->parent->right;
            if (w->color == RED) {
                w->color = BLACK;
                x->parent->color = RED;
                left_rotate(tree, x->parent);
                w = x->parent->right;
            }
            if (w->left->color == BLACK && w->right->color == BLACK) {
                w->color = RED;
                x = x->parent;
            } else {
                if (w->right->color == BLACK) {
                    w->left->color = BLACK;
                    w->color = RED;
                    right_rotate(tree, w);
                    w = x->parent->right;
                }
                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->right->color = BLACK;
                left_rotate(tree, x->parent);
                x = tree->root;
            }
        } else {
            rbtree_node *w = x->parent->left;
            if (w->color == RED) {
                w->color = BLACK;
                x->parent->color = RED;
                right_rotate(tree, x->parent);
                w = x->parent->left;
            }
            if (w->right->color == BLACK && w->left->color == BLACK) {
                w->color = RED;
                x = x->parent;
            } else {
                if (w->left->color == BLACK) {
                    w->right->color = BLACK;
                    w->color = RED;
                    left_rotate(tree, w);
                    w = x->parent->left;
                }
                w->color = x->parent->color;
                x->parent->color = BLACK;
                w->left->color = BLACK;
                right_rotate(tree, x->parent);
                x = tree->root;
            }
        }
    }
    x->color = BLACK;
}

// 替换节点
void transplant(rbtree_t *tree, rbtree_node *u, rbtree_node *v) {
    if (u->parent == tree->nil) {
        tree->root = v;
    } else if (u == u->parent->left) {
        u->parent->left = v;
    } else {
        u->parent->right = v;
    }
    v->parent = u->parent;
}

// 删除节点
void delete_node(rbtree_t *tree, rbtree_node *z) {
    rbtree_node *y = z;
    rbtree_node *x;
    unsigned char y_original_color = y->color;
    
    if (z->left == tree->nil) {
        x = z->right;
        transplant(tree, z, z->right);
    } else if (z->right == tree->nil) {
        x = z->left;
        transplant(tree, z, z->left);
    } else {
        y = minimum(tree, z->right);
        y_original_color = y->color;
        x = y->right;
        
        if (y->parent == z) {
            x->parent = y;
        } else {
            transplant(tree, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }
        
        transplant(tree, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->color = z->color;
    }
    
    if (y_original_color == BLACK) {
        delete_fixup(tree, x);
    }
}

// 递归销毁
void destroy_tree(rbtree_t *tree, rbtree_node *node) {
    if (node == tree->nil) return;
    
    destroy_tree(tree, node->left);
    destroy_tree(tree, node->right);
    
    free(node->key);
    free(node->value);
    free(node);
}

// 创建红黑树
int kv_store_rbtree_create(rbtree_t *tree) {
    if (!tree) return -1;
    
    // 创建哨兵节点
    tree->nil = (rbtree_node*)malloc(sizeof(rbtree_node));
    if (!tree->nil) return -1;
    
    tree->nil->color = BLACK;
    tree->nil->left = NULL;
    tree->nil->right = NULL;
    tree->nil->parent = NULL;
    tree->nil->key = NULL;
    tree->nil->value = NULL;
    
    tree->root = tree->nil;
    tree->count = 0;
    
    return 0;
}

// 销毁红黑树
void kv_store_rbtree_destroy(rbtree_t *tree) {
    if (!tree) return;
    
    if (tree->root != tree->nil) {
        destroy_tree(tree, tree->root);
    }
    
    free(tree->nil);
    tree->nil = NULL;
    tree->root = NULL;
    tree->count = 0;
}

// 设置键值对
int kvs_rbtree_set(rbtree_t *tree, char *key, char *value) {
    if (!tree || !key || !value) return -1;
    
    // 查找是否已存在
    rbtree_node *existing = search(tree, key);
    if (existing) {
        // 更新值
        free(existing->value);
        existing->value = strdup(value);
        return 0;
    }
    
    // 创建新节点
    rbtree_node *z = create_node(key, value);
    if (!z) return -1;
    
    // 初始化节点
    z->left = tree->nil;
    z->right = tree->nil;
    z->parent = tree->nil;
    z->color = RED;
    
    // 插入节点
    rbtree_node *y = tree->nil;
    rbtree_node *x = tree->root;
    
    while (x != tree->nil) {
        y = x;
        if (strcmp(z->key, x->key) < 0) {
            x = x->left;
        } else {
            x = x->right;
        }
    }
    
    z->parent = y;
    if (y == tree->nil) {
        tree->root = z;
    } else if (strcmp(z->key, y->key) < 0) {
        y->left = z;
    } else {
        y->right = z;
    }
    
    // 修复红黑树性质
    insert_fixup(tree, z);
    
    tree->count++;
    return 0;
}

// 获取键值
char* kvs_rbtree_get(rbtree_t *tree, char *key) {
    if (!tree || !key) return NULL;
    
    rbtree_node *node = search(tree, key);
    if (node) {
        return strdup((char*)node->value);
    }
    
    return NULL;
}

// 删除键值对
int kvs_rbtree_delete(rbtree_t *tree, char *key) {
    if (!tree || !key) return -1;
    
    rbtree_node *node = search(tree, key);
    if (!node) return 1;
    
    delete_node(tree, node);
    
    free(node->key);
    free(node->value);
    free(node);
    
    tree->count--;
    return 0;
}

// 修改键值对
int kvs_rbtree_modify(rbtree_t *tree, char *key, char *value) {
    if (!tree || !key || !value) return -1;
    
    rbtree_node *node = search(tree, key);
    if (!node) return 1;
    
    free(node->value);
    node->value = strdup(value);
    
    return 0;
}

// 获取键值对数量
int kvs_rbtree_count(rbtree_t *tree) {
    if (!tree) return -1;
    return tree->count;
}

// 测试类
class KvstoreRbtreeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 初始化红黑树
        ASSERT_EQ(0, kv_store_rbtree_create(&tree));
    }
    
    void TearDown() override {
        // 销毁红黑树
        kv_store_rbtree_destroy(&tree);
    }
    
    rbtree_t tree;
};

// 测试创建和销毁
TEST_F(KvstoreRbtreeTest, CreateAndDestroy) {
    // 已在SetUp和TearDown中测试
}

// 测试基本的设置和获取
TEST_F(KvstoreRbtreeTest, SetAndGet) {
    // 设置键值对
    ASSERT_EQ(0, kvs_rbtree_set(&tree, (char*)"key1", (char*)"value1"));
    ASSERT_EQ(0, kvs_rbtree_set(&tree, (char*)"key2", (char*)"value2"));
    
    // 获取并验证值
    char *value = kvs_rbtree_get(&tree, (char*)"key1");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value1", value);
    free(value); // 注意释放内存
    
    value = kvs_rbtree_get(&tree, (char*)"key2");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value2", value);
    free(value);
    
    // 测试获取不存在的键
    value = kvs_rbtree_get(&tree, (char*)"non_existent_key");
    ASSERT_EQ(nullptr, value);
}

// 测试删除
TEST_F(KvstoreRbtreeTest, Delete) {
    // 设置键值对
    ASSERT_EQ(0, kvs_rbtree_set(&tree, (char*)"key1", (char*)"value1"));
    ASSERT_EQ(0, kvs_rbtree_set(&tree, (char*)"key2", (char*)"value2"));
    
    // 验证设置成功
    ASSERT_EQ(2, kvs_rbtree_count(&tree));
    
    // 删除键
    ASSERT_EQ(0, kvs_rbtree_delete(&tree, (char*)"key1"));
    
    // 验证删除成功
    ASSERT_EQ(1, kvs_rbtree_count(&tree));
    ASSERT_EQ(nullptr, kvs_rbtree_get(&tree, (char*)"key1"));
    
    char *value = kvs_rbtree_get(&tree, (char*)"key2");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value2", value);
    free(value);
    
    // 测试删除不存在的键
    ASSERT_NE(0, kvs_rbtree_delete(&tree, (char*)"non_existent_key"));
    
    // 测试NULL参数
    ASSERT_NE(0, kvs_rbtree_delete(nullptr, (char*)"key"));
    ASSERT_NE(0, kvs_rbtree_delete(&tree, nullptr));
}

// 测试修改
TEST_F(KvstoreRbtreeTest, Modify) {
    // 设置键值对
    ASSERT_EQ(0, kvs_rbtree_set(&tree, (char*)"key1", (char*)"value1"));
    
    // 修改值
    ASSERT_EQ(0, kvs_rbtree_modify(&tree, (char*)"key1", (char*)"new_value1"));
    
    // 验证修改成功
    char *value = kvs_rbtree_get(&tree, (char*)"key1");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("new_value1", value);
    free(value);
    
    // 测试修改不存在的键
    ASSERT_NE(0, kvs_rbtree_modify(&tree, (char*)"non_existent_key", (char*)"value"));
    
    // 测试NULL参数
    ASSERT_NE(0, kvs_rbtree_modify(nullptr, (char*)"key", (char*)"value"));
    ASSERT_NE(0, kvs_rbtree_modify(&tree, nullptr, (char*)"value"));
    ASSERT_NE(0, kvs_rbtree_modify(&tree, (char*)"key", nullptr));
}

// 测试计数
TEST_F(KvstoreRbtreeTest, Count) {
    // 初始计数应为0
    ASSERT_EQ(0, kvs_rbtree_count(&tree));
    
    // 添加键值对
    ASSERT_EQ(0, kvs_rbtree_set(&tree, (char*)"key1", (char*)"value1"));
    ASSERT_EQ(1, kvs_rbtree_count(&tree));
    
    ASSERT_EQ(0, kvs_rbtree_set(&tree, (char*)"key2", (char*)"value2"));
    ASSERT_EQ(2, kvs_rbtree_count(&tree));
    
    // 删除键值对
    ASSERT_EQ(0, kvs_rbtree_delete(&tree, (char*)"key1"));
    ASSERT_EQ(1, kvs_rbtree_count(&tree));
    
    // 测试NULL参数
    ASSERT_EQ(-1, kvs_rbtree_count(nullptr));
}

// 测试大量数据
TEST_F(KvstoreRbtreeTest, LargeDataSet) {
    const int NUM_ENTRIES = 100;
    std::vector<std::string> keys;
    
    // 创建键列表
    for (int i = 0; i < NUM_ENTRIES; i++) {
        keys.push_back("key" + std::to_string(i));
    }
    
    // 随机打乱键的顺序
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(keys.begin(), keys.end(), g);
    
    // 按随机顺序插入键值对
    for (const auto& key : keys) {
        std::string value = "value_for_" + key;
        ASSERT_EQ(0, kvs_rbtree_set(&tree, (char*)key.c_str(), (char*)value.c_str()));
    }
    
    // 验证计数
    ASSERT_EQ(NUM_ENTRIES, kvs_rbtree_count(&tree));
    
    // 随机打乱键的顺序
    std::shuffle(keys.begin(), keys.end(), g);
    
    // 验证所有键值对
    for (const auto& key : keys) {
        std::string expected_value = "value_for_" + key;
        char *value = kvs_rbtree_get(&tree, (char*)key.c_str());
        ASSERT_NE(nullptr, value);
        ASSERT_STREQ(expected_value.c_str(), value);
        free(value);
    }
    
    // 删除一半的键
    int half = NUM_ENTRIES / 2;
    for (int i = 0; i < half; i++) {
        ASSERT_EQ(0, kvs_rbtree_delete(&tree, (char*)keys[i].c_str()));
    }
    
    // 验证计数更新
    ASSERT_EQ(NUM_ENTRIES - half, kvs_rbtree_count(&tree));
    
    // 验证删除的键不存在，剩余的键存在
    for (int i = 0; i < NUM_ENTRIES; i++) {
        char *value = kvs_rbtree_get(&tree, (char*)keys[i].c_str());
        if (i < half) {
            ASSERT_EQ(nullptr, value);
        } else {
            std::string expected_value = "value_for_" + keys[i];
            ASSERT_NE(nullptr, value);
            ASSERT_STREQ(expected_value.c_str(), value);
            free(value);
        }
    }
}

// 测试重复键
TEST_F(KvstoreRbtreeTest, DuplicateKey) {
    // 设置键值对
    ASSERT_EQ(0, kvs_rbtree_set(&tree, (char*)"key1", (char*)"value1"));
    
    // 再次设置相同的键
    ASSERT_EQ(0, kvs_rbtree_set(&tree, (char*)"key1", (char*)"value2"));
    
    // 验证值已更新
    char *value = kvs_rbtree_get(&tree, (char*)"key1");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value2", value);
    free(value);
    
    // 验证计数仍为1
    ASSERT_EQ(1, kvs_rbtree_count(&tree));
}

// 测试边界情况
TEST_F(KvstoreRbtreeTest, EdgeCases) {
    // 测试空字符串键和值
    ASSERT_EQ(0, kvs_rbtree_set(&tree, (char*)"", (char*)"empty_key"));
    ASSERT_EQ(0, kvs_rbtree_set(&tree, (char*)"empty_value", (char*)""));
    
    // 验证空字符串键
    char *value = kvs_rbtree_get(&tree, (char*)"");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("empty_key", value);
    free(value);
    
    // 验证空字符串值
    value = kvs_rbtree_get(&tree, (char*)"empty_value");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("", value);
    free(value);
    
    // 删除空字符串键
    ASSERT_EQ(0, kvs_rbtree_delete(&tree, (char*)""));
    ASSERT_EQ(nullptr, kvs_rbtree_get(&tree, (char*)""));
} 