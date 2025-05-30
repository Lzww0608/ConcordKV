/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV testing framework - testing.h
 */
/**
 * @file testing.h
 * @brief ConcordKV测试工具框架
 */

#ifndef __CONCORD_TESTING_H__
#define __CONCORD_TESTING_H__

#include <stdint.h>
#include <stddef.h>
#include <time.h>

// 测试结果状态
typedef enum {
    CONCORD_TEST_PASS = 0,
    CONCORD_TEST_FAIL,
    CONCORD_TEST_SKIP,
    CONCORD_TEST_TIMEOUT
} concord_test_result_t;

// 性能测试统计
typedef struct {
    uint64_t total_operations;    // 总操作数
    uint64_t successful_ops;      // 成功操作数
    uint64_t failed_ops;          // 失败操作数
    uint64_t total_time_ms;       // 总耗时(毫秒)
    double avg_latency_ms;        // 平均延迟(毫秒)
    double min_latency_ms;        // 最小延迟
    double max_latency_ms;        // 最大延迟
    double p95_latency_ms;        // 95%分位延迟
    double p99_latency_ms;        // 99%分位延迟
    double throughput_ops_sec;    // 吞吐量(ops/sec)
} concord_test_stats_t;

// 故障类型
typedef enum {
    CONCORD_FAULT_NETWORK_DELAY = 1,      // 网络延迟
    CONCORD_FAULT_NETWORK_DROP = 2,       // 网络丢包
    CONCORD_FAULT_MEMORY_LEAK = 4,        // 内存泄漏
    CONCORD_FAULT_CPU_SPIKE = 8,          // CPU尖峰
    CONCORD_FAULT_DISK_FULL = 16,         // 磁盘满
    CONCORD_FAULT_CONNECTION_RESET = 32,  // 连接重置
    CONCORD_FAULT_TIMEOUT = 64            // 超时
} concord_fault_type_t;

// 故障注入配置
typedef struct {
    concord_fault_type_t type;    // 故障类型
    double probability;           // 故障概率(0.0-1.0)
    uint64_t duration_ms;         // 持续时间(毫秒)
    int severity;                 // 严重程度(1-10)
    char *target;                 // 目标组件
} concord_fault_config_t;

// 模拟客户端配置
typedef struct {
    char *server_host;            // 服务器地址
    int server_port;              // 服务器端口
    int concurrent_clients;       // 并发客户端数
    uint64_t test_duration_ms;    // 测试持续时间
    int operations_per_client;    // 每客户端操作数
    double read_ratio;            // 读操作比例
    double write_ratio;           // 写操作比例
    size_t key_size;              // 键大小
    size_t value_size;            // 值大小
} concord_mock_client_config_t;

// 测试数据生成器配置
typedef struct {
    size_t total_keys;            // 总键数
    size_t key_min_size;          // 键最小大小
    size_t key_max_size;          // 键最大大小
    size_t value_min_size;        // 值最小大小
    size_t value_max_size;        // 值最大大小
    char *key_pattern;            // 键模式
    char *value_pattern;          // 值模式
    int use_zipf;                 // 是否使用Zipf分布
    double zipf_s;                // Zipf参数s
} concord_test_data_config_t;

// 前向声明
typedef struct concord_mock_client_t concord_mock_client_t;
typedef struct concord_fault_injector_t concord_fault_injector_t;
typedef struct concord_benchmark_t concord_benchmark_t;
typedef struct concord_test_data_gen_t concord_test_data_gen_t;

/* === 模拟客户端API === */

/**
 * @brief 创建模拟客户端
 */
concord_mock_client_t *concord_mock_client_create(concord_mock_client_config_t *config);

/**
 * @brief 销毁模拟客户端
 */
void concord_mock_client_destroy(concord_mock_client_t *client);

/**
 * @brief 启动模拟客户端测试
 */
int concord_mock_client_start(concord_mock_client_t *client);

/**
 * @brief 停止模拟客户端测试
 */
int concord_mock_client_stop(concord_mock_client_t *client);

/**
 * @brief 获取测试统计
 */
concord_test_stats_t *concord_mock_client_get_stats(concord_mock_client_t *client);

/* === 故障注入API === */

/**
 * @brief 创建故障注入器
 */
concord_fault_injector_t *concord_fault_injector_create(void);

/**
 * @brief 销毁故障注入器
 */
void concord_fault_injector_destroy(concord_fault_injector_t *injector);

/**
 * @brief 添加故障配置
 */
int concord_fault_injector_add_fault(concord_fault_injector_t *injector, concord_fault_config_t *config);

/**
 * @brief 启动故障注入
 */
int concord_fault_injector_start(concord_fault_injector_t *injector);

/**
 * @brief 停止故障注入
 */
int concord_fault_injector_stop(concord_fault_injector_t *injector);

/**
 * @brief 检查是否应该注入故障
 */
int concord_fault_injector_should_inject(concord_fault_injector_t *injector, const char *target, concord_fault_type_t type);

/* === 性能基准测试API === */

/**
 * @brief 创建基准测试
 */
concord_benchmark_t *concord_benchmark_create(const char *name);

/**
 * @brief 销毁基准测试
 */
void concord_benchmark_destroy(concord_benchmark_t *benchmark);

/**
 * @brief 开始测试
 */
int concord_benchmark_start(concord_benchmark_t *benchmark);

/**
 * @brief 结束测试
 */
int concord_benchmark_stop(concord_benchmark_t *benchmark);

/**
 * @brief 记录操作开始
 */
uint64_t concord_benchmark_op_start(concord_benchmark_t *benchmark);

/**
 * @brief 记录操作结束
 */
int concord_benchmark_op_end(concord_benchmark_t *benchmark, uint64_t start_time, int success);

/**
 * @brief 获取测试统计
 */
concord_test_stats_t *concord_benchmark_get_stats(concord_benchmark_t *benchmark);

/**
 * @brief 打印测试报告
 */
void concord_benchmark_print_report(concord_benchmark_t *benchmark);

/* === 测试数据生成API === */

/**
 * @brief 创建测试数据生成器
 */
concord_test_data_gen_t *concord_test_data_gen_create(concord_test_data_config_t *config);

/**
 * @brief 销毁测试数据生成器
 */
void concord_test_data_gen_destroy(concord_test_data_gen_t *generator);

/**
 * @brief 生成随机键
 */
char *concord_test_data_gen_key(concord_test_data_gen_t *generator);

/**
 * @brief 生成随机值
 */
char *concord_test_data_gen_value(concord_test_data_gen_t *generator);

/**
 * @brief 生成键值对
 */
int concord_test_data_gen_kv_pair(concord_test_data_gen_t *generator, char **key, char **value);

/**
 * @brief 生成测试数据集
 */
int concord_test_data_gen_dataset(concord_test_data_gen_t *generator, const char *output_file);

/**
 * @brief 加载测试数据集
 */
int concord_test_data_load_dataset(const char *input_file, char ***keys, char ***values, size_t *count);

/* === 辅助函数 === */

/**
 * @brief 创建默认模拟客户端配置
 */
concord_mock_client_config_t concord_mock_client_default_config(void);

/**
 * @brief 创建默认测试数据配置
 */
concord_test_data_config_t concord_test_data_default_config(void);

/**
 * @brief 初始化测试框架
 */
int concord_testing_init(void);

/**
 * @brief 清理测试框架
 */
void concord_testing_cleanup(void);

#endif /* __CONCORD_TESTING_H__ */ 