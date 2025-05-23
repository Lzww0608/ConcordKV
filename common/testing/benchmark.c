/**
 * @file benchmark.c
 * @brief 性能基准测试实现
 */

#define _GNU_SOURCE  // 为了使用strdup等函数
#include "testing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <math.h>

#define MAX_LATENCIES 1000000  // 最大延迟记录数

// 基准测试结构
struct concord_benchmark_t {
    char *name;                        // 测试名称
    concord_test_stats_t stats;        // 测试统计
    double *latencies;                 // 延迟记录数组
    size_t latency_count;              // 延迟记录数
    size_t latency_capacity;           // 延迟数组容量
    pthread_mutex_t mutex;             // 互斥锁
    uint64_t start_time;               // 测试开始时间
    uint64_t end_time;                 // 测试结束时间
    volatile int running;              // 运行状态
};

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

// 比较函数，用于排序
static int compare_double(const void *a, const void *b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    return (da > db) - (da < db);
}

// 计算百分位数
static double calculate_percentile(double *latencies, size_t count, double percentile) {
    if (count == 0) return 0.0;
    
    // 创建副本进行排序
    double *sorted = malloc(count * sizeof(double));
    if (!sorted) return 0.0;
    
    memcpy(sorted, latencies, count * sizeof(double));
    qsort(sorted, count, sizeof(double), compare_double);
    
    size_t index = (size_t)(percentile * (count - 1));
    double result = sorted[index];
    
    free(sorted);
    return result;
}

// 计算标准差
static double calculate_stddev(double *latencies, size_t count, double mean) {
    if (count <= 1) return 0.0;
    
    double sum_squared_diff = 0.0;
    for (size_t i = 0; i < count; i++) {
        double diff = latencies[i] - mean;
        sum_squared_diff += diff * diff;
    }
    
    return sqrt(sum_squared_diff / (count - 1));
}

// 添加延迟记录
static void add_latency(concord_benchmark_t *benchmark, double latency_ms) {
    pthread_mutex_lock(&benchmark->mutex);
    
    // 扩展延迟数组
    if (benchmark->latency_count >= benchmark->latency_capacity) {
        if (benchmark->latency_capacity >= MAX_LATENCIES) {
            // 如果已达到最大容量，使用环形缓冲区
            size_t index = benchmark->latency_count % MAX_LATENCIES;
            benchmark->latencies[index] = latency_ms;
        } else {
            // 扩展数组
            size_t new_capacity = benchmark->latency_capacity * 2;
            if (new_capacity > MAX_LATENCIES) {
                new_capacity = MAX_LATENCIES;
            }
            
            double *new_latencies = realloc(benchmark->latencies, 
                                          new_capacity * sizeof(double));
            if (new_latencies) {
                benchmark->latencies = new_latencies;
                benchmark->latency_capacity = new_capacity;
                benchmark->latencies[benchmark->latency_count] = latency_ms;
            }
        }
    } else {
        benchmark->latencies[benchmark->latency_count] = latency_ms;
    }
    
    benchmark->latency_count++;
    
    pthread_mutex_unlock(&benchmark->mutex);
}

// 更新统计信息
static void update_stats(concord_benchmark_t *benchmark) {
    pthread_mutex_lock(&benchmark->mutex);
    
    size_t effective_count = (benchmark->latency_count > MAX_LATENCIES) ? 
                           MAX_LATENCIES : benchmark->latency_count;
    
    if (effective_count == 0) {
        pthread_mutex_unlock(&benchmark->mutex);
        return;
    }
    
    // 计算平均延迟
    double total_latency = 0.0;
    double min_latency = benchmark->latencies[0];
    double max_latency = benchmark->latencies[0];
    
    for (size_t i = 0; i < effective_count; i++) {
        double latency = benchmark->latencies[i];
        total_latency += latency;
        
        if (latency < min_latency) min_latency = latency;
        if (latency > max_latency) max_latency = latency;
    }
    
    benchmark->stats.avg_latency_ms = total_latency / effective_count;
    benchmark->stats.min_latency_ms = min_latency;
    benchmark->stats.max_latency_ms = max_latency;
    
    // 计算百分位数
    benchmark->stats.p95_latency_ms = calculate_percentile(benchmark->latencies, effective_count, 0.95);
    benchmark->stats.p99_latency_ms = calculate_percentile(benchmark->latencies, effective_count, 0.99);
    
    // 计算总耗时和吞吐量
    if (benchmark->end_time > benchmark->start_time) {
        benchmark->stats.total_time_ms = (benchmark->end_time - benchmark->start_time) / 1000;
        if (benchmark->stats.total_time_ms > 0) {
            benchmark->stats.throughput_ops_sec = 
                (double)benchmark->stats.successful_ops * 1000.0 / benchmark->stats.total_time_ms;
        }
    }
    
    pthread_mutex_unlock(&benchmark->mutex);
}

// 创建基准测试
concord_benchmark_t *concord_benchmark_create(const char *name) {
    if (!name) return NULL;
    
    concord_benchmark_t *benchmark = calloc(1, sizeof(concord_benchmark_t));
    if (!benchmark) return NULL;
    
    benchmark->name = strdup(name);
    if (!benchmark->name) {
        free(benchmark);
        return NULL;
    }
    
    // 初始化延迟数组
    benchmark->latency_capacity = 1000;
    benchmark->latencies = malloc(benchmark->latency_capacity * sizeof(double));
    if (!benchmark->latencies) {
        free(benchmark->name);
        free(benchmark);
        return NULL;
    }
    
    // 初始化互斥锁
    if (pthread_mutex_init(&benchmark->mutex, NULL) != 0) {
        free(benchmark->latencies);
        free(benchmark->name);
        free(benchmark);
        return NULL;
    }
    
    // 初始化统计
    memset(&benchmark->stats, 0, sizeof(benchmark->stats));
    
    return benchmark;
}

// 销毁基准测试
void concord_benchmark_destroy(concord_benchmark_t *benchmark) {
    if (!benchmark) return;
    
    free(benchmark->name);
    free(benchmark->latencies);
    pthread_mutex_destroy(&benchmark->mutex);
    free(benchmark);
}

// 开始测试
int concord_benchmark_start(concord_benchmark_t *benchmark) {
    if (!benchmark || benchmark->running) return -1;
    
    printf("开始基准测试: %s\n", benchmark->name);
    
    benchmark->running = 1;
    benchmark->start_time = get_time_us();
    
    // 重置统计
    pthread_mutex_lock(&benchmark->mutex);
    memset(&benchmark->stats, 0, sizeof(benchmark->stats));
    benchmark->latency_count = 0;
    pthread_mutex_unlock(&benchmark->mutex);
    
    return 0;
}

// 结束测试
int concord_benchmark_stop(concord_benchmark_t *benchmark) {
    if (!benchmark || !benchmark->running) return -1;
    
    benchmark->end_time = get_time_us();
    benchmark->running = 0;
    
    // 更新最终统计
    update_stats(benchmark);
    
    printf("基准测试结束: %s\n", benchmark->name);
    return 0;
}

// 记录操作开始
uint64_t concord_benchmark_op_start(concord_benchmark_t *benchmark) {
    if (!benchmark || !benchmark->running) return 0;
    return get_time_us();
}

// 记录操作结束
int concord_benchmark_op_end(concord_benchmark_t *benchmark, uint64_t start_time, int success) {
    if (!benchmark || !benchmark->running || start_time == 0) return -1;
    
    uint64_t end_time = get_time_us();
    double latency_ms = (end_time - start_time) / 1000.0;
    
    pthread_mutex_lock(&benchmark->mutex);
    
    benchmark->stats.total_operations++;
    if (success) {
        benchmark->stats.successful_ops++;
    } else {
        benchmark->stats.failed_ops++;
    }
    
    pthread_mutex_unlock(&benchmark->mutex);
    
    // 添加延迟记录
    add_latency(benchmark, latency_ms);
    
    return 0;
}

// 获取测试统计
concord_test_stats_t *concord_benchmark_get_stats(concord_benchmark_t *benchmark) {
    if (!benchmark) return NULL;
    
    // 更新统计
    update_stats(benchmark);
    
    return &benchmark->stats;
}

// 打印测试报告
void concord_benchmark_print_report(concord_benchmark_t *benchmark) {
    if (!benchmark) return;
    
    // 更新统计
    update_stats(benchmark);
    
    concord_test_stats_t *stats = &benchmark->stats;
    
    printf("\n==========================================\n");
    printf("基准测试报告: %s\n", benchmark->name);
    printf("==========================================\n");
    
    printf("总操作数:         %llu\n", (unsigned long long)stats->total_operations);
    printf("成功操作数:       %llu\n", (unsigned long long)stats->successful_ops);
    printf("失败操作数:       %llu\n", (unsigned long long)stats->failed_ops);
    
    if (stats->total_operations > 0) {
        double success_rate = (double)stats->successful_ops / stats->total_operations * 100.0;
        printf("成功率:           %.2f%%\n", success_rate);
    }
    
    printf("总耗时:           %llu ms\n", (unsigned long long)stats->total_time_ms);
    printf("吞吐量:           %.2f ops/sec\n", stats->throughput_ops_sec);
    
    printf("\n延迟统计 (毫秒):\n");
    printf("  平均延迟:       %.3f\n", stats->avg_latency_ms);
    printf("  最小延迟:       %.3f\n", stats->min_latency_ms);
    printf("  最大延迟:       %.3f\n", stats->max_latency_ms);
    printf("  P95延迟:        %.3f\n", stats->p95_latency_ms);
    printf("  P99延迟:        %.3f\n", stats->p99_latency_ms);
    
    // 计算并显示标准差
    if (benchmark->latency_count > 1) {
        size_t effective_count = (benchmark->latency_count > MAX_LATENCIES) ? 
                               MAX_LATENCIES : benchmark->latency_count;
        double stddev = calculate_stddev(benchmark->latencies, effective_count, stats->avg_latency_ms);
        printf("  标准差:         %.3f\n", stddev);
    }
    
    printf("\n延迟分布:\n");
    if (benchmark->latency_count > 0) {
        size_t effective_count = (benchmark->latency_count > MAX_LATENCIES) ? 
                               MAX_LATENCIES : benchmark->latency_count;
        
        // 计算更多百分位数
        double p50 = calculate_percentile(benchmark->latencies, effective_count, 0.50);
        double p75 = calculate_percentile(benchmark->latencies, effective_count, 0.75);
        double p90 = calculate_percentile(benchmark->latencies, effective_count, 0.90);
        double p95 = calculate_percentile(benchmark->latencies, effective_count, 0.95);
        double p99 = calculate_percentile(benchmark->latencies, effective_count, 0.99);
        double p999 = calculate_percentile(benchmark->latencies, effective_count, 0.999);
        
        printf("  P50:            %.3f ms\n", p50);
        printf("  P75:            %.3f ms\n", p75);
        printf("  P90:            %.3f ms\n", p90);
        printf("  P95:            %.3f ms\n", p95);
        printf("  P99:            %.3f ms\n", p99);
        printf("  P99.9:          %.3f ms\n", p999);
    }
    
    printf("==========================================\n\n");
} 