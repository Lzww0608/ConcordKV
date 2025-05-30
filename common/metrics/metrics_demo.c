/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV metrics system - metrics_demo.c
 */
/**
 * @file metrics_demo.c
 * @brief ConcordKV指标框架示例程序
 */

#include "metrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <stdarg.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/select.h>

// 运行标志
static volatile int g_running = 1;
static pthread_t req_thread, mem_thread;

// 信号处理函数
static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\n接收到中断信号(%d)，准备退出...\n", sig);
        g_running = 0;
        
        // 向工作线程发送取消信号
        pthread_cancel(req_thread);
        pthread_cancel(mem_thread);
    }
}

// 安装所有需要的信号处理程序
static void setup_signal_handlers() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    
    // 阻塞其他信号，防止信号处理函数被中断
    sigfillset(&sa.sa_mask);
    
    // 安装信号处理函数
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    
    // 忽略SIGPIPE信号，防止向已关闭的socket写入数据导致程序崩溃
    signal(SIGPIPE, SIG_IGN);
}

// 随机生成1到max之间的整数
static int random_int(int max) {
    return rand() % max + 1;
}

// 可中断的睡眠函数
static int interruptible_sleep(int seconds) {
    fd_set readfds;
    struct timeval tv;
    
    // 设置超时时间
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    
    // 使用select作为可中断的睡眠
    int result = select(0, NULL, NULL, NULL, &tv);
    
    // 检查是否被信号中断
    if (result < 0 && errno == EINTR) {
        return -1;
    }
    
    return 0;
}

// 可中断的微秒级睡眠
static int interruptible_usleep(unsigned long usec) {
    fd_set readfds;
    struct timeval tv;
    
    // 设置超时时间
    tv.tv_sec = usec / 1000000;
    tv.tv_usec = usec % 1000000;
    
    // 使用select作为可中断的睡眠
    int result = select(0, NULL, NULL, NULL, &tv);
    
    // 检查是否被信号中断
    if (result < 0 && errno == EINTR) {
        return -1;
    }
    
    return 0;
}

// 模拟请求处理线程
static void *request_thread(void *arg) {
    concord_metrics_repo_t *repo = (concord_metrics_repo_t *)arg;
    concord_metric_t *requests_total = concord_metrics_find(repo, "requests_total");
    concord_metric_t *request_duration = concord_metrics_find(repo, "request_duration");
    concord_metric_t *active_connections = concord_metrics_find(repo, "active_connections");
    concord_metric_t *request_rate = concord_metrics_find(repo, "request_rate");
    
    // 设置线程可取消状态
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
    while (g_running) {
        // 模拟活跃连接变化
        concord_metrics_gauge_set(active_connections, random_int(100));
        
        // 模拟请求处理
        int num_requests = random_int(10);
        
        for (int i = 0; i < num_requests && g_running; i++) {
            // 增加请求计数
            concord_metrics_counter_inc(requests_total, 1);
            
            // 记录请求速率
            concord_metrics_meter_mark(request_rate, 1);
            
            // 模拟请求处理时间 (1-100ms)
            double duration = random_int(100);
            concord_metrics_histogram_observe(request_duration, duration);
            
            // 短暂睡眠，使用可中断版本
            if (interruptible_usleep(random_int(10) * 1000) < 0) {
                break;
            }
        }
        
        // 休眠随机时间 (100-500ms)，使用可中断版本
        if (interruptible_usleep((100 + random_int(400)) * 1000) < 0) {
            break;
        }
    }
    
    return NULL;
}

// 内存使用模拟线程
static void *memory_thread(void *arg) {
    concord_metrics_repo_t *repo = (concord_metrics_repo_t *)arg;
    concord_metric_t *memory_used = concord_metrics_find(repo, "memory_used");
    concord_metric_t *memory_free = concord_metrics_find(repo, "memory_free");
    
    double base_memory = 1024.0;  // 基础内存使用量（MB）
    
    // 设置线程可取消状态
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    
    while (g_running) {
        // 模拟内存使用波动
        double used = base_memory + random_int(512);
        double free = 8192.0 - used;  // 总内存8GB
        
        concord_metrics_gauge_set(memory_used, used);
        concord_metrics_gauge_set(memory_free, free);
        
        // 每2秒更新一次，使用可中断版本
        if (interruptible_sleep(2) < 0) {
            break;
        }
        
        // 缓慢增加基础内存使用量，模拟内存泄漏
        base_memory += 0.5;
        if (base_memory > 2048) {
            // 模拟内存回收
            base_memory = 1024.0;
        }
    }
    
    return NULL;
}

// 打印指标信息
static void print_metric(concord_metric_t *metric, void *user_data) {
    const char *name = concord_metrics_get_name(metric);
    const char *desc = concord_metrics_get_description(metric);
    concord_metric_type_t type = concord_metrics_get_type(metric);
    
    printf("指标: %s\n", name);
    if (desc) {
        printf("  描述: %s\n", desc);
    }
    
    printf("  类型: ");
    switch (type) {
        case CONCORD_METRIC_COUNTER:
            printf("计数器\n");
            printf("  值: %lld\n", (long long)concord_metrics_counter_get(metric));
            break;
            
        case CONCORD_METRIC_GAUGE:
            printf("仪表盘\n");
            printf("  值: %.2f\n", concord_metrics_gauge_get(metric));
            break;
            
        case CONCORD_METRIC_HISTOGRAM:
            printf("直方图\n");
            printf("  计数: %lld\n", (long long)concord_metrics_histogram_count(metric));
            printf("  平均值: %.2f\n", concord_metrics_histogram_mean(metric));
            printf("  中位数(p50): %.2f\n", concord_metrics_histogram_percentile(metric, 0.5));
            printf("  p90: %.2f\n", concord_metrics_histogram_percentile(metric, 0.9));
            printf("  p99: %.2f\n", concord_metrics_histogram_percentile(metric, 0.99));
            break;
            
        case CONCORD_METRIC_METER:
            printf("计量表\n");
            printf("  1分钟速率: %.2f/秒\n", concord_metrics_meter_rate1(metric));
            printf("  5分钟速率: %.2f/秒\n", concord_metrics_meter_rate5(metric));
            printf("  15分钟速率: %.2f/秒\n", concord_metrics_meter_rate15(metric));
            printf("  平均速率: %.2f/秒\n", concord_metrics_meter_rate_mean(metric));
            break;
            
        default:
            printf("未知\n");
            break;
    }
    
    printf("\n");
}

int main(int argc, char *argv[]) {
    // 设置随机数种子
    srand(time(NULL));
    
    // 注册信号处理函数
    setup_signal_handlers();
    
    // 创建指标仓库
    concord_metrics_repo_t *repo = concord_metrics_repo_create(32);
    if (!repo) {
        fprintf(stderr, "创建指标仓库失败\n");
        return 1;
    }
    
    printf("ConcordKV指标框架示例程序\n");
    printf("按Ctrl+C退出\n\n");
    
    // 创建请求相关指标
    concord_metrics_create_counter(
        repo,
        "requests_total",
        "处理的请求总数",
        0
    );
    
    concord_histogram_config_t hist_config = {
        .min = 0.0,
        .max = 1000.0,
        .bucket_count = 20
    };
    
    concord_metrics_create_histogram(
        repo,
        "request_duration",
        "请求处理时间(毫秒)",
        &hist_config
    );
    
    concord_metrics_create_gauge(
        repo,
        "active_connections",
        "当前活跃连接数",
        0
    );
    
    concord_metrics_create_meter(
        repo,
        "request_rate",
        "请求速率"
    );
    
    // 创建内存相关指标
    concord_metrics_create_gauge(
        repo,
        "memory_used",
        "已使用内存(MB)",
        1024.0
    );
    
    concord_metrics_create_gauge(
        repo,
        "memory_free",
        "可用内存(MB)",
        7168.0
    );
    
    // 启动HTTP服务器
    if (concord_metrics_start_server(repo, "0.0.0.0", 8080, "/metrics") != 0) {
        fprintf(stderr, "启动HTTP服务器失败\n");
        concord_metrics_repo_destroy(repo);
        return 1;
    }
    
    printf("指标HTTP服务器已启动，访问 http://localhost:8080/metrics 获取指标数据\n\n");
    
    // 创建模拟线程
    pthread_create(&req_thread, NULL, request_thread, repo);
    pthread_create(&mem_thread, NULL, memory_thread, repo);
    
    printf("模拟工作负载已启动\n\n");
    
    // 主循环，每10秒打印一次指标信息
    while (g_running) {
        // 使用可中断的睡眠
        if (interruptible_sleep(10) < 0) {
            break;
        }
        
        if (!g_running) break;
        
        printf("\n==================================\n");
        printf("当前指标状态:\n");
        printf("==================================\n");
        
        concord_metrics_foreach(repo, print_metric, NULL);
        
        printf("==================================\n");
        printf("提示: 使用浏览器访问 http://localhost:8080/metrics 查看Prometheus格式指标\n");
    }
    
    printf("\n开始清理资源...\n");
    
    // 尝试等待线程结束，带超时
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1;  // 1秒超时
    
    printf("等待请求线程结束...\n");
    pthread_join(req_thread, NULL);
    
    printf("等待内存线程结束...\n");
    pthread_join(mem_thread, NULL);
    
    printf("停止HTTP服务器...\n");
    // 停止HTTP服务器
    concord_metrics_stop_server(repo);
    
    printf("销毁指标仓库...\n");
    // 销毁指标仓库
    concord_metrics_repo_destroy(repo);
    
    printf("\n程序已退出\n");
    return 0;
} 