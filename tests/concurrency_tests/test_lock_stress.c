/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV test suite - test_lock_stress.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include "../../kvserver/kv_concurrency.h"

#define MAX_THREADS 32
#define MAX_KEYS 1000
#define TEST_DURATION_SEC 10
#define LOCK_TIMEOUT_MS 1000

// 测试统计信息
typedef struct {
    long long total_operations;
    long long successful_locks;
    long long timeout_locks;
    long long deadlock_detections;
    long long errors;
    long long total_lock_time_us;  // 总锁定时间（微秒）
    long long max_lock_time_us;    // 最大锁定时间
    long long min_lock_time_us;    // 最小锁定时间
    pthread_mutex_t stats_mutex;
} test_stats_t;

// 线程工作参数
typedef struct {
    int thread_id;
    int num_threads;
    kv_segment_locks_t *seglocks;
    test_stats_t *stats;
    char **keys;
    int key_count;
    volatile int *stop_flag;
    int read_write_ratio;  // 读写比例 (0-100, 100表示全部读操作)
} worker_param_t;

// 全局统计信息
test_stats_t g_stats = {0};
volatile int g_stop_flag = 0;

// 获取微秒级时间
static long long get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
}

// 更新统计信息
static void update_stats(test_stats_t *stats, int result, long long lock_time_us) {
    pthread_mutex_lock(&stats->stats_mutex);
    
    stats->total_operations++;
    
    switch (result) {
        case KV_LOCK_SUCCESS:
            stats->successful_locks++;
            stats->total_lock_time_us += lock_time_us;
            if (lock_time_us > stats->max_lock_time_us) {
                stats->max_lock_time_us = lock_time_us;
            }
            if (stats->min_lock_time_us == 0 || lock_time_us < stats->min_lock_time_us) {
                stats->min_lock_time_us = lock_time_us;
            }
            break;
        case KV_LOCK_TIMEOUT:
            stats->timeout_locks++;
            break;
        case KV_LOCK_DEADLOCK:
            stats->deadlock_detections++;
            break;
        default:
            stats->errors++;
            break;
    }
    
    pthread_mutex_unlock(&stats->stats_mutex);
}

// 工作线程函数 - 单键操作
void* worker_single_key(void *arg) {
    worker_param_t *param = (worker_param_t*)arg;
    
    printf("线程 %d 开始单键压力测试\n", param->thread_id);
    
    while (!*(param->stop_flag)) {
        // 随机选择一个键
        int key_idx = rand() % param->key_count;
        const char *key = param->keys[key_idx];
        
        // 根据读写比例决定操作类型
        bool is_read = (rand() % 100) < param->read_write_ratio;
        
        long long start_time = get_time_us();
        int result;
        
        if (is_read) {
            // 读操作
            result = kv_segment_read_lock_timeout(param->seglocks, key, LOCK_TIMEOUT_MS);
            if (result == KV_LOCK_SUCCESS) {
                // 模拟读操作
                usleep(100 + (rand() % 500)); // 100-600微秒
                kv_segment_read_unlock(param->seglocks, key);
            }
        } else {
            // 写操作
            result = kv_segment_write_lock_timeout(param->seglocks, key, LOCK_TIMEOUT_MS);
            if (result == KV_LOCK_SUCCESS) {
                // 模拟写操作
                usleep(200 + (rand() % 1000)); // 200-1200微秒
                kv_segment_write_unlock(param->seglocks, key);
            }
        }
        
        long long lock_time = get_time_us() - start_time;
        update_stats(param->stats, result, lock_time);
        
        // 短暂休息
        usleep(10 + (rand() % 100)); // 10-110微秒
    }
    
    printf("线程 %d 单键测试完成\n", param->thread_id);
    return NULL;
}

// 工作线程函数 - 多键操作
void* worker_multi_key(void *arg) {
    worker_param_t *param = (worker_param_t*)arg;
    
    printf("线程 %d 开始多键压力测试\n", param->thread_id);
    
    while (!*(param->stop_flag)) {
        // 随机选择2-5个键
        int lock_count = 2 + (rand() % 4);
        const char *selected_keys[5];
        
        for (int i = 0; i < lock_count; i++) {
            int key_idx = rand() % param->key_count;
            selected_keys[i] = param->keys[key_idx];
        }
        
        // 随机选择锁类型
        kv_lock_type_t lock_type = (rand() % 100 < param->read_write_ratio) ? 
                                   LOCK_TYPE_READ : LOCK_TYPE_WRITE;
        
        long long start_time = get_time_us();
        int result = kv_segment_multi_lock(param->seglocks, selected_keys, lock_count, lock_type);
        
        if (result == KV_LOCK_SUCCESS) {
            // 模拟操作
            if (lock_type == LOCK_TYPE_READ) {
                usleep(200 + (rand() % 800)); // 200-1000微秒
            } else {
                usleep(500 + (rand() % 1500)); // 500-2000微秒
            }
            
            kv_segment_multi_unlock(param->seglocks, selected_keys, lock_count, lock_type);
        }
        
        long long lock_time = get_time_us() - start_time;
        update_stats(param->stats, result, lock_time);
        
        // 短暂休息
        usleep(50 + (rand() % 200)); // 50-250微秒
    }
    
    printf("线程 %d 多键测试完成\n", param->thread_id);
    return NULL;
}

// 信号处理函数
void signal_handler(int sig) {
    if (sig == SIGALRM) {
        g_stop_flag = 1;
        printf("\n测试时间到，停止所有线程...\n");
    }
}

// 打印统计信息
void print_stats(test_stats_t *stats, int duration_sec, int num_threads) {
    pthread_mutex_lock(&stats->stats_mutex);
    
    printf("\n=== 压力测试统计结果 ===\n");
    printf("测试时长: %d 秒\n", duration_sec);
    printf("线程数量: %d\n", num_threads);
    printf("总操作数: %lld\n", stats->total_operations);
    printf("成功锁定: %lld (%.2f%%)\n", stats->successful_locks, 
           (double)stats->successful_locks / stats->total_operations * 100);
    printf("超时锁定: %lld (%.2f%%)\n", stats->timeout_locks,
           (double)stats->timeout_locks / stats->total_operations * 100);
    printf("死锁检测: %lld (%.2f%%)\n", stats->deadlock_detections,
           (double)stats->deadlock_detections / stats->total_operations * 100);
    printf("错误操作: %lld (%.2f%%)\n", stats->errors,
           (double)stats->errors / stats->total_operations * 100);
    
    if (stats->successful_locks > 0) {
        printf("平均锁定时间: %.2f 微秒\n", 
               (double)stats->total_lock_time_us / stats->successful_locks);
        printf("最大锁定时间: %lld 微秒\n", stats->max_lock_time_us);
        printf("最小锁定时间: %lld 微秒\n", stats->min_lock_time_us);
    }
    
    printf("吞吐量: %.2f 操作/秒\n", (double)stats->total_operations / duration_sec);
    printf("成功吞吐量: %.2f 操作/秒\n", (double)stats->successful_locks / duration_sec);
    
    pthread_mutex_unlock(&stats->stats_mutex);
}

// 单键压力测试
int test_single_key_stress(int num_threads, int num_keys, int read_write_ratio) {
    printf("\n=== 单键压力测试 ===\n");
    printf("线程数: %d, 键数: %d, 读写比例: %d%%读\n", num_threads, num_keys, read_write_ratio);
    
    // 初始化分段锁
    kv_segment_locks_t seglocks;
    int ret = kv_segment_locks_init_with_timeout(&seglocks, 64, LOCK_TIMEOUT_MS);
    assert(ret == KV_LOCK_SUCCESS);
    
    // 准备测试键
    char **keys = malloc(num_keys * sizeof(char*));
    for (int i = 0; i < num_keys; i++) {
        keys[i] = malloc(32);
        snprintf(keys[i], 32, "stress_key_%d", i);
    }
    
    // 初始化统计信息
    memset(&g_stats, 0, sizeof(test_stats_t));
    pthread_mutex_init(&g_stats.stats_mutex, NULL);
    g_stop_flag = 0;
    
    // 设置信号处理
    signal(SIGALRM, signal_handler);
    alarm(TEST_DURATION_SEC);
    
    // 创建工作线程
    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    worker_param_t *params = malloc(num_threads * sizeof(worker_param_t));
    
    long long start_time = get_time_us();
    
    for (int i = 0; i < num_threads; i++) {
        params[i].thread_id = i;
        params[i].num_threads = num_threads;
        params[i].seglocks = &seglocks;
        params[i].stats = &g_stats;
        params[i].keys = keys;
        params[i].key_count = num_keys;
        params[i].stop_flag = &g_stop_flag;
        params[i].read_write_ratio = read_write_ratio;
        
        pthread_create(&threads[i], NULL, worker_single_key, &params[i]);
    }
    
    // 等待所有线程完成
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    long long total_time = get_time_us() - start_time;
    int actual_duration = total_time / 1000000;
    
    // 打印结果
    print_stats(&g_stats, actual_duration, num_threads);
    
    // 清理资源
    for (int i = 0; i < num_keys; i++) {
        free(keys[i]);
    }
    free(keys);
    free(threads);
    free(params);
    
    kv_segment_locks_destroy(&seglocks);
    pthread_mutex_destroy(&g_stats.stats_mutex);
    
    return 0;
}

// 多键压力测试
int test_multi_key_stress(int num_threads, int num_keys, int read_write_ratio) {
    printf("\n=== 多键压力测试 ===\n");
    printf("线程数: %d, 键数: %d, 读写比例: %d%%读\n", num_threads, num_keys, read_write_ratio);
    
    // 初始化死锁检测器
    kv_deadlock_detector_init(LOCK_TIMEOUT_MS);
    
    // 初始化分段锁
    kv_segment_locks_t seglocks;
    int ret = kv_segment_locks_init_with_timeout(&seglocks, 32, LOCK_TIMEOUT_MS);
    assert(ret == KV_LOCK_SUCCESS);
    
    // 准备测试键
    char **keys = malloc(num_keys * sizeof(char*));
    for (int i = 0; i < num_keys; i++) {
        keys[i] = malloc(32);
        snprintf(keys[i], 32, "multi_key_%d", i);
    }
    
    // 初始化统计信息
    memset(&g_stats, 0, sizeof(test_stats_t));
    pthread_mutex_init(&g_stats.stats_mutex, NULL);
    g_stop_flag = 0;
    
    // 设置信号处理
    signal(SIGALRM, signal_handler);
    alarm(TEST_DURATION_SEC);
    
    // 创建工作线程
    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    worker_param_t *params = malloc(num_threads * sizeof(worker_param_t));
    
    long long start_time = get_time_us();
    
    for (int i = 0; i < num_threads; i++) {
        params[i].thread_id = i;
        params[i].num_threads = num_threads;
        params[i].seglocks = &seglocks;
        params[i].stats = &g_stats;
        params[i].keys = keys;
        params[i].key_count = num_keys;
        params[i].stop_flag = &g_stop_flag;
        params[i].read_write_ratio = read_write_ratio;
        
        pthread_create(&threads[i], NULL, worker_multi_key, &params[i]);
    }
    
    // 等待所有线程完成
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    long long total_time = get_time_us() - start_time;
    int actual_duration = total_time / 1000000;
    
    // 打印结果
    print_stats(&g_stats, actual_duration, num_threads);
    
    // 清理资源
    for (int i = 0; i < num_keys; i++) {
        free(keys[i]);
    }
    free(keys);
    free(threads);
    free(params);
    
    kv_segment_locks_destroy(&seglocks);
    kv_deadlock_detector_destroy();
    pthread_mutex_destroy(&g_stats.stats_mutex);
    
    return 0;
}

// 可扩展性测试
int test_scalability(void) {
    printf("\n=== 可扩展性测试 ===\n");
    
    int thread_counts[] = {1, 2, 4, 8, 16, 32};
    int num_tests = sizeof(thread_counts) / sizeof(thread_counts[0]);
    
    printf("线程数\t吞吐量(ops/s)\t成功率(%%)\t平均延迟(μs)\n");
    printf("------\t-----------\t--------\t-----------\n");
    
    for (int i = 0; i < num_tests; i++) {
        int num_threads = thread_counts[i];
        
        // 运行短时间测试
        kv_segment_locks_t seglocks;
        kv_segment_locks_init_with_timeout(&seglocks, 32, LOCK_TIMEOUT_MS);
        
        char **keys = malloc(100 * sizeof(char*));
        for (int j = 0; j < 100; j++) {
            keys[j] = malloc(32);
            snprintf(keys[j], 32, "scale_key_%d", j);
        }
        
        memset(&g_stats, 0, sizeof(test_stats_t));
        pthread_mutex_init(&g_stats.stats_mutex, NULL);
        g_stop_flag = 0;
        
        pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
        worker_param_t *params = malloc(num_threads * sizeof(worker_param_t));
        
        // 运行3秒测试
        signal(SIGALRM, signal_handler);
        alarm(3);
        
        long long start_time = get_time_us();
        
        for (int j = 0; j < num_threads; j++) {
            params[j].thread_id = j;
            params[j].seglocks = &seglocks;
            params[j].stats = &g_stats;
            params[j].keys = keys;
            params[j].key_count = 100;
            params[j].stop_flag = &g_stop_flag;
            params[j].read_write_ratio = 70; // 70%读操作
            
            pthread_create(&threads[j], NULL, worker_single_key, &params[j]);
        }
        
        for (int j = 0; j < num_threads; j++) {
            pthread_join(threads[j], NULL);
        }
        
        long long total_time = get_time_us() - start_time;
        double duration_sec = total_time / 1000000.0;
        
        double throughput = g_stats.total_operations / duration_sec;
        double success_rate = (double)g_stats.successful_locks / g_stats.total_operations * 100;
        double avg_latency = g_stats.successful_locks > 0 ? 
                           (double)g_stats.total_lock_time_us / g_stats.successful_locks : 0;
        
        printf("%d\t%.2f\t\t%.2f\t\t%.2f\n", num_threads, throughput, success_rate, avg_latency);
        
        // 清理
        for (int j = 0; j < 100; j++) {
            free(keys[j]);
        }
        free(keys);
        free(threads);
        free(params);
        kv_segment_locks_destroy(&seglocks);
        pthread_mutex_destroy(&g_stats.stats_mutex);
    }
    
    return 0;
}

int main(int argc, char *argv[]) {
    printf("ConcordKV 并发控制压力测试\n");
    printf("测试持续时间: %d 秒\n", TEST_DURATION_SEC);
    printf("锁超时时间: %d 毫秒\n", LOCK_TIMEOUT_MS);
    
    // 设置随机种子
    srand(time(NULL));
    
    // 解析命令行参数
    int num_threads = 8;
    int num_keys = 100;
    int read_write_ratio = 70; // 70%读操作
    
    if (argc > 1) num_threads = atoi(argv[1]);
    if (argc > 2) num_keys = atoi(argv[2]);
    if (argc > 3) read_write_ratio = atoi(argv[3]);
    
    printf("默认配置: %d 线程, %d 键, %d%% 读操作\n", 
           num_threads, num_keys, read_write_ratio);
    
    // 运行测试
    test_single_key_stress(num_threads, num_keys, read_write_ratio);
    test_multi_key_stress(num_threads / 2, num_keys, read_write_ratio);
    test_scalability();
    
    printf("\n所有压力测试完成！\n");
    return 0;
} 