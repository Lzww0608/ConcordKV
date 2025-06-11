/*
 * @Author: Lzww0608
 * @Date: 2025-6-11 16:35:15
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-11 16:35:15
 * @Description: ConcordKV io_uring 基础测试程序
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
#define TEST_FILE_PATH "/tmp/kv_uring_test_file"
#define TEST_DATA_SIZE (64 * 1024)  // 64KB
#define TEST_TIMEOUT_MS (5000)      // 5秒超时

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

// === 测试函数 ===

// 测试1: io_uring支持检测
static bool test_io_uring_support(void) {
    print_test_header("测试io_uring支持");
    
    uint64_t start_time = kv_uring_get_timestamp_us();
    bool supported = kv_uring_is_supported();
    uint64_t end_time = kv_uring_get_timestamp_us();
    
    printf("io_uring支持状态: %s\n", supported ? "支持" : "不支持");
    
    print_test_result("io_uring支持检测", supported, end_time - start_time);
    return supported;
}

// 测试2: 配置创建和验证
static bool test_config_creation(void) {
    print_test_header("测试配置创建和验证");
    
    uint64_t start_time = kv_uring_get_timestamp_us();
    
    kv_uring_config_t *config = kv_uring_config_create();
    if (!config) {
        printf("ERROR: 配置创建失败\n");
        print_test_result("配置创建", false, kv_uring_get_timestamp_us() - start_time);
        return false;
    }
    
    printf("默认配置: queue_depth=%u, buffer_size=%zu, timeout_ms=%u\n", 
           config->queue_depth, config->buffer_size, config->timeout_ms);
    
    int ret = kv_uring_config_validate(config);
    bool valid = (ret == KV_SUCCESS);
    
    printf("配置验证结果: %s\n", valid ? "有效" : "无效");
    
    kv_uring_config_destroy(config);
    
    uint64_t end_time = kv_uring_get_timestamp_us();
    print_test_result("配置创建和验证", valid, end_time - start_time);
    return valid;
}

// 测试3: io_uring实例创建和销毁
static bool test_uring_lifecycle(void) {
    print_test_header("测试io_uring生命周期管理");
    
    uint64_t start_time = kv_uring_get_timestamp_us();
    
    kv_uring_config_t *config = kv_uring_config_create();
    if (!config) {
        printf("ERROR: 配置创建失败\n");
        print_test_result("io_uring生命周期", false, kv_uring_get_timestamp_us() - start_time);
        return false;
    }
    
    // 设置小的队列深度用于测试
    kv_uring_config_set_queue_depth(config, 8);
    
    kv_uring_t *uring = kv_uring_create(config);
    if (!uring) {
        printf("ERROR: io_uring创建失败\n");
        kv_uring_config_destroy(config);
        print_test_result("io_uring生命周期", false, kv_uring_get_timestamp_us() - start_time);
        return false;
    }
    
    printf("io_uring实例创建成功\n");
    
    // 启动io_uring
    int start_ret = kv_uring_start(uring);
    if (start_ret != KV_SUCCESS) {
        printf("ERROR: io_uring启动失败: %d\n", start_ret);
        kv_uring_destroy(uring);
        kv_uring_config_destroy(config);
        print_test_result("io_uring生命周期", false, kv_uring_get_timestamp_us() - start_time);
        return false;
    }
    
    printf("io_uring实例启动成功\n");
    
    // 停止并销毁实例
    kv_uring_stop(uring);
    kv_uring_destroy(uring);
    kv_uring_config_destroy(config);
    
    printf("io_uring实例停止和销毁完成\n");
    
    uint64_t end_time = kv_uring_get_timestamp_us();
    print_test_result("io_uring生命周期", true, end_time - start_time);
    return true;
}

// === 主函数 ===
int main(int argc, char *argv[]) {
    printf("ConcordKV io_uring 基础测试程序\n");
    printf("===============================\n");
    printf("测试超时设置: %d毫秒\n", TEST_TIMEOUT_MS);
    printf("测试文件路径: %s\n", TEST_FILE_PATH);
    printf("测试数据大小: %d字节\n", TEST_DATA_SIZE);
    printf("\n");
    fflush(stdout);
    
    // 清理之前的测试文件
    unlink(TEST_FILE_PATH);
    
    bool all_passed = true;
    
    // 执行基础测试
    all_passed &= test_io_uring_support();
    all_passed &= test_config_creation();
    all_passed &= test_uring_lifecycle();
    
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
        printf("\n🎉 所有测试通过！\n");
        return 0;
    } else {
        printf("\n❌ 测试失败！\n");
        return 1;
    }
} 