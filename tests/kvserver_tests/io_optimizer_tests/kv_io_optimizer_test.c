/*
 * @Author: Lzww0608
 * @Date: 2025-6-9 20:00:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-9 20:00:00
 * @Description: ConcordKV I/O 优化器测试 - 综合测试套件
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>

#include "../../../kvserver/kv_io_optimizer.h"
#include "../../../kvserver/kv_error.h"

// === 测试框架 ===
#define TEST_TIMEOUT_SECONDS    30
#define TEST_DATA_SIZE          (1024 * 1024)  // 1MB
#define TEST_FILE_PREFIX        "/tmp/kv_io_test"
#define TEST_BUFFER_SIZE        (64 * 1024)    // 64KB
#define SEPARATOR_LINE          "=================================================="

// 测试状态
static int test_count = 0;
static int test_passed = 0;
static int test_failed = 0;
static jmp_buf timeout_buf;

// 超时处理
static void timeout_handler(int sig) {
    printf("测试超时！跳过当前测试...\n");
    longjmp(timeout_buf, 1);
}

// 设置超时
static void set_test_timeout(int seconds) {
    signal(SIGALRM, timeout_handler);
    alarm(seconds);
}

// 清除超时
static void clear_test_timeout(void) {
    alarm(0);
    signal(SIGALRM, SIG_DFL);
}

// 测试断言
#define ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf("❌ 断言失败: %s (行 %d)\n", message, __LINE__); \
            test_failed++; \
            return; \
        } \
    } while(0)

// 开始测试
#define START_TEST(name) \
    do { \
        test_count++; \
        printf("\n🧪 测试 %d: %s\n", test_count, name); \
        if (setjmp(timeout_buf) != 0) { \
            printf("⏰ 测试超时，跳过\n"); \
            test_failed++; \
            clear_test_timeout(); \
            return; \
        } \
        set_test_timeout(TEST_TIMEOUT_SECONDS); \
    } while(0)

// 结束测试
#define END_TEST() \
    do { \
        clear_test_timeout(); \
        printf("✅ 测试通过\n"); \
        test_passed++; \
    } while(0)

// === 工具函数 ===
static uint64_t get_timestamp_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
}

static char* create_temp_file(size_t size) {
    static int file_counter = 0;
    char *filename = malloc(256);
    if (!filename) return NULL;
    
    snprintf(filename, 256, "%s_%d_%d.tmp", TEST_FILE_PREFIX, getpid(), ++file_counter);
    
    int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        free(filename);
        return NULL;
    }
    
    // 填充测试数据
    char *buffer = malloc(4096);
    if (!buffer) {
        close(fd);
        unlink(filename);
        free(filename);
        return NULL;
    }
    
    memset(buffer, 0xAA, 4096);
    size_t written = 0;
    while (written < size) {
        size_t chunk = (size - written > 4096) ? 4096 : (size - written);
        if (write(fd, buffer, chunk) != (ssize_t)chunk) {
            close(fd);
            unlink(filename);
            free(buffer);
            free(filename);
            return NULL;
        }
        written += chunk;
    }
    
    close(fd);
    free(buffer);
    return filename;
}

static void cleanup_temp_file(const char *filename) {
    if (filename) {
        unlink(filename);
    }
}

// === 测试用例 ===

// 测试1: 配置管理测试
static void test_config_management(void) {
    START_TEST("配置管理和验证");
    
    // 创建配置
    kv_io_optimizer_config_t *config = kv_io_optimizer_config_create();
    ASSERT(config != NULL, "配置创建失败");
    
    // 验证默认配置
    ASSERT(config->buffer_size == KV_IO_DEFAULT_BUFFER_SIZE, "默认缓冲区大小错误");
    ASSERT(config->max_batch_size == KV_IO_MAX_BATCH_SIZE, "默认批量大小错误");
    ASSERT(config->enable_async_io == true, "默认异步I/O设置错误");
    ASSERT(config->enable_mmap == true, "默认内存映射设置错误");
    
    // 测试配置设置
    int ret = kv_io_optimizer_config_set_buffer_size(config, 128 * 1024);
    ASSERT(ret == KV_SUCCESS, "设置缓冲区大小失败");
    ASSERT(config->buffer_size == 128 * 1024, "缓冲区大小设置不正确");
    
    ret = kv_io_optimizer_config_set_strategy(config, KV_IO_STRATEGY_DIRECT);
    ASSERT(ret == KV_SUCCESS, "设置I/O策略失败");
    ASSERT(config->default_strategy == KV_IO_STRATEGY_DIRECT, "I/O策略设置不正确");
    
    ret = kv_io_optimizer_config_set_access_pattern(config, KV_IO_ACCESS_SEQUENTIAL);
    ASSERT(ret == KV_SUCCESS, "设置访问模式失败");
    ASSERT(config->access_pattern == KV_IO_ACCESS_SEQUENTIAL, "访问模式设置不正确");
    
    // 测试配置验证
    ret = kv_io_optimizer_config_validate(config);
    ASSERT(ret == KV_SUCCESS, "配置验证失败");
    
    // 测试无效配置
    config->buffer_size = 0;
    ret = kv_io_optimizer_config_validate(config);
    ASSERT(ret != KV_SUCCESS, "无效配置验证应该失败");
    
    kv_io_optimizer_config_destroy(config);
    
    END_TEST();
}

// 测试2: I/O优化器生命周期测试
static void test_optimizer_lifecycle(void) {
    START_TEST("I/O优化器生命周期管理");
    
    // 创建配置
    kv_io_optimizer_config_t *config = kv_io_optimizer_config_create();
    ASSERT(config != NULL, "配置创建失败");
    
    // 创建优化器
    kv_io_optimizer_t *optimizer = kv_io_optimizer_create(config);
    ASSERT(optimizer != NULL, "I/O优化器创建失败");
    ASSERT(optimizer->is_initialized == true, "优化器初始化状态错误");
    ASSERT(optimizer->is_shutdown == false, "优化器关闭状态错误");
    
    // 启动优化器
    int ret = kv_io_optimizer_start(optimizer);
    ASSERT(ret == KV_SUCCESS, "优化器启动失败");
    
    // 停止优化器
    ret = kv_io_optimizer_stop(optimizer);
    ASSERT(ret == KV_SUCCESS, "优化器停止失败");
    ASSERT(optimizer->is_shutdown == true, "优化器关闭状态错误");
    
    // 销毁优化器
    kv_io_optimizer_destroy(optimizer);
    kv_io_optimizer_config_destroy(config);
    
    END_TEST();
}

// 测试3: 基础I/O操作测试
static void test_basic_io_operations(void) {
    START_TEST("基础I/O操作功能");
    
    // 创建测试文件
    char *filename = create_temp_file(TEST_DATA_SIZE);
    ASSERT(filename != NULL, "创建测试文件失败");
    
    // 创建优化器
    kv_io_optimizer_config_t *config = kv_io_optimizer_config_create();
    ASSERT(config != NULL, "配置创建失败");
    
    kv_io_optimizer_t *optimizer = kv_io_optimizer_create(config);
    ASSERT(optimizer != NULL, "优化器创建失败");
    
    int ret = kv_io_optimizer_start(optimizer);
    ASSERT(ret == KV_SUCCESS, "优化器启动失败");
    
    // 打开文件
    int fd = open(filename, O_RDWR);
    ASSERT(fd >= 0, "文件打开失败");
    
    // 分配缓冲区
    void *read_buffer = malloc(TEST_BUFFER_SIZE);
    void *write_buffer = malloc(TEST_BUFFER_SIZE);
    ASSERT(read_buffer != NULL && write_buffer != NULL, "缓冲区分配失败");
    
    // 填充写入数据
    memset(write_buffer, 0xBB, TEST_BUFFER_SIZE);
    
    // 测试写入操作
    int written = kv_io_write(optimizer, fd, write_buffer, TEST_BUFFER_SIZE, 0);
    ASSERT(written == TEST_BUFFER_SIZE, "写入操作失败");
    
    // 测试读取操作
    int read_bytes = kv_io_read(optimizer, fd, read_buffer, TEST_BUFFER_SIZE, 0);
    ASSERT(read_bytes == TEST_BUFFER_SIZE, "读取操作失败");
    
    // 验证数据正确性
    ASSERT(memcmp(read_buffer, write_buffer, TEST_BUFFER_SIZE) == 0, "读写数据不匹配");
    
    // 测试同步操作
    ret = kv_io_sync(optimizer, fd);
    ASSERT(ret == KV_SUCCESS, "同步操作失败");
    
    // 清理资源
    close(fd);
    free(read_buffer);
    free(write_buffer);
    kv_io_optimizer_destroy(optimizer);
    kv_io_optimizer_config_destroy(config);
    cleanup_temp_file(filename);
    free(filename);
    
    END_TEST();
}

// 测试4: 缓冲区管理测试
static void test_buffer_management(void) {
    START_TEST("缓冲区管理功能");
    
    // 创建优化器
    kv_io_optimizer_config_t *config = kv_io_optimizer_config_create();
    ASSERT(config != NULL, "配置创建失败");
    
    kv_io_optimizer_t *optimizer = kv_io_optimizer_create(config);
    ASSERT(optimizer != NULL, "优化器创建失败");
    
    // 创建普通缓冲区
    kv_io_buffer_t *buffer1 = kv_io_buffer_create(optimizer, TEST_BUFFER_SIZE, false);
    ASSERT(buffer1 != NULL, "普通缓冲区创建失败");
    ASSERT(buffer1->size == TEST_BUFFER_SIZE, "缓冲区大小错误");
    ASSERT(buffer1->is_aligned == false, "缓冲区对齐状态错误");
    
    // 创建对齐缓冲区
    kv_io_buffer_t *buffer2 = kv_io_buffer_create(optimizer, TEST_BUFFER_SIZE, true);
    ASSERT(buffer2 != NULL, "对齐缓冲区创建失败");
    ASSERT(buffer2->is_aligned == true, "对齐缓冲区状态错误");
    ASSERT(kv_io_is_aligned(buffer2->data, KV_IO_DIRECT_IO_ALIGNMENT), "缓冲区内存未正确对齐");
    
    // 测试缓冲区写入
    char test_data[] = "Hello, I/O Optimizer!";
    int ret = kv_io_buffer_write(buffer1, test_data, strlen(test_data), 0);
    ASSERT(ret == (int)strlen(test_data), "缓冲区写入失败");
    ASSERT(buffer1->used == strlen(test_data), "缓冲区使用大小错误");
    ASSERT(buffer1->is_dirty == true, "缓冲区脏标志错误");
    
    // 测试缓冲区读取
    char read_data[256];
    ret = kv_io_buffer_read(buffer1, read_data, strlen(test_data), 0);
    ASSERT(ret == (int)strlen(test_data), "缓冲区读取失败");
    read_data[strlen(test_data)] = '\0';
    ASSERT(strcmp(read_data, test_data) == 0, "缓冲区读取数据错误");
    
    // 清理资源
    kv_io_buffer_destroy(buffer1);
    kv_io_buffer_destroy(buffer2);
    kv_io_optimizer_destroy(optimizer);
    kv_io_optimizer_config_destroy(config);
    
    END_TEST();
}

// 测试5: 异步I/O操作测试
static void test_async_io_operations(void) {
    START_TEST("异步I/O操作功能");
    
    // 创建测试文件
    char *filename = create_temp_file(TEST_DATA_SIZE);
    ASSERT(filename != NULL, "创建测试文件失败");
    
    // 创建优化器
    kv_io_optimizer_config_t *config = kv_io_optimizer_config_create();
    ASSERT(config != NULL, "配置创建失败");
    config->enable_async_io = true;
    
    kv_io_optimizer_t *optimizer = kv_io_optimizer_create(config);
    ASSERT(optimizer != NULL, "优化器创建失败");
    
    int ret = kv_io_optimizer_start(optimizer);
    ASSERT(ret == KV_SUCCESS, "优化器启动失败");
    
    // 打开文件
    int fd = open(filename, O_RDWR);
    ASSERT(fd >= 0, "文件打开失败");
    
    // 分配缓冲区
    void *write_buffer = malloc(TEST_BUFFER_SIZE);
    void *read_buffer = malloc(TEST_BUFFER_SIZE);
    ASSERT(write_buffer != NULL && read_buffer != NULL, "缓冲区分配失败");
    
    memset(write_buffer, 0xCC, TEST_BUFFER_SIZE);
    
    // 测试异步写入
    kv_io_request_t *write_req = kv_io_async_write(optimizer, fd, write_buffer, 
                                                  TEST_BUFFER_SIZE, 0, NULL, NULL);
    ASSERT(write_req != NULL, "异步写入请求创建失败");
    
    // 等待写入完成
    int write_result = kv_io_async_wait(optimizer, write_req, 5000);
    ASSERT(write_result == TEST_BUFFER_SIZE, "异步写入操作失败");
    
    // 测试异步读取
    kv_io_request_t *read_req = kv_io_async_read(optimizer, fd, read_buffer, 
                                                TEST_BUFFER_SIZE, 0, NULL, NULL);
    ASSERT(read_req != NULL, "异步读取请求创建失败");
    
    // 等待读取完成
    int read_result = kv_io_async_wait(optimizer, read_req, 5000);
    ASSERT(read_result == TEST_BUFFER_SIZE, "异步读取操作失败");
    
    // 验证数据正确性
    ASSERT(memcmp(read_buffer, write_buffer, TEST_BUFFER_SIZE) == 0, "异步读写数据不匹配");
    
    // 清理资源
    kv_io_request_destroy(write_req);
    kv_io_request_destroy(read_req);
    close(fd);
    free(write_buffer);
    free(read_buffer);
    kv_io_optimizer_destroy(optimizer);
    kv_io_optimizer_config_destroy(config);
    cleanup_temp_file(filename);
    free(filename);
    
    END_TEST();
}

// 测试6: 性能基准测试
static void test_performance_benchmark(void) {
    START_TEST("性能基准测试");
    
    // 创建测试文件
    char *filename = create_temp_file(TEST_DATA_SIZE);
    ASSERT(filename != NULL, "创建测试文件失败");
    
    // 创建优化器
    kv_io_optimizer_config_t *config = kv_io_optimizer_config_create();
    ASSERT(config != NULL, "配置创建失败");
    
    kv_io_optimizer_t *optimizer = kv_io_optimizer_create(config);
    ASSERT(optimizer != NULL, "优化器创建失败");
    
    int ret = kv_io_optimizer_start(optimizer);
    ASSERT(ret == KV_SUCCESS, "优化器启动失败");
    
    // 打开文件
    int fd = open(filename, O_RDWR);
    ASSERT(fd >= 0, "文件打开失败");
    
    // 分配缓冲区
    void *buffer = malloc(TEST_BUFFER_SIZE);
    ASSERT(buffer != NULL, "缓冲区分配失败");
    memset(buffer, 0xDD, TEST_BUFFER_SIZE);
    
    // 性能测试：写入
    uint64_t start_time = get_timestamp_us();
    const int num_operations = 100;
    
    for (int i = 0; i < num_operations; i++) {
        off_t offset = (i * TEST_BUFFER_SIZE) % TEST_DATA_SIZE;
        int written = kv_io_write(optimizer, fd, buffer, TEST_BUFFER_SIZE, offset);
        ASSERT(written == TEST_BUFFER_SIZE, "性能测试写入失败");
    }
    
    uint64_t write_time = get_timestamp_us() - start_time;
    
    // 性能测试：读取
    start_time = get_timestamp_us();
    
    for (int i = 0; i < num_operations; i++) {
        off_t offset = (i * TEST_BUFFER_SIZE) % TEST_DATA_SIZE;
        int read_bytes = kv_io_read(optimizer, fd, buffer, TEST_BUFFER_SIZE, offset);
        ASSERT(read_bytes == TEST_BUFFER_SIZE, "性能测试读取失败");
    }
    
    uint64_t read_time = get_timestamp_us() - start_time;
    
    // 计算性能指标
    double write_throughput = (double)(num_operations * TEST_BUFFER_SIZE) / 
                             (write_time / 1000000.0) / (1024.0 * 1024.0);
    double read_throughput = (double)(num_operations * TEST_BUFFER_SIZE) / 
                            (read_time / 1000000.0) / (1024.0 * 1024.0);
    
    printf("📊 性能结果:\n");
    printf("   写入吞吐量: %.2f MB/s (%d次操作, %.3f秒)\n", 
           write_throughput, num_operations, write_time / 1000000.0);
    printf("   读取吞吐量: %.2f MB/s (%d次操作, %.3f秒)\n", 
           read_throughput, num_operations, read_time / 1000000.0);
    
    // 基本性能要求验证
    ASSERT(write_throughput > 10.0, "写入性能过低"); // 至少10MB/s
    ASSERT(read_throughput > 10.0, "读取性能过低");   // 至少10MB/s
    
    // 清理资源
    close(fd);
    free(buffer);
    kv_io_optimizer_destroy(optimizer);
    kv_io_optimizer_config_destroy(config);
    cleanup_temp_file(filename);
    free(filename);
    
    END_TEST();
}

// 测试7: 统计信息测试
static void test_statistics_collection(void) {
    START_TEST("统计信息收集功能");
    
    // 创建测试文件
    char *filename = create_temp_file(TEST_DATA_SIZE);
    ASSERT(filename != NULL, "创建测试文件失败");
    
    // 创建优化器
    kv_io_optimizer_config_t *config = kv_io_optimizer_config_create();
    ASSERT(config != NULL, "配置创建失败");
    
    kv_io_optimizer_t *optimizer = kv_io_optimizer_create(config);
    ASSERT(optimizer != NULL, "优化器创建失败");
    
    int ret = kv_io_optimizer_start(optimizer);
    ASSERT(ret == KV_SUCCESS, "优化器启动失败");
    
    // 重置统计信息
    ret = kv_io_reset_stats(optimizer);
    ASSERT(ret == KV_SUCCESS, "重置统计失败");
    
    // 打开文件
    int fd = open(filename, O_RDWR);
    ASSERT(fd >= 0, "文件打开失败");
    
    // 分配缓冲区
    void *buffer = malloc(TEST_BUFFER_SIZE);
    ASSERT(buffer != NULL, "缓冲区分配失败");
    memset(buffer, 0xEE, TEST_BUFFER_SIZE);
    
    // 执行一些I/O操作
    const int num_ops = 10;
    for (int i = 0; i < num_ops; i++) {
        kv_io_write(optimizer, fd, buffer, TEST_BUFFER_SIZE, i * TEST_BUFFER_SIZE);
        kv_io_read(optimizer, fd, buffer, TEST_BUFFER_SIZE, i * TEST_BUFFER_SIZE);
    }
    
    kv_io_sync(optimizer, fd);
    
    // 获取统计信息
    kv_io_stats_t stats;
    ret = kv_io_get_stats(optimizer, &stats);
    ASSERT(ret == KV_SUCCESS, "获取统计信息失败");
    
    // 验证统计信息
    ASSERT(stats.read_count == num_ops, "读操作计数错误");
    ASSERT(stats.write_count == num_ops, "写操作计数错误");
    ASSERT(stats.sync_count == 1, "同步操作计数错误");
    ASSERT(stats.bytes_read == num_ops * TEST_BUFFER_SIZE, "读取字节数错误");
    ASSERT(stats.bytes_written == num_ops * TEST_BUFFER_SIZE, "写入字节数错误");
    
    printf("📈 统计信息:\n");
    printf("   读操作: %lu 次\n", stats.read_count);
    printf("   写操作: %lu 次\n", stats.write_count);
    printf("   同步操作: %lu 次\n", stats.sync_count);
    printf("   读取字节: %lu (%.2f KB)\n", stats.bytes_read, 
           (double)stats.bytes_read / 1024.0);
    printf("   写入字节: %lu (%.2f KB)\n", stats.bytes_written,
           (double)stats.bytes_written / 1024.0);
    printf("   平均读延迟: %.2f us\n", stats.avg_read_latency);
    printf("   平均写延迟: %.2f us\n", stats.avg_write_latency);
    
    // 测试统计信息打印
    printf("\n📊 完整统计报告:\n");
    kv_io_print_stats(optimizer, stdout);
    
    // 清理资源
    close(fd);
    free(buffer);
    kv_io_optimizer_destroy(optimizer);
    kv_io_optimizer_config_destroy(config);
    cleanup_temp_file(filename);
    free(filename);
    
    END_TEST();
}

// 测试8: 工具函数测试
static void test_utility_functions(void) {
    START_TEST("工具函数功能");
    
    // 测试时间戳获取
    uint64_t timestamp1 = kv_io_get_timestamp_us();
    usleep(1000); // 等待1ms
    uint64_t timestamp2 = kv_io_get_timestamp_us();
    ASSERT(timestamp2 > timestamp1, "时间戳获取错误");
    ASSERT(timestamp2 - timestamp1 >= 1000, "时间戳精度不足");
    
    // 测试页面大小获取
    int page_size = kv_io_get_page_size();
    ASSERT(page_size > 0, "页面大小获取失败");
    ASSERT(page_size >= 4096, "页面大小过小");
    printf("📏 系统页面大小: %d 字节\n", page_size);
    
    // 测试内存对齐
    void *aligned_ptr = kv_io_aligned_alloc(512, 4096);
    ASSERT(aligned_ptr != NULL, "内存对齐分配失败");
    ASSERT(kv_io_is_aligned(aligned_ptr, 512), "内存对齐检查失败");
    
    // 测试非对齐内存
    void *unaligned_ptr = malloc(1024);
    ASSERT(unaligned_ptr != NULL, "普通内存分配失败");
    // 注意：malloc返回的内存不保证512字节对齐
    
    printf("🧮 内存对齐测试:\n");
    printf("   对齐内存地址: %p (512字节对齐: %s)\n", 
           aligned_ptr, kv_io_is_aligned(aligned_ptr, 512) ? "是" : "否");
    printf("   普通内存地址: %p (512字节对齐: %s)\n", 
           unaligned_ptr, kv_io_is_aligned(unaligned_ptr, 512) ? "是" : "否");
    
    // 清理内存
    kv_io_aligned_free(aligned_ptr);
    free(unaligned_ptr);
    
    END_TEST();
}

// 测试9: 错误处理测试
static void test_error_handling(void) {
    START_TEST("错误处理和边界条件");
    
    // 测试NULL参数处理
    kv_io_optimizer_t *null_optimizer = NULL;
    int ret = kv_io_read(null_optimizer, 0, NULL, 0, 0);
    ASSERT(ret == KV_ERR_PARAM, "NULL优化器参数检查失败");
    
    // 创建优化器用于测试
    kv_io_optimizer_config_t *config = kv_io_optimizer_config_create();
    ASSERT(config != NULL, "配置创建失败");
    
    kv_io_optimizer_t *optimizer = kv_io_optimizer_create(config);
    ASSERT(optimizer != NULL, "优化器创建失败");
    
    // 测试无效文件描述符
    char buffer[1024];
    ret = kv_io_read(optimizer, -1, buffer, sizeof(buffer), 0);
    ASSERT(ret == KV_ERR_PARAM, "无效文件描述符检查失败");
    
    // 测试NULL缓冲区
    ret = kv_io_read(optimizer, 0, NULL, 1024, 0);
    ASSERT(ret == KV_ERR_PARAM, "NULL缓冲区检查失败");
    
    // 测试零大小
    ret = kv_io_read(optimizer, 0, buffer, 0, 0);
    ASSERT(ret == KV_ERR_PARAM, "零大小检查失败");
    
    // 测试无效配置
    kv_io_optimizer_config_t *invalid_config = kv_io_optimizer_config_create();
    ASSERT(invalid_config != NULL, "无效配置创建失败");
    
    invalid_config->buffer_size = 0;  // 无效大小
    ret = kv_io_optimizer_config_validate(invalid_config);
    ASSERT(ret != KV_SUCCESS, "无效配置验证应该失败");
    
    // 测试空配置创建优化器
    kv_io_optimizer_t *invalid_optimizer = kv_io_optimizer_create(invalid_config);
    ASSERT(invalid_optimizer == NULL, "无效配置应该创建失败");
    
    printf("🛡️ 错误处理测试通过:\n");
    printf("   NULL参数检查: ✓\n");
    printf("   无效文件描述符检查: ✓\n");
    printf("   无效配置检查: ✓\n");
    
    // 清理资源
    kv_io_optimizer_destroy(optimizer);
    kv_io_optimizer_config_destroy(config);
    kv_io_optimizer_config_destroy(invalid_config);
    
    END_TEST();
}

// 测试10: 集成测试
static void test_integration_features(void) {
    START_TEST("集成功能测试");
    
    // 创建多个测试文件
    char *file1 = create_temp_file(TEST_DATA_SIZE / 2);
    char *file2 = create_temp_file(TEST_DATA_SIZE / 2);
    ASSERT(file1 != NULL && file2 != NULL, "测试文件创建失败");
    
    // 创建优化器
    kv_io_optimizer_config_t *config = kv_io_optimizer_config_create();
    ASSERT(config != NULL, "配置创建失败");
    
    // 启用多种优化策略
    config->enable_async_io = true;
    config->enable_mmap = true;
    config->enable_batch_io = true;
    config->enable_readahead = true;
    
    kv_io_optimizer_t *optimizer = kv_io_optimizer_create(config);
    ASSERT(optimizer != NULL, "优化器创建失败");
    
    int ret = kv_io_optimizer_start(optimizer);
    ASSERT(ret == KV_SUCCESS, "优化器启动失败");
    
    // 打开文件
    int fd1 = open(file1, O_RDWR);
    int fd2 = open(file2, O_RDWR);
    ASSERT(fd1 >= 0 && fd2 >= 0, "文件打开失败");
    
    // 创建多个缓冲区
    kv_io_buffer_t *buffer1 = kv_io_buffer_create(optimizer, TEST_BUFFER_SIZE, false);
    kv_io_buffer_t *buffer2 = kv_io_buffer_create(optimizer, TEST_BUFFER_SIZE, true);
    ASSERT(buffer1 != NULL && buffer2 != NULL, "缓冲区创建失败");
    
    // 混合操作测试
    char test_data1[] = "Integration Test Data 1";
    char test_data2[] = "Integration Test Data 2";
    
    // 缓冲区操作
    ret = kv_io_buffer_write(buffer1, test_data1, strlen(test_data1), 0);
    ASSERT(ret > 0, "缓冲区1写入失败");
    
    ret = kv_io_buffer_write(buffer2, test_data2, strlen(test_data2), 0);
    ASSERT(ret > 0, "缓冲区2写入失败");
    
    // 文件I/O操作
    int written1 = kv_io_write(optimizer, fd1, buffer1->data, buffer1->used, 0);
    int written2 = kv_io_write(optimizer, fd2, buffer2->data, buffer2->used, 0);
    ASSERT(written1 > 0 && written2 > 0, "文件写入失败");
    
    // 同步操作
    ret = kv_io_sync(optimizer, fd1);
    ASSERT(ret == KV_SUCCESS, "文件1同步失败");
    
    ret = kv_io_sync(optimizer, fd2);
    ASSERT(ret == KV_SUCCESS, "文件2同步失败");
    
    // 验证读取
    char read_buffer1[256];
    char read_buffer2[256];
    
    int read1 = kv_io_read(optimizer, fd1, read_buffer1, strlen(test_data1), 0);
    int read2 = kv_io_read(optimizer, fd2, read_buffer2, strlen(test_data2), 0);
    ASSERT(read1 == (int)strlen(test_data1) && read2 == (int)strlen(test_data2), "文件读取失败");
    
    read_buffer1[read1] = '\0';
    read_buffer2[read2] = '\0';
    ASSERT(strcmp(read_buffer1, test_data1) == 0, "文件1数据验证失败");
    ASSERT(strcmp(read_buffer2, test_data2) == 0, "文件2数据验证失败");
    
    // 获取最终统计
    kv_io_stats_t final_stats;
    ret = kv_io_get_stats(optimizer, &final_stats);
    ASSERT(ret == KV_SUCCESS, "获取最终统计失败");
    
    printf("🔗 集成测试完成:\n");
    printf("   文件操作: %lu 读 + %lu 写\n", final_stats.read_count, final_stats.write_count);
    printf("   数据传输: %.2f KB 读 + %.2f KB 写\n", 
           (double)final_stats.bytes_read / 1024.0,
           (double)final_stats.bytes_written / 1024.0);
    
    // 清理资源
    close(fd1);
    close(fd2);
    kv_io_buffer_destroy(buffer1);
    kv_io_buffer_destroy(buffer2);
    kv_io_optimizer_destroy(optimizer);
    kv_io_optimizer_config_destroy(config);
    cleanup_temp_file(file1);
    cleanup_temp_file(file2);
    free(file1);
    free(file2);
    
    END_TEST();
}

// === 主测试函数 ===
int main(void) {
    printf("🚀 启动 ConcordKV I/O 优化器测试套件\n");
    printf("⏰ 每个测试超时时间: %d 秒\n", TEST_TIMEOUT_SECONDS);
    printf("📂 测试数据大小: %.2f MB\n", (double)TEST_DATA_SIZE / (1024 * 1024));
    printf("🔧 测试缓冲区大小: %.2f KB\n", (double)TEST_BUFFER_SIZE / 1024);
    
    // 简化初始化：跳过错误处理系统初始化
    printf("🔧 开始运行测试...\n");
    
    // 运行所有测试
    test_config_management();
    test_optimizer_lifecycle();
    test_basic_io_operations();
    test_buffer_management();
    test_async_io_operations();
    test_performance_benchmark();
    test_statistics_collection();
    test_utility_functions();
    test_error_handling();
    test_integration_features();
    
    // 输出测试结果
    printf("\n%s\n", SEPARATOR_LINE);
    printf("🏁 测试完成！\n");
    printf("📊 测试统计:\n");
    printf("   总测试数: %d\n", test_count);
    printf("   通过测试: %d ✅\n", test_passed);
    printf("   失败测试: %d ❌\n", test_failed);
    printf("   成功率: %.1f%%\n", 
           test_count > 0 ? (double)test_passed / test_count * 100.0 : 0.0);
    
    if (test_failed == 0) {
        printf("\n🎉 所有测试通过！I/O优化器功能正常！\n");
        return 0;
    } else {
        printf("\n⚠️  有 %d 个测试失败，请检查实现。\n", test_failed);
        return 1;
    }
} 