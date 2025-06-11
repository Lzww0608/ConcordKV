/*
 * @Author: Lzww0608
 * @Date: 2025-6-11 16:45:15
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-11 16:45:15
 * @Description: ConcordKV io_uring 异步I/O操作测试程序
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <assert.h>

#include "../../../kvserver/kv_io_uring.h"

// === 测试配置 ===
#define TEST_FILE_PATH "/tmp/kv_uring_async_test_file"
#define TEST_DATA_SIZE (64 * 1024)  // 64KB
#define TEST_TIMEOUT_MS (10000)     // 10秒超时
#define TEST_SMALL_SIZE (4 * 1024)  // 4KB

// === 测试统计 ===
typedef struct test_stats_s {
    int tests_total;
    int tests_passed;
    int tests_failed;
    int tests_timeout;
    uint64_t total_time_us;
} test_stats_t;

static test_stats_t g_test_stats = {0};
static volatile bool g_timeout_triggered = false;

// === 超时处理 ===
static void timeout_handler(int sig) {
    (void)sig;  // 避免未使用参数警告
    g_timeout_triggered = true;
    printf("WARNING: Test timeout triggered!\n");
    fflush(stdout);
}

static void setup_timeout(uint32_t timeout_ms) {
    g_timeout_triggered = false;
    signal(SIGALRM, timeout_handler);
    alarm(timeout_ms / 1000 + 1);
}

static void clear_timeout(void) {
    alarm(0);
    signal(SIGALRM, SIG_DFL);
}

// === 测试工具函数 ===
static void print_test_header(const char *test_name) {
    printf("\n=== %s ===\n", test_name);
    fflush(stdout);
}

static void print_test_result(const char *test_name, bool passed, uint64_t time_us) {
    const char *status = passed ? "PASS" : "FAIL";
    g_test_stats.tests_total++;
    if (passed) {
        g_test_stats.tests_passed++;
    } else {
        g_test_stats.tests_failed++;
    }
    g_test_stats.total_time_us += time_us;
    
    printf("%s: %s (%.3f ms)\n", test_name, status, time_us / 1000.0);
    fflush(stdout);
}

// === 回调函数 ===
typedef struct test_context_s {
    bool completed;
    int result;
    uint64_t completion_time;
} test_context_t;

static void write_callback(kv_uring_request_t *req, int result, void *data) {
    test_context_t *ctx = (test_context_t*)data;
    ctx->completed = true;
    ctx->result = result;
    ctx->completion_time = kv_uring_get_timestamp_us();
    
    printf("WRITE完成回调: 请求ID=%lu, 结果=%d\n", req->request_id, result);
    fflush(stdout);
}

static void read_callback(kv_uring_request_t *req, int result, void *data) {
    test_context_t *ctx = (test_context_t*)data;
    ctx->completed = true;
    ctx->result = result;
    ctx->completion_time = kv_uring_get_timestamp_us();
    
    printf("READ完成回调: 请求ID=%lu, 结果=%d\n", req->request_id, result);
    fflush(stdout);
}

static void fsync_callback(kv_uring_request_t *req, int result, void *data) {
    test_context_t *ctx = (test_context_t*)data;
    ctx->completed = true;
    ctx->result = result;
    ctx->completion_time = kv_uring_get_timestamp_us();
    
    printf("FSYNC完成回调: 请求ID=%lu, 结果=%d\n", req->request_id, result);
    fflush(stdout);
}

// === 测试函数 ===

// 测试1: 基本异步写操作
static bool test_async_write(void) {
    print_test_header("测试异步写操作");
    
    setup_timeout(TEST_TIMEOUT_MS);
    uint64_t start_time = kv_uring_get_timestamp_us();
    bool success = false;
    
    kv_uring_t *uring = NULL;
    char *test_data = NULL;
    int fd = -1;
    
    // 创建测试数据
    test_data = malloc(TEST_DATA_SIZE);
    if (!test_data) {
        printf("ERROR: 内存分配失败\n");
        goto cleanup;
    }
    
    // 填充测试数据
    for (int i = 0; i < TEST_DATA_SIZE; i++) {
        test_data[i] = (char)(i % 256);
    }
    
    // 创建测试文件
    fd = open(TEST_FILE_PATH, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        printf("ERROR: 文件创建失败: %s\n", strerror(errno));
        goto cleanup;
    }
    
    // 创建io_uring实例
    kv_uring_config_t *config = kv_uring_config_create();
    if (!config) {
        printf("ERROR: 配置创建失败\n");
        goto cleanup;
    }
    
    uring = kv_uring_create(config);
    if (!uring) {
        printf("ERROR: io_uring创建失败\n");
        kv_uring_config_destroy(config);
        goto cleanup;
    }
    
    // 启动io_uring
    if (kv_uring_start(uring) != KV_SUCCESS) {
        printf("ERROR: io_uring启动失败\n");
        goto cleanup;
    }
    
    // 设置回调上下文
    test_context_t ctx = {false, 0, 0};
    
    // 提交异步写请求
    printf("提交异步写请求: 大小=%d字节\n", TEST_DATA_SIZE);
    kv_uring_request_t *request = kv_uring_write_async(uring, fd, test_data, TEST_DATA_SIZE, 0,
                                                       write_callback, &ctx);
    
    if (!request) {
        printf("ERROR: 异步写请求提交失败\n");
        goto cleanup;
    }
    
    printf("异步写请求已提交, 请求ID=%lu\n", request->request_id);
    
    // 等待完成（带超时）
    int wait_ret = kv_uring_wait_request(uring, request, TEST_TIMEOUT_MS);
    if (wait_ret != KV_SUCCESS) {
        if (wait_ret == KV_ERR_TIMEOUT) {
            printf("ERROR: 等待超时\n");
            g_test_stats.tests_timeout++;
        } else {
            printf("ERROR: 等待失败: %d\n", wait_ret);
        }
        goto cleanup;
    }
    
    // 检查结果
    if (ctx.result == TEST_DATA_SIZE) {
        printf("写操作成功: 写入%d字节\n", ctx.result);
        success = true;
    } else {
        printf("ERROR: 写操作失败: 期望%d字节, 实际%d字节\n", TEST_DATA_SIZE, ctx.result);
    }
    
    // 清理请求
    kv_uring_request_destroy(request);
    
    kv_uring_config_destroy(config);
    
cleanup:
    if (uring) {
        kv_uring_stop(uring);
        kv_uring_destroy(uring);
    }
    if (test_data) free(test_data);
    if (fd >= 0) close(fd);
    clear_timeout();
    
    uint64_t end_time = kv_uring_get_timestamp_us();
    print_test_result("异步写操作", success && !g_timeout_triggered, end_time - start_time);
    return success && !g_timeout_triggered;
}

// 测试2: 基本异步读操作
static bool test_async_read(void) {
    print_test_header("测试异步读操作");
    
    setup_timeout(TEST_TIMEOUT_MS);
    uint64_t start_time = kv_uring_get_timestamp_us();
    bool success = false;
    
    kv_uring_t *uring = NULL;
    char *read_buffer = NULL;
    int fd = -1;
    
    // 打开测试文件
    fd = open(TEST_FILE_PATH, O_RDONLY);
    if (fd < 0) {
        printf("ERROR: 文件打开失败: %s\n", strerror(errno));
        goto cleanup;
    }
    
    // 分配读缓冲区
    read_buffer = malloc(TEST_DATA_SIZE);
    if (!read_buffer) {
        printf("ERROR: 内存分配失败\n");
        goto cleanup;
    }
    
    // 创建io_uring实例
    kv_uring_config_t *config = kv_uring_config_create();
    if (!config) {
        printf("ERROR: 配置创建失败\n");
        goto cleanup;
    }
    
    uring = kv_uring_create(config);
    if (!uring) {
        printf("ERROR: io_uring创建失败\n");
        kv_uring_config_destroy(config);
        goto cleanup;
    }
    
    // 启动io_uring
    if (kv_uring_start(uring) != KV_SUCCESS) {
        printf("ERROR: io_uring启动失败\n");
        goto cleanup;
    }
    
    // 设置回调上下文
    test_context_t ctx = {false, 0, 0};
    
    // 提交异步读请求
    printf("提交异步读请求: 大小=%d字节\n", TEST_DATA_SIZE);
    kv_uring_request_t *request = kv_uring_read_async(uring, fd, read_buffer, TEST_DATA_SIZE, 0,
                                                      read_callback, &ctx);
    
    if (!request) {
        printf("ERROR: 异步读请求提交失败\n");
        goto cleanup;
    }
    
    printf("异步读请求已提交, 请求ID=%lu\n", request->request_id);
    
    // 等待完成（带超时）
    int wait_ret = kv_uring_wait_request(uring, request, TEST_TIMEOUT_MS);
    if (wait_ret != KV_SUCCESS) {
        if (wait_ret == KV_ERR_TIMEOUT) {
            printf("ERROR: 等待超时\n");
            g_test_stats.tests_timeout++;
        } else {
            printf("ERROR: 等待失败: %d\n", wait_ret);
        }
        goto cleanup;
    }
    
    // 检查结果
    if (ctx.result == TEST_DATA_SIZE) {
        printf("读操作成功: 读取%d字节\n", ctx.result);
        
        // 验证数据正确性
        bool data_valid = true;
        for (int i = 0; i < TEST_DATA_SIZE; i++) {
            if (read_buffer[i] != (char)(i % 256)) {
                printf("ERROR: 数据验证失败在位置%d: 期望%d, 实际%d\n", 
                       i, (char)(i % 256), read_buffer[i]);
                data_valid = false;
                break;
            }
        }
        
        if (data_valid) {
            printf("数据验证成功\n");
            success = true;
        }
    } else {
        printf("ERROR: 读操作失败: 期望%d字节, 实际%d字节\n", TEST_DATA_SIZE, ctx.result);
    }
    
    // 清理请求
    kv_uring_request_destroy(request);
    
    kv_uring_config_destroy(config);
    
cleanup:
    if (uring) {
        kv_uring_stop(uring);
        kv_uring_destroy(uring);
    }
    if (read_buffer) free(read_buffer);
    if (fd >= 0) close(fd);
    clear_timeout();
    
    uint64_t end_time = kv_uring_get_timestamp_us();
    print_test_result("异步读操作", success && !g_timeout_triggered, end_time - start_time);
    return success && !g_timeout_triggered;
}

// 测试3: 异步fsync操作
static bool test_async_fsync(void) {
    print_test_header("测试异步fsync操作");
    
    setup_timeout(TEST_TIMEOUT_MS);
    uint64_t start_time = kv_uring_get_timestamp_us();
    bool success = false;
    
    kv_uring_t *uring = NULL;
    int fd = -1;
    
    // 打开测试文件
    fd = open(TEST_FILE_PATH, O_WRONLY);
    if (fd < 0) {
        printf("ERROR: 文件打开失败: %s\n", strerror(errno));
        goto cleanup;
    }
    
    // 创建io_uring实例
    kv_uring_config_t *config = kv_uring_config_create();
    if (!config) {
        printf("ERROR: 配置创建失败\n");
        goto cleanup;
    }
    
    uring = kv_uring_create(config);
    if (!uring) {
        printf("ERROR: io_uring创建失败\n");
        kv_uring_config_destroy(config);
        goto cleanup;
    }
    
    // 启动io_uring
    if (kv_uring_start(uring) != KV_SUCCESS) {
        printf("ERROR: io_uring启动失败\n");
        goto cleanup;
    }
    
    // 设置回调上下文
    test_context_t ctx = {false, 0, 0};
    
    // 提交异步fsync请求
    printf("提交异步fsync请求\n");
    kv_uring_request_t *request = kv_uring_fsync_async(uring, fd, fsync_callback, &ctx);
    
    if (!request) {
        printf("ERROR: 异步fsync请求提交失败\n");
        goto cleanup;
    }
    
    printf("异步fsync请求已提交, 请求ID=%lu\n", request->request_id);
    
    // 等待完成（带超时）
    int wait_ret = kv_uring_wait_request(uring, request, TEST_TIMEOUT_MS);
    if (wait_ret != KV_SUCCESS) {
        if (wait_ret == KV_ERR_TIMEOUT) {
            printf("ERROR: 等待超时\n");
            g_test_stats.tests_timeout++;
        } else {
            printf("ERROR: 等待失败: %d\n", wait_ret);
        }
        goto cleanup;
    }
    
    // 检查结果
    if (ctx.result == 0) {
        printf("fsync操作成功\n");
        success = true;
    } else {
        printf("ERROR: fsync操作失败: 结果=%d\n", ctx.result);
    }
    
    // 清理请求
    kv_uring_request_destroy(request);
    
    kv_uring_config_destroy(config);
    
cleanup:
    if (uring) {
        kv_uring_stop(uring);
        kv_uring_destroy(uring);
    }
    if (fd >= 0) close(fd);
    clear_timeout();
    
    uint64_t end_time = kv_uring_get_timestamp_us();
    print_test_result("异步fsync操作", success && !g_timeout_triggered, end_time - start_time);
    return success && !g_timeout_triggered;
}

// === 主函数 ===
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;  // 避免未使用参数警告
    
    printf("ConcordKV io_uring 异步I/O操作测试程序\n");
    printf("=====================================\n");
    printf("测试超时设置: %d毫秒\n", TEST_TIMEOUT_MS);
    printf("测试文件路径: %s\n", TEST_FILE_PATH);
    printf("测试数据大小: %d字节\n", TEST_DATA_SIZE);
    printf("\n");
    fflush(stdout);
    
    // 清理之前的测试文件
    unlink(TEST_FILE_PATH);
    
    bool all_passed = true;
    
    // 执行异步I/O测试
    all_passed &= test_async_write();
    all_passed &= test_async_read();
    all_passed &= test_async_fsync();
    
    // 清理测试文件
    unlink(TEST_FILE_PATH);
    
    // 打印最终统计
    printf("\n=== 测试结果统计 ===\n");
    printf("总测试数: %d\n", g_test_stats.tests_total);
    printf("通过: %d\n", g_test_stats.tests_passed);
    printf("失败: %d\n", g_test_stats.tests_failed);
    printf("超时: %d\n", g_test_stats.tests_timeout);
    
    if (g_test_stats.tests_total > 0) {
        double success_rate = (double)g_test_stats.tests_passed / g_test_stats.tests_total * 100.0;
        printf("成功率: %.1f%%\n", success_rate);
        
        double avg_latency_us = (double)g_test_stats.total_time_us / g_test_stats.tests_total;
        printf("平均延迟: %.3f ms\n", avg_latency_us / 1000.0);
    }
    
    // 返回结果
    if (all_passed && g_test_stats.tests_failed == 0 && g_test_stats.tests_timeout == 0) {
        printf("\n🎉 所有异步I/O测试通过！\n");
        return 0;
    } else {
        printf("\n❌ 异步I/O测试失败！\n");
        return 1;
    }
} 