/*
 * @Author: Lzww0608
 * @Date: 2025-6-7 16:00:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-7 19:13:30
 * @Description: ConcordKV LSM-Tree批量写入功能GTest单元测试
 * 
 * 参考开源项目最佳实践：
 * - Google Test最佳实践
 * - RocksDB批量写入测试设计
 * - LevelDB WriteBatch测试模式
 */

#include <gtest/gtest.h>

extern "C" {
#include "lsm_tree.h"
#include "kv_memory.h"
#include "kv_error.h"
}

#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <random>
#include <future>
#include <atomic>
#include <algorithm>
#include <unordered_set>

// === 测试基础设施 ===

class LSMBatchWriteTest : public ::testing::Test {
protected:
    static constexpr const char* TEST_DIR_BASE = "./gtest_lsm_batch";
    static constexpr int DEFAULT_TIMEOUT_SEC = 30;
    static constexpr int PERFORMANCE_TIMEOUT_SEC = 60;
    
    std::string test_dir_;
    lsm_tree_t* tree_;
    lsm_batch_writer_t* writer_;
    
    void SetUp() override {
        // 为每个测试用例创建唯一目录
        test_dir_ = std::string(TEST_DIR_BASE) + "_" + 
                   ::testing::UnitTest::GetInstance()->current_test_info()->name();
        
        // 清理测试目录
        CleanupTestDirectory(test_dir_);
        
        // 创建LSM-Tree实例
        tree_ = CreateTestLSMTree(test_dir_);
        ASSERT_NE(tree_, nullptr) << "Failed to create LSM-Tree instance";
        
        writer_ = nullptr;
    }
    
    void TearDown() override {
        // 清理批量写入器
        if (writer_) {
            lsm_batch_writer_destroy(writer_);
            writer_ = nullptr;
        }
        
        // 清理LSM-Tree
        if (tree_) {
            lsm_tree_destroy(tree_);
            tree_ = nullptr;
        }
        
        // 清理测试目录
        CleanupTestDirectory(test_dir_);
    }
    
    // 创建测试专用的LSM-Tree实例
    lsm_tree_t* CreateTestLSMTree(const std::string& dir) {
        lsm_tree_config_t config = lsm_tree_default_config(dir.c_str());
        config.enable_wal = false; // 大部分测试禁用WAL提高速度
        config.compaction.enable_background_compaction = false;
        config.memtable_size = 1024 * 1024; // 1MB
        
        lsm_tree_t* tree = lsm_tree_create(&config);
        if (tree && lsm_tree_open(tree) != KV_ERR_NONE) {
            lsm_tree_destroy(tree);
            return nullptr;
        }
        return tree;
    }
    
    // 创建批量写入器
    lsm_batch_writer_t* CreateBatchWriter(const lsm_batch_config_t& config) {
        return lsm_batch_writer_create(tree_, &config);
    }
    
    // 生成测试键值对
    std::pair<std::string, std::string> GenerateKV(int index, const std::string& prefix = "key") {
        return {
            prefix + "_" + std::to_string(index),
            "value_" + std::to_string(index) + "_data_" + std::string(50, 'x')
        };
    }
    
    // 超时执行辅助函数
    template<typename Func>
    bool ExecuteWithTimeout(Func func, int timeout_sec = DEFAULT_TIMEOUT_SEC) {
        std::promise<bool> promise;
        auto future = promise.get_future();
        
        std::thread worker([&promise, func]() {
            try {
                func();
                promise.set_value(true);
            } catch (...) {
                promise.set_value(false);
            }
        });
        
        bool completed = (future.wait_for(std::chrono::seconds(timeout_sec)) == 
                         std::future_status::ready);
        
        if (completed) {
            worker.join();
            return future.get();
        } else {
            worker.detach(); // 超时情况下分离线程
            return false;
        }
    }
    
    // 清理目录
    void CleanupTestDirectory(const std::string& path) {
        std::string cmd = "rm -rf " + path;
        int ret = std::system(cmd.c_str());
        (void)ret; // 忽略返回值
    }
    
    // 验证数据完整性
    void VerifyKeyExists(const std::string& key, const std::string& expected_value) {
        char* value = nullptr;
        size_t value_len = 0;
        int ret = lsm_tree_get(tree_, key.c_str(), key.length(), &value, &value_len);
        
        ASSERT_EQ(ret, KV_ERR_NONE) << "Key '" << key << "' should exist";
        ASSERT_NE(value, nullptr);
        ASSERT_EQ(std::string(value, value_len), expected_value);
        free(value);
    }
    
    void VerifyKeyNotExists(const std::string& key) {
        char* value = nullptr;
        size_t value_len = 0;
        int ret = lsm_tree_get(tree_, key.c_str(), key.length(), &value, &value_len);
        ASSERT_EQ(ret, KV_ERR_NOT_FOUND) << "Key '" << key << "' should not exist";
    }
};

// === 基础功能测试 ===

TEST_F(LSMBatchWriteTest, BasicPutOperations) {
    ASSERT_TRUE(ExecuteWithTimeout([this]() {
        // 创建批量写入器
        lsm_batch_config_t config = lsm_batch_default_config();
        config.max_batch_size = 100;
        writer_ = CreateBatchWriter(config);
        ASSERT_NE(writer_, nullptr);
        
        // 添加PUT操作
        const int num_ops = 50;
        std::vector<std::pair<std::string, std::string>> kv_pairs;
        
        for (int i = 0; i < num_ops; ++i) {
            auto kv = GenerateKV(i, "batch_put");
            kv_pairs.push_back(kv);
            
            int ret = lsm_batch_put(writer_, kv.first.c_str(), kv.first.length(),
                                   kv.second.c_str(), kv.second.length());
            EXPECT_EQ(ret, KV_ERR_NONE) << "Failed to add PUT operation " << i;
        }
        
        // 检查批次状态
        size_t entry_count, memory_usage;
        int ret = lsm_batch_get_status(writer_, &entry_count, &memory_usage);
        EXPECT_EQ(ret, KV_ERR_NONE);
        EXPECT_EQ(entry_count, num_ops);
        EXPECT_GT(memory_usage, 0);
        
        // 提交批次
        lsm_batch_result_t result;
        ret = lsm_batch_commit(writer_, &result);
        EXPECT_EQ(ret, KV_ERR_NONE);
        EXPECT_EQ(result.committed_entries, num_ops);
        EXPECT_EQ(result.failed_entries, 0);
        EXPECT_GT(result.commit_time_us, 0);
        
        // 验证数据完整性
        for (const auto& kv : kv_pairs) {
            VerifyKeyExists(kv.first, kv.second);
        }
    }));
}

TEST_F(LSMBatchWriteTest, BasicDeleteOperations) {
    ASSERT_TRUE(ExecuteWithTimeout([this]() {
        // 首先插入一些数据
        const int num_keys = 20;
        std::vector<std::string> keys;
        
        for (int i = 0; i < num_keys; ++i) {
            auto kv = GenerateKV(i, "batch_del");
            keys.push_back(kv.first);
            
            int ret = lsm_tree_put(tree_, kv.first.c_str(), kv.first.length(),
                                  kv.second.c_str(), kv.second.length());
            EXPECT_EQ(ret, KV_ERR_NONE);
        }
        
        // 创建批量写入器进行删除
        lsm_batch_config_t config = lsm_batch_default_config();
        writer_ = CreateBatchWriter(config);
        ASSERT_NE(writer_, nullptr);
        
        // 删除一半的键  
        const int del_count = num_keys / 2;
        for (int i = 0; i < del_count; ++i) {
            int ret = lsm_batch_delete(writer_, keys[i].c_str(), keys[i].length());
            EXPECT_EQ(ret, KV_ERR_NONE);
        }
        
        // 提交删除操作
        lsm_batch_result_t result;
        int ret = lsm_batch_commit(writer_, &result);
        EXPECT_EQ(ret, KV_ERR_NONE);
        EXPECT_EQ(result.committed_entries, del_count);
        
        // 验证删除结果
        for (int i = 0; i < num_keys; ++i) {
            if (i < del_count) {
                VerifyKeyNotExists(keys[i]);
            } else {
                auto kv = GenerateKV(i, "batch_del");
                VerifyKeyExists(kv.first, kv.second);
            }
        }
    }));
}

TEST_F(LSMBatchWriteTest, MixedOperations) {
    ASSERT_TRUE(ExecuteWithTimeout([this]() {
        lsm_batch_config_t config = lsm_batch_default_config();
        config.max_batch_size = 200;
        writer_ = CreateBatchWriter(config);
        ASSERT_NE(writer_, nullptr);
        
        // 混合操作：PUT和DELETE
        const int base_count = 30;
        std::vector<std::pair<std::string, std::string>> put_pairs;
        std::vector<std::string> delete_keys;
        
        // 添加PUT操作
        for (int i = 0; i < base_count; ++i) {
            auto kv = GenerateKV(i, "mixed_put");
            put_pairs.push_back(kv);
            
            int ret = lsm_batch_put(writer_, kv.first.c_str(), kv.first.length(),
                                   kv.second.c_str(), kv.second.length());
            EXPECT_EQ(ret, KV_ERR_NONE);
        }
        
        // 添加DELETE操作（删除不存在的键）
        for (int i = 0; i < base_count / 2; ++i) {
            std::string key = "mixed_del_" + std::to_string(i);
            delete_keys.push_back(key);
            
            int ret = lsm_batch_delete(writer_, key.c_str(), key.length());
            EXPECT_EQ(ret, KV_ERR_NONE);
        }
        
        // 提交混合操作
        lsm_batch_result_t result;
        int ret = lsm_batch_commit(writer_, &result);
        EXPECT_EQ(ret, KV_ERR_NONE);
        EXPECT_EQ(result.committed_entries, base_count + base_count / 2);
        
        // 验证PUT操作结果
        for (const auto& kv : put_pairs) {
            VerifyKeyExists(kv.first, kv.second);
        }
        
        // 验证DELETE操作结果（删除不存在的键应该成功但无效果）
        for (const auto& key : delete_keys) {
            VerifyKeyNotExists(key);
        }
    }));
}

// === 去重功能测试 ===

TEST_F(LSMBatchWriteTest, DeduplicationFeature) {
    ASSERT_TRUE(ExecuteWithTimeout([this]() {
        lsm_batch_config_t config = lsm_batch_default_config();
        config.enable_deduplication = true;
        config.max_batch_size = 100;
        writer_ = CreateBatchWriter(config);
        ASSERT_NE(writer_, nullptr);
        
        const std::string key = "dedup_key";
        const std::string value1 = "value_1";
        const std::string value2 = "value_2";
        const std::string value3 = "value_3_final";
        
        // 对同一个键执行多次PUT操作
        int ret = lsm_batch_put(writer_, key.c_str(), key.length(),
                               value1.c_str(), value1.length());
        EXPECT_EQ(ret, KV_ERR_NONE);
        
        ret = lsm_batch_put(writer_, key.c_str(), key.length(),
                           value2.c_str(), value2.length());
        EXPECT_EQ(ret, KV_ERR_NONE);
        
        ret = lsm_batch_put(writer_, key.c_str(), key.length(),
                           value3.c_str(), value3.length());
        EXPECT_EQ(ret, KV_ERR_NONE);
        
        // 检查批次状态（去重后应该只有1个条目）
        size_t entry_count, memory_usage;
        ret = lsm_batch_get_status(writer_, &entry_count, &memory_usage);
        EXPECT_EQ(ret, KV_ERR_NONE);
        EXPECT_EQ(entry_count, 1) << "Deduplication should keep only the latest entry";
        
        // 提交并验证最终值
        lsm_batch_result_t result;
        ret = lsm_batch_commit(writer_, &result);
        EXPECT_EQ(ret, KV_ERR_NONE);
        EXPECT_EQ(result.committed_entries, 1);
        
        // 应该只保留最后一个值
        VerifyKeyExists(key, value3);
    }));
}

TEST_F(LSMBatchWriteTest, DeduplicationPutDelete) {
    ASSERT_TRUE(ExecuteWithTimeout([this]() {
        lsm_batch_config_t config = lsm_batch_default_config();
        config.enable_deduplication = true;
        writer_ = CreateBatchWriter(config);
        ASSERT_NE(writer_, nullptr);
        
        const std::string key = "put_del_key";
        const std::string value = "test_value";
        
        // PUT -> DELETE -> PUT序列
        int ret = lsm_batch_put(writer_, key.c_str(), key.length(),
                               value.c_str(), value.length());
        EXPECT_EQ(ret, KV_ERR_NONE);
        
        ret = lsm_batch_delete(writer_, key.c_str(), key.length());
        EXPECT_EQ(ret, KV_ERR_NONE);
        
        ret = lsm_batch_put(writer_, key.c_str(), key.length(),
                           value.c_str(), value.length());
        EXPECT_EQ(ret, KV_ERR_NONE);
        
        // 去重后应该只有1个PUT操作
        size_t entry_count, memory_usage;
        ret = lsm_batch_get_status(writer_, &entry_count, &memory_usage);
        EXPECT_EQ(ret, KV_ERR_NONE);
        EXPECT_EQ(entry_count, 1);
        
        // 提交并验证
        lsm_batch_result_t result;
        ret = lsm_batch_commit(writer_, &result);
        EXPECT_EQ(ret, KV_ERR_NONE);
        
        VerifyKeyExists(key, value);
    }));
}

// === 容量限制测试 ===

TEST_F(LSMBatchWriteTest, BatchSizeLimit) {
    ASSERT_TRUE(ExecuteWithTimeout([this]() {
        lsm_batch_config_t config = lsm_batch_default_config();
        config.max_batch_size = 5; // 很小的批次大小
        config.max_batch_memory = SIZE_MAX; // 不限制内存
        writer_ = CreateBatchWriter(config);
        ASSERT_NE(writer_, nullptr);
        
        // 添加操作直到达到限制
        for (int i = 0; i < 5; ++i) {
            auto kv = GenerateKV(i);
            int ret = lsm_batch_put(writer_, kv.first.c_str(), kv.first.length(),
                                   kv.second.c_str(), kv.second.length());
            EXPECT_EQ(ret, KV_ERR_NONE);
        }
        
        // 第6个操作应该失败
        auto kv = GenerateKV(6);
        int ret = lsm_batch_put(writer_, kv.first.c_str(), kv.first.length(),
                               kv.second.c_str(), kv.second.length());
        EXPECT_EQ(ret, KV_ERR_BATCH_FULL);
        
        // 检查状态
        size_t entry_count, memory_usage;
        ret = lsm_batch_get_status(writer_, &entry_count, &memory_usage);
        EXPECT_EQ(ret, KV_ERR_NONE);
        EXPECT_EQ(entry_count, 5);
    }));
}

TEST_F(LSMBatchWriteTest, MemoryLimit) {
    ASSERT_TRUE(ExecuteWithTimeout([this]() {
        lsm_batch_config_t config = lsm_batch_default_config();
        config.max_batch_size = 1000; // 很大的批次大小
        config.max_batch_memory = 1024; // 很小的内存限制：1KB
        writer_ = CreateBatchWriter(config);
        ASSERT_NE(writer_, nullptr);
        
        // 添加大量数据直到内存限制
        int successful_ops = 0;
        for (int i = 0; i < 100; ++i) {
            auto kv = GenerateKV(i);
            int ret = lsm_batch_put(writer_, kv.first.c_str(), kv.first.length(),
                                   kv.second.c_str(), kv.second.length());
            if (ret == KV_ERR_NONE) {
                successful_ops++;
            } else if (ret == KV_ERR_BATCH_TOO_LARGE) {
                break; // 达到内存限制
            } else {
                FAIL() << "Unexpected error: " << ret;
            }
        }
        
        EXPECT_GT(successful_ops, 0) << "Should add at least some operations";
        EXPECT_LT(successful_ops, 100) << "Should hit memory limit before 100 operations";
        
        // 验证能正常提交
        lsm_batch_result_t result;
        int ret = lsm_batch_commit(writer_, &result);
        EXPECT_EQ(ret, KV_ERR_NONE);
        EXPECT_EQ(result.committed_entries, successful_ops);
    }));
}

// === 性能测试 ===

TEST_F(LSMBatchWriteTest, PerformanceComparison) {
    ASSERT_TRUE(ExecuteWithTimeout([this]() {
        const int num_operations = 1000;
        
        // 单次写入性能测试
        auto start_time = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < num_operations; ++i) {
            auto kv = GenerateKV(i, "single");
            int ret = lsm_tree_put(tree_, kv.first.c_str(), kv.first.length(),
                                  kv.second.c_str(), kv.second.length());
            EXPECT_EQ(ret, KV_ERR_NONE);
        }
        auto single_duration = std::chrono::high_resolution_clock::now() - start_time;
        double single_time_us = std::chrono::duration_cast<std::chrono::microseconds>(single_duration).count();
        double single_ops_per_sec = (single_time_us > 0) ? (num_operations * 1000000.0 / single_time_us) : 0;
        
        // 批量写入性能测试（优化配置）
        lsm_batch_config_t config = lsm_batch_default_config();
        config.max_batch_size = num_operations;
        config.enable_deduplication = false; // 性能测试禁用去重
        config.enable_sorting = false;       // 性能测试禁用排序
        writer_ = CreateBatchWriter(config);
        ASSERT_NE(writer_, nullptr);
        
        start_time = std::chrono::high_resolution_clock::now();
        
        // 添加所有操作到批次
        for (int i = 0; i < num_operations; ++i) {
            auto kv = GenerateKV(i, "batch");
            int ret = lsm_batch_put(writer_, kv.first.c_str(), kv.first.length(),
                                   kv.second.c_str(), kv.second.length());
            EXPECT_EQ(ret, KV_ERR_NONE);
        }
        
        // 提交批次
        lsm_batch_result_t result;
        int ret = lsm_batch_commit(writer_, &result);
        EXPECT_EQ(ret, KV_ERR_NONE);
        
        auto batch_duration = std::chrono::high_resolution_clock::now() - start_time;
        double batch_time_us = std::chrono::duration_cast<std::chrono::microseconds>(batch_duration).count();
        double batch_ops_per_sec = (batch_time_us > 0) ? (num_operations * 1000000.0 / batch_time_us) : 0;
        
        // 输出性能对比结果
        std::cout << "\n=== 性能对比结果 ===" << std::endl;
        std::cout << "单次写入: " << single_ops_per_sec << " ops/sec" << std::endl;
        std::cout << "批量写入: " << batch_ops_per_sec << " ops/sec" << std::endl;
        std::cout << "性能提升: " << (batch_ops_per_sec / single_ops_per_sec) << "x" << std::endl;
        
        // 批量写入应该至少不比单次写入慢（如果两者都有有效的测量结果）
        if (single_ops_per_sec > 0 && batch_ops_per_sec > 0) {
            EXPECT_GE(batch_ops_per_sec, single_ops_per_sec * 0.95) 
                << "Batch write should be at least 95% as fast as single writes";
        } else {
            // 如果时间太短无法准确测量，至少验证操作都成功了
            EXPECT_EQ(result.committed_entries, num_operations);
        }
        
    }, PERFORMANCE_TIMEOUT_SEC));
}

// === 并发安全测试 ===

TEST_F(LSMBatchWriteTest, ConcurrentBatchWriters) {
    ASSERT_TRUE(ExecuteWithTimeout([this]() {
        const int num_threads = 4;
        const int ops_per_thread = 100;
        std::vector<std::thread> threads;
        std::atomic<int> successful_commits{0};
        std::atomic<int> total_committed_entries{0};
        
        // 启动多个线程进行并发批量写入
        for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
            threads.emplace_back([this, thread_id, ops_per_thread, &successful_commits, &total_committed_entries]() {
                lsm_batch_config_t config = lsm_batch_default_config();
                config.max_batch_size = ops_per_thread;
                
                lsm_batch_writer_t* thread_writer = lsm_batch_writer_create(tree_, &config);
                if (!thread_writer) return;
                
                // 添加操作
                bool add_success = true;
                for (int i = 0; i < ops_per_thread; ++i) {
                    std::string key = "thread_" + std::to_string(thread_id) + "_key_" + std::to_string(i);
                    std::string value = "thread_" + std::to_string(thread_id) + "_value_" + std::to_string(i);
                    
                    int ret = lsm_batch_put(thread_writer, key.c_str(), key.length(),
                                           value.c_str(), value.length());
                    if (ret != KV_ERR_NONE) {
                        add_success = false;
                        break;
                    }
                }
                
                // 提交批次
                if (add_success) {
                    lsm_batch_result_t result;
                    int ret = lsm_batch_commit(thread_writer, &result);
                    if (ret == KV_ERR_NONE) {
                        successful_commits++;
                        total_committed_entries += result.committed_entries;
                    }
                }
                
                lsm_batch_writer_destroy(thread_writer);
            });
        }
        
        // 等待所有线程完成
        for (auto& t : threads) {
            t.join();
        }
        
        // 验证结果
        EXPECT_EQ(successful_commits.load(), num_threads) 
            << "All batch commits should succeed";
        EXPECT_EQ(total_committed_entries.load(), num_threads * ops_per_thread)
            << "All entries should be committed";
        
        // 验证数据完整性（抽样检查）
        for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
            for (int i = 0; i < 10; ++i) { // 只检查前10个
                std::string key = "thread_" + std::to_string(thread_id) + "_key_" + std::to_string(i);
                std::string expected_value = "thread_" + std::to_string(thread_id) + "_value_" + std::to_string(i);
                VerifyKeyExists(key, expected_value);
            }
        }
        
    }, PERFORMANCE_TIMEOUT_SEC));
}

// === WAL集成测试 ===

TEST_F(LSMBatchWriteTest, WALIntegration) {
    ASSERT_TRUE(ExecuteWithTimeout([this]() {
        // 清理并重新创建带WAL的LSM-Tree
        lsm_tree_destroy(tree_);
        
        lsm_tree_config_t config = lsm_tree_default_config(test_dir_.c_str());
        config.enable_wal = true; // 启用WAL
        config.compaction.enable_background_compaction = false;
        
        tree_ = lsm_tree_create(&config);
        ASSERT_NE(tree_, nullptr);
        ASSERT_EQ(lsm_tree_open(tree_), KV_ERR_NONE);
        
        // 创建批量写入器
        lsm_batch_config_t batch_config = lsm_batch_default_config();
        batch_config.sync_wal = true;
        writer_ = CreateBatchWriter(batch_config);
        ASSERT_NE(writer_, nullptr);
        
        // 执行批量写入
        const int num_ops = 50;
        std::vector<std::pair<std::string, std::string>> kv_pairs;
        
        for (int i = 0; i < num_ops; ++i) {
            auto kv = GenerateKV(i, "wal_batch");
            kv_pairs.push_back(kv);
            
            int ret = lsm_batch_put(writer_, kv.first.c_str(), kv.first.length(),
                                   kv.second.c_str(), kv.second.length());
            EXPECT_EQ(ret, KV_ERR_NONE);
        }
        
        // 提交批次
        lsm_batch_result_t result;
        int ret = lsm_batch_commit(writer_, &result);
        EXPECT_EQ(ret, KV_ERR_NONE);
        EXPECT_EQ(result.committed_entries, num_ops);
        
        // 关闭LSM-Tree（模拟崩溃）
        lsm_batch_writer_destroy(writer_);
        writer_ = nullptr;
        lsm_tree_destroy(tree_);
        
        // 重新打开LSM-Tree（模拟重启恢复）
        tree_ = lsm_tree_create(&config);
        ASSERT_NE(tree_, nullptr);
        ASSERT_EQ(lsm_tree_open(tree_), KV_ERR_NONE);
        
        // 验证WAL恢复后的数据完整性
        for (const auto& kv : kv_pairs) {
            VerifyKeyExists(kv.first, kv.second);
        }
        
    }, PERFORMANCE_TIMEOUT_SEC));
}

// === 错误处理测试 ===

TEST_F(LSMBatchWriteTest, ErrorConditions) {
    ASSERT_TRUE(ExecuteWithTimeout([this]() {
        // 测试空指针参数
        EXPECT_EQ(lsm_batch_writer_create(nullptr, nullptr), nullptr);
        
        lsm_batch_config_t config = lsm_batch_default_config();
        writer_ = CreateBatchWriter(config);
        ASSERT_NE(writer_, nullptr);
        
        // 测试空键
        int ret = lsm_batch_put(writer_, nullptr, 0, "value", 5);
        EXPECT_NE(ret, KV_ERR_NONE);
        
        ret = lsm_batch_put(writer_, "key", 3, nullptr, 0);
        EXPECT_NE(ret, KV_ERR_NONE);
        
        // 测试空批次提交
        lsm_batch_result_t result;
        ret = lsm_batch_commit(writer_, &result);
        EXPECT_EQ(ret, KV_ERR_NONE); // 空批次提交应该成功
        EXPECT_EQ(result.committed_entries, 0);
        
        // 测试重复提交
        ret = lsm_batch_commit(writer_, &result);
        EXPECT_EQ(ret, KV_ERR_NONE); // 重复空批次提交应该成功
    }));
}

// === 压力测试 ===

TEST_F(LSMBatchWriteTest, StressTest) {
    ASSERT_TRUE(ExecuteWithTimeout([this]() {
        lsm_batch_config_t config = lsm_batch_default_config();
        config.max_batch_size = 10000;
        config.max_batch_memory = 100 * 1024 * 1024; // 100MB
        writer_ = CreateBatchWriter(config);
        ASSERT_NE(writer_, nullptr);
        
        const int num_operations = 5000;
        std::unordered_set<std::string> expected_keys;
        
        // 添加大量随机操作
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> op_dist(0, 1); // 0=PUT, 1=DELETE
        
        for (int i = 0; i < num_operations; ++i) {
            auto kv = GenerateKV(i, "stress");
            
            if (op_dist(gen) == 0) { // PUT
                int ret = lsm_batch_put(writer_, kv.first.c_str(), kv.first.length(),
                                       kv.second.c_str(), kv.second.length());
                EXPECT_EQ(ret, KV_ERR_NONE);
                expected_keys.insert(kv.first);
            } else { // DELETE
                int ret = lsm_batch_delete(writer_, kv.first.c_str(), kv.first.length());
                EXPECT_EQ(ret, KV_ERR_NONE);
                expected_keys.erase(kv.first);
            }
        }
        
        // 提交大批次
        lsm_batch_result_t result;
        int ret = lsm_batch_commit(writer_, &result);
        EXPECT_EQ(ret, KV_ERR_NONE);
        EXPECT_EQ(result.committed_entries, num_operations);
        EXPECT_EQ(result.failed_entries, 0);
        
        std::cout << "\n=== 压力测试结果 ===" << std::endl;
        std::cout << "总操作数: " << num_operations << std::endl;
        std::cout << "提交时间: " << result.commit_time_us << " μs" << std::endl;
        std::cout << "平均延迟: " << (result.commit_time_us / num_operations) << " μs/op" << std::endl;
        std::cout << "期望存在键数: " << expected_keys.size() << std::endl;
        
        // 抽样验证数据完整性（验证前100个键）
        int verified_count = 0;
        for (const auto& key : expected_keys) {
            if (verified_count++ >= 100) break;
            
            char* value = nullptr;
            size_t value_len = 0;
            int get_ret = lsm_tree_get(tree_, key.c_str(), key.length(), &value, &value_len);
            EXPECT_EQ(get_ret, KV_ERR_NONE) << "Key should exist: " << key;
            if (value) free(value);
        }
        
    }, PERFORMANCE_TIMEOUT_SEC * 2)); // 压力测试需要更长时间
}

// === 测试套件统计 ===

class LSMBatchWriteTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        std::cout << "\n🚀 开始LSM-Tree批量写入功能测试..." << std::endl;
        std::cout << "测试超时设置: 30秒" << std::endl;
    }
    
    void TearDown() override {
        std::cout << "\n🎉 LSM-Tree批量写入功能测试完成!" << std::endl;
    }
};

// 注册测试环境
static ::testing::Environment* const lsm_batch_env = 
    ::testing::AddGlobalTestEnvironment(new LSMBatchWriteTestEnvironment);