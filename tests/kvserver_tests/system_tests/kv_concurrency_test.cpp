#include <gtest/gtest.h>
#include <pthread.h>
#include <unistd.h>
#include <vector>
#include <atomic>

extern "C" {
#include "kv_concurrency.h"
}

// 读写锁测试类
class KvRwlockTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(0, kv_rwlock_init(&rwlock));
    }
    
    void TearDown() override {
        kv_rwlock_destroy(&rwlock);
    }
    
    kv_rwlock_t rwlock;
};

// 测试读写锁基本功能
TEST_F(KvRwlockTest, BasicLockUnlock) {
    // 测试读锁
    ASSERT_EQ(0, kv_read_lock(&rwlock));
    ASSERT_EQ(0, kv_read_unlock(&rwlock));
    
    // 测试写锁
    ASSERT_EQ(0, kv_write_lock(&rwlock));
    ASSERT_EQ(0, kv_write_unlock(&rwlock));
    
    // 测试尝试获取锁
    ASSERT_EQ(0, kv_try_read_lock(&rwlock));
    ASSERT_EQ(0, kv_read_unlock(&rwlock));
    
    ASSERT_EQ(0, kv_try_write_lock(&rwlock));
    ASSERT_EQ(0, kv_write_unlock(&rwlock));
}

// 测试多个读者
struct ThreadData {
    kv_rwlock_t* rwlock;
    std::atomic<int>* counter;
    int iterations;
};

void* reader_thread(void* arg) {
    ThreadData* data = static_cast<ThreadData*>(arg);
    
    for (int i = 0; i < data->iterations; i++) {
        EXPECT_EQ(0, kv_read_lock(data->rwlock));
        // 读操作不应该修改计数器
        int temp = data->counter->load();
        usleep(1); // 短暂延迟，增加并发的机会
        EXPECT_EQ(temp, data->counter->load());
        EXPECT_EQ(0, kv_read_unlock(data->rwlock));
    }
    
    return NULL;
}

void* writer_thread(void* arg) {
    ThreadData* data = static_cast<ThreadData*>(arg);
    
    for (int i = 0; i < data->iterations; i++) {
        EXPECT_EQ(0, kv_write_lock(data->rwlock));
        // 写操作修改计数器
        data->counter->fetch_add(1);
        usleep(1); // 短暂延迟，增加并发的机会
        EXPECT_EQ(0, kv_write_unlock(data->rwlock));
    }
    
    return NULL;
}

TEST_F(KvRwlockTest, ConcurrentReadersWriters) {
    const int NUM_READERS = 5;
    const int NUM_WRITERS = 2;
    const int ITERATIONS = 1000;
    
    std::atomic<int> counter(0);
    ThreadData thread_data = {&rwlock, &counter, ITERATIONS};
    
    pthread_t readers[NUM_READERS];
    pthread_t writers[NUM_WRITERS];
    
    // 创建读者线程
    for (int i = 0; i < NUM_READERS; i++) {
        ASSERT_EQ(0, pthread_create(&readers[i], NULL, reader_thread, &thread_data));
    }
    
    // 创建写者线程
    for (int i = 0; i < NUM_WRITERS; i++) {
        ASSERT_EQ(0, pthread_create(&writers[i], NULL, writer_thread, &thread_data));
    }
    
    // 等待所有线程完成
    for (int i = 0; i < NUM_READERS; i++) {
        ASSERT_EQ(0, pthread_join(readers[i], NULL));
    }
    
    for (int i = 0; i < NUM_WRITERS; i++) {
        ASSERT_EQ(0, pthread_join(writers[i], NULL));
    }
    
    // 验证计数器值
    ASSERT_EQ(NUM_WRITERS * ITERATIONS, counter.load());
}

// 自旋锁测试类
class KvSpinlockTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(0, kv_spinlock_init(&spinlock));
    }
    
    void TearDown() override {
        kv_spinlock_destroy(&spinlock);
    }
    
    kv_spinlock_t spinlock;
};

// 测试自旋锁基本功能
TEST_F(KvSpinlockTest, BasicLockUnlock) {
    ASSERT_EQ(0, kv_spin_lock(&spinlock));
    ASSERT_EQ(0, kv_spin_unlock(&spinlock));
    
    ASSERT_EQ(0, kv_try_spin_lock(&spinlock));
    ASSERT_EQ(0, kv_spin_unlock(&spinlock));
}

// 自旋锁多线程测试
void* spin_counter_thread(void* arg) {
    ThreadData* data = static_cast<ThreadData*>(arg);
    
    for (int i = 0; i < data->iterations; i++) {
        EXPECT_EQ(0, kv_spin_lock((kv_spinlock_t*)data->rwlock));
        data->counter->fetch_add(1);
        EXPECT_EQ(0, kv_spin_unlock((kv_spinlock_t*)data->rwlock));
    }
    
    return NULL;
}

TEST_F(KvSpinlockTest, ConcurrentIncrement) {
    const int NUM_THREADS = 4;
    const int ITERATIONS = 10000;
    
    std::atomic<int> counter(0);
    ThreadData thread_data = {(kv_rwlock_t*)&spinlock, &counter, ITERATIONS};
    
    pthread_t threads[NUM_THREADS];
    
    // 创建线程
    for (int i = 0; i < NUM_THREADS; i++) {
        ASSERT_EQ(0, pthread_create(&threads[i], NULL, spin_counter_thread, &thread_data));
    }
    
    // 等待线程完成
    for (int i = 0; i < NUM_THREADS; i++) {
        ASSERT_EQ(0, pthread_join(threads[i], NULL));
    }
    
    // 验证计数器值
    ASSERT_EQ(NUM_THREADS * ITERATIONS, counter.load());
}

// 分段锁测试类
class KvSegmentLocksTest : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_EQ(0, kv_segment_locks_init(&seglocks, NUM_SEGMENTS));
    }
    
    void TearDown() override {
        kv_segment_locks_destroy(&seglocks);
    }
    
    static const int NUM_SEGMENTS = 16;
    kv_segment_locks_t seglocks;
};

// 测试分段锁基本功能
TEST_F(KvSegmentLocksTest, BasicSegmentLocks) {
    const char* key1 = "key1";
    const char* key2 = "key2";
    
    // 测试键读锁
    ASSERT_EQ(0, kv_segment_read_lock(&seglocks, key1));
    ASSERT_EQ(0, kv_segment_read_unlock(&seglocks, key1));
    
    // 测试键写锁
    ASSERT_EQ(0, kv_segment_write_lock(&seglocks, key1));
    ASSERT_EQ(0, kv_segment_write_unlock(&seglocks, key1));
    
    // 测试多个键的锁互不干扰
    ASSERT_EQ(0, kv_segment_write_lock(&seglocks, key1));
    ASSERT_EQ(0, kv_segment_write_lock(&seglocks, key2));
    ASSERT_EQ(0, kv_segment_write_unlock(&seglocks, key1));
    ASSERT_EQ(0, kv_segment_write_unlock(&seglocks, key2));
}

// 测试全局锁
TEST_F(KvSegmentLocksTest, GlobalLocks) {
    ASSERT_EQ(0, kv_segment_write_lock_all(&seglocks));
    ASSERT_EQ(0, kv_segment_write_unlock_all(&seglocks));
}

// 分段锁并发测试的线程函数
struct SegmentThreadData {
    kv_segment_locks_t* seglocks;
    std::vector<std::atomic<int>>* counters;
    std::vector<std::string>* keys;
    int iterations;
};

void* segment_writer_thread(void* arg) {
    SegmentThreadData* data = static_cast<SegmentThreadData*>(arg);
    
    for (int i = 0; i < data->iterations; i++) {
        for (size_t k = 0; k < data->keys->size(); k++) {
            const char* key = (*data->keys)[k].c_str();
            EXPECT_EQ(0, kv_segment_write_lock(data->seglocks, key));
            (*data->counters)[k].fetch_add(1);
            EXPECT_EQ(0, kv_segment_write_unlock(data->seglocks, key));
        }
    }
    
    return NULL;
}

// 测试分段锁并发性能
TEST_F(KvSegmentLocksTest, ConcurrentSegmentAccess) {
    const int NUM_THREADS = 4;
    const int ITERATIONS = 1000;
    const int NUM_KEYS = 50;
    
    std::vector<std::string> keys;
    std::vector<std::atomic<int>> counters(NUM_KEYS);
    
    // 初始化计数器和键
    for (int i = 0; i < NUM_KEYS; i++) {
        counters[i] = 0;
        keys.push_back("key" + std::to_string(i));
    }
    
    SegmentThreadData thread_data = {&seglocks, &counters, &keys, ITERATIONS};
    
    pthread_t threads[NUM_THREADS];
    
    // 创建线程
    for (int i = 0; i < NUM_THREADS; i++) {
        ASSERT_EQ(0, pthread_create(&threads[i], NULL, segment_writer_thread, &thread_data));
    }
    
    // 等待线程完成
    for (int i = 0; i < NUM_THREADS; i++) {
        ASSERT_EQ(0, pthread_join(threads[i], NULL));
    }
    
    // 验证所有计数器值
    for (int i = 0; i < NUM_KEYS; i++) {
        ASSERT_EQ(NUM_THREADS * ITERATIONS, counters[i].load());
    }
} 