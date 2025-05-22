/**
 * @file timer_demo.c
 * @brief 定时器使用示例
 */

#include "timer.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

// 全局定时器管理器
static concord_timer_manager_t *g_timer_manager = NULL;
static int g_running = 1;

// 信号处理函数
static void signal_handler(int signo) {
    if (signo == SIGINT) {
        printf("接收到中断信号，准备退出...\n");
        g_running = 0;
    }
}

// 定时器回调函数1：打印当前时间
static void timer_callback1(void *arg, uint64_t fired_time) {
    int count = *(int *)arg;
    char time_buf[64];
    
    concord_time_format(fired_time, "%Y-%m-%d %H:%M:%S", time_buf, sizeof(time_buf));
    printf("定时器1触发 [计数: %d, 时间: %s.%03llu]\n", 
           count, time_buf, fired_time % 1000);
    
    (*(int *)arg)++;
}

// 定时器回调函数2：打印内存使用情况
static void timer_callback2(void *arg, uint64_t fired_time) {
    char time_buf[64];
    concord_time_format(fired_time, "%H:%M:%S", time_buf, sizeof(time_buf));
    printf("定时器2触发 [时间: %s.%03llu] - 模拟内存监控\n", 
           time_buf, fired_time % 1000);
}

// 定时器回调函数3：一次性任务
static void timer_callback3(void *arg, uint64_t fired_time) {
    char time_buf[64];
    concord_time_format(fired_time, "%H:%M:%S", time_buf, sizeof(time_buf));
    printf("定时器3触发 [时间: %s.%03llu] - 一次性任务执行\n", 
           time_buf, fired_time % 1000);
}

// 定时器回调函数4：取消其他定时器
static void timer_callback4(void *arg, uint64_t fired_time) {
    concord_timer_id_t timer_id = *(concord_timer_id_t *)arg;
    char time_buf[64];
    concord_time_format(fired_time, "%H:%M:%S", time_buf, sizeof(time_buf));
    printf("定时器4触发 [时间: %s.%03llu] - 取消定时器ID: %llu\n", 
           time_buf, fired_time % 1000, (unsigned long long)timer_id);
    
    concord_timer_cancel(g_timer_manager, timer_id);
}

int main() {
    // 注册信号处理函数
    signal(SIGINT, signal_handler);
    
    // 创建定时器管理器
    g_timer_manager = concord_timer_manager_create();
    if (!g_timer_manager) {
        fprintf(stderr, "创建定时器管理器失败\n");
        return 1;
    }
    
    printf("定时器示例程序启动，按Ctrl+C退出\n");
    
    // 用于计数的变量
    int counter1 = 0;
    
    // 添加定时器1：每1秒触发一次
    concord_timer_id_t timer1 = concord_timer_add(
        g_timer_manager,
        CONCORD_TIMER_PERIODIC,
        1000,  // 1000毫秒
        timer_callback1,
        &counter1
    );
    
    // 添加定时器2：每2.5秒触发一次
    concord_timer_id_t timer2 = concord_timer_add(
        g_timer_manager,
        CONCORD_TIMER_PERIODIC,
        2500,  // 2500毫秒
        timer_callback2,
        NULL
    );
    
    // 添加定时器3：5秒后触发一次
    concord_timer_id_t timer3 = concord_timer_add(
        g_timer_manager,
        CONCORD_TIMER_ONCE,
        5000,  // 5000毫秒
        timer_callback3,
        NULL
    );
    
    // 添加定时器4：10秒后触发，取消定时器2
    concord_timer_id_t *timer2_ptr = malloc(sizeof(concord_timer_id_t));
    if (timer2_ptr) {
        *timer2_ptr = timer2;
        concord_timer_add(
            g_timer_manager,
            CONCORD_TIMER_ONCE,
            10000,  // 10000毫秒
            timer_callback4,
            timer2_ptr
        );
    }
    
    printf("已添加定时器:\n");
    printf("- 定时器1: ID=%llu, 周期性, 每1秒触发\n", (unsigned long long)timer1);
    printf("- 定时器2: ID=%llu, 周期性, 每2.5秒触发\n", (unsigned long long)timer2);
    printf("- 定时器3: ID=%llu, 一次性, 5秒后触发\n", (unsigned long long)timer3);
    printf("- 定时器4: 一次性, 10秒后触发, 将取消定时器2\n");
    
    // 主循环
    while (g_running) {
        // 处理定时器事件，超时等待100毫秒
        int processed = concord_timer_process(g_timer_manager, 100);
        if (processed < 0) {
            fprintf(stderr, "处理定时器事件出错\n");
            break;
        }
    }
    
    // 清理
    concord_timer_manager_destroy(g_timer_manager);
    free(timer2_ptr);
    
    printf("程序退出\n");
    return 0;
} 