/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV test suite - persistence_benchmark.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>
#include <assert.h>
#include <time.h>
#include "../kvserver/kv_persist.h"

#define BENCHMARK_TIMEOUT 60
#define NUM_OPERATIONS 100000
#define NUM_THREADS 4
#define KEY_SIZE 32
#define VALUE_SIZE 128

// 基准测试结果结构
typedef struct {
    double write_latency_avg;
    double write_latency_p99;
    double sync_latency_avg;
    double throughput_ops_per_sec;
    size_t total_bytes_written;
    int total_operations;
} benchmark_result_t;

// 全局变量
static int g_benchmark_timeout = 0;
static double g_latencies[NUM_OPERATIONS];
static int g_latency_count = 0;
static pthread_mutex_t g_latency_mutex = PTHREAD_MUTEX_INITIALIZER;

// 超时处理
void benchmark_timeout_handler(int sig) {
    printf("[TIMEOUT] Benchmark timed out after %d seconds\n", BENCHMARK_TIMEOUT);
    g_benchmark_timeout = 1;
    exit(1);
}

void setup_benchmark_timeout() {
    signal(SIGALRM, benchmark_timeout_handler);
    alarm(BENCHMARK_TIMEOUT);
}

void clear_benchmark_timeout() {
    alarm(0);
}

// 获取当前时间（微秒）
double get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000.0 + tv.tv_usec;
}

// 记录延迟
void record_latency(double latency) {
    pthread_mutex_lock(&g_latency_mutex);
    if (g_latency_count < NUM_OPERATIONS) {
        g_latencies[g_latency_count++] = latency;
    }
    pthread_mutex_unlock(&g_latency_mutex);
}

// 计算百分位数
double calculate_percentile(double *latencies, int count, double percentile) {
    if (count == 0) return 0.0;
    
    // 简单的冒泡排序
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (latencies[j] > latencies[j + 1]) {
                double temp = latencies[j];
                latencies[j] = latencies[j + 1];
                latencies[j + 1] = temp;
            }
        }
    }
    
    int index = (int)(count * percentile / 100.0);
    if (index >= count) index = count - 1;
    return latencies[index];
}

// 计算平均值
double calculate_average(double *latencies, int count) {
    if (count == 0) return 0.0;
    
    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += latencies[i];
    }
    return sum / count;
}

// 生成随机字符串
void generate_random_string(char *str, int length) {
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (int i = 0; i < length - 1; i++) {
        str[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    str[length - 1] = '\0';
}

// 单线程写入基准测试
void benchmark_single_thread_write(wal_t *wal, benchmark_result_t *result) {
    printf("\n=== 单线程写入性能基准测试 ===\n");
    
    char key[KEY_SIZE];
    char value[VALUE_SIZE];
    double start_time = get_time_us();
    
    for (int i = 0; i < NUM_OPERATIONS; i++) {
        generate_random_string(key, KEY_SIZE);
        generate_random_string(value, VALUE_SIZE);
        
        double op_start = get_time_us();
        assert(wal_append(wal, WAL_OP_SET, key, value) == 0);
        double op_end = get_time_us();
        
        record_latency(op_end - op_start);
        
        if (i % 10000 == 0) {
            printf("已完成 %d/%d 操作\n", i, NUM_OPERATIONS);
        }
    }
    
    double end_time = get_time_us();
    double total_time = (end_time - start_time) / 1000000.0; // 转换为秒
    
    result->write_latency_avg = calculate_average(g_latencies, g_latency_count);
    result->write_latency_p99 = calculate_percentile(g_latencies, g_latency_count, 99.0);
    result->throughput_ops_per_sec = NUM_OPERATIONS / total_time;
    result->total_operations = NUM_OPERATIONS;
    result->total_bytes_written = NUM_OPERATIONS * (KEY_SIZE + VALUE_SIZE);
    
    printf("单线程写入完成:\n");
    printf("  总操作数: %d\n", result->total_operations);
    printf("  总耗时: %.2f 秒\n", total_time);
    printf("  吞吐量: %.0f ops/sec\n", result->throughput_ops_per_sec);
    printf("  平均延迟: %.2f μs\n", result->write_latency_avg);
    printf("  P99延迟: %.2f μs\n", result->write_latency_p99);
    printf("  总写入字节: %zu bytes\n", result->total_bytes_written);
}

// 多线程写入基准测试
typedef struct {
    wal_t *wal;
    int thread_id;
    int operations_per_thread;
    double *thread_latencies;
    int thread_latency_count;
} thread_benchmark_args_t;

void* thread_benchmark_worker(void *arg) {
    thread_benchmark_args_t *args = (thread_benchmark_args_t *)arg;
    char key[KEY_SIZE];
    char value[VALUE_SIZE];
    
    args->thread_latency_count = 0;
    args->thread_latencies = malloc(args->operations_per_thread * sizeof(double));
    
    for (int i = 0; i < args->operations_per_thread; i++) {
        snprintf(key, sizeof(key), "thread_%d_key_%d", args->thread_id, i);
        generate_random_string(value, VALUE_SIZE);
        
        double op_start = get_time_us();
        if (wal_append(args->wal, WAL_OP_SET, key, value) == 0) {
            double op_end = get_time_us();
            args->thread_latencies[args->thread_latency_count++] = op_end - op_start;
        }
    }
    
    return NULL;
}

void benchmark_multi_thread_write(wal_t *wal, benchmark_result_t *result) {
    printf("\n=== 多线程写入性能基准测试 ===\n");
    
    pthread_t threads[NUM_THREADS];
    thread_benchmark_args_t thread_args[NUM_THREADS];
    int operations_per_thread = NUM_OPERATIONS / NUM_THREADS;
    
    double start_time = get_time_us();
    
    // 创建线程
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_args[i].wal = wal;
        thread_args[i].thread_id = i;
        thread_args[i].operations_per_thread = operations_per_thread;
        assert(pthread_create(&threads[i], NULL, thread_benchmark_worker, &thread_args[i]) == 0);
    }
    
    // 等待所有线程完成
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    double end_time = get_time_us();
    double total_time = (end_time - start_time) / 1000000.0;
    
    // 收集所有线程的延迟数据
    double all_latencies[NUM_OPERATIONS];
    int total_latency_count = 0;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        for (int j = 0; j < thread_args[i].thread_latency_count; j++) {
            all_latencies[total_latency_count++] = thread_args[i].thread_latencies[j];
        }
        free(thread_args[i].thread_latencies);
    }
    
    result->write_latency_avg = calculate_average(all_latencies, total_latency_count);
    result->write_latency_p99 = calculate_percentile(all_latencies, total_latency_count, 99.0);
    result->throughput_ops_per_sec = total_latency_count / total_time;
    result->total_operations = total_latency_count;
    result->total_bytes_written = total_latency_count * (KEY_SIZE + VALUE_SIZE);
    
    printf("多线程写入完成:\n");
    printf("  线程数: %d\n", NUM_THREADS);
    printf("  总操作数: %d\n", result->total_operations);
    printf("  总耗时: %.2f 秒\n", total_time);
    printf("  吞吐量: %.0f ops/sec\n", result->throughput_ops_per_sec);
    printf("  平均延迟: %.2f μs\n", result->write_latency_avg);
    printf("  P99延迟: %.2f μs\n", result->write_latency_p99);
    printf("  总写入字节: %zu bytes\n", result->total_bytes_written);
}

// 同步性能基准测试
void benchmark_sync_performance(wal_t *wal, benchmark_result_t *result) {
    printf("\n=== 同步性能基准测试 ===\n");
    
    char key[KEY_SIZE];
    char value[VALUE_SIZE];
    double sync_latencies[1000];
    int sync_count = 0;
    
    // 写入一些数据，然后测试同步性能
    for (int i = 0; i < 1000; i++) {
        generate_random_string(key, KEY_SIZE);
        generate_random_string(value, VALUE_SIZE);
        wal_append(wal, WAL_OP_SET, key, value);
        
        if (i % 10 == 0) { // 每10次操作同步一次
            double sync_start = get_time_us();
            wal_force_sync(wal);
            double sync_end = get_time_us();
            sync_latencies[sync_count++] = sync_end - sync_start;
        }
    }
    
    result->sync_latency_avg = calculate_average(sync_latencies, sync_count);
    
    printf("同步性能测试完成:\n");
    printf("  同步次数: %d\n", sync_count);
    printf("  平均同步延迟: %.2f μs\n", result->sync_latency_avg);
}

// 压缩性能基准测试
void benchmark_compaction_performance() {
    printf("\n=== 压缩性能基准测试 ===\n");
    
    // 这里需要模拟存储引擎，暂时跳过具体实现
    printf("压缩性能基准测试需要完整的存储引擎支持，暂时跳过\n");
}

// 内存使用情况监控
void print_memory_usage() {
    FILE *status = fopen("/proc/self/status", "r");
    if (status) {
        char line[256];
        while (fgets(line, sizeof(line), status)) {
            if (strncmp(line, "VmRSS:", 6) == 0) {
                printf("内存使用: %s", line + 6);
                break;
            }
        }
        fclose(status);
    }
}

// 主基准测试函数
int main() {
    printf("开始增强持久化功能性能基准测试...\n");
    setup_benchmark_timeout();
    
    srand(time(NULL));
    
    // 清理测试目录
    system("rm -rf /tmp/benchmark_*");
    
    benchmark_result_t sync_result, async_result, multi_result;
    
    // 测试同步写入性能
    printf("\n🔄 测试同步写入性能...\n");
    wal_t sync_wal;
    assert(wal_init(&sync_wal, "/tmp/benchmark_sync", 1) == 0);
    
    g_latency_count = 0; // 重置延迟计数
    benchmark_single_thread_write(&sync_wal, &sync_result);
    benchmark_sync_performance(&sync_wal, &sync_result);
    
    wal_destroy(&sync_wal);
    
    // 测试异步写入性能
    printf("\n🚀 测试异步写入性能...\n");
    wal_t async_wal;
    assert(wal_init(&async_wal, "/tmp/benchmark_async", 0) == 0);
    
    g_latency_count = 0; // 重置延迟计数
    benchmark_single_thread_write(&async_wal, &async_result);
    
    wal_destroy(&async_wal);
    
    // 测试多线程写入性能
    printf("\n⚡ 测试多线程写入性能...\n");
    wal_t multi_wal;
    assert(wal_init(&multi_wal, "/tmp/benchmark_multi", 1) == 0);
    
    benchmark_multi_thread_write(&multi_wal, &multi_result);
    
    wal_destroy(&multi_wal);
    
    // 测试压缩性能
    benchmark_compaction_performance();
    
    // 打印总结报告
    printf("\n📊 性能基准测试总结报告\n");
    printf("============================================================\n");
    
    printf("同步写入模式:\n");
    printf("  吞吐量: %.0f ops/sec\n", sync_result.throughput_ops_per_sec);
    printf("  平均延迟: %.2f μs\n", sync_result.write_latency_avg);
    printf("  P99延迟: %.2f μs\n", sync_result.write_latency_p99);
    printf("  同步延迟: %.2f μs\n", sync_result.sync_latency_avg);
    
    printf("\n异步写入模式:\n");
    printf("  吞吐量: %.0f ops/sec\n", async_result.throughput_ops_per_sec);
    printf("  平均延迟: %.2f μs\n", async_result.write_latency_avg);
    printf("  P99延迟: %.2f μs\n", async_result.write_latency_p99);
    
    printf("\n多线程写入模式:\n");
    printf("  吞吐量: %.0f ops/sec\n", multi_result.throughput_ops_per_sec);
    printf("  平均延迟: %.2f μs\n", multi_result.write_latency_avg);
    printf("  P99延迟: %.2f μs\n", multi_result.write_latency_p99);
    
    printf("\n性能对比:\n");
    printf("  异步 vs 同步吞吐量提升: %.1fx\n", 
           async_result.throughput_ops_per_sec / sync_result.throughput_ops_per_sec);
    printf("  多线程 vs 单线程吞吐量提升: %.1fx\n", 
           multi_result.throughput_ops_per_sec / sync_result.throughput_ops_per_sec);
    
    print_memory_usage();
    
    printf("\n🎯 基准测试完成！\n");
    
    // 清理测试目录
    system("rm -rf /tmp/benchmark_*");
    
    clear_benchmark_timeout();
    return 0;
} 