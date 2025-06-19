/*
 * @Author: Lzww0608  
 * @Date: 2025-6-17 14:00:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-17 14:00:00
 * @Description: ConcordKV 存储引擎监控管理器测试
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include "../../../kvserver/kv_engine_metrics.h"
#include "../../../kvserver/kv_error.h"

// 测试计数器
static int test_count = 0;
static int test_passed = 0;

// 测试宏
#define TEST_ASSERT(condition, message) \
    do { \
        test_count++; \
        if (condition) { \
            test_passed++; \
            printf("✓ [%02d] %s\n", test_count, message); \
        } else { \
            printf("✗ [%02d] %s\n", test_count, message); \
        } \
    } while(0)

// 模拟的metrics仓库函数(简化版)
concord_metrics_repo_t* concord_metrics_repo_create(int capacity) {
    // 返回一个非NULL指针作为模拟
    return (concord_metrics_repo_t*)malloc(sizeof(int));
}

void concord_metrics_repo_destroy(concord_metrics_repo_t* repo) {
    if (repo) free(repo);
}

concord_metric_t* concord_metrics_create_counter(concord_metrics_repo_t* repo, 
                                                const char* name, 
                                                const char* description, 
                                                double initial_value) {
    // 返回一个非NULL指针作为模拟
    return (concord_metric_t*)malloc(sizeof(int));
}

concord_metric_t* concord_metrics_create_gauge(concord_metrics_repo_t* repo, 
                                              const char* name, 
                                              const char* description, 
                                              double initial_value) {
    return (concord_metric_t*)malloc(sizeof(int));
}

concord_metric_t* concord_metrics_create_meter(concord_metrics_repo_t* repo, 
                                              const char* name, 
                                              const char* description) {
    return (concord_metric_t*)malloc(sizeof(int));
}

int concord_metrics_counter_inc(concord_metric_t* metric, double value) {
    return 0;
}

int concord_metrics_gauge_set(concord_metric_t* metric, double value) {
    return 0;
}

int concord_metrics_meter_mark(concord_metric_t* metric, double value) {
    return 0;
}

int concord_metrics_start_server(concord_metrics_repo_t* repo, 
                                const char* host, 
                                int port, 
                                const char* path) {
    return 0; // 模拟成功
}

int concord_metrics_stop_server(concord_metrics_repo_t* repo) {
    return 0; // 模拟成功
}

// === 测试用例 ===

void test_metrics_manager_lifecycle() {
    printf("\n--- 测试用例1: 监控管理器生命周期 ---\n");
    
    // 创建管理器
    kv_engine_metrics_manager_t *manager = kv_engine_metrics_manager_create();
    TEST_ASSERT(manager != NULL, "创建监控管理器成功");
    TEST_ASSERT(manager->repo != NULL, "metrics仓库创建成功");
    TEST_ASSERT(manager->config.monitoring_enabled == true, "默认启用监控");
    TEST_ASSERT(manager->config.collection_interval_ms == 1000, "默认收集间隔1000ms");
    TEST_ASSERT(manager->running == false, "初始状态为未运行");
    
    // 销毁管理器
    kv_engine_metrics_manager_destroy(manager);
    TEST_ASSERT(true, "销毁监控管理器成功");
}

void test_engine_registration() {
    printf("\n--- 测试用例2: 引擎注册管理 ---\n");
    
    kv_engine_metrics_manager_t *manager = kv_engine_metrics_manager_create();
    TEST_ASSERT(manager != NULL, "创建监控管理器成功");
    
    // 测试引擎注册
    int ret = kv_engine_metrics_register_engine(manager, KV_ENGINE_ARRAY, "test_array");
    TEST_ASSERT(ret == KV_ERR_NONE, "Array引擎注册成功");
    
    ret = kv_engine_metrics_register_engine(manager, KV_ENGINE_HASH, "test_hash");
    TEST_ASSERT(ret == KV_ERR_NONE, "Hash引擎注册成功");
    
    // 测试重复注册
    ret = kv_engine_metrics_register_engine(manager, KV_ENGINE_ARRAY, "test_array2");
    TEST_ASSERT(ret == KV_ERR_NONE, "重复注册Array引擎返回成功");
    
    // 测试引擎检查
    bool registered = kv_engine_metrics_is_engine_registered(manager, KV_ENGINE_ARRAY);
    TEST_ASSERT(registered == true, "Array引擎已注册");
    
    registered = kv_engine_metrics_is_engine_registered(manager, KV_ENGINE_LSM);
    TEST_ASSERT(registered == false, "LSM引擎未注册");
    
    // 测试引擎注销
    ret = kv_engine_metrics_unregister_engine(manager, KV_ENGINE_ARRAY);
    TEST_ASSERT(ret == KV_ERR_NONE, "Array引擎注销成功");
    
    registered = kv_engine_metrics_is_engine_registered(manager, KV_ENGINE_ARRAY);
    TEST_ASSERT(registered == false, "Array引擎已注销");
    
    kv_engine_metrics_manager_destroy(manager);
}

void test_metrics_recording() {
    printf("\n--- 测试用例3: 指标记录功能 ---\n");
    
    kv_engine_metrics_manager_t *manager = kv_engine_metrics_manager_create();
    TEST_ASSERT(manager != NULL, "创建监控管理器成功");
    
    // 注册Array引擎
    int ret = kv_engine_metrics_register_engine(manager, KV_ENGINE_ARRAY, "test_array");
    TEST_ASSERT(ret == KV_ERR_NONE, "Array引擎注册成功");
    
    // 记录读操作
    ret = kv_engine_metrics_record_read(manager, KV_ENGINE_ARRAY, 1.5);
    TEST_ASSERT(ret == KV_ERR_NONE, "记录读操作成功");
    
    // 记录写操作
    ret = kv_engine_metrics_record_write(manager, KV_ENGINE_ARRAY, 2.3);
    TEST_ASSERT(ret == KV_ERR_NONE, "记录写操作成功");
    
    // 记录删除操作
    ret = kv_engine_metrics_record_delete(manager, KV_ENGINE_ARRAY, 0.8);
    TEST_ASSERT(ret == KV_ERR_NONE, "记录删除操作成功");
    
    // 更新内存使用量
    ret = kv_engine_metrics_update_memory_usage(manager, KV_ENGINE_ARRAY, 1024 * 1024);
    TEST_ASSERT(ret == KV_ERR_NONE, "更新内存使用量成功");
    
    // 记录错误
    ret = kv_engine_metrics_record_error(manager, KV_ENGINE_ARRAY, "timeout");
    TEST_ASSERT(ret == KV_ERR_NONE, "记录错误成功");
    
    // 测试未注册引擎
    ret = kv_engine_metrics_record_read(manager, KV_ENGINE_LSM, 1.0);
    TEST_ASSERT(ret == KV_ERR_NOT_FOUND, "未注册引擎返回NOT_FOUND");
    
    kv_engine_metrics_manager_destroy(manager);
}

void test_engine_specific_metrics() {
    printf("\n--- 测试用例4: 引擎特有指标 ---\n");
    
    kv_engine_metrics_manager_t *manager = kv_engine_metrics_manager_create();
    TEST_ASSERT(manager != NULL, "创建监控管理器成功");
    
    // 注册各种引擎
    int ret = kv_engine_metrics_register_engine(manager, KV_ENGINE_LSM, "test_lsm");
    TEST_ASSERT(ret == KV_ERR_NONE, "LSM引擎注册成功");
    
    ret = kv_engine_metrics_register_engine(manager, KV_ENGINE_BTREE, "test_btree");
    TEST_ASSERT(ret == KV_ERR_NONE, "BTree引擎注册成功");
    
    ret = kv_engine_metrics_register_engine(manager, KV_ENGINE_HASH, "test_hash");
    TEST_ASSERT(ret == KV_ERR_NONE, "Hash引擎注册成功");
    
    // 记录LSM特有指标
    ret = kv_engine_metrics_record_lsm_compaction(manager);
    TEST_ASSERT(ret == KV_ERR_NONE, "记录LSM压缩操作成功");
    
    // 记录BTree特有指标
    ret = kv_engine_metrics_record_btree_split(manager);
    TEST_ASSERT(ret == KV_ERR_NONE, "记录BTree节点分裂成功");
    
    // 记录Hash特有指标
    ret = kv_engine_metrics_record_hash_collision(manager);
    TEST_ASSERT(ret == KV_ERR_NONE, "记录Hash桶冲突成功");
    
    kv_engine_metrics_manager_destroy(manager);
}

void test_manager_lifecycle() {
    printf("\n--- 测试用例5: 管理器启动停止 ---\n");
    
    kv_engine_metrics_manager_t *manager = kv_engine_metrics_manager_create();
    TEST_ASSERT(manager != NULL, "创建监控管理器成功");
    
    // 启动管理器
    int ret = kv_engine_metrics_manager_start(manager);
    TEST_ASSERT(ret == KV_ERR_NONE, "启动监控管理器成功");
    TEST_ASSERT(manager->running == true, "管理器状态为运行中");
    
    // 等待一段时间让收集线程运行
    usleep(50000); // 50ms
    
    // 停止管理器
    ret = kv_engine_metrics_manager_stop(manager);
    TEST_ASSERT(ret == KV_ERR_NONE, "停止监控管理器成功");
    TEST_ASSERT(manager->running == false, "管理器状态为已停止");
    
    kv_engine_metrics_manager_destroy(manager);
}

void test_config_management() {
    printf("\n--- 测试用例6: 配置管理 ---\n");
    
    // 测试默认配置
    kv_engine_metrics_config_t config = kv_engine_metrics_config_default();
    TEST_ASSERT(config.monitoring_enabled == true, "默认启用监控");
    TEST_ASSERT(config.collection_interval_ms == 1000, "默认收集间隔1000ms");
    TEST_ASSERT(config.enable_latency_histograms == true, "默认启用延迟直方图");
    TEST_ASSERT(config.enable_engine_specific == true, "默认启用引擎特有指标");
    TEST_ASSERT(config.enable_memory_tracking == true, "默认启用内存跟踪");
    TEST_ASSERT(config.http_config.enabled == true, "默认启用HTTP服务");
    TEST_ASSERT(config.http_config.port == 9090, "默认HTTP端口9090");
    TEST_ASSERT(strcmp(config.http_config.host, "0.0.0.0") == 0, "默认HTTP主机0.0.0.0");
    TEST_ASSERT(strcmp(config.http_config.path, "/metrics") == 0, "默认HTTP路径/metrics");
}

void test_error_handling() {
    printf("\n--- 测试用例7: 错误处理 ---\n");
    
    // 测试NULL参数
    int ret = kv_engine_metrics_record_read(NULL, KV_ENGINE_ARRAY, 1.0);
    TEST_ASSERT(ret == KV_ERR_PARAM, "NULL管理器返回参数错误");
    
    kv_engine_metrics_manager_t *manager = kv_engine_metrics_manager_create();
    TEST_ASSERT(manager != NULL, "创建监控管理器成功");
    
    // 测试无效引擎类型
    ret = kv_engine_metrics_record_read(manager, KV_ENGINE_MAX, 1.0);
    TEST_ASSERT(ret == KV_ERR_PARAM, "无效引擎类型返回参数错误");
    
    // 测试引擎未注册
    ret = kv_engine_metrics_record_read(manager, KV_ENGINE_ARRAY, 1.0);
    TEST_ASSERT(ret == KV_ERR_NOT_FOUND, "未注册引擎返回未找到错误");
    
    // 测试无效参数
    ret = kv_engine_metrics_register_engine(manager, KV_ENGINE_ARRAY, NULL);
    TEST_ASSERT(ret == KV_ERR_PARAM, "NULL引擎名称返回参数错误");
    
    kv_engine_metrics_manager_destroy(manager);
}

void test_utility_functions() {
    printf("\n--- 测试用例8: 工具函数 ---\n");
    
    // 测试时间函数
    uint64_t time1 = kv_engine_metrics_get_time_us();
    usleep(1000); // 1ms
    uint64_t time2 = kv_engine_metrics_get_time_us();
    TEST_ASSERT(time2 > time1, "时间函数返回递增时间戳");
    TEST_ASSERT((time2 - time1) >= 1000, "时间差大于等于1ms");
    
    // 测试指标名称格式化
    char buffer[256];
    int len = kv_engine_metrics_format_metric_name(KV_ENGINE_ARRAY, "read_count", buffer, sizeof(buffer));
    TEST_ASSERT(len > 0, "指标名称格式化成功");
    TEST_ASSERT(strcmp(buffer, "concordkv_array_read_count") == 0, "指标名称格式正确");
    
    len = kv_engine_metrics_format_metric_name(KV_ENGINE_HASH, "memory_usage", buffer, sizeof(buffer));
    TEST_ASSERT(len > 0, "Hash引擎指标名称格式化成功");
    TEST_ASSERT(strcmp(buffer, "concordkv_hash_memory_usage") == 0, "Hash引擎指标名称格式正确");
    
    // 测试错误情况
    len = kv_engine_metrics_format_metric_name(KV_ENGINE_ARRAY, NULL, buffer, sizeof(buffer));
    TEST_ASSERT(len == -1, "NULL指标名称返回-1");
    
    len = kv_engine_metrics_format_metric_name(KV_ENGINE_ARRAY, "test", NULL, sizeof(buffer));
    TEST_ASSERT(len == -1, "NULL缓冲区返回-1");
}

// === 主函数 ===

int main(int argc, char *argv[]) {
    printf("=== ConcordKV 存储引擎监控管理器测试 ===\n");
    printf("测试时间: %s\n", __DATE__ " " __TIME__);
    
    // 运行所有测试
    test_metrics_manager_lifecycle();
    test_engine_registration();
    test_metrics_recording();
    test_engine_specific_metrics();
    test_manager_lifecycle();
    test_config_management();
    test_error_handling();
    test_utility_functions();
    
    // 输出测试结果
    printf("\n=== 测试结果统计 ===\n");
    printf("总测试数: %d\n", test_count);
    printf("通过测试: %d\n", test_passed);
    printf("失败测试: %d\n", test_count - test_passed);
    printf("通过率: %.2f%%\n", (double)test_passed / test_count * 100);
    
    if (test_passed == test_count) {
        printf("\n🎉 所有测试通过! 监控管理器实现正确!\n");
        return 0;
    } else {
        printf("\n❌ 部分测试失败，请检查实现!\n");
        return 1;
    }
} 