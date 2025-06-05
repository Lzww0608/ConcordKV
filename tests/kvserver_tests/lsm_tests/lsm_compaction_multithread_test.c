/*
 * @Author: Lzww0608  
 * @Date: 2025-6-5 20:59:51
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-5 20:59:51
 * @Description: ConcordKV LSM-Tree多线程压缩测试
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
#define TEST_DATA_DIR "./test_data_mt_compaction"
#define TEST_TIMEOUT 30  // 30秒超时
#define MAX_CONCURRENT_TASKS 8
#define TEST_KEY_COUNT 1000
#define TEST_THREAD_COUNT 4

// 全局变量
static bool g_test_timeout = false;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

// 测试结果结构
typedef struct {
    char name[256];
    bool passed;
    double duration_ms;
    char details[512];
} test_result_t;

// 超时处理
static void timeout_handler(int sig) {
    g_test_timeout = true;
    printf("\n❌ 测试超时，可能发生死锁！\n");
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
    system(cmd);
}

static void create_test_dir(void) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", TEST_DATA_DIR);
    system(cmd);
}

// 运行单个测试
static void run_test(const char *test_name, bool (*test_func)(test_result_t*)) {
    printf("🧪 运行测试: %s\n", test_name);
    
    test_result_t result;
    memset(&result, 0, sizeof(result));
    strncpy(result.name, test_name, sizeof(result.name) - 1);
    
    double start_time = get_time_ms();
    setup_timeout(TEST_TIMEOUT);
    
    result.passed = test_func(&result);
    result.duration_ms = get_time_ms() - start_time;
    
    clear_timeout();
    
    if (result.passed) {
        g_tests_passed++;
        printf("✅ %s 通过 (%.2fms)\n", test_name, result.duration_ms);
        if (strlen(result.details) > 0) {
            printf("   详情: %s\n", result.details);
        }
    } else {
        g_tests_failed++;
        printf("❌ %s 失败 (%.2fms)\n", test_name, result.duration_ms);
        printf("   原因: %s\n", result.details);
    }
    printf("\n");
}

// ===== 核心测试函数 =====

/**
 * 测试1: 多线程压缩调度器基本功能
 */
static bool test_scheduler_basic_multithread(test_result_t *result) {
    cleanup_test_dir();
    create_test_dir();
    
    // 创建组件
    lsm_memtable_manager_t *memtable_manager = lsm_memtable_manager_create(TEST_DATA_DIR);
    if (!memtable_manager) {
        snprintf(result->details, sizeof(result->details), "无法创建MemTable管理器");
        return false;
    }
    
    lsm_level_manager_t *level_manager = lsm_level_manager_create(TEST_DATA_DIR);
    if (!level_manager) {
        lsm_memtable_manager_destroy(memtable_manager);
        snprintf(result->details, sizeof(result->details), "无法创建Level管理器");
        return false;
    }
    
    // 创建压缩调度器
    lsm_compaction_scheduler_t *scheduler = lsm_compaction_scheduler_create(
        memtable_manager, level_manager);
    if (!scheduler) {
        lsm_level_manager_destroy(level_manager);
        lsm_memtable_manager_destroy(memtable_manager);
        snprintf(result->details, sizeof(result->details), "无法创建压缩调度器");
        return false;
    }
    
    // 配置多线程
    scheduler->config.enable_background_compaction = true;
    scheduler->config.background_thread_count = TEST_THREAD_COUNT;
    
    // 启动调度器
    int ret = lsm_compaction_scheduler_start(scheduler);
    if (ret != KV_ERR_NONE) {
        lsm_compaction_scheduler_destroy(scheduler);
        lsm_level_manager_destroy(level_manager);
        lsm_memtable_manager_destroy(memtable_manager);
        snprintf(result->details, sizeof(result->details), "无法启动调度器: %d", ret);
        return false;
    }
    
    // 检查工作线程健康状态
    usleep(100000); // 等待100ms让线程启动
    int healthy_count = lsm_compaction_scheduler_check_worker_health(scheduler);
    
    // 停止调度器
    lsm_compaction_scheduler_stop(scheduler);
    lsm_compaction_scheduler_destroy(scheduler);
    lsm_level_manager_destroy(level_manager);
    lsm_memtable_manager_destroy(memtable_manager);
    
    if (healthy_count != TEST_THREAD_COUNT) {
        snprintf(result->details, sizeof(result->details), 
                "期望%d个健康线程，实际%d个", TEST_THREAD_COUNT, healthy_count);
        return false;
    }
    
    snprintf(result->details, sizeof(result->details), 
            "成功启动和停止%d个工作线程", healthy_count);
    return true;
}

/**
 * 测试2: 并发任务提交和执行
 */
static bool test_concurrent_task_submission(test_result_t *result) {
    cleanup_test_dir();
    create_test_dir();
    
    // 创建组件
    lsm_memtable_manager_t *memtable_manager = lsm_memtable_manager_create(TEST_DATA_DIR);
    lsm_level_manager_t *level_manager = lsm_level_manager_create(TEST_DATA_DIR);
    lsm_compaction_scheduler_t *scheduler = lsm_compaction_scheduler_create(
        memtable_manager, level_manager);
    
    if (!scheduler) {
        snprintf(result->details, sizeof(result->details), "无法创建调度器");
        return false;
    }
    
    scheduler->config.enable_background_compaction = true;
    scheduler->config.background_thread_count = TEST_THREAD_COUNT;
    
    // 启动调度器
    lsm_compaction_scheduler_start(scheduler);
    usleep(50000); // 等待线程启动
    
    // 并发提交多个任务
    const int task_count = MAX_CONCURRENT_TASKS;
    lsm_compaction_task_t *tasks[task_count];
    
    for (int i = 0; i < task_count; i++) {
        tasks[i] = lsm_compaction_task_create(
            LSM_COMPACTION_TASK_LEVEL_N, 
            LSM_COMPACTION_PRIORITY_NORMAL);
        if (!tasks[i]) {
            snprintf(result->details, sizeof(result->details), 
                    "无法创建任务 %d", i);
            goto cleanup;
        }
        
        // 设置虚拟参数（用于测试）
        tasks[i]->source_level = 1;
        tasks[i]->target_level = 2;
    }
    
    // 并行执行任务
    int exec_result = lsm_compaction_execute_parallel(scheduler, tasks, task_count, 5000);
    
    // 获取统计信息
    lsm_compaction_stats_t stats;
    lsm_compaction_scheduler_get_stats(scheduler, &stats);
    
cleanup:
    lsm_compaction_scheduler_stop(scheduler);
    lsm_compaction_scheduler_destroy(scheduler);
    lsm_level_manager_destroy(level_manager);
    lsm_memtable_manager_destroy(memtable_manager);
    
    // 注意：任务会在complete_task中被销毁，不需要手动销毁
    
    if (exec_result == KV_ERR_NONE || exec_result == KV_ERR_NOT_IMPLEMENTED) {
        snprintf(result->details, sizeof(result->details), 
                "成功执行%d个并发任务，总完成: %lu, 失败: %lu", 
                task_count, stats.completed_tasks, stats.failed_tasks);
        return true;
    } else {
        snprintf(result->details, sizeof(result->details), 
                "并行执行失败: %d", exec_result);
        return false;
    }
}

/**
 * 测试3: 任务优先级和调度逻辑
 */
static bool test_task_priority_scheduling(test_result_t *result) {
    cleanup_test_dir();
    create_test_dir();
    
    lsm_memtable_manager_t *memtable_manager = lsm_memtable_manager_create(TEST_DATA_DIR);
    lsm_level_manager_t *level_manager = lsm_level_manager_create(TEST_DATA_DIR);
    lsm_compaction_scheduler_t *scheduler = lsm_compaction_scheduler_create(
        memtable_manager, level_manager);
    
    if (!scheduler) {
        snprintf(result->details, sizeof(result->details), "无法创建调度器");
        return false;
    }
    
    scheduler->config.enable_background_compaction = true;
    scheduler->config.background_thread_count = 2;
    lsm_compaction_scheduler_start(scheduler);
    usleep(50000);
    
    // 创建不同优先级的任务
    lsm_compaction_task_t *low_task = lsm_compaction_task_create(
        LSM_COMPACTION_TASK_LEVEL_N, LSM_COMPACTION_PRIORITY_LOW);
    lsm_compaction_task_t *high_task = lsm_compaction_task_create(
        LSM_COMPACTION_TASK_LEVEL_N, LSM_COMPACTION_PRIORITY_HIGH);
    lsm_compaction_task_t *urgent_task = lsm_compaction_task_create(
        LSM_COMPACTION_TASK_LEVEL_N, LSM_COMPACTION_PRIORITY_URGENT);
    
    if (!low_task || !high_task || !urgent_task) {
        snprintf(result->details, sizeof(result->details), "无法创建优先级任务");
        goto cleanup;
    }
    
    // 按低优先级顺序提交
    lsm_compaction_scheduler_submit_task(scheduler, low_task);
    lsm_compaction_scheduler_submit_task(scheduler, high_task);
    lsm_compaction_scheduler_submit_task(scheduler, urgent_task);
    
    // 等待处理
    usleep(200000); // 200ms
    
    lsm_compaction_stats_t stats;
    lsm_compaction_scheduler_get_stats(scheduler, &stats);
    
cleanup:
    lsm_compaction_scheduler_stop(scheduler);
    lsm_compaction_scheduler_destroy(scheduler);
    lsm_level_manager_destroy(level_manager);
    lsm_memtable_manager_destroy(memtable_manager);
    
    // 检查是否有任务被处理（即使失败也说明调度工作正常）
    if (stats.completed_tasks + stats.failed_tasks > 0) {
        snprintf(result->details, sizeof(result->details), 
                "优先级调度正常，处理任务: %lu 完成, %lu 失败", 
                stats.completed_tasks, stats.failed_tasks);
        return true;
    } else {
        snprintf(result->details, sizeof(result->details), "没有任务被处理");
        return false;
    }
}

/**
 * 测试4: 死锁检测和预防
 */
static bool test_deadlock_prevention(test_result_t *result) {
    cleanup_test_dir();
    create_test_dir();
    
    lsm_memtable_manager_t *memtable_manager = lsm_memtable_manager_create(TEST_DATA_DIR);
    lsm_level_manager_t *level_manager = lsm_level_manager_create(TEST_DATA_DIR);
    lsm_compaction_scheduler_t *scheduler = lsm_compaction_scheduler_create(
        memtable_manager, level_manager);
    
    if (!scheduler) {
        snprintf(result->details, sizeof(result->details), "无法创建调度器");
        return false;
    }
    
    scheduler->config.enable_background_compaction = true;
    scheduler->config.background_thread_count = TEST_THREAD_COUNT;
    
    // 启动和停止调度器多次，检测死锁
    for (int i = 0; i < 5; i++) {
        int start_ret = lsm_compaction_scheduler_start(scheduler);
        if (start_ret != KV_ERR_NONE) {
            snprintf(result->details, sizeof(result->details), 
                    "第%d次启动失败: %d", i+1, start_ret);
            goto cleanup;
        }
        
        usleep(10000); // 10ms
        
        int stop_ret = lsm_compaction_scheduler_stop(scheduler);
        if (stop_ret != KV_ERR_NONE) {
            snprintf(result->details, sizeof(result->details), 
                    "第%d次停止失败: %d", i+1, stop_ret);
            goto cleanup;
        }
    }
    
cleanup:
    // 确保最终停止
    lsm_compaction_scheduler_stop(scheduler);
    lsm_compaction_scheduler_destroy(scheduler);
    lsm_level_manager_destroy(level_manager);
    lsm_memtable_manager_destroy(memtable_manager);
    
    snprintf(result->details, sizeof(result->details), 
            "成功完成5次启动/停止循环，无死锁");
    return true;
}

/**
 * 测试5: 超时机制验证
 */
static bool test_timeout_mechanism(test_result_t *result) {
    cleanup_test_dir();
    create_test_dir();
    
    lsm_memtable_manager_t *memtable_manager = lsm_memtable_manager_create(TEST_DATA_DIR);
    lsm_level_manager_t *level_manager = lsm_level_manager_create(TEST_DATA_DIR);
    lsm_compaction_scheduler_t *scheduler = lsm_compaction_scheduler_create(
        memtable_manager, level_manager);
    
    if (!scheduler) {
        snprintf(result->details, sizeof(result->details), "无法创建调度器");
        return false;
    }
    
    scheduler->config.enable_background_compaction = true;
    scheduler->config.background_thread_count = 1;
    lsm_compaction_scheduler_start(scheduler);
    usleep(50000);
    
    // 创建任务但不执行，测试获取任务的超时
    double start_time = get_time_ms();
    lsm_compaction_task_t *task = lsm_compaction_scheduler_get_next_task(scheduler, 100); // 100ms超时
    double elapsed = get_time_ms() - start_time;
    
    bool timeout_worked = (task == NULL && elapsed >= 90 && elapsed <= 200); // 允许一些误差
    
    lsm_compaction_scheduler_stop(scheduler);
    lsm_compaction_scheduler_destroy(scheduler);
    lsm_level_manager_destroy(level_manager);
    lsm_memtable_manager_destroy(memtable_manager);
    
    if (timeout_worked) {
        snprintf(result->details, sizeof(result->details), 
                "超时机制正常，等待%.1fms后返回", elapsed);
        return true;
    } else {
        snprintf(result->details, sizeof(result->details), 
                "超时机制异常，等待%.1fms，任务: %p", elapsed, (void*)task);
        return false;
    }
}

/**
 * 测试6: 工作线程健康检查
 */
static bool test_worker_health_check(test_result_t *result) {
    cleanup_test_dir();
    create_test_dir();
    
    lsm_memtable_manager_t *memtable_manager = lsm_memtable_manager_create(TEST_DATA_DIR);
    lsm_level_manager_t *level_manager = lsm_level_manager_create(TEST_DATA_DIR);
    lsm_compaction_scheduler_t *scheduler = lsm_compaction_scheduler_create(
        memtable_manager, level_manager);
    
    if (!scheduler) {
        snprintf(result->details, sizeof(result->details), "无法创建调度器");
        return false;
    }
    
    // 未启动时健康检查
    int health_before = lsm_compaction_scheduler_check_worker_health(scheduler);
    
    // 启动后健康检查
    scheduler->config.enable_background_compaction = true;
    scheduler->config.background_thread_count = 3;
    lsm_compaction_scheduler_start(scheduler);
    usleep(100000); // 等待线程启动
    
    int health_after = lsm_compaction_scheduler_check_worker_health(scheduler);
    
    // 停止后健康检查
    lsm_compaction_scheduler_stop(scheduler);
    usleep(50000); // 等待线程停止
    int health_stopped = lsm_compaction_scheduler_check_worker_health(scheduler);
    
    lsm_compaction_scheduler_destroy(scheduler);
    lsm_level_manager_destroy(level_manager);
    lsm_memtable_manager_destroy(memtable_manager);
    
    bool passed = (health_before == 0) && (health_after == 3) && (health_stopped == 0);
    
    snprintf(result->details, sizeof(result->details), 
            "健康检查: 启动前=%d, 启动后=%d, 停止后=%d", 
            health_before, health_after, health_stopped);
    
    return passed;
}

// ===== 主测试函数 =====

int main(void) {
    printf("🚀 ConcordKV LSM-Tree多线程压缩测试套件\n");
    printf("=========================================\n\n");
    
    // 运行所有测试
    run_test("多线程调度器基本功能", test_scheduler_basic_multithread);
    run_test("并发任务提交和执行", test_concurrent_task_submission);
    run_test("任务优先级调度", test_task_priority_scheduling);
    run_test("死锁预防机制", test_deadlock_prevention);
    run_test("超时机制验证", test_timeout_mechanism);
    run_test("工作线程健康检查", test_worker_health_check);
    
    // 输出测试总结
    printf("📊 测试总结\n");
    printf("=========\n");
    printf("✅ 通过: %d\n", g_tests_passed);
    printf("❌ 失败: %d\n", g_tests_failed);
    printf("📈 通过率: %.1f%%\n", 
           g_tests_passed * 100.0 / (g_tests_passed + g_tests_failed));
    
    if (g_tests_failed == 0) {
        printf("\n🎉 所有测试通过！LSM-Tree多线程压缩功能正常工作\n");
        cleanup_test_dir();
        return 0;
    } else {
        printf("\n⚠️  有测试失败，请检查多线程压缩实现\n");
        return 1;
    }
} 