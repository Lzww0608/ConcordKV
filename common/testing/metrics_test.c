/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV testing framework - metrics_test.c
 */
#define _GNU_SOURCE
#include "../metrics/metrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// 测试结果统计
static int tests_passed = 0;
static int tests_failed = 0;

// 测试宏
#define TEST_ASSERT(condition, message) \
    do { \
        if (condition) { \
            tests_passed++; \
            printf("✅ PASS: %s\n", message); \
        } else { \
            tests_failed++; \
            printf("❌ FAIL: %s\n", message); \
        } \
    } while(0)

// 全局指标仓库
static concord_metrics_repo_t *g_repo = NULL;

// 测试度量仓库创建
void test_metrics_repo_create() {
    printf("\n=== 测试度量仓库创建 ===\n");
    
    g_repo = concord_metrics_repo_create(100);
    TEST_ASSERT(g_repo != NULL, "创建度量仓库");
    
    int count = concord_metrics_count(g_repo);
    TEST_ASSERT(count == 0, "新仓库初始指标数量为0");
}

// 测试计数器度量
void test_counter_metrics() {
    printf("\n=== 测试计数器度量 ===\n");
    
    if (!g_repo) {
        TEST_ASSERT(0, "需要先创建度量仓库");
        return;
    }
    
    // 创建计数器
    concord_metric_t *counter = concord_metrics_create_counter(g_repo, "test_counter", 
                                                              "Test counter description", 0);
    TEST_ASSERT(counter != NULL, "创建计数器");
    
    // 检查计数器类型
    concord_metric_type_t type = concord_metrics_get_type(counter);
    TEST_ASSERT(type == CONCORD_METRIC_COUNTER, "计数器类型正确");
    
    // 检查计数器名称
    const char *name = concord_metrics_get_name(counter);
    TEST_ASSERT(name && strcmp(name, "test_counter") == 0, "计数器名称正确");
    
    // 获取初始值
    int64_t initial_value = concord_metrics_counter_get(counter);
    TEST_ASSERT(initial_value == 0, "计数器初始值为0");
    
    // 增加计数器
    int result = concord_metrics_counter_inc(counter, 1);
    TEST_ASSERT(result == 0, "计数器增加操作成功");
    
    int64_t value1 = concord_metrics_counter_get(counter);
    TEST_ASSERT(value1 == 1, "计数器增加1");
    
    // 按指定值增加
    result = concord_metrics_counter_inc(counter, 5);
    TEST_ASSERT(result == 0, "计数器增加5操作成功");
    
    int64_t value2 = concord_metrics_counter_get(counter);
    TEST_ASSERT(value2 == 6, "计数器增加5后值为6");
    
    // 检查仓库中指标数量
    int count = concord_metrics_count(g_repo);
    TEST_ASSERT(count == 1, "仓库中有1个指标");
}

// 测试测量仪度量
void test_gauge_metrics() {
    printf("\n=== 测试测量仪度量 ===\n");
    
    if (!g_repo) {
        TEST_ASSERT(0, "需要先创建度量仓库");
        return;
    }
    
    // 创建测量仪
    concord_metric_t *gauge = concord_metrics_create_gauge(g_repo, "test_gauge", 
                                                          "Test gauge description", 42.5);
    TEST_ASSERT(gauge != NULL, "创建测量仪");
    
    // 检查测量仪类型
    concord_metric_type_t type = concord_metrics_get_type(gauge);
    TEST_ASSERT(type == CONCORD_METRIC_GAUGE, "测量仪类型正确");
    
    // 获取初始值
    double initial_value = concord_metrics_gauge_get(gauge);
    TEST_ASSERT(initial_value == 42.5, "测量仪初始值为42.5");
    
    // 设置值
    int result = concord_metrics_gauge_set(gauge, 100.0);
    TEST_ASSERT(result == 0, "设置测量仪值操作成功");
    
    double value1 = concord_metrics_gauge_get(gauge);
    TEST_ASSERT(value1 == 100.0, "设置测量仪值为100.0");
    
    // 增加值
    result = concord_metrics_gauge_inc(gauge, 7.5);
    TEST_ASSERT(result == 0, "测量仪增加操作成功");
    
    double value2 = concord_metrics_gauge_get(gauge);
    TEST_ASSERT(value2 == 107.5, "测量仪增加7.5后值为107.5");
    
    // 减少值
    result = concord_metrics_gauge_dec(gauge, 10.0);
    TEST_ASSERT(result == 0, "测量仪减少操作成功");
    
    double value3 = concord_metrics_gauge_get(gauge);
    TEST_ASSERT(value3 == 97.5, "测量仪减少10.0后值为97.5");
}

// 测试直方图度量
void test_histogram_metrics() {
    printf("\n=== 测试直方图度量 ===\n");
    
    if (!g_repo) {
        TEST_ASSERT(0, "需要先创建度量仓库");
        return;
    }
    
    // 配置直方图
    concord_histogram_config_t config = {
        .min = 0.0,
        .max = 10.0,
        .bucket_count = 10
    };
    
    // 创建直方图
    concord_metric_t *histogram = concord_metrics_create_histogram(g_repo, "test_histogram", 
                                                                  "Test histogram description", &config);
    TEST_ASSERT(histogram != NULL, "创建直方图");
    
    // 检查直方图类型
    concord_metric_type_t type = concord_metrics_get_type(histogram);
    TEST_ASSERT(type == CONCORD_METRIC_HISTOGRAM, "直方图类型正确");
    
    // 观察值
    int result = concord_metrics_histogram_observe(histogram, 0.3);
    TEST_ASSERT(result == 0, "直方图观察值0.3操作成功");
    
    result = concord_metrics_histogram_observe(histogram, 1.5);
    TEST_ASSERT(result == 0, "直方图观察值1.5操作成功");
    
    result = concord_metrics_histogram_observe(histogram, 0.8);
    TEST_ASSERT(result == 0, "直方图观察值0.8操作成功");
    
    result = concord_metrics_histogram_observe(histogram, 3.2);
    TEST_ASSERT(result == 0, "直方图观察值3.2操作成功");
    
    // 检查计数
    int64_t count = concord_metrics_histogram_count(histogram);
    TEST_ASSERT(count == 4, "直方图计数为4");
    
    // 检查均值（近似）
    double mean = concord_metrics_histogram_mean(histogram);
    TEST_ASSERT(mean > 1.3 && mean < 1.6, "直方图均值在合理范围"); // (0.3+1.5+0.8+3.2)/4 = 1.45
    
    // 检查百分位数
    double p50 = concord_metrics_histogram_percentile(histogram, 0.5);
    TEST_ASSERT(p50 > 0.0, "50%百分位数有效");
    
    printf("直方图统计：计数=%ld, 均值=%.2f, P50=%.2f\n", (long)count, mean, p50);
}

// 测试计量表度量
void test_meter_metrics() {
    printf("\n=== 测试计量表度量 ===\n");
    
    if (!g_repo) {
        TEST_ASSERT(0, "需要先创建度量仓库");
        return;
    }
    
    // 创建计量表
    concord_metric_t *meter = concord_metrics_create_meter(g_repo, "test_meter", 
                                                          "Test meter description");
    TEST_ASSERT(meter != NULL, "创建计量表");
    
    // 检查计量表类型
    concord_metric_type_t type = concord_metrics_get_type(meter);
    TEST_ASSERT(type == CONCORD_METRIC_METER, "计量表类型正确");
    
    // 记录事件
    int result = concord_metrics_meter_mark(meter, 10);
    TEST_ASSERT(result == 0, "计量表记录事件操作成功");
    
    result = concord_metrics_meter_mark(meter, 20);
    TEST_ASSERT(result == 0, "计量表记录第二次事件操作成功");
    
    // 获取速率（注意：速率可能需要一些时间来计算）
    usleep(100000); // 100ms
    
    double rate_mean = concord_metrics_meter_rate_mean(meter);
    TEST_ASSERT(rate_mean >= 0.0, "平均速率非负");
    
    double rate1 = concord_metrics_meter_rate1(meter);
    TEST_ASSERT(rate1 >= 0.0, "1分钟速率非负");
    
    printf("计量表速率：平均=%.2f, 1分钟=%.2f\n", rate_mean, rate1);
}

// 测试指标查找
void test_metrics_find() {
    printf("\n=== 测试指标查找 ===\n");
    
    if (!g_repo) {
        TEST_ASSERT(0, "需要先创建度量仓库");
        return;
    }
    
    // 查找已存在的指标
    concord_metric_t *found_counter = concord_metrics_find(g_repo, "test_counter");
    TEST_ASSERT(found_counter != NULL, "查找计数器指标");
    
    concord_metric_t *found_gauge = concord_metrics_find(g_repo, "test_gauge");
    TEST_ASSERT(found_gauge != NULL, "查找测量仪指标");
    
    // 查找不存在的指标
    concord_metric_t *not_found = concord_metrics_find(g_repo, "nonexistent_metric");
    TEST_ASSERT(not_found == NULL, "查找不存在的指标返回NULL");
    
    // 检查总指标数量
    int total_count = concord_metrics_count(g_repo);
    TEST_ASSERT(total_count == 4, "仓库中有4个指标"); // counter, gauge, histogram, meter
}

// 回调函数用于foreach测试
static int foreach_counter = 0;
static void foreach_callback(concord_metric_t *metric, void *user_data) {
    (void)user_data; // 避免未使用参数警告
    foreach_counter++;
    const char *name = concord_metrics_get_name(metric);
    concord_metric_type_t type = concord_metrics_get_type(metric);
    printf("遍历指标: %s (类型: %d)\n", name, type);
}

// 测试指标遍历
void test_metrics_foreach() {
    printf("\n=== 测试指标遍历 ===\n");
    
    if (!g_repo) {
        TEST_ASSERT(0, "需要先创建度量仓库");
        return;
    }
    
    foreach_counter = 0;
    
    int result = concord_metrics_foreach(g_repo, foreach_callback, NULL);
    TEST_ASSERT(result == 0, "遍历指标操作成功");
    
    int expected_count = concord_metrics_count(g_repo);
    TEST_ASSERT(foreach_counter == expected_count, "遍历了所有指标");
    
    printf("遍历了 %d 个指标\n", foreach_counter);
}

// 测试度量导出
void test_metrics_export() {
    printf("\n=== 测试度量导出 ===\n");
    
    if (!g_repo) {
        TEST_ASSERT(0, "需要先创建度量仓库");
        return;
    }
    
    // 导出为文本格式
    char text_buffer[4096];
    int result = concord_metrics_dump(g_repo, CONCORD_METRICS_FORMAT_TEXT, 
                                     text_buffer, sizeof(text_buffer));
    TEST_ASSERT(result > 0, "导出文本格式");
    
    printf("文本格式导出示例（前200字符）:\n%.200s...\n", text_buffer);
    
    // 导出为JSON格式
    char json_buffer[4096];
    result = concord_metrics_dump(g_repo, CONCORD_METRICS_FORMAT_JSON, 
                                 json_buffer, sizeof(json_buffer));
    TEST_ASSERT(result > 0, "导出JSON格式");
    
    // 检查JSON格式
    int has_json_structure = strstr(json_buffer, "{") != NULL && strstr(json_buffer, "}") != NULL;
    TEST_ASSERT(has_json_structure, "JSON导出格式正确");
    
    printf("JSON格式导出示例（前200字符）:\n%.200s...\n", json_buffer);
    
    // 导出为Prometheus格式
    char prometheus_buffer[4096];
    result = concord_metrics_dump(g_repo, CONCORD_METRICS_FORMAT_PROMETHEUS, 
                                 prometheus_buffer, sizeof(prometheus_buffer));
    TEST_ASSERT(result > 0, "导出Prometheus格式");
    
    printf("Prometheus格式导出示例（前200字符）:\n%.200s...\n", prometheus_buffer);
}

// 测试HTTP服务器（简单测试）
void test_metrics_http_server() {
    printf("\n=== 测试度量HTTP服务器 ===\n");
    
    if (!g_repo) {
        TEST_ASSERT(0, "需要先创建度量仓库");
        return;
    }
    
    // 启动HTTP服务器（使用非标准端口避免冲突）
    int port = 18080;
    int result = concord_metrics_start_server(g_repo, "localhost", port, "/metrics");
    TEST_ASSERT(result == 0, "启动HTTP服务器");
    
    // 等待服务器启动
    usleep(100000); // 100ms
    
    // 简单检查端口是否在监听
    int test_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (test_socket >= 0) {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons(port);
        
        int connect_result = connect(test_socket, (struct sockaddr*)&addr, sizeof(addr));
        close(test_socket);
        
        TEST_ASSERT(connect_result == 0, "HTTP服务器端口可连接");
    } else {
        TEST_ASSERT(0, "无法创建测试socket");
    }
    
    // 停止HTTP服务器
    result = concord_metrics_stop_server(g_repo);
    TEST_ASSERT(result == 0, "停止HTTP服务器");
    
    // 等待服务器关闭
    usleep(100000); // 100ms
    
    printf("HTTP服务器测试完成\n");
}

// 测试度量仓库销毁
void test_metrics_repo_destroy() {
    printf("\n=== 测试度量仓库销毁 ===\n");
    
    if (g_repo) {
        concord_metrics_repo_destroy(g_repo);
        g_repo = NULL;
        printf("度量仓库已销毁\n");
        TEST_ASSERT(1, "度量仓库销毁完成");
    } else {
        TEST_ASSERT(0, "度量仓库不存在");
    }
}

// 主测试函数
int main() {
    printf("========================================\n");
    printf("ConcordKV Metrics 模块功能测试\n");
    printf("========================================\n");
    
    test_metrics_repo_create();
    test_counter_metrics();
    test_gauge_metrics();
    test_histogram_metrics();
    test_meter_metrics();
    test_metrics_find();
    test_metrics_foreach();
    test_metrics_export();
    test_metrics_http_server();
    test_metrics_repo_destroy();
    
    printf("\n========================================\n");
    printf("测试结果汇总\n");
    printf("========================================\n");
    printf("通过: %d\n", tests_passed);
    printf("失败: %d\n", tests_failed);
    printf("总计: %d\n", tests_passed + tests_failed);
    
    if (tests_failed == 0) {
        printf("\n🎉 所有测试通过！Metrics 模块功能正常\n");
        return 0;
    } else {
        printf("\n⚠️  有 %d 个测试失败，请检查代码\n", tests_failed);
        return 1;
    }
} 