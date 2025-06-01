/*
 * @Author: Lzww0608  
 * @Date: 2025-6-1 14:30:00
 * @LastEditors: Lzww0608
 * @LastEditTime:2025-6-1 20:23:07
 * @Description: ConcordKV LSM-Tree SSTable测试
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/stat.h>

// 包含测试目标
#include "../../../kvserver/lsm_sstable.h"
#include "../../../kvserver/lsm_memtable.h"
#include "../../../kvserver/kv_error.h"

// 测试统计
static int tests_run = 0;
static int tests_passed = 0;

// 测试超时处理
static volatile int timeout_flag = 0;

void timeout_handler(int sig) {
    (void)sig;
    timeout_flag = 1;
}

// 测试宏
#define ASSERT_EQ(expected, actual) do { \
    if ((expected) != (actual)) { \
        printf("❌ 断言失败 - 期望: %ld, 实际: %ld (位置: %s:%d)\n", \
               (long)(expected), (long)(actual), __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_NE(expected, actual) do { \
    if ((expected) == (actual)) { \
        printf("❌ 断言失败 - 不期望: %ld, 但得到相同值 (位置: %s:%d)\n", \
               (long)(expected), __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_TRUE(condition) do { \
    if (!(condition)) { \
        printf("❌ 断言失败 - 期望为真 (位置: %s:%d)\n", __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_FALSE(condition) do { \
    if ((condition)) { \
        printf("❌ 断言失败 - 期望为假 (位置: %s:%d)\n", __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        printf("❌ 断言失败 - 期望为NULL (位置: %s:%d)\n", __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        printf("❌ 断言失败 - 期望非NULL (位置: %s:%d)\n", __FILE__, __LINE__); \
        return 0; \
    } \
} while(0)

#define RUN_TEST(test_func) do { \
    printf("🧪 运行测试: %s\n", #test_func); \
    timeout_flag = 0; \
    alarm(30); \
    tests_run++; \
    if (test_func()) { \
        tests_passed++; \
        printf("✅ %s - 通过\n", #test_func); \
    } else { \
        printf("❌ %s - 失败\n", #test_func); \
    } \
    alarm(0); \
    if (timeout_flag) { \
        printf("⏰ %s - 超时\n", #test_func); \
    } \
} while(0)

// === 工具函数 ===
static uint64_t get_timestamp_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static void cleanup_test_files(void) {
    system("rm -f /tmp/test_*.sst");
}

// === 测试函数实现 ===

// 测试1: CRC32校验和功能
int test_crc32_checksum(void) {
    printf("  测试CRC32校验和功能...\n");
    
    const char *test_data = "Hello, ConcordKV SSTable!";
    uint32_t crc1 = sstable_crc32(test_data, strlen(test_data));
    uint32_t crc2 = sstable_crc32(test_data, strlen(test_data));
    
    // 相同数据应该产生相同的CRC32
    ASSERT_EQ(crc1, crc2);
    
    // 不同数据应该产生不同的CRC32
    const char *test_data2 = "Hello, ConcordKV SSTable?";
    uint32_t crc3 = sstable_crc32(test_data2, strlen(test_data2));
    ASSERT_NE(crc1, crc3);
    
    printf("    CRC32值: 0x%08X\n", crc1);
    
    return 1;
}

// 测试2: 布隆过滤器基础功能
int test_bloom_filter_basic(void) {
    printf("  测试布隆过滤器基础功能...\n");
    
    // 创建布隆过滤器，预期1000个条目，1%误判率
    sstable_bloom_filter_t *bloom = sstable_bloom_filter_create(1000, 100);
    ASSERT_NOT_NULL(bloom);
    
    // 添加一些键
    const char *keys[] = {"key1", "key2", "key3", "test_key", "hello"};
    int key_count = sizeof(keys) / sizeof(keys[0]);
    
    for (int i = 0; i < key_count; i++) {
        sstable_bloom_filter_add(bloom, keys[i], strlen(keys[i]));
    }
    
    // 检查添加的键都可能存在
    for (int i = 0; i < key_count; i++) {
        ASSERT_TRUE(sstable_bloom_filter_may_contain(bloom, keys[i], strlen(keys[i])));
    }
    
    // 检查一些未添加的键（可能存在误判，但概率很低）
    const char *missing_keys[] = {"missing1", "missing2", "notfound"};
    int missing_count = sizeof(missing_keys) / sizeof(missing_keys[0]);
    int false_positives = 0;
    
    for (int i = 0; i < missing_count; i++) {
        if (sstable_bloom_filter_may_contain(bloom, missing_keys[i], strlen(missing_keys[i]))) {
            false_positives++;
        }
    }
    
    printf("    误判数量: %d/%d\n", false_positives, missing_count);
    printf("    位数组大小: %zu bits\n", bloom->bit_count);
    printf("    哈希函数数量: %u\n", bloom->hash_count);
    
    sstable_bloom_filter_destroy(bloom);
    
    return 1;
}

// 测试3: 数据块操作
int test_data_block_operations(void) {
    printf("  测试数据块操作...\n");
    
    // 创建数据块
    sstable_data_block_t *block = sstable_data_block_create(4096, SSTABLE_COMPRESSION_NONE);
    ASSERT_NOT_NULL(block);
    ASSERT_EQ(4096, block->capacity);
    ASSERT_EQ(0, block->size);
    ASSERT_EQ(0, block->header.entry_count);
    
    // 添加条目
    const char *test_entries[][2] = {
        {"key1", "value1"},
        {"key2", "value2_longer_than_first"},
        {"key3", "short"},
        {"long_key_name_for_testing", "corresponding_long_value"}
    };
    
    int entry_count = sizeof(test_entries) / sizeof(test_entries[0]);
    
    for (int i = 0; i < entry_count; i++) {
        int ret = sstable_data_block_add_entry(block, 
                                              test_entries[i][0], strlen(test_entries[i][0]),
                                              test_entries[i][1], strlen(test_entries[i][1]),
                                              i + 1, false);
        ASSERT_EQ(KV_ERR_NONE, ret);
    }
    
    ASSERT_EQ((uint32_t)entry_count, block->header.entry_count);
    ASSERT_TRUE(block->size > 0);
    
    // 完成数据块
    int ret = sstable_data_block_finalize(block);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    // 验证头部信息
    ASSERT_EQ(SSTABLE_BLOCK_DATA, block->header.block_type);
    ASSERT_TRUE(block->header.crc32 != 0);
    ASSERT_EQ(block->size, block->header.uncompressed_size);
    
    printf("    块大小: %zu bytes\n", block->size);
    printf("    条目数量: %u\n", block->header.entry_count);
    printf("    CRC32: 0x%08X\n", block->header.crc32);
    
    sstable_data_block_destroy(block);
    
    return 1;
}

// 测试4: 索引块操作
int test_index_block_operations(void) {
    printf("  测试索引块操作...\n");
    
    // 创建索引块
    sstable_index_block_t *index_block = sstable_index_block_create(100);
    ASSERT_NOT_NULL(index_block);
    ASSERT_EQ(100, index_block->capacity);
    ASSERT_EQ(0, index_block->entry_count);
    
    // 添加索引条目
    const char *keys[] = {"aaa", "bbb", "ccc", "ddd", "eee"};
    uint64_t offsets[] = {0, 1024, 2048, 3072, 4096};
    uint32_t sizes[] = {1024, 1024, 1024, 1024, 1024};
    
    int count = sizeof(keys) / sizeof(keys[0]);
    
    for (int i = 0; i < count; i++) {
        int ret = sstable_index_block_add_entry(index_block, 
                                               keys[i], strlen(keys[i]),
                                               offsets[i], sizes[i], i + 1);
        ASSERT_EQ(KV_ERR_NONE, ret);
    }
    
    ASSERT_EQ((uint32_t)count, index_block->entry_count);
    
    // 验证索引条目
    for (int i = 0; i < count; i++) {
        sstable_index_entry_t *entry = &index_block->entries[i];
        ASSERT_EQ(0, strcmp(keys[i], entry->key));
        ASSERT_EQ(strlen(keys[i]), entry->key_len);
        ASSERT_EQ(offsets[i], entry->offset);
        ASSERT_EQ(sizes[i], entry->size);
        ASSERT_EQ((uint64_t)(i + 1), entry->seq_num);
    }
    
    printf("    索引条目数量: %u\n", index_block->entry_count);
    
    sstable_index_block_destroy(index_block);
    
    return 1;
}

// 测试5: 文件名格式化
int test_filename_formatting(void) {
    printf("  测试文件名格式化...\n");
    
    char buffer[256];
    
    // 测试正常情况
    int ret = sstable_format_filename(buffer, sizeof(buffer), "/tmp", 0, 123);
    ASSERT_EQ(KV_ERR_NONE, ret);
    ASSERT_EQ(0, strcmp("/tmp/level-0-000123.sst", buffer));
    
    ret = sstable_format_filename(buffer, sizeof(buffer), "/data/lsm", 2, 456789);
    ASSERT_EQ(KV_ERR_NONE, ret);
    ASSERT_EQ(0, strcmp("/data/lsm/level-2-456789.sst", buffer));
    
    // 测试缓冲区太小的情况
    char small_buffer[10];
    ret = sstable_format_filename(small_buffer, sizeof(small_buffer), "/tmp", 0, 123);
    ASSERT_EQ(KV_ERR_PARAM, ret);
    
    printf("    格式化结果: %s\n", buffer);
    
    return 1;
}

// 测试6: SSTable写入器基础功能
int test_sstable_writer_basic(void) {
    printf("  测试SSTable写入器基础功能...\n");
    
    const char *filename = "/tmp/test_basic.sst";
    cleanup_test_files();
    
    // 创建写入器
    sstable_writer_t *writer = sstable_writer_create(filename, SSTABLE_COMPRESSION_NONE);
    ASSERT_NOT_NULL(writer);
    ASSERT_FALSE(writer->finalized);
    ASSERT_EQ(0, writer->entry_count);
    
    // 写入一些数据
    const char *test_data[][2] = {
        {"apple", "fruit"},
        {"banana", "yellow_fruit"},
        {"cherry", "red_fruit"},
        {"date", "sweet_fruit"},
        {"elderberry", "purple_fruit"}
    };
    
    int count = sizeof(test_data) / sizeof(test_data[0]);
    
    for (int i = 0; i < count; i++) {
        int ret = sstable_writer_put(writer, 
                                   test_data[i][0], strlen(test_data[i][0]),
                                   test_data[i][1], strlen(test_data[i][1]),
                                   i + 1, false);
        ASSERT_EQ(KV_ERR_NONE, ret);
    }
    
    ASSERT_EQ((uint64_t)count, writer->entry_count);
    
    // 完成写入
    int ret = sstable_writer_finalize(writer);
    ASSERT_EQ(KV_ERR_NONE, ret);
    ASSERT_TRUE(writer->finalized);
    
    // 检查文件是否存在
    struct stat st;
    ASSERT_EQ(0, stat(filename, &st));
    ASSERT_TRUE(st.st_size > 0);
    
    printf("    文件大小: %ld bytes\n", st.st_size);
    printf("    条目数量: %lu\n", writer->entry_count);
    printf("    最小序列号: %lu\n", writer->min_seq_num);
    printf("    最大序列号: %lu\n", writer->max_seq_num);
    
    sstable_writer_destroy(writer);
    
    return 1;
}

// 测试7: 从MemTable创建SSTable
int test_create_from_memtable(void) {
    printf("  测试从MemTable创建SSTable...\n");
    
    const char *filename = "/tmp/test_from_memtable.sst";
    cleanup_test_files();
    
    // 创建MemTable并添加数据
    lsm_memtable_t *memtable = lsm_memtable_create(4096, "test_memtable");
    ASSERT_NOT_NULL(memtable);
    
    const char *test_data[][2] = {
        {"alpha", "first"},
        {"beta", "second"},
        {"gamma", "third"},
        {"delta", "fourth"},
        {"epsilon", "fifth"}
    };
    
    int count = sizeof(test_data) / sizeof(test_data[0]);
    
    for (int i = 0; i < count; i++) {
        int ret = lsm_memtable_put(memtable, 
                                  test_data[i][0], strlen(test_data[i][0]),
                                  test_data[i][1], strlen(test_data[i][1]));
        ASSERT_EQ(KV_ERR_NONE, ret);
    }
    
    // 从MemTable创建SSTable
    int ret = sstable_create_from_memtable(memtable, filename, SSTABLE_COMPRESSION_NONE);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    // 检查文件是否存在
    struct stat st;
    ASSERT_EQ(0, stat(filename, &st));
    ASSERT_TRUE(st.st_size > 0);
    
    printf("    SSTable文件大小: %ld bytes\n", st.st_size);
    
    lsm_memtable_destroy(memtable);
    
    return 1;
}

// 测试8: 布隆过滤器性能测试
int test_bloom_filter_performance(void) {
    printf("  测试布隆过滤器性能...\n");
    
    const int test_count = 10000;
    sstable_bloom_filter_t *bloom = sstable_bloom_filter_create(test_count, 100);
    ASSERT_NOT_NULL(bloom);
    
    uint64_t start_time = get_timestamp_us();
    
    // 添加大量键
    char key[32];
    for (int i = 0; i < test_count; i++) {
        snprintf(key, sizeof(key), "performance_test_key_%d", i);
        sstable_bloom_filter_add(bloom, key, strlen(key));
    }
    
    uint64_t add_time = get_timestamp_us();
    
    // 查询所有键
    int found_count = 0;
    for (int i = 0; i < test_count; i++) {
        snprintf(key, sizeof(key), "performance_test_key_%d", i);
        if (sstable_bloom_filter_may_contain(bloom, key, strlen(key))) {
            found_count++;
        }
    }
    
    uint64_t query_time = get_timestamp_us();
    
    // 查询不存在的键
    int false_positives = 0;
    for (int i = test_count; i < test_count + 1000; i++) {
        snprintf(key, sizeof(key), "missing_key_%d", i);
        if (sstable_bloom_filter_may_contain(bloom, key, strlen(key))) {
            false_positives++;
        }
    }
    
    uint64_t end_time = get_timestamp_us();
    
    double add_duration = (add_time - start_time) / 1000.0;      // ms
    double query_duration = (query_time - add_time) / 1000.0;   // ms
    double missing_duration = (end_time - query_time) / 1000.0; // ms
    
    printf("    添加性能: %.2f ms (%d条目) - %.0f ops/sec\n",
           add_duration, test_count, test_count * 1000.0 / add_duration);
    printf("    查询性能: %.2f ms (%d次查询) - %.0f ops/sec\n",
           query_duration, test_count, test_count * 1000.0 / query_duration);
    printf("    缺失查询: %.2f ms (1000次查询) - %.0f ops/sec\n",
           missing_duration, 1000 * 1000.0 / missing_duration);
    printf("    找到数量: %d/%d (应该是100%%)\n", found_count, test_count);
    printf("    误判数量: %d/1000 (%.2f%%)\n", false_positives, false_positives / 10.0);
    
    ASSERT_EQ(test_count, found_count);  // 所有添加的键都应该被找到
    
    sstable_bloom_filter_destroy(bloom);
    
    return 1;
}

// 测试9: 数据块容量测试
int test_data_block_capacity(void) {
    printf("  测试数据块容量...\n");
    
    sstable_data_block_t *block = sstable_data_block_create(1024, SSTABLE_COMPRESSION_NONE);
    ASSERT_NOT_NULL(block);
    
    // 添加条目直到块满
    char key[32], value[64];
    int added_count = 0;
    
    for (int i = 0; i < 1000; i++) {
        snprintf(key, sizeof(key), "capacity_key_%d", i);
        snprintf(value, sizeof(value), "capacity_value_for_key_%d", i);
        
        int ret = sstable_data_block_add_entry(block, key, strlen(key), 
                                              value, strlen(value), i + 1, false);
        
        if (ret == KV_ERR_PARAM) {
            // 块已满
            break;
        }
        
        ASSERT_EQ(KV_ERR_NONE, ret);
        added_count++;
    }
    
    printf("    容量: %zu bytes\n", block->capacity);
    printf("    使用: %zu bytes\n", block->size);
    printf("    条目数量: %d\n", added_count);
    printf("    利用率: %.2f%%\n", (double)block->size / block->capacity * 100.0);
    
    ASSERT_TRUE(added_count > 0);
    ASSERT_TRUE(block->size <= block->capacity);
    
    sstable_data_block_destroy(block);
    
    return 1;
}

// 测试10: 错误处理
int test_error_handling(void) {
    printf("  测试错误处理...\n");
    
    // 测试NULL参数
    ASSERT_NULL(sstable_data_block_create(0, SSTABLE_COMPRESSION_NONE));
    ASSERT_NULL(sstable_index_block_create(0));
    ASSERT_NULL(sstable_bloom_filter_create(0, 100));
    ASSERT_NULL(sstable_bloom_filter_create(1000, 0));
    
    // 测试写入器错误 - 无效路径
    sstable_writer_t *writer = sstable_writer_create("", SSTABLE_COMPRESSION_NONE);
    printf("    DEBUG: writer = %p (应该为NULL)\n", (void*)writer);
    ASSERT_NULL(writer);  // 无法创建文件
    
    // 测试数据块添加条目错误
    sstable_data_block_t *block = sstable_data_block_create(100, SSTABLE_COMPRESSION_NONE);
    ASSERT_NOT_NULL(block);
    
    // 添加过大的条目
    char large_value[200];
    memset(large_value, 'A', sizeof(large_value) - 1);
    large_value[sizeof(large_value) - 1] = '\0';
    
    int ret = sstable_data_block_add_entry(block, "key", 3, large_value, strlen(large_value), 1, false);
    ASSERT_EQ(KV_ERR_PARAM, ret);  // 条目太大
    
    sstable_data_block_destroy(block);
    
    return 1;
}

// 测试11: 删除标记测试
int test_deletion_markers(void) {
    printf("  测试删除标记...\n");
    
    const char *filename = "/tmp/test_deletion.sst";
    cleanup_test_files();
    
    sstable_writer_t *writer = sstable_writer_create(filename, SSTABLE_COMPRESSION_NONE);
    ASSERT_NOT_NULL(writer);
    
    // 写入正常条目和删除标记
    int ret = sstable_writer_put(writer, "key1", 4, "value1", 6, 1, false);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    ret = sstable_writer_put(writer, "key2", 4, "", 0, 2, true);  // 删除标记
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    ret = sstable_writer_put(writer, "key3", 4, "value3", 6, 3, false);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    ret = sstable_writer_finalize(writer);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    // 检查文件
    struct stat st;
    ASSERT_EQ(0, stat(filename, &st));
    ASSERT_TRUE(st.st_size > 0);
    
    printf("    包含删除标记的SSTable大小: %ld bytes\n", st.st_size);
    
    sstable_writer_destroy(writer);
    
    return 1;
}

// 测试12: 压力测试
int test_stress_operations(void) {
    printf("  测试压力操作（1000条记录）...\n");
    
    const char *filename = "/tmp/test_stress.sst";
    cleanup_test_files();
    
    sstable_writer_t *writer = sstable_writer_create(filename, SSTABLE_COMPRESSION_NONE);
    ASSERT_NOT_NULL(writer);
    
    uint64_t start_time = get_timestamp_us();
    
    // 写入1000条记录
    char key[64], value[128];
    const int record_count = 1000;
    
    for (int i = 0; i < record_count; i++) {
        snprintf(key, sizeof(key), "stress_test_key_%06d", i);
        snprintf(value, sizeof(value), "stress_test_value_for_key_%06d_with_additional_data", i);
        
        int ret = sstable_writer_put(writer, key, strlen(key), value, strlen(value), i + 1, false);
        ASSERT_EQ(KV_ERR_NONE, ret);
    }
    
    uint64_t write_time = get_timestamp_us();
    
    // 完成写入
    int ret = sstable_writer_finalize(writer);
    ASSERT_EQ(KV_ERR_NONE, ret);
    
    uint64_t finalize_time = get_timestamp_us();
    
    // 获取统计信息
    struct stat st;
    ASSERT_EQ(0, stat(filename, &st));
    
    double write_duration = (write_time - start_time) / 1000.0;      // ms
    double finalize_duration = (finalize_time - write_time) / 1000.0; // ms
    double total_duration = (finalize_time - start_time) / 1000.0;   // ms
    
    printf("    写入性能: %.2f ms (%d条记录) - %.0f ops/sec\n",
           write_duration, record_count, record_count * 1000.0 / write_duration);
    printf("    完成性能: %.2f ms - %.0f ops/sec\n",
           finalize_duration, record_count * 1000.0 / finalize_duration);
    printf("    总体性能: %.2f ms - %.0f ops/sec\n",
           total_duration, record_count * 1000.0 / total_duration);
    printf("    文件大小: %ld bytes\n", st.st_size);
    printf("    平均条目大小: %.1f bytes\n", (double)st.st_size / record_count);
    printf("    数据块数量: %u\n", writer->stats.block_count);
    
    sstable_writer_destroy(writer);
    
    return 1;
}

// 测试13: SSTable读取器基础功能
int test_sstable_reader_basic(void) {
    printf("  测试SSTable读取器基础功能...\n");
    
    const char *filename = "/tmp/test_reader.sst";
    cleanup_test_files();
    
    // 首先创建一个SSTable文件
    sstable_writer_t *writer = sstable_writer_create(filename, SSTABLE_COMPRESSION_NONE);
    ASSERT_NOT_NULL(writer);
    
    // 写入测试数据
    const char *test_data[][2] = {
        {"apple", "red_fruit"},
        {"banana", "yellow_fruit"},
        {"cherry", "red_fruit"},
        {"date", "sweet_fruit"},
        {"elderberry", "purple_fruit"}
    };
    
    int count = sizeof(test_data) / sizeof(test_data[0]);
    
    for (int i = 0; i < count; i++) {
        int ret = sstable_writer_put(writer, 
                                   test_data[i][0], strlen(test_data[i][0]),
                                   test_data[i][1], strlen(test_data[i][1]),
                                   i + 1, false);
        ASSERT_EQ(KV_ERR_NONE, ret);
    }
    
    // 完成写入
    int ret = sstable_writer_finalize(writer);
    ASSERT_EQ(KV_ERR_NONE, ret);
    sstable_writer_destroy(writer);
    
    // 现在测试读取器
    sstable_reader_t *reader = sstable_reader_create(filename);
    ASSERT_NOT_NULL(reader);
    
    // 测试布隆过滤器检查
    ASSERT_TRUE(sstable_reader_may_contain(reader, "apple", 5));
    ASSERT_TRUE(sstable_reader_may_contain(reader, "banana", 6));
    ASSERT_FALSE(sstable_reader_may_contain(reader, "grape", 5));  // 不存在的键
    
    // 测试键值查找
    char *value = NULL;
    size_t value_len = 0;
    uint64_t seq_num = 0;
    bool deleted = false;
    
    ret = sstable_reader_get(reader, "apple", 5, &value, &value_len, &seq_num, &deleted);
    ASSERT_EQ(KV_ERR_NONE, ret);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(0, strcmp("red_fruit", value));
    ASSERT_EQ(9, value_len);
    ASSERT_EQ(1, seq_num);
    ASSERT_FALSE(deleted);
    if (value) kv_store_free(value);
    
    // 测试查找不存在的键
    ret = sstable_reader_get(reader, "grape", 5, &value, &value_len, &seq_num, &deleted);
    ASSERT_EQ(KV_ERR_NOT_FOUND, ret);
    
    // 测试统计信息
    sstable_stats_t stats;
    ret = sstable_reader_get_stats(reader, &stats);
    ASSERT_EQ(KV_ERR_NONE, ret);
    ASSERT_EQ((uint64_t)count, stats.total_entries);
    ASSERT_TRUE(stats.file_size > 0);
    
    printf("    读取器文件大小: %lu bytes\n", stats.file_size);
    printf("    读取器条目数量: %lu\n", stats.total_entries);
    
    sstable_reader_destroy(reader);
    
    return 1;
}

// === 主测试函数 ===
int main(void) {
    printf("🚀 ConcordKV LSM-Tree SSTable测试套件\n");
    printf("====================================\n");
    
    // 设置超时处理
    signal(SIGALRM, timeout_handler);
    
    // 初始化错误处理
    kv_error_init(KV_LOG_ERROR, NULL, 1);
    
    uint64_t start_time = get_timestamp_us();
    
    // 运行所有测试
    RUN_TEST(test_crc32_checksum);
    RUN_TEST(test_bloom_filter_basic);
    RUN_TEST(test_data_block_operations);
    RUN_TEST(test_index_block_operations);
    RUN_TEST(test_filename_formatting);
    RUN_TEST(test_sstable_writer_basic);
    RUN_TEST(test_create_from_memtable);
    RUN_TEST(test_sstable_reader_basic);
    RUN_TEST(test_bloom_filter_performance);
    RUN_TEST(test_data_block_capacity);
    RUN_TEST(test_error_handling);
    RUN_TEST(test_deletion_markers);
    RUN_TEST(test_stress_operations);
    
    uint64_t end_time = get_timestamp_us();
    double total_time = (end_time - start_time) / 1000.0;  // ms
    
    // 清理测试文件
    cleanup_test_files();
    
    // 打印测试结果
    printf("\n📊 测试结果汇总\n");
    printf("================\n");
    printf("总测试数: %d\n", tests_run);
    printf("通过测试: %d\n", tests_passed);
    printf("失败测试: %d\n", tests_run - tests_passed);
    printf("成功率: %.1f%%\n", tests_run > 0 ? (100.0 * tests_passed / tests_run) : 0.0);
    printf("总耗时: %.2f ms\n", total_time);
    
    if (tests_passed == tests_run) {
        printf("🎉 所有测试通过！SSTable文件格式实现正确！\n");
        return 0;
    } else {
        printf("❌ 部分测试失败，请检查实现\n");
        return 1;
    }
} 