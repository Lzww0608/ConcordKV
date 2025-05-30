/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV test suite - test_deadlock_detection.c
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
#include "../../kvserver/kv_concurrency.h"

#define TEST_TIMEOUT_MS 2000
#define NUM_THREADS 4
#define NUM_KEYS 10

// 测试结果结构
typedef struct {
    int success_count;
    int timeout_count;
    int deadlock_count;
    int error_count;
    pthread_mutex_t result_mutex;
} test_result_t;

// 线程参数结构
typedef struct {
    int thread_id;
    kv_segment_locks_t *seglocks;
    test_result_t *result;
    char **keys;
    int key_count;
    int test_duration_ms;
} thread_param_t;

// 全局测试结果
test_result_t g_test_result = {0};

// 获取当前时间（毫秒）
static long long get_current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// 更新测试结果
static void update_result(test_result_t *result, int ret_code) {
    pthread_mutex_lock(&result->result_mutex);
    switch (ret_code) {
        case KV_LOCK_SUCCESS:
            result->success_count++;
            break;
        case KV_LOCK_TIMEOUT:
            result->timeout_count++;
            break;
        case KV_LOCK_DEADLOCK:
            result->deadlock_count++;
            break;
        default:
            result->error_count++;
            break;
    }
    pthread_mutex_unlock(&result->result_mutex);
}

// 线程函数包装器
void* timeout_test_wrapper(void *arg) {
    kv_rwlock_t *lock = (kv_rwlock_t*)arg;
    int result = kv_write_lock_timeout(lock, 1000);
    return (void*)(intptr_t)result;
}

// 多键锁定包装器
void* multi_lock_wrapper(void *arg) {
    thread_param_t *param = (thread_param_t*)arg;
    const char *keys[] = {"key_b", "key_c", "key_a"};
    int result = kv_segment_multi_lock(param->seglocks, keys, 3, LOCK_TYPE_WRITE);
    return (void*)(intptr_t)result;
}

// 测试线程函数 - 随机锁定多个键
void* test_random_multi_lock(void *arg) {
    thread_param_t *param = (thread_param_t*)arg;
    long long start_time = get_current_time_ms();
    long long end_time = start_time + param->test_duration_ms;
    
    printf("线程 %d 开始测试，持续时间: %d ms\n", param->thread_id, param->test_duration_ms);
    
    while (get_current_time_ms() < end_time) {
        // 随机选择2-4个键进行锁定
        int lock_count = 2 + (rand() % 3);
        const char *selected_keys[4];
        
        // 随机选择键
        for (int i = 0; i < lock_count; i++) {
            int key_idx = rand() % param->key_count;
            selected_keys[i] = param->keys[key_idx];
        }
        
        // 随机选择锁类型
        kv_lock_type_t lock_type = (rand() % 2) ? LOCK_TYPE_READ : LOCK_TYPE_WRITE;
        
        // 尝试获取多个锁
        int ret = kv_segment_multi_lock(param->seglocks, selected_keys, lock_count, lock_type);
        update_result(param->result, ret);
        
        if (ret == KV_LOCK_SUCCESS) {
            // 持有锁一段时间
            usleep(10000 + (rand() % 50000)); // 10-60ms
            
            // 释放锁
            kv_segment_multi_unlock(param->seglocks, selected_keys, lock_count, lock_type);
        }
        
        // 短暂休息
        usleep(1000 + (rand() % 5000)); // 1-6ms
    }
    
    printf("线程 %d 测试完成\n", param->thread_id);
    return NULL;
}

// 测试线程函数 - 故意制造死锁场景
void* test_deadlock_scenario(void *arg) {
    thread_param_t *param = (thread_param_t*)arg;
    
    printf("线程 %d 开始死锁测试\n", param->thread_id);
    
    // 根据线程ID选择不同的锁定顺序
    const char *keys[2];
    if (param->thread_id % 2 == 0) {
        keys[0] = "key_deadlock_1";
        keys[1] = "key_deadlock_2";
    } else {
        keys[0] = "key_deadlock_2";
        keys[1] = "key_deadlock_1";
    }
    
    // 获取第一个锁
    int ret1 = kv_segment_write_lock_timeout(param->seglocks, keys[0], TEST_TIMEOUT_MS);
    update_result(param->result, ret1);
    
    if (ret1 == KV_LOCK_SUCCESS) {
        printf("线程 %d 获取了锁 %s\n", param->thread_id, keys[0]);
        
        // 等待一段时间，让其他线程也获取第一个锁
        usleep(100000); // 100ms
        
        // 尝试获取第二个锁（可能导致死锁）
        int ret2 = kv_segment_write_lock_timeout(param->seglocks, keys[1], TEST_TIMEOUT_MS);
        update_result(param->result, ret2);
        
        if (ret2 == KV_LOCK_SUCCESS) {
            printf("线程 %d 获取了锁 %s\n", param->thread_id, keys[1]);
            kv_segment_write_unlock(param->seglocks, keys[1]);
        } else {
            printf("线程 %d 获取锁 %s 失败: %d\n", param->thread_id, keys[1], ret2);
        }
        
        kv_segment_write_unlock(param->seglocks, keys[0]);
    } else {
        printf("线程 %d 获取锁 %s 失败: %d\n", param->thread_id, keys[0], ret1);
    }
    
    printf("线程 %d 死锁测试完成\n", param->thread_id);
    return NULL;
}

// 测试基本锁超时功能
int test_basic_timeout(void) {
    printf("\n=== 测试基本锁超时功能 ===\n");
    
    kv_rwlock_t lock;
    int ret = kv_rwlock_init_with_timeout(&lock, 1000); // 1秒超时
    assert(ret == KV_LOCK_SUCCESS);
    
    // 在主线程中获取写锁
    ret = kv_write_lock(&lock);
    assert(ret == KV_LOCK_SUCCESS);
    printf("主线程获取写锁成功\n");
    
    // 创建子线程尝试获取写锁（应该超时）
    pthread_t thread;
    long long start_time = get_current_time_ms();
    
    pthread_create(&thread, NULL, timeout_test_wrapper, &lock);
    
    // 等待子线程完成
    void *thread_result;
    pthread_join(thread, &thread_result);
    
    long long elapsed = get_current_time_ms() - start_time;
    printf("子线程等待时间: %lld ms\n", elapsed);
    printf("子线程返回结果: %d\n", (int)(intptr_t)thread_result);
    
    // 释放主线程的锁
    kv_write_unlock(&lock);
    kv_rwlock_destroy(&lock);
    
    printf("基本超时测试完成\n");
    return 0;
}

// 测试分段锁的并发性能
int test_segment_lock_performance(void) {
    printf("\n=== 测试分段锁并发性能 ===\n");
    
    kv_segment_locks_t seglocks;
    int ret = kv_segment_locks_init_with_timeout(&seglocks, 16, TEST_TIMEOUT_MS);
    assert(ret == KV_LOCK_SUCCESS);
    
    // 准备测试键
    char *keys[NUM_KEYS];
    for (int i = 0; i < NUM_KEYS; i++) {
        keys[i] = malloc(32);
        snprintf(keys[i], 32, "test_key_%d", i);
    }
    
    // 初始化测试结果
    memset(&g_test_result, 0, sizeof(test_result_t));
    pthread_mutex_init(&g_test_result.result_mutex, NULL);
    
    // 创建测试线程
    pthread_t threads[NUM_THREADS];
    thread_param_t params[NUM_THREADS];
    
    long long start_time = get_current_time_ms();
    
    for (int i = 0; i < NUM_THREADS; i++) {
        params[i].thread_id = i;
        params[i].seglocks = &seglocks;
        params[i].result = &g_test_result;
        params[i].keys = keys;
        params[i].key_count = NUM_KEYS;
        params[i].test_duration_ms = 5000; // 5秒测试
        
        pthread_create(&threads[i], NULL, test_random_multi_lock, &params[i]);
    }
    
    // 等待所有线程完成
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    long long total_time = get_current_time_ms() - start_time;
    
    // 输出测试结果
    printf("并发性能测试结果:\n");
    printf("  总测试时间: %lld ms\n", total_time);
    printf("  成功操作: %d\n", g_test_result.success_count);
    printf("  超时操作: %d\n", g_test_result.timeout_count);
    printf("  死锁检测: %d\n", g_test_result.deadlock_count);
    printf("  错误操作: %d\n", g_test_result.error_count);
    printf("  总操作数: %d\n", g_test_result.success_count + g_test_result.timeout_count + 
                                g_test_result.deadlock_count + g_test_result.error_count);
    
    // 清理资源
    for (int i = 0; i < NUM_KEYS; i++) {
        free(keys[i]);
    }
    
    kv_segment_locks_destroy(&seglocks);
    pthread_mutex_destroy(&g_test_result.result_mutex);
    
    printf("分段锁性能测试完成\n");
    return 0;
}

// 测试死锁检测功能
int test_deadlock_detection(void) {
    printf("\n=== 测试死锁检测功能 ===\n");
    
    // 初始化死锁检测器
    int ret = kv_deadlock_detector_init(TEST_TIMEOUT_MS);
    assert(ret == KV_LOCK_SUCCESS);
    
    kv_segment_locks_t seglocks;
    ret = kv_segment_locks_init_with_timeout(&seglocks, 8, TEST_TIMEOUT_MS);
    assert(ret == KV_LOCK_SUCCESS);
    
    // 重置测试结果
    memset(&g_test_result, 0, sizeof(test_result_t));
    pthread_mutex_init(&g_test_result.result_mutex, NULL);
    
    // 创建死锁测试线程
    pthread_t threads[4];
    thread_param_t params[4];
    
    for (int i = 0; i < 4; i++) {
        params[i].thread_id = i;
        params[i].seglocks = &seglocks;
        params[i].result = &g_test_result;
        params[i].keys = NULL;
        params[i].key_count = 0;
        params[i].test_duration_ms = 0;
        
        pthread_create(&threads[i], NULL, test_deadlock_scenario, &params[i]);
    }
    
    // 等待所有线程完成
    for (int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // 输出死锁测试结果
    printf("死锁检测测试结果:\n");
    printf("  成功操作: %d\n", g_test_result.success_count);
    printf("  超时操作: %d\n", g_test_result.timeout_count);
    printf("  死锁检测: %d\n", g_test_result.deadlock_count);
    printf("  错误操作: %d\n", g_test_result.error_count);
    
    // 清理资源
    kv_segment_locks_destroy(&seglocks);
    kv_deadlock_detector_destroy();
    pthread_mutex_destroy(&g_test_result.result_mutex);
    
    printf("死锁检测测试完成\n");
    return 0;
}

// 测试锁排序功能（避免死锁）
int test_lock_ordering(void) {
    printf("\n=== 测试锁排序功能 ===\n");
    
    kv_segment_locks_t seglocks;
    int ret = kv_segment_locks_init_with_timeout(&seglocks, 8, TEST_TIMEOUT_MS);
    assert(ret == KV_LOCK_SUCCESS);
    
    // 测试多键锁定的排序功能
    const char *keys1[] = {"key_c", "key_a", "key_b"};
    
    printf("测试多键锁定排序...\n");
    
    // 两个线程同时锁定相同的键，但顺序不同
    // 由于内部排序，应该不会发生死锁
    pthread_t thread2;
    
    // 线程1锁定
    ret = kv_segment_multi_lock(&seglocks, keys1, 3, LOCK_TYPE_WRITE);
    printf("线程1多键锁定结果: %d\n", ret);
    assert(ret == KV_LOCK_SUCCESS);
    
    // 创建线程2尝试锁定（应该超时，但不会死锁）
    long long start_time = get_current_time_ms();
    
    // 创建参数结构
    thread_param_t param = {
        .seglocks = &seglocks,
        .thread_id = 2
    };
    
    // 在新线程中尝试锁定
    pthread_create(&thread2, NULL, multi_lock_wrapper, &param);
    
    // 等待一段时间后释放线程1的锁
    usleep(500000); // 500ms
    kv_segment_multi_unlock(&seglocks, keys1, 3, LOCK_TYPE_WRITE);
    printf("线程1释放锁\n");
    
    // 等待线程2完成
    void *thread_result;
    pthread_join(thread2, &thread_result);
    
    long long elapsed = get_current_time_ms() - start_time;
    printf("锁排序测试耗时: %lld ms\n", elapsed);
    printf("线程2返回结果: %d\n", (int)(intptr_t)thread_result);
    
    kv_segment_locks_destroy(&seglocks);
    
    printf("锁排序测试完成\n");
    return 0;
}

int main(void) {
    printf("开始并发控制增强功能测试\n");
    printf("测试超时设置: %d ms\n", TEST_TIMEOUT_MS);
    
    // 设置随机种子
    srand(time(NULL));
    
    // 运行各项测试
    test_basic_timeout();
    test_segment_lock_performance();
    test_deadlock_detection();
    test_lock_ordering();
    
    printf("\n所有测试完成！\n");
    return 0;
} 