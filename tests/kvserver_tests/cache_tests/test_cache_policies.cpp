#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

extern "C" {
#include "kv_cache.h"
}

class CachePoliciesTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 测试前的设置
    }

    void TearDown() override {
        // 测试后的清理
    }

    // 辅助函数：创建指定策略的缓存
    kv_cache_t* create_cache(kv_cache_policy_t policy, size_t max_entries = 100) {
        kv_cache_config_t* config = kv_cache_config_create_default(policy);
        EXPECT_NE(config, nullptr);
        
        config->max_entries = max_entries;
        config->default_ttl = 0; // 永不过期
        
        kv_cache_t* cache = kv_cache_create(config);
        kv_cache_config_destroy(config);
        
        return cache;
    }

    // 辅助函数：设置键值对
    void set_key_value(kv_cache_t* cache, const std::string& key, const std::string& value) {
        int result = kv_cache_set(cache, key.c_str(), key.length(), 
                                  value.c_str(), value.length(), 0);
        EXPECT_EQ(result, KV_SUCCESS);
    }

    // 辅助函数：获取值
    std::string get_value(kv_cache_t* cache, const std::string& key) {
        char* value = nullptr;
        size_t value_len = 0;
        
        int result = kv_cache_get(cache, key.c_str(), key.length(), &value, &value_len);
        if (result == KV_SUCCESS && value != nullptr) {
            std::string result_str(value, value_len);
            free(value);
            return result_str;
        }
        return "";
    }

    // 辅助函数：检查键是否存在
    bool key_exists(kv_cache_t* cache, const std::string& key) {
        return kv_cache_exists(cache, key.c_str(), key.length());
    }
};

// 测试LFU策略基本功能
TEST_F(CachePoliciesTest, LFU_BasicFunctionality) {
    kv_cache_t* cache = create_cache(KV_CACHE_POLICY_LFU, 3);
    ASSERT_NE(cache, nullptr);

    // 添加三个条目
    set_key_value(cache, "key1", "value1");
    set_key_value(cache, "key2", "value2");
    set_key_value(cache, "key3", "value3");

    // 验证所有条目都存在
    EXPECT_TRUE(key_exists(cache, "key1"));
    EXPECT_TRUE(key_exists(cache, "key2"));
    EXPECT_TRUE(key_exists(cache, "key3"));

    // 模拟不同的访问频率
    // key1: 访问5次（高频）
    for (int i = 0; i < 5; i++) {
        get_value(cache, "key1");
    }
    
    // key2: 访问2次（中频）
    for (int i = 0; i < 2; i++) {
        get_value(cache, "key2");
    }
    
    // key3: 不额外访问（低频，只有初始的1次）

    // 添加第四个条目，应该淘汰最少使用的key3
    set_key_value(cache, "key4", "value4");

    // 验证淘汰结果
    EXPECT_TRUE(key_exists(cache, "key1"));  // 高频，应该保留
    EXPECT_TRUE(key_exists(cache, "key2"));  // 中频，应该保留
    EXPECT_TRUE(key_exists(cache, "key4"));  // 新添加，应该存在

    kv_cache_destroy(cache);
}

// 测试CLOCK策略基本功能
TEST_F(CachePoliciesTest, CLOCK_BasicFunctionality) {
    kv_cache_t* cache = create_cache(KV_CACHE_POLICY_CLOCK, 4);
    ASSERT_NE(cache, nullptr);

    // 添加四个条目
    set_key_value(cache, "page1", "data1");
    set_key_value(cache, "page2", "data2");
    set_key_value(cache, "page3", "data3");
    set_key_value(cache, "page4", "data4");

    // 验证所有条目都存在
    EXPECT_TRUE(key_exists(cache, "page1"));
    EXPECT_TRUE(key_exists(cache, "page2"));
    EXPECT_TRUE(key_exists(cache, "page3"));
    EXPECT_TRUE(key_exists(cache, "page4"));

    // 访问部分页面（设置引用位）
    get_value(cache, "page1");  // 引用位=1
    get_value(cache, "page3");  // 引用位=1
    // page2和page4未访问，引用位=0

    // 添加第五个条目，应该淘汰引用位为0的条目
    set_key_value(cache, "page5", "data5");

    // 验证page5存在
    EXPECT_TRUE(key_exists(cache, "page5"));
    
    // 被访问过的页面应该有更高的保留概率
    EXPECT_TRUE(key_exists(cache, "page1"));
    EXPECT_TRUE(key_exists(cache, "page3"));

    kv_cache_destroy(cache);
}

// 测试ARC策略基本功能
TEST_F(CachePoliciesTest, ARC_BasicFunctionality) {
    kv_cache_t* cache = create_cache(KV_CACHE_POLICY_ARC, 4);
    ASSERT_NE(cache, nullptr);

    // 添加条目到T1队列（最近访问）
    set_key_value(cache, "doc1", "content1");
    set_key_value(cache, "doc2", "content2");

    // 再次访问，移动到T2队列（频繁访问）
    get_value(cache, "doc1");
    get_value(cache, "doc2");

    // 添加更多条目
    set_key_value(cache, "doc3", "content3");
    set_key_value(cache, "doc4", "content4");

    // 验证所有条目都存在
    EXPECT_TRUE(key_exists(cache, "doc1"));
    EXPECT_TRUE(key_exists(cache, "doc2"));
    EXPECT_TRUE(key_exists(cache, "doc3"));
    EXPECT_TRUE(key_exists(cache, "doc4"));

    // 创建混合访问模式
    get_value(cache, "doc1");  // 频繁访问
    get_value(cache, "doc3");  // 偶尔访问

    // 添加新条目，触发ARC自适应淘汰
    set_key_value(cache, "doc5", "content5");

    // 验证新条目存在
    EXPECT_TRUE(key_exists(cache, "doc5"));
    
    // 频繁访问的条目应该有更高的保留概率
    EXPECT_TRUE(key_exists(cache, "doc1"));

    kv_cache_destroy(cache);
}

// 测试所有策略的并发安全性
TEST_F(CachePoliciesTest, ConcurrentAccess) {
    std::vector<kv_cache_policy_t> policies = {
        KV_CACHE_POLICY_LFU,
        KV_CACHE_POLICY_CLOCK,
        KV_CACHE_POLICY_ARC
    };

    for (auto policy : policies) {
        kv_cache_t* cache = create_cache(policy, 100);
        ASSERT_NE(cache, nullptr);

        const int num_threads = 4;
        const int ops_per_thread = 100;
        std::vector<std::thread> threads;

        // 启动多个线程进行并发操作
        for (int t = 0; t < num_threads; t++) {
            threads.emplace_back([cache, t, ops_per_thread]() {
                for (int i = 0; i < ops_per_thread; i++) {
                    std::string key = "thread" + std::to_string(t) + "_key" + std::to_string(i);
                    std::string value = "thread" + std::to_string(t) + "_value" + std::to_string(i);
                    
                    // 设置键值对
                    kv_cache_set(cache, key.c_str(), key.length(), 
                                value.c_str(), value.length(), 0);
                    
                    // 获取值
                    char* retrieved_value = nullptr;
                    size_t value_len = 0;
                    kv_cache_get(cache, key.c_str(), key.length(), 
                                &retrieved_value, &value_len);
                    if (retrieved_value) {
                        free(retrieved_value);
                    }
                }
            });
        }

        // 等待所有线程完成
        for (auto& thread : threads) {
            thread.join();
        }

        // 验证缓存完整性
        EXPECT_TRUE(kv_cache_validate_integrity(cache));

        kv_cache_destroy(cache);
    }
}

// 测试策略性能对比
TEST_F(CachePoliciesTest, PerformanceComparison) {
    std::vector<kv_cache_policy_t> policies = {
        KV_CACHE_POLICY_LRU,
        KV_CACHE_POLICY_LFU,
        KV_CACHE_POLICY_FIFO,
        KV_CACHE_POLICY_RANDOM,
        KV_CACHE_POLICY_CLOCK,
        KV_CACHE_POLICY_ARC
    };

    const int num_operations = 1000;
    const size_t cache_size = 100;

    for (auto policy : policies) {
        kv_cache_t* cache = create_cache(policy, cache_size);
        ASSERT_NE(cache, nullptr);

        auto start_time = std::chrono::high_resolution_clock::now();

        // 执行操作
        for (int i = 0; i < num_operations; i++) {
            std::string key = "perf_key_" + std::to_string(i % (cache_size * 2));
            std::string value = "perf_value_" + std::to_string(i);
            
            set_key_value(cache, key, value);
            
            // 偶尔进行读取操作
            if (i % 3 == 0) {
                get_value(cache, key);
            }
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time).count();

        // 获取统计信息
        kv_cache_stats_t stats;
        EXPECT_EQ(kv_cache_get_stats(cache, &stats), KV_SUCCESS);

        // 输出性能信息（仅用于调试）
        printf("策略 %s: 执行时间 %ld μs, 命中率 %.2f%%, 淘汰次数 %llu\n",
               kv_cache_policy_name(policy), duration,
               kv_cache_hit_ratio(&stats) * 100.0,
               (unsigned long long)stats.evictions);

        kv_cache_destroy(cache);
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
} 