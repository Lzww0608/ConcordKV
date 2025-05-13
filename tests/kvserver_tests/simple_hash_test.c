#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 简单哈希表实现 - 独立自包含版本，不依赖外部头文件

// 哈希节点结构
typedef struct hash_node_s {
    char *key;
    char *value;
    struct hash_node_s *next;
    unsigned char node_type;
} hash_node_t;

// 桶结构
typedef struct bucket_s {
    union {
        hash_node_t *list;
        void *tree;
    } data;
    unsigned char is_tree;
    int size;
} bucket_t;

// 哈希表结构
typedef struct hashtable_s {
    bucket_t *buckets;
    int max_slots;
    int count;
} hashtable_t;

// 常量定义
#define INITIAL_CAPACITY     16    // 小点方便测试

// 内存分配函数
void *kv_store_malloc(size_t size) {
    return malloc(size);
}

void kv_store_free(void *ptr) {
    free(ptr);
}

// 简化版哈希函数 - 只用于测试
static int _hash(const char *key, int size) {
    if (!key) return 0;
    
    int hash = 0;
    while (*key) {
        hash = (hash * 31) + (*key++);
    }
    
    return hash % size;
}

// 创建哈希节点
static hash_node_t *_create_node(const char *key, const char *value) {
    hash_node_t *node = (hash_node_t*)kv_store_malloc(sizeof(hash_node_t));
    if (!node) return NULL;

    node->key = strdup(key);
    if (!node->key) {
        kv_store_free(node);
        return NULL;
    }

    node->value = strdup(value);
    if (!node->value) {
        kv_store_free(node->key);
        kv_store_free(node);
        return NULL;
    }

    node->next = NULL;
    node->node_type = 0;  // 链表节点

    return node;
}

// 初始化哈希表
int kv_store_hash_create(hashtable_t *hash) {
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
void kv_store_hash_destroy(hashtable_t *hash) {
    if (!hash) return;

    for (int i = 0; i < hash->max_slots; i++) {
        bucket_t *bucket = &hash->buckets[i];
        
        // 只处理链表，测试中不会用到红黑树
        if (!bucket->is_tree) {
            hash_node_t *node = bucket->data.list;
            hash_node_t *next;
            
            while (node) {
                next = node->next;
                
                if (node->key) kv_store_free(node->key);
                if (node->value) kv_store_free(node->value);
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

// 设置键值对
int kvs_hash_set(hashtable_t *hash, char *key, char *value) {
    if (!hash || !key || !value) return -1;

    int idx = _hash(key, hash->max_slots);
    bucket_t *bucket = &hash->buckets[idx];

    // 检查是否已存在
    hash_node_t *current = bucket->data.list;
    while (current) {
        if (strcmp(current->key, key) == 0) {
            // 更新值
            kv_store_free(current->value);
            current->value = strdup(value);
            return 0;
        }
        current = current->next;
    }

    // 创建新节点
    hash_node_t *new_node = _create_node(key, value);
    if (!new_node) return -1;
    
    // 插入链表头
    new_node->next = bucket->data.list;
    bucket->data.list = new_node;
    
    bucket->size++;
    hash->count++;
    
    return 0;
}

// 获取值
char *kvs_hash_get(hashtable_t *hash, char *key) {
    if (!hash || !key) return NULL;

    int idx = _hash(key, hash->max_slots);
    bucket_t *bucket = &hash->buckets[idx];
    
    hash_node_t *current = bucket->data.list;
    while (current) {
        if (strcmp(current->key, key) == 0) {
            return current->value;
        }
        current = current->next;
    }

    return NULL;
}

// 删除键值对
int kvs_hash_delete(hashtable_t *hash, char *key) {
    if (!hash || !key) return -1;
    
    int idx = _hash(key, hash->max_slots);
    bucket_t *bucket = &hash->buckets[idx];
    
    hash_node_t *prev = NULL;
    hash_node_t *current = bucket->data.list;
    
    while (current) {
        if (strcmp(current->key, key) == 0) {
            // 从链表中删除
            if (prev) {
                prev->next = current->next;
            } else {
                bucket->data.list = current->next;
            }
            
            kv_store_free(current->key);
            kv_store_free(current->value);
            kv_store_free(current);
            
            bucket->size--;
            hash->count--;
            
            return 0;
        }
        
        prev = current;
        current = current->next;
    }
    
    return -1;  // 键不存在
}

// 获取键值对数量
int kvs_hash_count(hashtable_t *hash) {
    return hash ? hash->count : 0;
}

// 修改键值对
int kvs_hash_modify(hashtable_t *hash, char *key, char *value) {
    return kvs_hash_set(hash, key, value);
}

int main() {
    // 创建哈希表
    hashtable_t hash;
    printf("创建哈希表...\n");
    if (kv_store_hash_create(&hash) != 0) {
        printf("创建哈希表失败\n");
        return 1;
    }
    
    // 插入一些键值对
    printf("插入键值对...\n");
    kvs_hash_set(&hash, "key1", "value1");
    kvs_hash_set(&hash, "key2", "value2");
    kvs_hash_set(&hash, "key3", "value3");
    
    // 查询键值对
    printf("查询键值对...\n");
    char *val1 = kvs_hash_get(&hash, "key1");
    char *val2 = kvs_hash_get(&hash, "key2");
    char *val3 = kvs_hash_get(&hash, "key3");
    
    if (val1) printf("key1: %s\n", val1);
    if (val2) printf("key2: %s\n", val2);
    if (val3) printf("key3: %s\n", val3);
    
    // 统计当前键值对数量
    printf("当前哈希表中有 %d 个键值对\n", kvs_hash_count(&hash));
    
    // 删除一个键值对
    printf("删除键值对 key2...\n");
    kvs_hash_delete(&hash, "key2");
    
    // 再次查询
    val2 = kvs_hash_get(&hash, "key2");
    if (!val2) {
        printf("key2 已被成功删除\n");
    }
    
    // 统计当前键值对数量
    printf("当前哈希表中有 %d 个键值对\n", kvs_hash_count(&hash));
    
    // 测试红黑树转换
    printf("测试更多键值对...\n");
    // 插入更多键，在生产环境会触发链表到红黑树的转换
    for (int i = 0; i < 10; i++) {
        char key[20], value[20];
        sprintf(key, "tree_key_%d", i);
        sprintf(value, "tree_value_%d", i);
        kvs_hash_set(&hash, key, value);
    }
    
    // 查询其中一个键
    char *tree_val = kvs_hash_get(&hash, "tree_key_5");
    if (tree_val) {
        printf("tree_key_5: %s\n", tree_val);
    }
    
    // 销毁哈希表
    printf("销毁哈希表...\n");
    kv_store_hash_destroy(&hash);
    
    printf("测试完成!\n");
    return 0;
} 