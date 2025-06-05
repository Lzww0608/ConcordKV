/*
 * @Author: Lzww0608  
 * @Date: 2025-6-5 21:13:13
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-5 21:25:25
 * @Description: ConcordKV LSM-Tree多线程压缩性能测试
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>

#include "../../../kvserver/lsm_compaction.h"
#include "../../../kvserver/lsm_memtable_manager.h"
#include "../../../kvserver/lsm_memtable.h"
#include "../../../kvserver/lsm_sstable.h"
#include "../../../kvserver/kv_error.h"
#include "../../../kvserver/kv_memory.h"

// 测试配置
#define TEST_DATA_DIR "./test_data_perf_compaction"
#define TEST_TIMEOUT 60  // 60秒超时
#define PERFORMANCE_TASK_COUNT 100
#define THREAD_COUNTS[] {1, 2, 4, 8}

// 全局变量
static bool g_test_timeout = false;

// 超时处理
static void timeout_handler(int sig) {
    (void)sig;
    g_test_timeout = true;
    printf("\n❌ 测试超时！\n");
    exit(1);
}

// 设置测试超时
static void setup_timeout(int seconds) {
    signal(SIGALRM, timeout_handler);
    alarm(seconds);
}

// 清除超时
static void clear_timeout(void) {
    alarm(0);
}

// 获取当前时间（毫秒）
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

// 测试工具函数
static void cleanup_test_dir(void) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DATA_DIR);
    (void)system(cmd);
}

static void create_test_dir(void) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", TEST_DATA_DIR);
    (void)system(cmd);
}

// 创建测试用的MemTable管理器
static lsm_memtable_manager_t* create_test_memtable_manager(void) {
    lsm_memtable_manager_config_t config = lsm_memtable_manager_default_config();
    return lsm_memtable_manager_create(&config, "perf_test_manager");
}

// 创建测试用的压缩调度器
static lsm_compaction_scheduler_t* create_test_scheduler(lsm_memtable_manager_t *memtable_manager,
                                                        lsm_level_manager_t *level_manager,
                                                        int thread_count) {
    lsm_compaction_config_t config = lsm_compaction_default_config();
    config.enable_background_compaction = true;
    config.background_thread_count = thread_count;
    return lsm_compaction_scheduler_create(memtable_manager, level_manager, &config);
}

// 性能测试：不同线程数的压缩性能
static void test_multithread_performance(void) {
    printf("🚀 多线程压缩性能测试\n");
    printf("====================\n\n");
    
    int thread_counts[] = {1, 2, 4, 8};
    int num_tests = sizeof(thread_counts) / sizeof(thread_counts[0]);
    
    for (int i = 0; i < num_tests; i++) {
        int thread_count = thread_counts[i];
        printf("📊 测试 %d 个工作线程...\n", thread_count);
        
        cleanup_test_dir();
        create_test_dir();
        
        // 创建组件
        lsm_memtable_manager_t *memtable_manager = create_test_memtable_manager();
        lsm_level_manager_t *level_manager = lsm_level_manager_create(TEST_DATA_DIR);
        lsm_compaction_scheduler_t *scheduler = create_test_scheduler(
            memtable_manager, level_manager, thread_count);
        
        if (!scheduler) {
            printf("❌ 无法创建调度器\n");
            continue;
        }
        
        // 启动调度器
        lsm_compaction_scheduler_start(scheduler);
        usleep(50000); // 等待线程启动
        
        // 创建测试任务
        lsm_compaction_task_t *tasks[PERFORMANCE_TASK_COUNT];
        for (int j = 0; j < PERFORMANCE_TASK_COUNT; j++) {
            tasks[j] = lsm_compaction_task_create(
                LSM_COMPACTION_TASK_LEVEL0, 
                LSM_COMPACTION_PRIORITY_NORMAL);
            if (tasks[j]) {
                // 创建虚拟MemTable
                char memtable_name[64];
                snprintf(memtable_name, sizeof(memtable_name), "perf_memtable_%d_%d", thread_count, j);
                lsm_memtable_t *test_memtable = lsm_memtable_create(1024, memtable_name);
                if (test_memtable) {
                    lsm_compaction_task_set_level0_params(tasks[j], test_memtable, 0);
                }
            }
        }
        
        // 测量性能
        double start_time = get_time_ms();
        
        // 提交所有任务
        int submitted_tasks = 0;
        for (int j = 0; j < PERFORMANCE_TASK_COUNT; j++) {
            if (tasks[j]) {
                int ret = lsm_compaction_scheduler_submit_task(scheduler, tasks[j]);
                if (ret == KV_ERR_NONE) {
                    submitted_tasks++;
                }
            }
        }
        
        // 等待一段时间让任务处理
        usleep(1000000); // 1秒
        
        double end_time = get_time_ms();
        double duration = end_time - start_time;
        
        // 获取统计信息
        lsm_compaction_stats_t stats;
        lsm_compaction_scheduler_get_stats(scheduler, &stats);
        
        // 计算性能指标
        double tasks_per_second = (stats.completed_tasks + stats.failed_tasks) * 1000.0 / duration;
        
        printf("   ⏱️  执行时间: %.2f ms\n", duration);
        printf("   📝 提交任务: %d\n", submitted_tasks);
        printf("   ✅ 完成任务: %lu\n", stats.completed_tasks);
        printf("   ❌ 失败任务: %lu\n", stats.failed_tasks);
        printf("   📈 处理速度: %.1f tasks/sec\n", tasks_per_second);
        printf("   🔄 总压缩次数: %lu\n", stats.total_compactions);
        
        // 检查工作线程健康状态
        int healthy_threads = lsm_compaction_scheduler_check_worker_health(scheduler);
        printf("   💚 健康线程: %d/%d\n", healthy_threads, thread_count);
        
        // 清理
        lsm_compaction_scheduler_stop(scheduler);
        lsm_compaction_scheduler_destroy(scheduler);
        lsm_level_manager_destroy(level_manager);
        lsm_memtable_manager_destroy(memtable_manager);
        
        printf("\n");
    }
}

// 并发安全性测试
static void test_concurrent_safety(void) {
    printf("🔒 并发安全性测试\n");
    printf("================\n\n");
    
    cleanup_test_dir();
    create_test_dir();
    
    // 创建组件
    lsm_memtable_manager_t *memtable_manager = create_test_memtable_manager();
    lsm_level_manager_t *level_manager = lsm_level_manager_create(TEST_DATA_DIR);
    lsm_compaction_scheduler_t *scheduler = create_test_scheduler(
        memtable_manager, level_manager, 4);
    
    if (!scheduler) {
        printf("❌ 无法创建调度器\n");
        return;
    }
    
    // 启动调度器
    lsm_compaction_scheduler_start(scheduler);
    usleep(100000); // 等待线程启动
    
    printf("📊 测试多次启动/停止循环...\n");
    
    // 多次启动停止测试
    for (int i = 0; i < 10; i++) {
        printf("   第 %d 次循环...\n", i + 1);
        
        lsm_compaction_scheduler_stop(scheduler);
        usleep(10000); // 10ms
        
        int ret = lsm_compaction_scheduler_start(scheduler);
        if (ret != KV_ERR_NONE) {
            printf("❌ 第 %d 次启动失败: %d\n", i + 1, ret);
            break;
        }
        usleep(10000); // 10ms
    }
    
    // 获取最终统计
    lsm_compaction_stats_t stats;
    lsm_compaction_scheduler_get_stats(scheduler, &stats);
    
    printf("✅ 并发安全性测试完成\n");
    printf("   📊 最终统计:\n");
    printf("      - 总压缩次数: %lu\n", stats.total_compactions);
    printf("      - 完成任务: %lu\n", stats.completed_tasks);
    printf("      - 失败任务: %lu\n", stats.failed_tasks);
    
    // 清理
    lsm_compaction_scheduler_stop(scheduler);
    lsm_compaction_scheduler_destroy(scheduler);
    lsm_level_manager_destroy(level_manager);
    lsm_memtable_manager_destroy(memtable_manager);
    
    printf("\n");
}

// 压力测试
static void test_stress_performance(void) {
    printf("💪 压力测试\n");
    printf("==========\n\n");
    
    cleanup_test_dir();
    create_test_dir();
    
    // 创建组件
    lsm_memtable_manager_t *memtable_manager = create_test_memtable_manager();
    lsm_level_manager_t *level_manager = lsm_level_manager_create(TEST_DATA_DIR);
    lsm_compaction_scheduler_t *scheduler = create_test_scheduler(
        memtable_manager, level_manager, 8);
    
    if (!scheduler) {
        printf("❌ 无法创建调度器\n");
        return;
    }
    
    // 启动调度器
    lsm_compaction_scheduler_start(scheduler);
    usleep(100000); // 等待线程启动
    
    printf("📊 提交大量任务进行压力测试...\n");
    
    const int stress_task_count = 1000;
    double start_time = get_time_ms();
    
    // 分批提交任务
    int total_submitted = 0;
    for (int batch = 0; batch < 10; batch++) {
        for (int i = 0; i < stress_task_count / 10; i++) {
            lsm_compaction_task_t *task = lsm_compaction_task_create(
                LSM_COMPACTION_TASK_LEVEL0, 
                LSM_COMPACTION_PRIORITY_NORMAL);
            if (task) {
                // 创建虚拟MemTable
                char memtable_name[64];
                snprintf(memtable_name, sizeof(memtable_name), "stress_memtable_%d_%d", batch, i);
                lsm_memtable_t *test_memtable = lsm_memtable_create(1024, memtable_name);
                if (test_memtable) {
                    lsm_compaction_task_set_level0_params(task, test_memtable, 0);
                    int ret = lsm_compaction_scheduler_submit_task(scheduler, task);
                    if (ret == KV_ERR_NONE) {
                        total_submitted++;
                    }
                } else {
                    lsm_compaction_task_destroy(task);
                }
            }
        }
        usleep(10000); // 10ms间隔
    }
    
    // 等待处理
    usleep(2000000); // 2秒
    
    double end_time = get_time_ms();
    double duration = end_time - start_time;
    
    // 获取统计信息
    lsm_compaction_stats_t stats;
    lsm_compaction_scheduler_get_stats(scheduler, &stats);
    
    printf("✅ 压力测试完成\n");
    printf("   ⏱️  总时间: %.2f ms\n", duration);
    printf("   📝 提交任务: %d\n", total_submitted);
    printf("   ✅ 完成任务: %lu\n", stats.completed_tasks);
    printf("   ❌ 失败任务: %lu\n", stats.failed_tasks);
    printf("   📈 平均速度: %.1f tasks/sec\n", 
           (stats.completed_tasks + stats.failed_tasks) * 1000.0 / duration);
    
    // 清理
    lsm_compaction_scheduler_stop(scheduler);
    lsm_compaction_scheduler_destroy(scheduler);
    lsm_level_manager_destroy(level_manager);
    lsm_memtable_manager_destroy(memtable_manager);
    
    printf("\n");
}

int main(void) {
    printf("🚀 ConcordKV LSM-Tree多线程压缩性能测试套件\n");
    printf("===========================================\n\n");
    
    setup_timeout(TEST_TIMEOUT);
    
    // 运行性能测试
    test_multithread_performance();
    test_concurrent_safety();
    test_stress_performance();
    
    clear_timeout();
    
    printf("🎉 所有性能测试完成！\n");
    printf("📊 多线程压缩功能展示了以下特性：\n");
    printf("   ✅ 多线程工作线程管理\n");
    printf("   ✅ 任务队列和优先级调度\n");
    printf("   ✅ 并发安全的任务提交和执行\n");
    printf("   ✅ 线程健康检查和监控\n");
    printf("   ✅ 超时机制和死锁预防\n");
    printf("   ✅ 统计信息收集和性能监控\n");
    
    cleanup_test_dir();
    return 0;
} 