/**
 * @file testing_demo.c
 * @brief ConcordKV测试工具框架示例程序
 */

#include "testing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

// 运行标志
static volatile int g_running = 1;

// 信号处理函数
static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\n接收到中断信号(%d)，准备退出...\n", sig);
        g_running = 0;
    }
}

// 演示模拟客户端功能
static void demo_mock_client(void) {
    printf("\n========================================\n");
    printf("演示模拟客户端功能\n");
    printf("========================================\n");
    
    // 创建配置
    concord_mock_client_config_t config = concord_mock_client_default_config();
    config.concurrent_clients = 5;
    config.test_duration_ms = 10000;  // 10秒
    config.read_ratio = 0.8;          // 80%读操作
    config.write_ratio = 0.2;         // 20%写操作
    
    printf("配置参数:\n");
    printf("  并发客户端数: %d\n", config.concurrent_clients);
    printf("  测试时长: %llu ms\n", config.test_duration_ms);
    printf("  读操作比例: %.1f%%\n", config.read_ratio * 100);
    printf("  写操作比例: %.1f%%\n", config.write_ratio * 100);
    printf("  键大小: %zu bytes\n", config.key_size);
    printf("  值大小: %zu bytes\n", config.value_size);
    
    // 创建模拟客户端
    concord_mock_client_t *client = concord_mock_client_create(&config);
    if (!client) {
        printf("创建模拟客户端失败\n");
        return;
    }
    
    // 启动测试
    if (concord_mock_client_start(client) != 0) {
        printf("启动模拟客户端失败\n");
        concord_mock_client_destroy(client);
        return;
    }
    
    // 等待测试完成
    while (g_running) {
        sleep(1);
        
        // 获取实时统计
        concord_test_stats_t *stats = concord_mock_client_get_stats(client);
        if (stats && stats->total_operations > 0) {
            printf("实时统计 - 总操作: %llu, 成功: %llu, 失败: %llu, 平均延迟: %.2f ms\n",
                   (unsigned long long)stats->total_operations,
                   (unsigned long long)stats->successful_ops,
                   (unsigned long long)stats->failed_ops,
                   stats->avg_latency_ms);
        }
        
        // 检查是否测试完成
        if (stats && stats->total_time_ms >= config.test_duration_ms) {
            break;
        }
    }
    
    // 停止测试
    concord_mock_client_stop(client);
    
    // 输出最终统计
    concord_test_stats_t *final_stats = concord_mock_client_get_stats(client);
    if (final_stats) {
        printf("\n最终测试统计:\n");
        printf("  总操作数: %llu\n", (unsigned long long)final_stats->total_operations);
        printf("  成功操作数: %llu\n", (unsigned long long)final_stats->successful_ops);
        printf("  失败操作数: %llu\n", (unsigned long long)final_stats->failed_ops);
        printf("  总耗时: %llu ms\n", (unsigned long long)final_stats->total_time_ms);
        printf("  平均延迟: %.3f ms\n", final_stats->avg_latency_ms);
        printf("  P95延迟: %.3f ms\n", final_stats->p95_latency_ms);
        printf("  P99延迟: %.3f ms\n", final_stats->p99_latency_ms);
        printf("  吞吐量: %.2f ops/sec\n", final_stats->throughput_ops_sec);
    }
    
    // 清理
    concord_mock_client_destroy(client);
}

// 演示故障注入功能
static void demo_fault_injection(void) {
    printf("\n========================================\n");
    printf("演示故障注入功能\n");
    printf("========================================\n");
    
    // 创建故障注入器
    concord_fault_injector_t *injector = concord_fault_injector_create();
    if (!injector) {
        printf("创建故障注入器失败\n");
        return;
    }
    
    // 添加网络延迟故障
    concord_fault_config_t network_delay = {
        .type = CONCORD_FAULT_NETWORK_DELAY,
        .probability = 0.3,        // 30%概率
        .duration_ms = 5000,       // 持续5秒
        .severity = 5,             // 中等严重程度
        .target = strdup("network")
    };
    concord_fault_injector_add_fault(injector, &network_delay);
    
    // 添加CPU尖峰故障
    concord_fault_config_t cpu_spike = {
        .type = CONCORD_FAULT_CPU_SPIKE,
        .probability = 0.2,        // 20%概率
        .duration_ms = 3000,       // 持续3秒
        .severity = 3,             // 轻度
        .target = strdup("cpu")
    };
    concord_fault_injector_add_fault(injector, &cpu_spike);
    
    // 启动故障注入
    if (concord_fault_injector_start(injector) != 0) {
        printf("启动故障注入器失败\n");
        concord_fault_injector_destroy(injector);
        return;
    }
    
    // 运行10秒，观察故障注入效果
    printf("故障注入运行中，持续10秒...\n");
    for (int i = 0; i < 10 && g_running; i++) {
        sleep(1);
        
        // 模拟检查是否应该注入故障
        if (concord_fault_injector_should_inject(injector, "network", CONCORD_FAULT_NETWORK_DELAY)) {
            printf("检测到网络延迟故障应该被注入\n");
        }
        
        if (concord_fault_injector_should_inject(injector, "cpu", CONCORD_FAULT_CPU_SPIKE)) {
            printf("检测到CPU尖峰故障应该被注入\n");
        }
    }
    
    // 停止故障注入
    concord_fault_injector_stop(injector);
    
    // 清理
    free(network_delay.target);
    free(cpu_spike.target);
    concord_fault_injector_destroy(injector);
}

// 演示基准测试功能
static void demo_benchmark(void) {
    printf("\n========================================\n");
    printf("演示基准测试功能\n");
    printf("========================================\n");
    
    // 创建基准测试
    concord_benchmark_t *benchmark = concord_benchmark_create("KV操作基准测试");
    if (!benchmark) {
        printf("创建基准测试失败\n");
        return;
    }
    
    // 开始测试
    concord_benchmark_start(benchmark);
    
    printf("执行1000个模拟操作...\n");
    
    // 模拟1000个操作
    for (int i = 0; i < 1000 && g_running; i++) {
        uint64_t start_time = concord_benchmark_op_start(benchmark);
        
        // 模拟操作延迟 (1-20ms)
        usleep((rand() % 20 + 1) * 1000);
        
        // 模拟95%成功率
        int success = (rand() % 100) < 95;
        
        concord_benchmark_op_end(benchmark, start_time, success);
        
        // 每100个操作显示进度
        if ((i + 1) % 100 == 0) {
            printf("已完成: %d/1000 操作\n", i + 1);
        }
    }
    
    // 结束测试
    concord_benchmark_stop(benchmark);
    
    // 打印详细报告
    concord_benchmark_print_report(benchmark);
    
    // 清理
    concord_benchmark_destroy(benchmark);
}

// 演示测试数据生成功能
static void demo_test_data_generation(void) {
    printf("\n========================================\n");
    printf("演示测试数据生成功能\n");
    printf("========================================\n");
    
    // 创建配置
    concord_test_data_config_t config = concord_test_data_default_config();
    config.total_keys = 1000;         // 生成1000个键值对
    config.key_min_size = 10;
    config.key_max_size = 20;
    config.value_min_size = 50;
    config.value_max_size = 100;
    config.key_pattern = "user_#_x";  // 模式：user_[索引]_[随机字符]
    config.use_zipf = 1;              // 使用Zipf分布
    config.zipf_s = 1.2;              // Zipf参数
    
    printf("数据生成配置:\n");
    printf("  键数量: %zu\n", config.total_keys);
    printf("  键大小: %zu - %zu\n", config.key_min_size, config.key_max_size);
    printf("  值大小: %zu - %zu\n", config.value_min_size, config.value_max_size);
    printf("  键模式: %s\n", config.key_pattern ? config.key_pattern : "随机");
    printf("  Zipf分布: %s (s=%.2f)\n", config.use_zipf ? "是" : "否", config.zipf_s);
    
    // 创建数据生成器
    concord_test_data_gen_t *generator = concord_test_data_gen_create(&config);
    if (!generator) {
        printf("创建测试数据生成器失败\n");
        return;
    }
    
    // 生成几个示例键值对
    printf("\n生成的示例键值对:\n");
    for (int i = 0; i < 10; i++) {
        char *key, *value;
        if (concord_test_data_gen_kv_pair(generator, &key, &value) == 0) {
            printf("  %d: %s = %s\n", i + 1, key, value);
            free(key);
            free(value);
        }
    }
    
    // 生成数据集文件
    const char *dataset_file = "/tmp/concord_test_dataset.txt";
    printf("\n生成测试数据集文件: %s\n", dataset_file);
    
    if (concord_test_data_gen_dataset(generator, dataset_file) == 0) {
        printf("数据集生成成功\n");
        
        // 测试加载数据集
        char **keys, **values;
        size_t count;
        
        if (concord_test_data_load_dataset(dataset_file, &keys, &values, &count) == 0) {
            printf("成功加载 %zu 个键值对\n", count);
            
            // 显示前5个键值对
            printf("前5个键值对:\n");
            for (size_t i = 0; i < 5 && i < count; i++) {
                printf("  %zu: %s = %s\n", i + 1, keys[i], values[i]);
            }
            
            // 清理加载的数据
            for (size_t i = 0; i < count; i++) {
                free(keys[i]);
                free(values[i]);
            }
            free(keys);
            free(values);
        }
    }
    
    // 清理
    concord_test_data_gen_destroy(generator);
}

// 主函数
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    // 注册信号处理函数
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 设置随机数种子
    srand(time(NULL));
    
    printf("ConcordKV测试工具框架演示程序\n");
    printf("按Ctrl+C退出\n");
    
    // 初始化测试框架
    if (concord_testing_init() != 0) {
        printf("初始化测试框架失败\n");
        return 1;
    }
    
    // 运行各个功能演示
    if (g_running) demo_test_data_generation();
    if (g_running) demo_benchmark();
    if (g_running) demo_fault_injection();
    if (g_running) demo_mock_client();
    
    printf("\n所有演示完成\n");
    
    // 清理测试框架
    concord_testing_cleanup();
    
    return 0;
}

// 测试框架初始化和清理的简单实现
int concord_testing_init(void) {
    printf("测试框架初始化完成\n");
    return 0;
}

void concord_testing_cleanup(void) {
    printf("测试框架清理完成\n");
} 