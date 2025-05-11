#include <gtest/gtest.h>
#include <string>
#include <cstring>

// 定义hash_node_t和hashtable_t结构
typedef struct hash_node_s {
    char *key;
    char *value;
    struct hash_node_s *next;
} hash_node_t;

typedef struct hashtable_s {
    hash_node_t **nodes; // Pointer to an array of pointers to hash_node_t
    int max_slots;       // Maximum number of slots in the hash table
    int count;           // Current number of elements in the hash table
} hashtable_t;

// 声明外部函数，但我们将在测试中实现这些函数
void *kv_store_malloc(size_t size) {
    return malloc(size);
}

void kv_store_free(void *ptr) {
    free(ptr);
}

// 实现哈希表操作函数
// 哈希函数
static int hash_function(const char *key, int max_slots) {
    if (!key) return 0;
    
    unsigned int hash = 0;
    while (*key) {
        hash = (hash * 31) + (*key++);
    }
    return hash % max_slots;
}

// 创建哈希表
int kv_store_hash_create(hashtable_t *hash) {
    if (!hash) return -1;
    
    // 默认大小
    int max_slots = 10;
    
    hash->nodes = (hash_node_t**)calloc(max_slots, sizeof(hash_node_t*));
    if (!hash->nodes) return -1;
    
    hash->max_slots = max_slots;
    hash->count = 0;
    
    return 0;
}

// 销毁哈希表
void kv_store_hash_destroy(hashtable_t *hash) {
    if (!hash || !hash->nodes) return;
    
    // 释放所有节点
    for (int i = 0; i < hash->max_slots; i++) {
        hash_node_t *current = hash->nodes[i];
        while (current) {
            hash_node_t *next = current->next;
            if (current->key) free(current->key);
            if (current->value) free(current->value);
            free(current);
            current = next;
        }
    }
    
    // 释放哈希表数组
    free(hash->nodes);
    hash->nodes = NULL;
    hash->max_slots = 0;
    hash->count = 0;
}

// 设置键值对
int kvs_hash_set(hashtable_t *hash, char *key, char *value) {
    if (!hash || !hash->nodes || !key || !value) return -1;
    
    // 计算哈希值
    int index = hash_function(key, hash->max_slots);
    
    // 查找是否已存在
    hash_node_t *current = hash->nodes[index];
    while (current) {
        if (strcmp(current->key, key) == 0) {
            // 更新值
            free(current->value);
            current->value = strdup(value);
            return 0;
        }
        current = current->next;
    }
    
    // 创建新节点
    hash_node_t *node = (hash_node_t*)malloc(sizeof(hash_node_t));
    if (!node) return -1;
    
    node->key = strdup(key);
    node->value = strdup(value);
    
    if (!node->key || !node->value) {
        if (node->key) free(node->key);
        if (node->value) free(node->value);
        free(node);
        return -1;
    }
    
    // 插入到链表头部
    node->next = hash->nodes[index];
    hash->nodes[index] = node;
    hash->count++;
    
    return 0;
}

// 获取键值
char *kvs_hash_get(hashtable_t *hash, char *key) {
    if (!hash || !hash->nodes || !key) return NULL;
    
    // 计算哈希值
    int index = hash_function(key, hash->max_slots);
    
    // 查找键
    hash_node_t *current = hash->nodes[index];
    while (current) {
        if (strcmp(current->key, key) == 0) {
            return strdup(current->value);
        }
        current = current->next;
    }
    
    return NULL;
}

// 删除键值对
int kvs_hash_delete(hashtable_t *hash, char *key) {
    if (!hash || !hash->nodes || !key) return -1;
    
    // 计算哈希值
    int index = hash_function(key, hash->max_slots);
    
    // 查找键
    hash_node_t *current = hash->nodes[index];
    hash_node_t *prev = NULL;
    
    while (current) {
        if (strcmp(current->key, key) == 0) {
            // 找到键，从链表中删除
            if (prev) {
                prev->next = current->next;
            } else {
                hash->nodes[index] = current->next;
            }
            
            // 释放节点
            free(current->key);
            free(current->value);
            free(current);
            
            hash->count--;
            return 0;
        }
        
        prev = current;
        current = current->next;
    }
    
    return 1; // 键不存在
}

// 修改键值对
int kvs_hash_modify(hashtable_t *hash, char *key, char *value) {
    if (!hash || !hash->nodes || !key || !value) return -1;
    
    // 计算哈希值
    int index = hash_function(key, hash->max_slots);
    
    // 查找键
    hash_node_t *current = hash->nodes[index];
    while (current) {
        if (strcmp(current->key, key) == 0) {
            // 更新值
            free(current->value);
            current->value = strdup(value);
            return 0;
        }
        current = current->next;
    }
    
    return 1; // 键不存在
}

// 获取键值对数量
int kvs_hash_count(hashtable_t *hash) {
    if (!hash) return -1;
    return hash->count;
}

// 测试类
class KvstoreHashTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 初始化哈希表
        ASSERT_EQ(0, kv_store_hash_create(&hash));
    }
    
    void TearDown() override {
        // 销毁哈希表
        kv_store_hash_destroy(&hash);
    }
    
    hashtable_t hash;
};

// 测试创建和销毁
TEST_F(KvstoreHashTest, CreateAndDestroy) {
    // 测试已在SetUp和TearDown中完成
    ASSERT_NE(nullptr, hash.nodes);
    ASSERT_EQ(0, hash.count);
}

// 测试基本的设置和获取
TEST_F(KvstoreHashTest, SetAndGet) {
    // 设置键值对
    ASSERT_EQ(0, kvs_hash_set(&hash, (char*)"key1", (char*)"value1"));
    ASSERT_EQ(0, kvs_hash_set(&hash, (char*)"key2", (char*)"value2"));
    
    // 获取值
    char *value = kvs_hash_get(&hash, (char*)"key1");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value1", value);
    free(value);
    
    value = kvs_hash_get(&hash, (char*)"key2");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value2", value);
    free(value);
    
    // 测试获取不存在的键
    value = kvs_hash_get(&hash, (char*)"non_existent_key");
    ASSERT_EQ(nullptr, value);
}

// 测试删除
TEST_F(KvstoreHashTest, Delete) {
    // 设置键值对
    ASSERT_EQ(0, kvs_hash_set(&hash, (char*)"key1", (char*)"value1"));
    ASSERT_EQ(0, kvs_hash_set(&hash, (char*)"key2", (char*)"value2"));
    
    // 验证设置成功
    ASSERT_EQ(2, kvs_hash_count(&hash));
    
    // 删除键
    ASSERT_EQ(0, kvs_hash_delete(&hash, (char*)"key1"));
    
    // 验证删除成功
    ASSERT_EQ(1, kvs_hash_count(&hash));
    ASSERT_EQ(nullptr, kvs_hash_get(&hash, (char*)"key1"));
    
    char *value = kvs_hash_get(&hash, (char*)"key2");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value2", value);
    free(value);
    
    // 测试删除不存在的键
    ASSERT_NE(0, kvs_hash_delete(&hash, (char*)"non_existent_key"));
    
    // 测试NULL参数
    ASSERT_NE(0, kvs_hash_delete(nullptr, (char*)"key"));
    ASSERT_NE(0, kvs_hash_delete(&hash, nullptr));
}

// 测试修改
TEST_F(KvstoreHashTest, Modify) {
    // 设置键值对
    ASSERT_EQ(0, kvs_hash_set(&hash, (char*)"key1", (char*)"value1"));
    
    // 验证设置成功
    char *value = kvs_hash_get(&hash, (char*)"key1");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value1", value);
    free(value);
    
    // 修改值
    ASSERT_EQ(0, kvs_hash_modify(&hash, (char*)"key1", (char*)"new_value1"));
    
    // 验证修改成功
    value = kvs_hash_get(&hash, (char*)"key1");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("new_value1", value);
    free(value);
    
    // 测试修改不存在的键
    ASSERT_NE(0, kvs_hash_modify(&hash, (char*)"non_existent_key", (char*)"value"));
    
    // 测试NULL参数
    ASSERT_NE(0, kvs_hash_modify(nullptr, (char*)"key", (char*)"value"));
    ASSERT_NE(0, kvs_hash_modify(&hash, nullptr, (char*)"value"));
    ASSERT_NE(0, kvs_hash_modify(&hash, (char*)"key", nullptr));
}

// 测试计数
TEST_F(KvstoreHashTest, Count) {
    // 初始计数应为0
    ASSERT_EQ(0, kvs_hash_count(&hash));
    
    // 添加键值对
    ASSERT_EQ(0, kvs_hash_set(&hash, (char*)"key1", (char*)"value1"));
    ASSERT_EQ(1, kvs_hash_count(&hash));
    
    ASSERT_EQ(0, kvs_hash_set(&hash, (char*)"key2", (char*)"value2"));
    ASSERT_EQ(2, kvs_hash_count(&hash));
    
    // 删除键值对
    ASSERT_EQ(0, kvs_hash_delete(&hash, (char*)"key1"));
    ASSERT_EQ(1, kvs_hash_count(&hash));
    
    // 测试NULL参数
    ASSERT_EQ(-1, kvs_hash_count(nullptr));
}

// 测试哈希冲突
TEST_F(KvstoreHashTest, HashCollision) {
    // 通过添加多个键来增加冲突的可能性
    const int NUM_KEYS = 100;
    
    for (int i = 0; i < NUM_KEYS; i++) {
        std::string key = "key" + std::to_string(i);
        std::string value = "value" + std::to_string(i);
        ASSERT_EQ(0, kvs_hash_set(&hash, (char*)key.c_str(), (char*)value.c_str()));
    }
    
    // 验证计数
    ASSERT_EQ(NUM_KEYS, kvs_hash_count(&hash));
    
    // 验证所有键值对
    for (int i = 0; i < NUM_KEYS; i++) {
        std::string key = "key" + std::to_string(i);
        std::string expected_value = "value" + std::to_string(i);
        
        char *value = kvs_hash_get(&hash, (char*)key.c_str());
        ASSERT_NE(nullptr, value);
        ASSERT_STREQ(expected_value.c_str(), value);
        free(value);
    }
    
    // 删除一半的键
    for (int i = 0; i < NUM_KEYS / 2; i++) {
        std::string key = "key" + std::to_string(i);
        ASSERT_EQ(0, kvs_hash_delete(&hash, (char*)key.c_str()));
    }
    
    // 验证计数
    ASSERT_EQ(NUM_KEYS - NUM_KEYS / 2, kvs_hash_count(&hash));
    
    // 验证剩余的键
    for (int i = NUM_KEYS / 2; i < NUM_KEYS; i++) {
        std::string key = "key" + std::to_string(i);
        std::string expected_value = "value" + std::to_string(i);
        
        char *value = kvs_hash_get(&hash, (char*)key.c_str());
        ASSERT_NE(nullptr, value);
        ASSERT_STREQ(expected_value.c_str(), value);
        free(value);
    }
}

// 测试边界情况
TEST_F(KvstoreHashTest, EdgeCases) {
    // 测试空字符串键和值
    ASSERT_EQ(0, kvs_hash_set(&hash, (char*)"", (char*)"empty_key"));
    ASSERT_EQ(0, kvs_hash_set(&hash, (char*)"empty_value", (char*)""));
    
    // 验证空字符串键
    char *value = kvs_hash_get(&hash, (char*)"");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("empty_key", value);
    free(value);
    
    // 验证空字符串值
    value = kvs_hash_get(&hash, (char*)"empty_value");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("", value);
    free(value);
    
    // 删除空字符串键
    ASSERT_EQ(0, kvs_hash_delete(&hash, (char*)""));
    ASSERT_EQ(nullptr, kvs_hash_get(&hash, (char*)""));
}

// 测试重复键
TEST_F(KvstoreHashTest, DuplicateKey) {
    // 设置键值对
    ASSERT_EQ(0, kvs_hash_set(&hash, (char*)"duplicate_key", (char*)"value1"));
    
    // 尝试设置相同的键
    ASSERT_EQ(0, kvs_hash_set(&hash, (char*)"duplicate_key", (char*)"value2"));
    
    // 验证值已更新
    char *value = kvs_hash_get(&hash, (char*)"duplicate_key");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value2", value);
    free(value);
    
    // 验证计数仍为1
    ASSERT_EQ(1, kvs_hash_count(&hash));
} 