/**
 * @file mock_client.c
 * @brief 模拟客户端实现
 */

#include "testing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>

// 模拟客户端结构
struct concord_mock_client_t {
    concord_mock_client_config_t config;
    pthread_t *threads;               // 客户端线程数组
    concord_test_stats_t stats;       // 测试统计
    volatile int running;             // 运行状态
    pthread_mutex_t stats_mutex;      // 统计互斥锁
    double *latencies;                // 延迟数组
    size_t latency_count;             // 延迟记录数
    size_t latency_capacity;          // 延迟数组容量
    uint64_t start_time;              // 测试开始时间
};

// 客户端线程参数
typedef struct {
    concord_mock_client_t *client;
    int thread_id;
    int socket_fd;
} client_thread_args_t;

// 获取当前时间（微秒）
static uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

// 获取当前时间（毫秒）
static uint64_t get_time_ms(void) {
    return get_time_us() / 1000;
}

// 生成随机字符串
static char *generate_random_string(size_t length) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    char *str = malloc(length + 1);
    if (!str) return NULL;
    
    for (size_t i = 0; i < length; i++) {
        str[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    str[length] = '\0';
    return str;
}

// 模拟网络操作
static int simulate_network_operation(concord_mock_client_t *client, const char *operation, 
                                    const char *key, const char *value) {
    (void)client;  // 抑制未使用警告
    
    // 模拟网络延迟 (1-10ms)
    usleep((rand() % 10 + 1) * 1000);
    
    // 模拟操作成功率 95%
    if (rand() % 100 < 95) {
        printf("[模拟] %s 操作成功: key=%s, value=%s\n", operation, key, value ? value : "N/A");
        return 1;  // 成功
    } else {
        printf("[模拟] %s 操作失败: key=%s\n", operation, key);
        return 0;  // 失败
    }
}

// 记录延迟
static void record_latency(concord_mock_client_t *client, double latency_ms) {
    pthread_mutex_lock(&client->stats_mutex);
    
    // 扩展延迟数组
    if (client->latency_count >= client->latency_capacity) {
        client->latency_capacity *= 2;
        client->latencies = realloc(client->latencies, 
                                  client->latency_capacity * sizeof(double));
    }
    
    if (client->latencies) {
        client->latencies[client->latency_count++] = latency_ms;
    }
    
    pthread_mutex_unlock(&client->stats_mutex);
}

// 更新统计信息
static void update_stats(concord_mock_client_t *client, int success, double latency_ms) {
    pthread_mutex_lock(&client->stats_mutex);
    
    client->stats.total_operations++;
    if (success) {
        client->stats.successful_ops++;
    } else {
        client->stats.failed_ops++;
    }
    
    // 更新延迟统计
    if (client->stats.min_latency_ms == 0 || latency_ms < client->stats.min_latency_ms) {
        client->stats.min_latency_ms = latency_ms;
    }
    if (latency_ms > client->stats.max_latency_ms) {
        client->stats.max_latency_ms = latency_ms;
    }
    
    pthread_mutex_unlock(&client->stats_mutex);
    
    // 记录延迟用于百分位数计算
    record_latency(client, latency_ms);
}

// 客户端线程函数
static void *client_thread(void *arg) {
    client_thread_args_t *args = (client_thread_args_t *)arg;
    concord_mock_client_t *client = args->client;
    int thread_id = args->thread_id;
    
    printf("客户端线程 %d 启动\n", thread_id);
    
    // 设置随机种子
    srand(time(NULL) + thread_id);
    
    int operations = 0;
    uint64_t thread_start_time = get_time_ms();
    
    while (client->running && 
           (client->config.test_duration_ms == 0 || 
            get_time_ms() - client->start_time < client->config.test_duration_ms) &&
           (client->config.operations_per_client == 0 || 
            operations < client->config.operations_per_client)) {
        
        // 生成测试数据
        char *key = generate_random_string(client->config.key_size);
        char *value = generate_random_string(client->config.value_size);
        
        if (!key || !value) {
            free(key);
            free(value);
            continue;
        }
        
        // 决定操作类型
        double op_rand = (double)rand() / RAND_MAX;
        const char *operation;
        int success;
        
        uint64_t op_start = get_time_us();
        
        if (op_rand < client->config.read_ratio) {
            // 读操作
            operation = "GET";
            success = simulate_network_operation(client, operation, key, NULL);
        } else {
            // 写操作
            operation = "PUT";
            success = simulate_network_operation(client, operation, key, value);
        }
        
        uint64_t op_end = get_time_us();
        double latency_ms = (op_end - op_start) / 1000.0;
        
        // 更新统计
        update_stats(client, success, latency_ms);
        
        free(key);
        free(value);
        operations++;
        
        // 控制请求频率，避免过于密集
        usleep(1000);  // 1ms间隔
    }
    
    uint64_t thread_end_time = get_time_ms();
    printf("客户端线程 %d 结束，执行了 %d 个操作，耗时 %llu ms\n", 
           thread_id, operations, thread_end_time - thread_start_time);
    
    return NULL;
}

// 比较函数，用于排序
static int compare_double(const void *a, const void *b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    return (da > db) - (da < db);
}

// 计算百分位数
static double calculate_percentile(double *latencies, size_t count, double percentile) {
    if (count == 0) return 0.0;
    
    qsort(latencies, count, sizeof(double), compare_double);
    size_t index = (size_t)(percentile * (count - 1));
    return latencies[index];
}

// 计算最终统计
static void calculate_final_stats(concord_mock_client_t *client) {
    pthread_mutex_lock(&client->stats_mutex);
    
    // 计算总耗时
    client->stats.total_time_ms = get_time_ms() - client->start_time;
    
    // 计算平均延迟
    if (client->latency_count > 0) {
        double total_latency = 0.0;
        for (size_t i = 0; i < client->latency_count; i++) {
            total_latency += client->latencies[i];
        }
        client->stats.avg_latency_ms = total_latency / client->latency_count;
        
        // 计算百分位数
        client->stats.p95_latency_ms = calculate_percentile(client->latencies, client->latency_count, 0.95);
        client->stats.p99_latency_ms = calculate_percentile(client->latencies, client->latency_count, 0.99);
    }
    
    // 计算吞吐量
    if (client->stats.total_time_ms > 0) {
        client->stats.throughput_ops_sec = (double)client->stats.successful_ops * 1000.0 / client->stats.total_time_ms;
    }
    
    pthread_mutex_unlock(&client->stats_mutex);
}

// 创建模拟客户端
concord_mock_client_t *concord_mock_client_create(concord_mock_client_config_t *config) {
    if (!config) return NULL;
    
    concord_mock_client_t *client = calloc(1, sizeof(concord_mock_client_t));
    if (!client) return NULL;
    
    // 复制配置
    client->config = *config;
    if (config->server_host) {
        client->config.server_host = strdup(config->server_host);
    }
    
    // 初始化统计
    memset(&client->stats, 0, sizeof(client->stats));
    
    // 初始化延迟数组
    client->latency_capacity = 1000;
    client->latencies = malloc(client->latency_capacity * sizeof(double));
    if (!client->latencies) {
        free(client->config.server_host);
        free(client);
        return NULL;
    }
    
    // 初始化互斥锁
    if (pthread_mutex_init(&client->stats_mutex, NULL) != 0) {
        free(client->latencies);
        free(client->config.server_host);
        free(client);
        return NULL;
    }
    
    return client;
}

// 销毁模拟客户端
void concord_mock_client_destroy(concord_mock_client_t *client) {
    if (!client) return;
    
    if (client->running) {
        concord_mock_client_stop(client);
    }
    
    free(client->threads);
    free(client->latencies);
    free(client->config.server_host);
    pthread_mutex_destroy(&client->stats_mutex);
    free(client);
}

// 启动模拟客户端测试
int concord_mock_client_start(concord_mock_client_t *client) {
    if (!client || client->running) return -1;
    
    printf("启动模拟客户端测试...\n");
    printf("服务器: %s:%d\n", client->config.server_host, client->config.server_port);
    printf("并发客户端数: %d\n", client->config.concurrent_clients);
    printf("测试持续时间: %llu ms\n", client->config.test_duration_ms);
    printf("每客户端操作数: %d\n", client->config.operations_per_client);
    printf("读操作比例: %.2f%%\n", client->config.read_ratio * 100);
    printf("写操作比例: %.2f%%\n", client->config.write_ratio * 100);
    
    // 分配线程数组
    client->threads = malloc(client->config.concurrent_clients * sizeof(pthread_t));
    if (!client->threads) return -1;
    
    client->running = 1;
    client->start_time = get_time_ms();
    
    // 创建客户端线程
    for (int i = 0; i < client->config.concurrent_clients; i++) {
        client_thread_args_t *args = malloc(sizeof(client_thread_args_t));
        args->client = client;
        args->thread_id = i;
        args->socket_fd = -1;  // 在模拟模式下不需要真实socket
        
        if (pthread_create(&client->threads[i], NULL, client_thread, args) != 0) {
            printf("创建客户端线程 %d 失败\n", i);
            client->running = 0;
            // 清理已创建的线程
            for (int j = 0; j < i; j++) {
                pthread_cancel(client->threads[j]);
                pthread_join(client->threads[j], NULL);
            }
            free(client->threads);
            client->threads = NULL;
            return -1;
        }
    }
    
    printf("所有客户端线程已启动\n");
    return 0;
}

// 停止模拟客户端测试
int concord_mock_client_stop(concord_mock_client_t *client) {
    if (!client || !client->running) return -1;
    
    printf("停止模拟客户端测试...\n");
    client->running = 0;
    
    // 等待所有线程结束
    for (int i = 0; i < client->config.concurrent_clients; i++) {
        pthread_join(client->threads[i], NULL);
    }
    
    free(client->threads);
    client->threads = NULL;
    
    // 计算最终统计
    calculate_final_stats(client);
    
    printf("模拟客户端测试已停止\n");
    return 0;
}

// 获取测试统计
concord_test_stats_t *concord_mock_client_get_stats(concord_mock_client_t *client) {
    if (!client) return NULL;
    return &client->stats;
}

// 创建默认模拟客户端配置
concord_mock_client_config_t concord_mock_client_default_config(void) {
    concord_mock_client_config_t config = {
        .server_host = "localhost",
        .server_port = 8080,
        .concurrent_clients = 10,
        .test_duration_ms = 30000,  // 30秒
        .operations_per_client = 0,  // 无限制
        .read_ratio = 0.7,          // 70%读操作
        .write_ratio = 0.3,         // 30%写操作
        .key_size = 16,
        .value_size = 64
    };
    return config;
} 