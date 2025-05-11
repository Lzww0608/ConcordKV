#include <gtest/gtest.h>
#include <string>
#include <cstring>

extern "C" {
#include "kv_store.h"

// 声明外部函数，这样测试可以直接访问这些函数
extern void *kv_store_malloc(size_t size);
extern void kv_store_free(void *ptr);
}

// 测试类
class KvstoreArrayTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 初始化数组存储
        ASSERT_EQ(0, kv_store_array_create(&array));
    }
    
    void TearDown() override {
        // 销毁数组存储
        kv_store_array_destroy(&array);
    }
    
    array_t array;
};

// 测试创建和销毁
TEST_F(KvstoreArrayTest, CreateAndDestroy) {
    // 测试已在SetUp和TearDown中完成
    ASSERT_NE(nullptr, array.array_table);
    ASSERT_EQ(0, array.array_idx);
}

// 测试设置和获取
TEST_F(KvstoreArrayTest, SetAndGet) {
    // 设置键值对
    ASSERT_EQ(0, kvs_array_set(&array, (char*)"key1", (char*)"value1"));
    ASSERT_EQ(0, kvs_array_set(&array, (char*)"key2", (char*)"value2"));
    
    // 获取并验证值
    char *value = kvs_array_get(&array, (char*)"key1");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value1", value);
    
    value = kvs_array_get(&array, (char*)"key2");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value2", value);
    
    // 测试获取不存在的键
    value = kvs_array_get(&array, (char*)"non_existent_key");
    ASSERT_EQ(nullptr, value);
}

// 测试删除
TEST_F(KvstoreArrayTest, Delete) {
    // 设置键值对
    ASSERT_EQ(0, kvs_array_set(&array, (char*)"key1", (char*)"value1"));
    ASSERT_EQ(0, kvs_array_set(&array, (char*)"key2", (char*)"value2"));
    
    // 验证设置成功
    ASSERT_EQ(2, kvs_array_count(&array));
    
    // 删除键
    ASSERT_EQ(0, kvs_array_delete(&array, (char*)"key1"));
    
    // 验证删除成功
    ASSERT_EQ(1, kvs_array_count(&array));
    ASSERT_EQ(nullptr, kvs_array_get(&array, (char*)"key1"));
    ASSERT_NE(nullptr, kvs_array_get(&array, (char*)"key2"));
    
    // 测试删除不存在的键
    ASSERT_GT(kvs_array_delete(&array, (char*)"non_existent_key"), 0);
    
    // 测试NULL参数
    ASSERT_LT(kvs_array_delete(nullptr, (char*)"key"), 0);
    ASSERT_LT(kvs_array_delete(&array, nullptr), 0);
}

// 测试修改
TEST_F(KvstoreArrayTest, Modify) {
    // 设置键值对
    ASSERT_EQ(0, kvs_array_set(&array, (char*)"key1", (char*)"value1"));
    
    // 验证设置成功
    char *value = kvs_array_get(&array, (char*)"key1");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("value1", value);
    
    // 修改值
    ASSERT_EQ(0, kvs_array_modify(&array, (char*)"key1", (char*)"new_value1"));
    
    // 验证修改成功
    value = kvs_array_get(&array, (char*)"key1");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("new_value1", value);
    
    // 测试修改不存在的键
    ASSERT_GT(kvs_array_modify(&array, (char*)"non_existent_key", (char*)"value"), 0);
    
    // 测试NULL参数
    ASSERT_LT(kvs_array_modify(nullptr, (char*)"key", (char*)"value"), 0);
    ASSERT_LT(kvs_array_modify(&array, nullptr, (char*)"value"), 0);
    ASSERT_LT(kvs_array_modify(&array, (char*)"key", nullptr), 0);
}

// 测试计数
TEST_F(KvstoreArrayTest, Count) {
    // 初始计数应为0
    ASSERT_EQ(0, kvs_array_count(&array));
    
    // 添加一个键值对
    ASSERT_EQ(0, kvs_array_set(&array, (char*)"key1", (char*)"value1"));
    ASSERT_EQ(1, kvs_array_count(&array));
    
    // 添加另一个键值对
    ASSERT_EQ(0, kvs_array_set(&array, (char*)"key2", (char*)"value2"));
    ASSERT_EQ(2, kvs_array_count(&array));
    
    // 删除一个键值对
    ASSERT_EQ(0, kvs_array_delete(&array, (char*)"key1"));
    ASSERT_EQ(1, kvs_array_count(&array));
    
    // 测试NULL参数
    ASSERT_LT(kvs_array_count(nullptr), 0);
}

// 测试边界情况
TEST_F(KvstoreArrayTest, EdgeCases) {
    // 测试空字符串键和值
    ASSERT_EQ(0, kvs_array_set(&array, (char*)"", (char*)"empty_key"));
    ASSERT_EQ(0, kvs_array_set(&array, (char*)"empty_value", (char*)""));
    
    // 验证空字符串键
    char *value = kvs_array_get(&array, (char*)"");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("empty_key", value);
    
    // 验证空字符串值
    value = kvs_array_get(&array, (char*)"empty_value");
    ASSERT_NE(nullptr, value);
    ASSERT_STREQ("", value);
}

// 测试大量数据
TEST_F(KvstoreArrayTest, LargeData) {
    const int NUM_ENTRIES = 100; // 不超过KVS_ARRAY_SIZE
    
    // 添加多个键值对
    for (int i = 0; i < NUM_ENTRIES; i++) {
        std::string key = "key" + std::to_string(i);
        std::string value = "value" + std::to_string(i);
        ASSERT_EQ(0, kvs_array_set(&array, (char*)key.c_str(), (char*)value.c_str()));
    }
    
    // 验证计数
    ASSERT_EQ(NUM_ENTRIES, kvs_array_count(&array));
    
    // 验证所有键值对
    for (int i = 0; i < NUM_ENTRIES; i++) {
        std::string key = "key" + std::to_string(i);
        std::string expected_value = "value" + std::to_string(i);
        
        char *value = kvs_array_get(&array, (char*)key.c_str());
        ASSERT_NE(nullptr, value);
        ASSERT_STREQ(expected_value.c_str(), value);
    }
    
    // 删除部分键值对
    for (int i = 0; i < NUM_ENTRIES / 2; i++) {
        std::string key = "key" + std::to_string(i);
        ASSERT_EQ(0, kvs_array_delete(&array, (char*)key.c_str()));
    }
    
    // 验证计数更新
    ASSERT_EQ(NUM_ENTRIES - NUM_ENTRIES / 2, kvs_array_count(&array));
}

// 测试错误处理
TEST_F(KvstoreArrayTest, ErrorHandling) {
    // 测试NULL参数
    ASSERT_EQ(nullptr, kvs_array_get(nullptr, (char*)"key"));
    ASSERT_LT(kvs_array_set(nullptr, (char*)"key", (char*)"value"), 0);
    ASSERT_LT(kvs_array_set(&array, nullptr, (char*)"value"), 0);
    ASSERT_LT(kvs_array_set(&array, (char*)"key", nullptr), 0);
}

// 修复kvstore_array.c中的bug
// Bug 1: kvs_array_set中找重复键的逻辑问题
// Bug 2: kvs_array_delete中删除后未清理空间
class KvstoreArrayBugTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(0, kv_store_array_create(&array));
    }
    
    void TearDown() override {
        kv_store_array_destroy(&array);
    }
    
    array_t array;
};

// 测试kvs_array_set中查找重复键的逻辑
TEST_F(KvstoreArrayBugTest, SetDuplicateKeyBug) {
    // 首先设置一个键值对
    ASSERT_EQ(0, kvs_array_set(&array, (char*)"duplicate_key", (char*)"original_value"));
    
    // 再次设置相同的键，不同的值
    ASSERT_EQ(0, kvs_array_set(&array, (char*)"duplicate_key", (char*)"new_value"));
    
    // 获取该键的值
    char *value = kvs_array_get(&array, (char*)"duplicate_key");
    ASSERT_NE(nullptr, value);
    
    // 应该是最新的值，但由于bug可能不是
    ASSERT_STREQ("new_value", value);
    
    // 检查计数，应该仍然是1，但如果有bug可能是2
    ASSERT_EQ(1, kvs_array_count(&array));
}

// 测试kvs_array_delete在删除后是否正确处理
TEST_F(KvstoreArrayBugTest, DeleteSpaceManagementBug) {
    // 添加多个键值对
    ASSERT_EQ(0, kvs_array_set(&array, (char*)"key1", (char*)"value1"));
    ASSERT_EQ(0, kvs_array_set(&array, (char*)"key2", (char*)"value2"));
    ASSERT_EQ(0, kvs_array_set(&array, (char*)"key3", (char*)"value3"));
    
    // 删除中间的键
    ASSERT_EQ(0, kvs_array_delete(&array, (char*)"key2"));
    
    // 验证计数
    ASSERT_EQ(2, kvs_array_count(&array));
    
    // 添加新的键值对，应该使用之前删除的空间
    ASSERT_EQ(0, kvs_array_set(&array, (char*)"key4", (char*)"value4"));
    
    // 验证所有键值对
    ASSERT_NE(nullptr, kvs_array_get(&array, (char*)"key1"));
    ASSERT_EQ(nullptr, kvs_array_get(&array, (char*)"key2"));
    ASSERT_NE(nullptr, kvs_array_get(&array, (char*)"key3"));
    ASSERT_NE(nullptr, kvs_array_get(&array, (char*)"key4"));
    
    // 验证计数
    ASSERT_EQ(3, kvs_array_count(&array));
} 