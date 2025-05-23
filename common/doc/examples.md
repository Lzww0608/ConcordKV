# ConcordKV Common 使用示例

本文档提供了ConcordKV Common模块的详细使用示例和最佳实践。

## 目录

- [快速开始](#快速开始)
- [配置管理示例](#配置管理示例)
- [通用工具示例](#通用工具示例)
- [监控指标示例](#监控指标示例)
- [测试工具示例](#测试工具示例)
- [完整应用示例](#完整应用示例)
- [最佳实践](#最佳实践)

## 快速开始

### 环境准备

```bash
# 编译依赖
sudo apt-get install build-essential libyaml-dev

# 克隆代码
git clone https://github.com/your-org/ConcordKV.git
cd ConcordKV/common

# 编译各个模块
make -C config
make -C util
make -C metrics
make -C testing
```

### 基本使用

```c
#include "config/config.h"
#include "util/util.h"
#include "metrics/metrics.h"

int main() {
    // 加载配置
    concord_config_t *cfg = concord_config_load("app.yaml");
    
    // 创建指标仓库
    concord_metrics_repo_t *metrics = concord_metrics_repo_create(64);
    
    // 创建计数器指标
    concord_metric_t *requests = concord_metrics_create_counter(
        metrics, "requests_total", "处理的请求总数", 0);
    
    // 使用指标
    concord_metrics_counter_inc(requests, 1);
    
    // 清理资源
    concord_config_destroy(cfg);
    concord_metrics_repo_destroy(metrics);
    
    return 0;
}
```

## 配置管理示例

### 1. 基础配置操作

```c
#include "config/config.h"

void basic_config_example() {
    // 创建配置对象
    concord_config_t *cfg = concord_config_init();
    
    // 设置配置值
    concord_config_set_string(cfg, "server.host", "0.0.0.0");
    concord_config_set_int(cfg, "server.port", 8080);
    concord_config_set_bool(cfg, "server.debug", 1);
    concord_config_set_float(cfg, "server.timeout", 30.5);
    
    // 获取配置值（带默认值）
    const char *host = concord_config_get_string(cfg, "server.host", "localhost");
    int port = concord_config_get_int(cfg, "server.port", 8000);
    int debug = concord_config_get_bool(cfg, "server.debug", 0);
    double timeout = concord_config_get_float(cfg, "server.timeout", 30.0);
    
    printf("服务器配置:\n");
    printf("  主机: %s\n", host);
    printf("  端口: %d\n", port);
    printf("  调试: %s\n", debug ? "是" : "否");
    printf("  超时: %.1f秒\n", timeout);
    
    // 保存配置到文件
    concord_config_save(cfg, "server.yaml");
    
    concord_config_destroy(cfg);
}
```

### 2. 配置文件加载

```yaml
# server.yaml
server:
  host: "0.0.0.0"
  port: 8080
  workers: 4
  debug: true
  
database:
  url: "postgresql://user:pass@localhost/db"
  pool_size: 10
  timeout: 30.0
  
logging:
  level: "info"
  file: "/var/log/app.log"
  max_size: "100MB"
```

```c
void config_file_example() {
    // 从文件加载配置
    concord_config_t *cfg = concord_config_load("server.yaml");
    if (!cfg) {
        fprintf(stderr, "配置文件加载失败\n");
        return;
    }
    
    // 读取嵌套配置
    const char *db_url = concord_config_get_string(cfg, "database.url", NULL);
    int pool_size = concord_config_get_int(cfg, "database.pool_size", 5);
    
    // 读取数组配置（如果支持）
    int workers = concord_config_get_int(cfg, "server.workers", 1);
    
    printf("数据库配置:\n");
    printf("  URL: %s\n", db_url ? db_url : "未配置");
    printf("  连接池大小: %d\n", pool_size);
    printf("  工作线程数: %d\n", workers);
    
    concord_config_destroy(cfg);
}
```

### 3. 配置热更新

```c
// 配置变更回调函数
void on_config_changed(const char *key, const void *old_value, const void *new_value, void *user_data) {
    printf("配置项 '%s' 已更新\n", key);
    // 重新初始化相关组件
}

void config_hot_reload_example() {
    concord_config_t *cfg = concord_config_load("server.yaml");
    
    // 注册配置变更监听器
    concord_config_watch(cfg, "server.port", on_config_changed, NULL);
    concord_config_watch(cfg, "server.workers", on_config_changed, NULL);
    
    // 启动配置监控线程
    concord_config_start_monitor(cfg);
    
    // 应用运行...
    
    // 停止监控
    concord_config_stop_monitor(cfg);
    concord_config_destroy(cfg);
}
```

## 通用工具示例

### 1. 字符串操作

```c
#include "util/util.h"

void string_util_example() {
    char *text = concord_util_strdup("  Hello, World!  ");
    
    // 去除空白字符
    char *trimmed = concord_util_trim(text);
    printf("原文: '%s'\n", text);
    printf("去空白: '%s'\n", trimmed);
    
    // 字符串分割
    char data[] = "apple,banana,orange";
    char *parts[10];
    int count = concord_util_split(data, ',', parts, 10);
    
    printf("分割结果 (%d 个):\n", count);
    for (int i = 0; i < count; i++) {
        printf("  %d: %s\n", i, parts[i]);
    }
    
    // 字符串连接
    char *joined = concord_util_join(parts, count, " | ");
    printf("连接结果: %s\n", joined);
    
    // 字符串替换
    char *replaced = concord_util_replace("Hello World", "World", "ConcordKV");
    printf("替换结果: %s\n", replaced);
    
    // 清理内存
    free(text);
    free(joined);
    free(replaced);
}
```

### 2. 文件操作

```c
void file_util_example() {
    const char *filename = "/tmp/test.txt";
    const char *content = "Hello, ConcordKV!\n这是测试内容。";
    
    // 写入文件
    if (concord_util_write_file(filename, content, strlen(content)) == 0) {
        printf("文件写入成功\n");
    }
    
    // 检查文件是否存在
    if (concord_util_file_exists(filename)) {
        printf("文件存在\n");
        
        // 读取文件
        size_t size;
        char *data = concord_util_read_file(filename, &size);
        if (data) {
            printf("文件内容 (%zu 字节):\n%s\n", size, data);
            free(data);
        }
    }
    
    // 创建目录
    const char *dir = "/tmp/concord_test";
    if (concord_util_create_dir(dir, 0755) == 0) {
        printf("目录创建成功: %s\n", dir);
    }
    
    // 复制文件
    const char *dest = "/tmp/concord_test/test_copy.txt";
    if (concord_util_copy_file(filename, dest) == 0) {
        printf("文件复制成功: %s -> %s\n", filename, dest);
    }
}
```

### 3. 网络工具

```c
#include "util/network.h"

void network_util_example() {
    // HTTP GET 请求
    const char *headers[] = {
        "User-Agent: ConcordKV/1.0",
        "Accept: application/json"
    };
    
    concord_http_response_t *response = concord_net_http_get(
        "https://api.github.com/repos/torvalds/linux", 
        headers, 2
    );
    
    if (response) {
        printf("HTTP状态码: %d\n", response->status_code);
        printf("响应大小: %zu 字节\n", response->content_length);
        printf("响应内容预览: %.100s...\n", response->content);
        
        concord_net_http_response_free(response);
    }
    
    // TCP套接字示例
    concord_socket_t *sock = concord_net_socket_create(CONCORD_SOCKET_TCP);
    if (sock) {
        if (concord_net_socket_connect(sock, "www.google.com", 80) == 0) {
            printf("连接成功\n");
            
            const char *request = "GET / HTTP/1.1\r\nHost: www.google.com\r\n\r\n";
            concord_net_socket_send(sock, request, strlen(request));
            
            char buffer[1024];
            int received = concord_net_socket_recv(sock, buffer, sizeof(buffer) - 1);
            if (received > 0) {
                buffer[received] = '\0';
                printf("响应预览: %.200s...\n", buffer);
            }
        }
        concord_net_socket_destroy(sock);
    }
}
```

### 4. 日志系统

```c
#include "util/log.h"

void logging_example() {
    // 配置日志系统
    concord_util_log_config_t config = {
        .level = CONCORD_LOG_INFO,
        .output = CONCORD_LOG_OUTPUT_BOTH,  // 控制台和文件
        .file_path = "/tmp/concord.log",
        .max_file_size = 10 * 1024 * 1024,  // 10MB
        .rotate_count = 5
    };
    
    concord_util_log_init(&config);
    
    // 使用日志
    concord_util_debug("这是调试信息: %d", 123);
    concord_util_info("应用启动，版本: %s", "1.0.0");
    concord_util_warn("这是警告信息");
    concord_util_error("这是错误信息: %s", "连接失败");
    
    // 结构化日志
    concord_util_log_structured(CONCORD_LOG_INFO, 
        "user_id", "12345",
        "action", "login",
        "ip", "192.168.1.100",
        "duration_ms", "150",
        NULL);
    
    concord_util_log_cleanup();
}
```

## 监控指标示例

### 1. 基础指标使用

```c
#include "metrics/metrics.h"

void basic_metrics_example() {
    // 创建指标仓库
    concord_metrics_repo_t *repo = concord_metrics_repo_create(128);
    
    // 创建各种类型的指标
    concord_metric_t *request_count = concord_metrics_create_counter(
        repo, "http_requests_total", "HTTP请求总数", 0);
    
    concord_metric_t *active_connections = concord_metrics_create_gauge(
        repo, "active_connections", "当前活跃连接数", 0.0);
    
    concord_histogram_config_t hist_config = {
        .min = 0.0,
        .max = 1000.0,
        .bucket_count = 20
    };
    concord_metric_t *request_latency = concord_metrics_create_histogram(
        repo, "http_request_duration_ms", "HTTP请求延迟分布", &hist_config);
    
    concord_metric_t *request_rate = concord_metrics_create_meter(
        repo, "http_request_rate", "HTTP请求速率");
    
    // 模拟使用指标
    for (int i = 0; i < 1000; i++) {
        // 增加请求计数
        concord_metrics_counter_inc(request_count, 1);
        
        // 更新活跃连接数
        concord_metrics_gauge_set(active_connections, 50 + (i % 20));
        
        // 记录请求延迟
        double latency = 10.0 + (rand() % 100);
        concord_metrics_histogram_observe(request_latency, latency);
        
        // 标记速率事件
        concord_metrics_meter_mark(request_rate, 1);
        
        usleep(1000);  // 1ms
    }
    
    // 获取指标值
    printf("指标统计:\n");
    printf("  请求总数: %lld\n", concord_metrics_counter_get(request_count));
    printf("  活跃连接: %.0f\n", concord_metrics_gauge_get(active_connections));
    printf("  平均延迟: %.2f ms\n", concord_metrics_histogram_mean(request_latency));
    printf("  P95延迟: %.2f ms\n", concord_metrics_histogram_percentile(request_latency, 0.95));
    printf("  请求速率: %.2f req/sec\n", concord_metrics_meter_rate(request_rate));
    
    concord_metrics_repo_destroy(repo);
}
```

### 2. 指标HTTP服务器

```c
void metrics_server_example() {
    concord_metrics_repo_t *repo = concord_metrics_repo_create(64);
    
    // 创建一些示例指标
    concord_metric_t *cpu_usage = concord_metrics_create_gauge(
        repo, "cpu_usage_percent", "CPU使用率", 0.0);
    
    concord_metric_t *memory_usage = concord_metrics_create_gauge(
        repo, "memory_usage_bytes", "内存使用量", 0.0);
    
    // 启动HTTP服务器
    printf("启动指标HTTP服务器 http://localhost:8080/metrics\n");
    if (concord_metrics_start_server(repo, "0.0.0.0", 8080, "/metrics") == 0) {
        printf("指标服务器启动成功\n");
        
        // 模拟更新指标
        for (int i = 0; i < 60; i++) {
            double cpu = 20.0 + sin(i * 0.1) * 30.0;
            double memory = 1024 * 1024 * 1024 + sin(i * 0.05) * 512 * 1024 * 1024;
            
            concord_metrics_gauge_set(cpu_usage, cpu);
            concord_metrics_gauge_set(memory_usage, memory);
            
            printf("更新指标: CPU=%.1f%%, Memory=%.1fMB\n", 
                   cpu, memory / (1024 * 1024));
            
            sleep(1);
        }
        
        concord_metrics_stop_server(repo);
    } else {
        printf("指标服务器启动失败\n");
    }
    
    concord_metrics_repo_destroy(repo);
}
```

### 3. 自定义指标导出

```c
void custom_metrics_export() {
    concord_metrics_repo_t *repo = concord_metrics_repo_create(32);
    
    // 添加一些指标
    concord_metric_t *errors = concord_metrics_create_counter(
        repo, "errors_total", "错误总数", 0);
    concord_metrics_counter_inc(errors, 42);
    
    // 导出为不同格式
    char buffer[8192];
    
    // 文本格式
    if (concord_metrics_dump(repo, CONCORD_METRICS_FORMAT_TEXT, 
                           buffer, sizeof(buffer)) == 0) {
        printf("文本格式:\n%s\n", buffer);
    }
    
    // JSON格式
    if (concord_metrics_dump(repo, CONCORD_METRICS_FORMAT_JSON, 
                           buffer, sizeof(buffer)) == 0) {
        printf("JSON格式:\n%s\n", buffer);
    }
    
    // Prometheus格式
    if (concord_metrics_dump(repo, CONCORD_METRICS_FORMAT_PROMETHEUS, 
                           buffer, sizeof(buffer)) == 0) {
        printf("Prometheus格式:\n%s\n", buffer);
    }
    
    concord_metrics_repo_destroy(repo);
}
```

## 测试工具示例

### 1. 模拟客户端测试

```c
#include "testing/testing.h"

void mock_client_example() {
    // 配置模拟客户端
    concord_mock_client_config_t config = {
        .server_host = "localhost",
        .server_port = 8080,
        .concurrent_clients = 10,
        .test_duration_ms = 30000,  // 30秒
        .read_ratio = 0.7,          // 70% 读操作
        .write_ratio = 0.3,         // 30% 写操作
        .key_size = 16,
        .value_size = 64
    };
    
    // 创建模拟客户端
    concord_mock_client_t *client = concord_mock_client_create(&config);
    if (!client) {
        printf("创建模拟客户端失败\n");
        return;
    }
    
    printf("开始负载测试...\n");
    
    // 启动测试
    if (concord_mock_client_start(client) != 0) {
        printf("启动测试失败\n");
        concord_mock_client_destroy(client);
        return;
    }
    
    // 监控测试进度
    for (int i = 0; i < 30; i++) {
        sleep(1);
        
        concord_test_stats_t *stats = concord_mock_client_get_stats(client);
        if (stats) {
            printf("进度 %d/30s - 操作: %llu, 成功率: %.1f%%, 延迟: %.2f ms\n",
                   i + 1,
                   (unsigned long long)stats->total_operations,
                   stats->total_operations > 0 ? 
                       (double)stats->successful_ops / stats->total_operations * 100.0 : 0.0,
                   stats->avg_latency_ms);
        }
    }
    
    // 停止测试
    concord_mock_client_stop(client);
    
    // 获取最终统计
    concord_test_stats_t *final_stats = concord_mock_client_get_stats(client);
    if (final_stats) {
        printf("\n最终测试结果:\n");
        printf("  总操作数: %llu\n", (unsigned long long)final_stats->total_operations);
        printf("  成功操作: %llu\n", (unsigned long long)final_stats->successful_ops);
        printf("  失败操作: %llu\n", (unsigned long long)final_stats->failed_ops);
        printf("  成功率: %.2f%%\n", 
               (double)final_stats->successful_ops / final_stats->total_operations * 100.0);
        printf("  平均延迟: %.3f ms\n", final_stats->avg_latency_ms);
        printf("  P95延迟: %.3f ms\n", final_stats->p95_latency_ms);
        printf("  P99延迟: %.3f ms\n", final_stats->p99_latency_ms);
        printf("  吞吐量: %.2f ops/sec\n", final_stats->throughput_ops_sec);
    }
    
    concord_mock_client_destroy(client);
}
```

### 2. 故障注入测试

```c
void fault_injection_example() {
    // 创建故障注入器
    concord_fault_injector_t *injector = concord_fault_injector_create();
    
    // 配置网络延迟故障
    concord_fault_config_t network_fault = {
        .type = CONCORD_FAULT_NETWORK_DELAY,
        .probability = 0.2,        // 20% 概率
        .duration_ms = 10000,      // 持续10秒
        .severity = 5,             // 中等强度
        .target = strdup("network")
    };
    concord_fault_injector_add_fault(injector, &network_fault);
    
    // 配置CPU尖峰故障
    concord_fault_config_t cpu_fault = {
        .type = CONCORD_FAULT_CPU_SPIKE,
        .probability = 0.1,        // 10% 概率
        .duration_ms = 5000,       // 持续5秒
        .severity = 8,             // 高强度
        .target = strdup("cpu")
    };
    concord_fault_injector_add_fault(injector, &cpu_fault);
    
    // 启动故障注入
    printf("启动故障注入...\n");
    concord_fault_injector_start(injector);
    
    // 运行应用逻辑，同时检查故障注入
    for (int i = 0; i < 20; i++) {
        // 检查是否应该注入网络故障
        if (concord_fault_injector_should_inject(injector, "network", 
                                                CONCORD_FAULT_NETWORK_DELAY)) {
            printf("第%d秒: 检测到网络延迟故障\n", i);
        }
        
        // 检查是否应该注入CPU故障
        if (concord_fault_injector_should_inject(injector, "cpu", 
                                                CONCORD_FAULT_CPU_SPIKE)) {
            printf("第%d秒: 检测到CPU尖峰故障\n", i);
        }
        
        // 模拟应用工作
        printf("第%d秒: 应用正常运行\n", i);
        sleep(1);
    }
    
    // 停止故障注入
    concord_fault_injector_stop(injector);
    
    // 清理
    free(network_fault.target);
    free(cpu_fault.target);
    concord_fault_injector_destroy(injector);
}
```

### 3. 基准测试

```c
void benchmark_example() {
    // 创建基准测试
    concord_benchmark_t *benchmark = concord_benchmark_create("数据库操作性能测试");
    
    // 开始基准测试
    concord_benchmark_start(benchmark);
    
    printf("执行性能基准测试...\n");
    
    // 模拟数据库操作
    for (int i = 0; i < 10000; i++) {
        // 记录操作开始时间
        uint64_t start_time = concord_benchmark_op_start(benchmark);
        
        // 模拟数据库查询（随机延迟1-50ms）
        int delay_us = (rand() % 50 + 1) * 1000;
        usleep(delay_us);
        
        // 模拟95%成功率
        int success = (rand() % 100) < 95;
        
        // 记录操作结束
        concord_benchmark_op_end(benchmark, start_time, success);
        
        // 每1000个操作显示进度
        if ((i + 1) % 1000 == 0) {
            printf("已完成 %d/10000 操作\n", i + 1);
        }
    }
    
    // 结束基准测试
    concord_benchmark_stop(benchmark);
    
    // 打印详细报告
    concord_benchmark_print_report(benchmark);
    
    concord_benchmark_destroy(benchmark);
}
```

### 4. 测试数据生成

```c
void test_data_example() {
    // 配置测试数据生成器
    concord_test_data_config_t config = {
        .total_keys = 10000,
        .key_min_size = 10,
        .key_max_size = 30,
        .value_min_size = 50,
        .value_max_size = 200,
        .key_pattern = "user_#_xxx",   // user_[数字]_[随机字符]
        .value_pattern = "data_#",     // data_[数字]
        .use_zipf = 1,                 // 使用Zipf分布
        .zipf_s = 1.5                  // Zipf参数
    };
    
    // 创建数据生成器
    concord_test_data_gen_t *generator = concord_test_data_gen_create(&config);
    if (!generator) {
        printf("创建数据生成器失败\n");
        return;
    }
    
    printf("生成测试数据示例:\n");
    
    // 生成几个示例键值对
    for (int i = 0; i < 10; i++) {
        char *key, *value;
        if (concord_test_data_gen_kv_pair(generator, &key, &value) == 0) {
            printf("  %d: %s = %s\n", i + 1, key, value);
            free(key);
            free(value);
        }
    }
    
    // 生成完整数据集文件
    const char *dataset_file = "/tmp/test_dataset.txt";
    printf("\n生成数据集文件: %s\n", dataset_file);
    
    if (concord_test_data_gen_dataset(generator, dataset_file) == 0) {
        printf("数据集生成完成\n");
        
        // 加载并验证数据集
        char **keys, **values;
        size_t count;
        
        if (concord_test_data_load_dataset(dataset_file, &keys, &values, &count) == 0) {
            printf("成功加载 %zu 个键值对\n", count);
            
            // 显示前5个
            printf("数据集预览:\n");
            for (size_t i = 0; i < 5 && i < count; i++) {
                printf("  %zu: %s = %s\n", i + 1, keys[i], values[i]);
            }
            
            // 清理
            for (size_t i = 0; i < count; i++) {
                free(keys[i]);
                free(values[i]);
            }
            free(keys);
            free(values);
        }
    }
    
    concord_test_data_gen_destroy(generator);
}
```

## 完整应用示例

### 简单的KV服务器

```c
#include "config/config.h"
#include "util/util.h"
#include "metrics/metrics.h"

typedef struct {
    concord_config_t *config;
    concord_metrics_repo_t *metrics;
    concord_metric_t *request_count;
    concord_metric_t *error_count;
    concord_metric_t *response_time;
    volatile int running;
} kv_server_t;

static kv_server_t g_server;

void handle_request(const char *method, const char *key, const char *value) {
    uint64_t start_time = concord_benchmark_op_start(NULL);
    
    // 增加请求计数
    concord_metrics_counter_inc(g_server.request_count, 1);
    
    // 模拟处理逻辑
    usleep((rand() % 10 + 1) * 1000);  // 1-10ms
    
    if (strcmp(method, "GET") == 0) {
        printf("GET %s\n", key);
    } else if (strcmp(method, "PUT") == 0) {
        printf("PUT %s = %s\n", key, value);
    } else {
        printf("未知方法: %s\n", method);
        concord_metrics_counter_inc(g_server.error_count, 1);
    }
    
    // 记录响应时间
    uint64_t end_time = concord_util_time_now();
    double duration = (end_time - start_time) / 1000.0;
    concord_metrics_histogram_observe(g_server.response_time, duration);
}

void signal_handler(int sig) {
    printf("接收到信号 %d，准备关闭服务器\n", sig);
    g_server.running = 0;
}

int main() {
    // 初始化服务器
    memset(&g_server, 0, sizeof(g_server));
    g_server.running = 1;
    
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 加载配置
    g_server.config = concord_config_load("server.yaml");
    if (!g_server.config) {
        g_server.config = concord_config_init();
        concord_config_set_int(g_server.config, "server.port", 8080);
        concord_config_set_string(g_server.config, "server.host", "0.0.0.0");
    }
    
    int port = concord_config_get_int(g_server.config, "server.port", 8080);
    const char *host = concord_config_get_string(g_server.config, "server.host", "0.0.0.0");
    
    // 初始化指标
    g_server.metrics = concord_metrics_repo_create(64);
    g_server.request_count = concord_metrics_create_counter(
        g_server.metrics, "requests_total", "请求总数", 0);
    g_server.error_count = concord_metrics_create_counter(
        g_server.metrics, "errors_total", "错误总数", 0);
    
    concord_histogram_config_t hist_config = {.min = 0.0, .max = 1000.0, .bucket_count = 20};
    g_server.response_time = concord_metrics_create_histogram(
        g_server.metrics, "response_time_ms", "响应时间", &hist_config);
    
    // 启动指标服务器
    int metrics_port = port + 1;
    concord_metrics_start_server(g_server.metrics, host, metrics_port, "/metrics");
    
    printf("KV服务器启动:\n");
    printf("  服务地址: %s:%d\n", host, port);
    printf("  指标地址: http://%s:%d/metrics\n", host, metrics_port);
    
    // 主循环（模拟处理请求）
    int request_id = 0;
    while (g_server.running) {
        // 模拟接收请求
        if (rand() % 10 < 7) {  // 70% GET 请求
            char key[32];
            snprintf(key, sizeof(key), "key_%d", rand() % 1000);
            handle_request("GET", key, NULL);
        } else {  // 30% PUT 请求
            char key[32], value[64];
            snprintf(key, sizeof(key), "key_%d", rand() % 1000);
            snprintf(value, sizeof(value), "value_%d", request_id++);
            handle_request("PUT", key, value);
        }
        
        // 控制请求频率
        usleep(100000);  // 100ms
    }
    
    // 清理资源
    printf("关闭服务器...\n");
    concord_metrics_stop_server(g_server.metrics);
    
    // 输出最终统计
    printf("\n服务器统计:\n");
    printf("  处理请求: %lld\n", concord_metrics_counter_get(g_server.request_count));
    printf("  错误请求: %lld\n", concord_metrics_counter_get(g_server.error_count));
    printf("  平均响应时间: %.2f ms\n", 
           concord_metrics_histogram_mean(g_server.response_time));
    
    concord_metrics_repo_destroy(g_server.metrics);
    concord_config_destroy(g_server.config);
    
    printf("服务器已关闭\n");
    return 0;
}
```

## 最佳实践

### 1. 错误处理

```c
// 始终检查返回值
concord_config_t *cfg = concord_config_load("app.yaml");
if (!cfg) {
    concord_util_error("配置加载失败，使用默认配置");
    cfg = concord_config_init();
    // 设置默认值...
}

// 使用默认值
int port = concord_config_get_int(cfg, "server.port", 8080);
const char *host = concord_config_get_string(cfg, "server.host", "localhost");
```

### 2. 资源管理

```c
void proper_cleanup_example() {
    concord_config_t *cfg = NULL;
    concord_metrics_repo_t *metrics = NULL;
    char *data = NULL;
    
    // 资源分配
    cfg = concord_config_load("app.yaml");
    metrics = concord_metrics_repo_create(32);
    data = concord_util_read_file("data.txt", NULL);
    
    // 业务逻辑...
    
cleanup:
    // 统一清理点
    if (cfg) concord_config_destroy(cfg);
    if (metrics) concord_metrics_repo_destroy(metrics);
    if (data) free(data);
}
```

### 3. 线程安全

```c
// 指标操作是线程安全的
void worker_thread(void *arg) {
    concord_metric_t *counter = (concord_metric_t *)arg;
    
    for (int i = 0; i < 1000; i++) {
        // 这是线程安全的
        concord_metrics_counter_inc(counter, 1);
        
        // 处理业务逻辑...
    }
}
```

### 4. 性能优化

```c
// 批量操作
void batch_operations_example() {
    concord_metric_t *counter = /* ... */;
    
    // 避免频繁的小增量，使用批量更新
    int batch_size = 0;
    for (int i = 0; i < 10000; i++) {
        batch_size++;
        
        if (batch_size >= 100) {
            concord_metrics_counter_inc(counter, batch_size);
            batch_size = 0;
        }
    }
    
    // 处理剩余的
    if (batch_size > 0) {
        concord_metrics_counter_inc(counter, batch_size);
    }
}
```

### 5. 配置管理最佳实践

```c
// 配置文件示例结构
void config_best_practices() {
    concord_config_t *cfg = concord_config_load("app.yaml");
    
    // 使用命名空间组织配置
    int port = concord_config_get_int(cfg, "server.port", 8080);
    int workers = concord_config_get_int(cfg, "server.workers", 4);
    
    // 数据库配置
    const char *db_url = concord_config_get_string(cfg, "database.url", "");
    int db_pool = concord_config_get_int(cfg, "database.pool_size", 10);
    
    // 日志配置
    const char *log_level = concord_config_get_string(cfg, "logging.level", "info");
    const char *log_file = concord_config_get_string(cfg, "logging.file", "app.log");
    
    // 验证关键配置
    if (strlen(db_url) == 0) {
        concord_util_error("数据库URL未配置");
        exit(1);
    }
    
    if (port < 1 || port > 65535) {
        concord_util_error("端口号无效: %d", port);
        exit(1);
    }
    
    concord_config_destroy(cfg);
}
```

这些示例展示了ConcordKV Common模块的完整使用方法和最佳实践。通过这些示例，开发者可以快速了解如何在实际项目中使用这些工具。 