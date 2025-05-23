# ConcordKV Common API 参考文档

本文档详细描述了ConcordKV common模块的所有API接口。

## 目录

- [配置管理 API](#配置管理-api)
- [通用工具 API](#通用工具-api)
- [监控指标 API](#监控指标-api)
- [测试工具 API](#测试工具-api)

## 配置管理 API

### 核心配置接口

```c
#include "config/config.h"

// 配置初始化和管理
concord_config_t *concord_config_init();
concord_config_t *concord_config_load(const char *filename);
int concord_config_save(concord_config_t *cfg, const char *filename);
void concord_config_destroy(concord_config_t *cfg);

// 配置值获取
int64_t concord_config_get_int(concord_config_t *cfg, const char *key, int64_t default_val);
double concord_config_get_float(concord_config_t *cfg, const char *key, double default_val);
int concord_config_get_bool(concord_config_t *cfg, const char *key, int default_val);
const char *concord_config_get_string(concord_config_t *cfg, const char *key, const char *default_val);

// 配置值设置
int concord_config_set_int(concord_config_t *cfg, const char *key, int64_t value);
int concord_config_set_float(concord_config_t *cfg, const char *key, double value);
int concord_config_set_bool(concord_config_t *cfg, const char *key, int value);
int concord_config_set_string(concord_config_t *cfg, const char *key, const char *value);
```

### 使用示例

```c
// 加载配置文件
concord_config_t *cfg = concord_config_load("server.yaml");

// 获取配置值
int port = concord_config_get_int(cfg, "server.port", 8080);
const char *host = concord_config_get_string(cfg, "server.host", "localhost");

// 设置配置值
concord_config_set_int(cfg, "server.port", 9090);

// 保存配置
concord_config_save(cfg, "server_updated.yaml");

// 清理
concord_config_destroy(cfg);
```

## 通用工具 API

### 字符串操作

```c
#include "util/util.h"

// 字符串基本操作
char *concord_util_strdup(const char *str);
char *concord_util_trim(char *str);
int concord_util_split(char *str, char delimiter, char **parts, int max_parts);
char *concord_util_join(char **parts, int count, const char *delimiter);
char *concord_util_replace(const char *str, const char *old, const char *new_str);
```

### 文件操作

```c
// 文件和目录操作
int concord_util_file_exists(const char *path);
int concord_util_create_dir(const char *path, int mode);
char *concord_util_read_file(const char *path, size_t *size);
int concord_util_write_file(const char *path, const void *data, size_t size);
int concord_util_copy_file(const char *src, const char *dst);
```

### 时间操作

```c
// 时间和定时器
uint64_t concord_util_time_now();  // 毫秒级时间戳
char *concord_util_time_format(time_t time, const char *format);
time_t concord_util_time_parse(const char *time_str, const char *format);
```

### 网络工具

```c
#include "util/network.h"

// 套接字操作
concord_socket_t *concord_net_socket_create(concord_socket_type_t type);
int concord_net_socket_connect(concord_socket_t *sock, const char *host, int port);
int concord_net_socket_send(concord_socket_t *sock, const void *data, size_t size);
int concord_net_socket_recv(concord_socket_t *sock, void *buffer, size_t size);

// HTTP客户端
concord_http_response_t *concord_net_http_get(const char *url, const char **headers, int header_count);
concord_http_response_t *concord_net_http_post(const char *url, const char *content_type, 
                                              const void *data, size_t data_len, 
                                              const char **headers, int header_count);
```

### 日志系统

```c
#include "util/log.h"

// 日志级别
typedef enum {
    CONCORD_LOG_DEBUG = 0,
    CONCORD_LOG_INFO,
    CONCORD_LOG_WARN,
    CONCORD_LOG_ERROR,
    CONCORD_LOG_FATAL
} concord_log_level_t;

// 日志函数
void concord_util_log(concord_log_level_t level, const char *fmt, ...);

// 便捷宏
#define concord_util_debug(fmt, ...) concord_util_log(CONCORD_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define concord_util_info(fmt, ...) concord_util_log(CONCORD_LOG_INFO, fmt, ##__VA_ARGS__)
#define concord_util_error(fmt, ...) concord_util_log(CONCORD_LOG_ERROR, fmt, ##__VA_ARGS__)
```

## 监控指标 API

### 基础指标接口

```c
#include "metrics/metrics.h"

// 指标仓库管理
concord_metrics_repo_t *concord_metrics_repo_create(int capacity);
void concord_metrics_repo_destroy(concord_metrics_repo_t *repo);

// 指标创建
concord_metric_t *concord_metrics_create_counter(concord_metrics_repo_t *repo, 
                                               const char *name, const char *description, 
                                               int64_t initial_value);
concord_metric_t *concord_metrics_create_gauge(concord_metrics_repo_t *repo, 
                                              const char *name, const char *description, 
                                              double initial_value);
concord_metric_t *concord_metrics_create_histogram(concord_metrics_repo_t *repo, 
                                                  const char *name, const char *description, 
                                                  concord_histogram_config_t *config);
```

### 指标操作

```c
// 计数器操作
int concord_metrics_counter_inc(concord_metric_t *metric, int64_t value);
int64_t concord_metrics_counter_get(concord_metric_t *metric);

// 仪表盘操作
int concord_metrics_gauge_set(concord_metric_t *metric, double value);
int concord_metrics_gauge_inc(concord_metric_t *metric, double value);
double concord_metrics_gauge_get(concord_metric_t *metric);

// 直方图操作
int concord_metrics_histogram_observe(concord_metric_t *metric, double value);
int64_t concord_metrics_histogram_count(concord_metric_t *metric);
double concord_metrics_histogram_mean(concord_metric_t *metric);
double concord_metrics_histogram_percentile(concord_metric_t *metric, double percentile);
```

### HTTP服务器

```c
// 指标HTTP服务器
int concord_metrics_start_server(concord_metrics_repo_t *repo, const char *host, 
                                int port, const char *path);
int concord_metrics_stop_server(concord_metrics_repo_t *repo);

// 指标导出
int concord_metrics_dump(concord_metrics_repo_t *repo, concord_metrics_format_t format, 
                        char *buffer, size_t size);
```

### 使用示例

```c
// 创建指标仓库
concord_metrics_repo_t *repo = concord_metrics_repo_create(32);

// 创建计数器
concord_metric_t *requests = concord_metrics_create_counter(repo, "requests_total", 
                                                          "处理的请求总数", 0);

// 创建直方图
concord_histogram_config_t hist_config = {.min = 0.0, .max = 1000.0, .bucket_count = 20};
concord_metric_t *latency = concord_metrics_create_histogram(repo, "request_latency", 
                                                           "请求延迟", &hist_config);

// 使用指标
concord_metrics_counter_inc(requests, 1);
concord_metrics_histogram_observe(latency, 25.5);

// 启动HTTP服务器
concord_metrics_start_server(repo, "0.0.0.0", 8080, "/metrics");
```

## 测试工具 API

### 模拟客户端

```c
#include "testing/testing.h"

// 模拟客户端配置
typedef struct {
    char *server_host;
    int server_port;
    int concurrent_clients;
    uint64_t test_duration_ms;
    double read_ratio;
    double write_ratio;
    size_t key_size;
    size_t value_size;
} concord_mock_client_config_t;

// 模拟客户端操作
concord_mock_client_t *concord_mock_client_create(concord_mock_client_config_t *config);
int concord_mock_client_start(concord_mock_client_t *client);
int concord_mock_client_stop(concord_mock_client_t *client);
concord_test_stats_t *concord_mock_client_get_stats(concord_mock_client_t *client);
void concord_mock_client_destroy(concord_mock_client_t *client);
```

### 故障注入

```c
// 故障注入配置
typedef struct {
    concord_fault_type_t type;
    double probability;
    uint64_t duration_ms;
    int severity;
    char *target;
} concord_fault_config_t;

// 故障注入操作
concord_fault_injector_t *concord_fault_injector_create(void);
int concord_fault_injector_add_fault(concord_fault_injector_t *injector, 
                                    concord_fault_config_t *config);
int concord_fault_injector_start(concord_fault_injector_t *injector);
int concord_fault_injector_should_inject(concord_fault_injector_t *injector, 
                                        const char *target, concord_fault_type_t type);
```

### 基准测试

```c
// 基准测试操作
concord_benchmark_t *concord_benchmark_create(const char *name);
int concord_benchmark_start(concord_benchmark_t *benchmark);
uint64_t concord_benchmark_op_start(concord_benchmark_t *benchmark);
int concord_benchmark_op_end(concord_benchmark_t *benchmark, uint64_t start_time, int success);
int concord_benchmark_stop(concord_benchmark_t *benchmark);
void concord_benchmark_print_report(concord_benchmark_t *benchmark);
```

### 测试数据生成

```c
// 测试数据生成配置
typedef struct {
    size_t total_keys;
    size_t key_min_size;
    size_t key_max_size;
    size_t value_min_size;
    size_t value_max_size;
    char *key_pattern;
    char *value_pattern;
    int use_zipf;
    double zipf_s;
} concord_test_data_config_t;

// 数据生成操作
concord_test_data_gen_t *concord_test_data_gen_create(concord_test_data_config_t *config);
char *concord_test_data_gen_key(concord_test_data_gen_t *generator);
char *concord_test_data_gen_value(concord_test_data_gen_t *generator);
int concord_test_data_gen_dataset(concord_test_data_gen_t *generator, const char *output_file);
```

### 使用示例

```c
// 创建基准测试
concord_benchmark_t *benchmark = concord_benchmark_create("KV性能测试");
concord_benchmark_start(benchmark);

// 测试操作
for (int i = 0; i < 1000; i++) {
    uint64_t start = concord_benchmark_op_start(benchmark);
    // 执行操作...
    concord_benchmark_op_end(benchmark, start, 1);
}

concord_benchmark_stop(benchmark);
concord_benchmark_print_report(benchmark);
```

## 编译和链接

### 编译配置

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -O2 -g -pthread
LDFLAGS = -pthread -lm

# 包含路径
INCLUDES = -I/path/to/ConcordKV/common

# 链接库
LIBS = -lconcord_config -lconcord_util -lconcord_metrics -lconcord_testing
```

### 项目集成

```c
// 在你的项目中包含头文件
#include "config/config.h"
#include "util/util.h"
#include "metrics/metrics.h"
#include "testing/testing.h"

// 初始化各个模块
int init_common_modules() {
    // 初始化测试框架
    if (concord_testing_init() != 0) {
        return -1;
    }
    
    // 其他初始化...
    return 0;
}
```

## 错误处理

所有API函数都遵循以下错误处理约定：

- 返回指针的函数：成功返回有效指针，失败返回`NULL`
- 返回整数的函数：成功返回`0`，失败返回`-1`
- 获取值的函数：失败时返回默认值或特殊值（如`NaN`）

### 错误检查示例

```c
// 检查配置加载
concord_config_t *cfg = concord_config_load("config.yaml");
if (!cfg) {
    fprintf(stderr, "配置加载失败\n");
    return -1;
}

// 检查操作结果
if (concord_metrics_counter_inc(counter, 1) != 0) {
    fprintf(stderr, "指标更新失败\n");
}
```

## 线程安全

- **配置管理**：读操作线程安全，写操作需要外部同步
- **通用工具**：大部分函数线程安全，具体见函数文档
- **监控指标**：完全线程安全
- **测试工具**：内部使用线程，外部调用需要注意同步

## 性能考虑

- 指标操作设计为低延迟，适合高频调用
- 配置读取经过优化，支持频繁访问
- 测试工具可能消耗较多资源，适合测试环境使用
- 建议使用连接池和缓存来优化网络操作 