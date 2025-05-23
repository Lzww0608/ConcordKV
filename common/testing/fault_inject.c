/**
 * @file fault_inject.c
 * @brief 故障注入实现
 */

#include "testing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>

#define MAX_FAULTS 32

// 故障注入器结构
struct concord_fault_injector_t {
    concord_fault_config_t faults[MAX_FAULTS];  // 故障配置数组
    int fault_count;                            // 故障数量
    volatile int running;                       // 运行状态
    pthread_t thread;                           // 故障注入线程
    pthread_mutex_t mutex;                      // 互斥锁
    uint64_t start_time;                        // 开始时间
};

// 获取当前时间（毫秒）
static uint64_t get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// 生成随机浮点数 [0.0, 1.0]
static double random_double(void) {
    return (double)rand() / RAND_MAX;
}

// 模拟网络延迟
static void inject_network_delay(concord_fault_config_t *fault) {
    // 基于严重程度计算延迟时间 (1-100ms)
    int delay_ms = fault->severity * 10;
    printf("[故障注入] 网络延迟: %d ms (目标: %s)\n", delay_ms, fault->target);
    usleep(delay_ms * 1000);
}

// 模拟网络丢包
static void inject_network_drop(concord_fault_config_t *fault) {
    printf("[故障注入] 网络丢包 (概率: %.2f%%, 目标: %s)\n", 
           fault->probability * 100, fault->target);
    // 在实际应用中，这里会设置网络丢包规则
    // 这里只是模拟输出
}

// 模拟内存泄漏
static void inject_memory_leak(concord_fault_config_t *fault) {
    // 基于严重程度分配内存但不释放
    size_t leak_size = fault->severity * 1024 * 1024;  // MB
    void *leaked_memory = malloc(leak_size);
    printf("[故障注入] 内存泄漏: %zu MB (目标: %s)\n", 
           leak_size / (1024 * 1024), fault->target);
    
    // 故意不释放内存，模拟内存泄漏
    (void)leaked_memory;
}

// 模拟CPU尖峰
static void inject_cpu_spike(concord_fault_config_t *fault) {
    printf("[故障注入] CPU尖峰: 强度 %d (目标: %s)\n", fault->severity, fault->target);
    
    // 创建CPU密集型计算
    uint64_t end_time = get_time_ms() + (fault->severity * 100);  // 持续时间与严重程度相关
    volatile long long sum = 0;
    
    while (get_time_ms() < end_time) {
        for (int i = 0; i < 10000; i++) {
            sum += i * i;
        }
    }
    
    printf("[故障注入] CPU尖峰结束 (计算结果: %lld)\n", sum);
}

// 模拟磁盘满
static void inject_disk_full(concord_fault_config_t *fault) {
    printf("[故障注入] 磁盘满模拟 (严重程度: %d, 目标: %s)\n", 
           fault->severity, fault->target);
    
    // 在实际应用中，这里会创建大文件占用磁盘空间
    // 这里只是模拟输出和设置环境变量
    char temp_file[256];
    snprintf(temp_file, sizeof(temp_file), "/tmp/fault_inject_disk_%d.tmp", getpid());
    
    FILE *fp = fopen(temp_file, "w");
    if (fp) {
        // 写入一些数据模拟磁盘占用
        size_t write_size = fault->severity * 1024;  // KB
        char *buffer = malloc(write_size);
        if (buffer) {
            memset(buffer, 'X', write_size);
            fwrite(buffer, 1, write_size, fp);
            free(buffer);
        }
        fclose(fp);
        printf("[故障注入] 创建临时文件: %s (%zu KB)\n", temp_file, write_size / 1024);
    }
}

// 模拟连接重置
static void inject_connection_reset(concord_fault_config_t *fault) {
    printf("[故障注入] 连接重置 (概率: %.2f%%, 目标: %s)\n", 
           fault->probability * 100, fault->target);
    // 在实际应用中，这里会强制关闭网络连接
}

// 模拟超时
static void inject_timeout(concord_fault_config_t *fault) {
    int timeout_ms = fault->severity * 1000;  // 基于严重程度的超时时间
    printf("[故障注入] 超时模拟: %d ms (目标: %s)\n", timeout_ms, fault->target);
    usleep(timeout_ms * 1000);
}

// 执行故障注入
static void execute_fault(concord_fault_config_t *fault) {
    switch (fault->type) {
        case CONCORD_FAULT_NETWORK_DELAY:
            inject_network_delay(fault);
            break;
        case CONCORD_FAULT_NETWORK_DROP:
            inject_network_drop(fault);
            break;
        case CONCORD_FAULT_MEMORY_LEAK:
            inject_memory_leak(fault);
            break;
        case CONCORD_FAULT_CPU_SPIKE:
            inject_cpu_spike(fault);
            break;
        case CONCORD_FAULT_DISK_FULL:
            inject_disk_full(fault);
            break;
        case CONCORD_FAULT_CONNECTION_RESET:
            inject_connection_reset(fault);
            break;
        case CONCORD_FAULT_TIMEOUT:
            inject_timeout(fault);
            break;
        default:
            printf("[故障注入] 未知故障类型: %d\n", fault->type);
            break;
    }
}

// 故障注入线程函数
static void *fault_injection_thread(void *arg) {
    concord_fault_injector_t *injector = (concord_fault_injector_t *)arg;
    
    printf("故障注入线程启动\n");
    
    while (injector->running) {
        pthread_mutex_lock(&injector->mutex);
        
        // 遍历所有故障配置
        for (int i = 0; i < injector->fault_count; i++) {
            concord_fault_config_t *fault = &injector->faults[i];
            
            // 检查是否在故障持续时间内
            uint64_t current_time = get_time_ms();
            if (fault->duration_ms > 0 && 
                current_time - injector->start_time > fault->duration_ms) {
                continue;
            }
            
            // 根据概率决定是否注入故障
            if (random_double() < fault->probability) {
                execute_fault(fault);
            }
        }
        
        pthread_mutex_unlock(&injector->mutex);
        
        // 睡眠1秒，避免过于频繁的故障注入
        sleep(1);
    }
    
    printf("故障注入线程结束\n");
    return NULL;
}

// 创建故障注入器
concord_fault_injector_t *concord_fault_injector_create(void) {
    concord_fault_injector_t *injector = calloc(1, sizeof(concord_fault_injector_t));
    if (!injector) return NULL;
    
    // 初始化互斥锁
    if (pthread_mutex_init(&injector->mutex, NULL) != 0) {
        free(injector);
        return NULL;
    }
    
    // 设置随机种子
    srand(time(NULL));
    
    return injector;
}

// 销毁故障注入器
void concord_fault_injector_destroy(concord_fault_injector_t *injector) {
    if (!injector) return;
    
    if (injector->running) {
        concord_fault_injector_stop(injector);
    }
    
    // 清理故障配置中的动态分配内存
    for (int i = 0; i < injector->fault_count; i++) {
        free(injector->faults[i].target);
    }
    
    pthread_mutex_destroy(&injector->mutex);
    free(injector);
}

// 添加故障配置
int concord_fault_injector_add_fault(concord_fault_injector_t *injector, concord_fault_config_t *config) {
    if (!injector || !config || injector->fault_count >= MAX_FAULTS) {
        return -1;
    }
    
    pthread_mutex_lock(&injector->mutex);
    
    // 复制故障配置
    concord_fault_config_t *fault = &injector->faults[injector->fault_count];
    fault->type = config->type;
    fault->probability = config->probability;
    fault->duration_ms = config->duration_ms;
    fault->severity = config->severity;
    fault->target = config->target ? strdup(config->target) : NULL;
    
    injector->fault_count++;
    
    pthread_mutex_unlock(&injector->mutex);
    
    printf("添加故障配置: 类型=%d, 概率=%.2f, 持续时间=%llu ms, 严重程度=%d, 目标=%s\n",
           fault->type, fault->probability, fault->duration_ms, fault->severity, 
           fault->target ? fault->target : "N/A");
    
    return 0;
}

// 启动故障注入
int concord_fault_injector_start(concord_fault_injector_t *injector) {
    if (!injector || injector->running) return -1;
    
    printf("启动故障注入器...\n");
    
    injector->running = 1;
    injector->start_time = get_time_ms();
    
    // 创建故障注入线程
    if (pthread_create(&injector->thread, NULL, fault_injection_thread, injector) != 0) {
        injector->running = 0;
        return -1;
    }
    
    printf("故障注入器已启动，配置了 %d 个故障\n", injector->fault_count);
    return 0;
}

// 停止故障注入
int concord_fault_injector_stop(concord_fault_injector_t *injector) {
    if (!injector || !injector->running) return -1;
    
    printf("停止故障注入器...\n");
    injector->running = 0;
    
    // 等待线程结束
    pthread_join(injector->thread, NULL);
    
    printf("故障注入器已停止\n");
    return 0;
}

// 检查是否应该注入故障
int concord_fault_injector_should_inject(concord_fault_injector_t *injector, 
                                        const char *target, 
                                        concord_fault_type_t type) {
    if (!injector || !target) return 0;
    
    pthread_mutex_lock(&injector->mutex);
    
    for (int i = 0; i < injector->fault_count; i++) {
        concord_fault_config_t *fault = &injector->faults[i];
        
        // 检查故障类型和目标是否匹配
        if ((fault->type & type) && 
            (!fault->target || strcmp(fault->target, target) == 0)) {
            
            // 检查是否在故障持续时间内
            if (fault->duration_ms > 0) {
                uint64_t current_time = get_time_ms();
                if (current_time - injector->start_time > fault->duration_ms) {
                    continue;
                }
            }
            
            // 根据概率决定是否注入故障
            if (random_double() < fault->probability) {
                pthread_mutex_unlock(&injector->mutex);
                return 1;
            }
        }
    }
    
    pthread_mutex_unlock(&injector->mutex);
    return 0;
} 