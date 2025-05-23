#define _GNU_SOURCE
#include "../util/timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>

// 测试结果统计
static int tests_passed = 0;
static int tests_failed = 0;

// 测试宏
#define TEST_ASSERT(condition, message) \
    do { \
        if (condition) { \
            tests_passed++; \
            printf("✅ PASS: %s\n", message); \
        } else { \
            tests_failed++; \
            printf("❌ FAIL: %s\n", message); \
        } \
    } while(0)

// 全局变量用于测试回调
static volatile int callback_count = 0;
static volatile int one_shot_fired = 0;
static volatile uint64_t last_fired_time = 0;
static concord_timer_manager_t *g_manager = NULL;

// 测试回调函数
static void test_callback_periodic(void *arg, uint64_t fired_time) {
    (void)arg;  // 避免未使用参数警告
    callback_count++;
    last_fired_time = fired_time;
    printf("周期性定时器触发，计数: %d\n", callback_count);
}

static void test_callback_oneshot(void *arg, uint64_t fired_time) {
    (void)arg;  // 避免未使用参数警告
    one_shot_fired = 1;
    last_fired_time = fired_time;
    printf("一次性定时器触发\n");
}

// 测试定时器管理器创建
void test_timer_manager_create() {
    printf("\n=== 测试定时器管理器创建 ===\n");
    
    g_manager = concord_timer_manager_create();
    TEST_ASSERT(g_manager != NULL, "创建定时器管理器");
}

// 测试时间函数
void test_time_functions() {
    printf("\n=== 测试时间函数 ===\n");
    
    // 测试当前时间获取
    uint64_t time_ns = concord_time_now_ns();
    uint64_t time_us = concord_time_now_us();
    uint64_t time_ms = concord_time_now_ms();
    
    TEST_ASSERT(time_ns > 0, "获取纳秒时间戳");
    TEST_ASSERT(time_us > 0, "获取微秒时间戳");
    TEST_ASSERT(time_ms > 0, "获取毫秒时间戳");
    
    // 检查时间关系
    TEST_ASSERT(time_us == time_ns / 1000 || time_us == time_ns / 1000 + 1, "时间戳转换关系正确");
    
    // 测试时间差计算
    uint64_t start = concord_time_now_ms();
    usleep(10000); // 10ms
    uint64_t end = concord_time_now_ms();
    uint64_t diff = concord_time_diff_ms(start, end);
    
    TEST_ASSERT(diff >= 8 && diff <= 20, "时间差计算合理"); // 允许一些误差
    
    // 测试时间格式化
    char buf[64];
    char *result = concord_time_format(time_ms, "%Y-%m-%d %H:%M:%S", buf, sizeof(buf));
    TEST_ASSERT(result != NULL && strlen(buf) > 0, "时间格式化");
    printf("格式化时间: %s\n", buf);
}

// 测试一次性定时器
void test_oneshot_timer() {
    printf("\n=== 测试一次性定时器 ===\n");
    
    if (!g_manager) {
        TEST_ASSERT(0, "需要先创建定时器管理器");
        return;
    }
    
    one_shot_fired = 0;
    
    // 创建100ms一次性定时器
    concord_timer_id_t timer_id = concord_timer_add(g_manager, CONCORD_TIMER_ONCE, 
                                                    100, test_callback_oneshot, NULL);
    TEST_ASSERT(timer_id != 0, "创建一次性定时器");
    
    // 处理定时器事件（等待100ms + 一些余量）
    printf("等待一次性定时器触发...\n");
    uint64_t start_time = concord_time_now_ms();
    while (!one_shot_fired && (concord_time_now_ms() - start_time) < 200) {
        int processed = concord_timer_process(g_manager, 50); // 50ms超时
        if (processed > 0) {
            printf("处理了 %d 个定时器事件\n", processed);
        }
    }
    
    TEST_ASSERT(one_shot_fired == 1, "一次性定时器触发");
    
    // 再等待一段时间，确保不会再次触发
    int old_count = one_shot_fired;
    concord_timer_process(g_manager, 100);
    TEST_ASSERT(one_shot_fired == old_count, "一次性定时器不会重复触发");
}

// 测试周期性定时器
void test_periodic_timer() {
    printf("\n=== 测试周期性定时器 ===\n");
    
    if (!g_manager) {
        TEST_ASSERT(0, "需要先创建定时器管理器");
        return;
    }
    
    callback_count = 0;
    
    // 创建80ms周期定时器
    concord_timer_id_t timer_id = concord_timer_add(g_manager, CONCORD_TIMER_PERIODIC, 
                                                    80, test_callback_periodic, NULL);
    TEST_ASSERT(timer_id != 0, "创建周期性定时器");
    
    // 运行250ms，应该触发3次左右
    printf("等待周期性定时器触发...\n");
    uint64_t start_time = concord_time_now_ms();
    while ((concord_time_now_ms() - start_time) < 250) {
        int processed = concord_timer_process(g_manager, 30); // 30ms超时
        if (processed > 0) {
            printf("处理了 %d 个定时器事件，当前计数: %d\n", processed, callback_count);
        }
    }
    
    // 取消定时器
    int result = concord_timer_cancel(g_manager, timer_id);
    TEST_ASSERT(result == 0, "取消周期性定时器");
    
    TEST_ASSERT(callback_count >= 2 && callback_count <= 5, "周期性定时器触发次数合理");
    printf("周期性定时器总共触发了 %d 次\n", callback_count);
}

// 测试定时器取消
void test_timer_cancel() {
    printf("\n=== 测试定时器取消 ===\n");
    
    if (!g_manager) {
        TEST_ASSERT(0, "需要先创建定时器管理器");
        return;
    }
    
    callback_count = 0;
    
    // 创建定时器
    concord_timer_id_t timer_id = concord_timer_add(g_manager, CONCORD_TIMER_PERIODIC, 
                                                    50, test_callback_periodic, NULL);
    TEST_ASSERT(timer_id != 0, "创建定时器用于取消测试");
    
    // 等待一小段时间
    concord_timer_process(g_manager, 30);
    
    // 取消定时器
    int result = concord_timer_cancel(g_manager, timer_id);
    TEST_ASSERT(result == 0, "取消定时器");
    
    int count_before = callback_count;
    
    // 等待确保不会再触发
    concord_timer_process(g_manager, 100);
    
    TEST_ASSERT(callback_count == count_before, "取消后定时器不再触发");
}

// 测试多个定时器
void test_multiple_timers() {
    printf("\n=== 测试多个定时器 ===\n");
    
    if (!g_manager) {
        TEST_ASSERT(0, "需要先创建定时器管理器");
        return;
    }
    
    callback_count = 0;
    one_shot_fired = 0;
    
    // 创建多个定时器
    concord_timer_id_t timer1 = concord_timer_add(g_manager, CONCORD_TIMER_PERIODIC, 
                                                  60, test_callback_periodic, NULL);
    concord_timer_id_t timer2 = concord_timer_add(g_manager, CONCORD_TIMER_ONCE, 
                                                  120, test_callback_oneshot, NULL);
    concord_timer_id_t timer3 = concord_timer_add(g_manager, CONCORD_TIMER_PERIODIC, 
                                                  90, test_callback_periodic, NULL);
    
    TEST_ASSERT(timer1 != 0 && timer2 != 0 && timer3 != 0, "创建多个定时器");
    TEST_ASSERT(timer1 != timer2 && timer2 != timer3 && timer1 != timer3, "定时器ID唯一性");
    
    // 等待定时器运行
    printf("等待多个定时器运行...\n");
    uint64_t start_time = concord_time_now_ms();
    while ((concord_time_now_ms() - start_time) < 200) {
        int processed = concord_timer_process(g_manager, 25);
        if (processed > 0) {
            printf("处理了 %d 个定时器事件\n", processed);
        }
    }
    
    // 停止所有定时器
    concord_timer_cancel(g_manager, timer1);
    concord_timer_cancel(g_manager, timer3);
    
    TEST_ASSERT(callback_count > 0, "多个周期性定时器有触发");
    TEST_ASSERT(one_shot_fired == 1, "一次性定时器在多定时器环境下正常触发");
}

// 测试定时器更新
void test_timer_update() {
    printf("\n=== 测试定时器更新 ===\n");
    
    if (!g_manager) {
        TEST_ASSERT(0, "需要先创建定时器管理器");
        return;
    }
    
    callback_count = 0;
    
    // 创建200ms周期定时器
    concord_timer_id_t timer_id = concord_timer_add(g_manager, CONCORD_TIMER_PERIODIC, 
                                                    200, test_callback_periodic, NULL);
    TEST_ASSERT(timer_id != 0, "创建定时器用于更新测试");
    
    // 更新为50ms
    int result = concord_timer_update(g_manager, timer_id, 50);
    TEST_ASSERT(result == 0, "更新定时器超时时间");
    
    // 运行150ms，如果更新成功，应该触发2-3次
    uint64_t start_time = concord_time_now_ms();
    while ((concord_time_now_ms() - start_time) < 150) {
        concord_timer_process(g_manager, 25);
    }
    
    concord_timer_cancel(g_manager, timer_id);
    
    TEST_ASSERT(callback_count >= 2, "更新后的定时器正常工作");
    printf("更新后定时器触发了 %d 次\n", callback_count);
}

// 测试定时器管理器销毁
void test_timer_manager_destroy() {
    printf("\n=== 测试定时器管理器销毁 ===\n");
    
    if (g_manager) {
        concord_timer_manager_destroy(g_manager);
        g_manager = NULL;
        printf("定时器管理器已销毁\n");
        TEST_ASSERT(1, "定时器管理器销毁完成");
    } else {
        TEST_ASSERT(0, "定时器管理器不存在");
    }
}

// 主测试函数
int main() {
    printf("========================================\n");
    printf("ConcordKV Timer 模块功能测试\n");
    printf("========================================\n");
    
    test_timer_manager_create();
    test_time_functions();
    test_oneshot_timer();
    test_periodic_timer();
    test_timer_cancel();
    test_multiple_timers();
    test_timer_update();
    test_timer_manager_destroy();
    
    printf("\n========================================\n");
    printf("测试结果汇总\n");
    printf("========================================\n");
    printf("通过: %d\n", tests_passed);
    printf("失败: %d\n", tests_failed);
    printf("总计: %d\n", tests_passed + tests_failed);
    
    if (tests_failed == 0) {
        printf("\n🎉 所有测试通过！Timer 模块功能正常\n");
        return 0;
    } else {
        printf("\n⚠️  有 %d 个测试失败，请检查代码\n", tests_failed);
        return 1;
    }
} 