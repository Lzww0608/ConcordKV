#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include "lsm_bloom_filter_optimized.h"

#define NUM_THREADS 8
#define NUM_OPERATIONS 1000

typedef struct {
    optimized_bloom_filter_t *filter;
    int thread_id;
    int num_operations;
    int operation_type; // 0: read, 1: write
} thread_data_t;

void* worker_thread(void* arg) {
    thread_data_t *data = (thread_data_t*)arg;
    char key[64];
    
    printf("线程 %d 开始 (%s 操作)\n", data->thread_id, 
           data->operation_type ? "写" : "读");
    
    for (int i = 0; i < data->num_operations; i++) {
        snprintf(key, sizeof(key), "thread_%d_key_%d", data->thread_id, i);
        
        if (data->operation_type == 1) {
            // 写操作
            bloom_filter_add_optimized(data->filter, key, strlen(key));
        } else {
            // 读操作
            bloom_filter_may_contain_optimized(data->filter, key, strlen(key));
        }
        
        // 模拟一些工作
        if (i % 100 == 0) {
            usleep(1); // 1微秒
        }
    }
    
    printf("线程 %d 完成\n", data->thread_id);
    return NULL;
}

int main(void) {
    printf("🧵 ConcordKV 布隆过滤器读写锁测试\n");
    printf("=====================================\n");
    
    // 创建布隆过滤器
    bloom_filter_config_t config = bloom_filter_create_default_config(100000, 100);
    config.type = BLOOM_TYPE_STANDARD;
    config.hash_type = BLOOM_HASH_MURMUR3;
    config.enable_statistics = true;
    
    optimized_bloom_filter_t *filter = bloom_filter_create_optimized(&config);
    if (!filter) {
        printf("❌ 创建布隆过滤器失败\n");
        return 1;
    }
    
    printf("✅ 布隆过滤器创建成功\n");
    printf("线程数: %d\n", NUM_THREADS);
    printf("每线程操作数: %d\n", NUM_OPERATIONS);
    
    // 创建线程
    pthread_t threads[NUM_THREADS];
    thread_data_t thread_data[NUM_THREADS];
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    // 启动线程 - 混合读写操作
    for (int i = 0; i < NUM_THREADS; i++) {
        thread_data[i].filter = filter;
        thread_data[i].thread_id = i;
        thread_data[i].num_operations = NUM_OPERATIONS;
        thread_data[i].operation_type = (i % 3 == 0) ? 1 : 0; // 1/3写线程，2/3读线程
        
        int result = pthread_create(&threads[i], NULL, worker_thread, &thread_data[i]);
        if (result != 0) {
            printf("❌ 创建线程 %d 失败\n", i);
            return 1;
        }
    }
    
    // 等待所有线程完成
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    
    printf("\n📊 测试结果:\n");
    printf("总耗时: %.3f 秒\n", elapsed);
    printf("总操作数: %d\n", NUM_THREADS * NUM_OPERATIONS);
    printf("平均吞吐量: %.0f ops/sec\n", (NUM_THREADS * NUM_OPERATIONS) / elapsed);
    
    // 获取统计信息
    bloom_filter_stats_t stats;
    if (bloom_filter_get_stats_optimized(filter, &stats) == 0) {
        printf("\n📈 布隆过滤器统计:\n");
        printf("总插入: %lu\n", stats.total_insertions);
        printf("总查询: %lu\n", stats.total_queries);
        printf("命中数: %lu\n", stats.total_hits);
        printf("当前条目数: %lu\n", filter->current_entries);
        printf("填充率: %.2f%%\n", stats.fill_ratio * 100.0);
    }
    
    // 验证数据一致性
    printf("\n🔍 验证数据一致性...\n");
    int write_threads = 0;
    for (int i = 0; i < NUM_THREADS; i++) {
        if (thread_data[i].operation_type == 1) {
            write_threads++;
        }
    }
    
    printf("写线程数: %d\n", write_threads);
    printf("预期插入数: %d\n", write_threads * NUM_OPERATIONS);
    printf("实际插入数: %lu\n", stats.total_insertions);
    
    if (stats.total_insertions == write_threads * NUM_OPERATIONS) {
        printf("✅ 数据一致性验证通过\n");
    } else {
        printf("❌ 数据一致性验证失败\n");
    }
    
    bloom_filter_destroy_optimized(filter);
    printf("\n🎉 读写锁测试完成！\n");
    
    return 0;
} 