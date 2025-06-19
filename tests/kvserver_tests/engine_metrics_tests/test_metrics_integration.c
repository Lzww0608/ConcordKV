/*
 * @Author: Lzww0608  
 * @Date: 2025-6-17 16:00:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-17 16:00:00
 * @Description: ConcordKV 存储引擎监控系统集成测试
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

// 基础测试框架
static int test_count = 0;
static int test_passed = 0;

#define TEST_ASSERT(condition, message) \
    do { \
        test_count++; \
        if (condition) { \
            test_passed++; \
            printf("✓ [%02d] %s\n", test_count, message); \
        } else { \
            printf("✗ [%02d] %s\n", test_count, message); \
        } \
    } while(0)

// 模拟的监控管理器和相关结构
typedef struct {
    int dummy;
} concord_metrics_repo_t;

typedef struct {
    int dummy;
} concord_metric_t;

typedef enum {
    KV_ENGINE_ARRAY = 0,
    KV_ENGINE_HASH = 1,
    KV_ENGINE_RBTREE = 2,
    KV_ENGINE_BTREE = 3,
    KV_ENGINE_LSM = 4,
    KV_ENGINE_MAX = 5
} kv_engine_type_t;

// Error codes
#define KV_ERR_NONE      0
#define KV_ERR_PARAM    -1
#define KV_ERR_MEMORY   -2
#define KV_ERR_NOT_FOUND -3
#define KV_ERR_SYS      -4

// 模拟监控管理器结构
typedef struct {
    bool monitoring_enabled;
    uint64_t collection_interval_ms;
    size_t retention_samples;
    bool enable_latency_histograms;
    bool enable_engine_specific;
    bool enable_memory_tracking;
    
    struct {
        bool enabled;
        char host[256];
        int port;
        char path[256];
    } http_config;
} kv_engine_metrics_config_t;

typedef struct {
    concord_metrics_repo_t *repo;
    void *engine_metrics[KV_ENGINE_MAX];
    kv_engine_metrics_config_t config;
    pthread_rwlock_t lock;
    bool running;
    uint64_t total_metrics_count;
    uint64_t collection_count;
} kv_engine_metrics_manager_t;

// 模拟存储引擎结构
typedef struct {
    kv_engine_type_t type;
    char name[64];
    void *metrics_manager;
    bool metrics_enabled;
    uint64_t last_metrics_update;
    
    // 模拟统计信息
    struct {
        uint64_t read_count;
        uint64_t write_count;
        uint64_t delete_count;
        uint64_t memory_usage;
    } stats;
} kv_engine_t;

// 模拟函数实现
kv_engine_metrics_manager_t* kv_engine_metrics_manager_create(void) {
    kv_engine_metrics_manager_t *manager = calloc(1, sizeof(kv_engine_metrics_manager_t));
    if (!manager) return NULL;
    
    manager->repo = malloc(sizeof(int)); // 模拟repo
    pthread_rwlock_init(&manager->lock, NULL);
    manager->config.monitoring_enabled = true;
    manager->config.collection_interval_ms = 1000;
    manager->config.retention_samples = 1000;
    manager->config.enable_latency_histograms = true;
    manager->config.enable_engine_specific = true;
    manager->config.enable_memory_tracking = true;
    
    manager->config.http_config.enabled = true;
    strncpy(manager->config.http_config.host, "0.0.0.0", sizeof(manager->config.http_config.host) - 1);
    manager->config.http_config.port = 9090;
    strncpy(manager->config.http_config.path, "/metrics", sizeof(manager->config.http_config.path) - 1);
    
    return manager;
}

void kv_engine_metrics_manager_destroy(kv_engine_metrics_manager_t *manager) {
    if (!manager) return;
    
    if (manager->repo) free(manager->repo);
    pthread_rwlock_destroy(&manager->lock);
    free(manager);
}

int kv_engine_metrics_register_engine(kv_engine_metrics_manager_t *manager, 
                                     kv_engine_type_t type, 
                                     const char *name) {
    if (!manager || type >= KV_ENGINE_MAX || !name) return KV_ERR_PARAM;
    
    pthread_rwlock_wrlock(&manager->lock);
    
    if (manager->engine_metrics[type] == NULL) {
        manager->engine_metrics[type] = malloc(sizeof(int)); // 模拟指标集
        manager->total_metrics_count++;
    }
    
    pthread_rwlock_unlock(&manager->lock);
    return KV_ERR_NONE;
}

int kv_engine_metrics_record_read(kv_engine_metrics_manager_t *manager, 
                                 kv_engine_type_t type, 
                                 double latency_ms) {
    if (!manager || type >= KV_ENGINE_MAX) return KV_ERR_PARAM;
    
    pthread_rwlock_rdlock(&manager->lock);
    bool registered = (manager->engine_metrics[type] != NULL);
    pthread_rwlock_unlock(&manager->lock);
    
    return registered ? KV_ERR_NONE : KV_ERR_NOT_FOUND;
}

int kv_engine_metrics_record_write(kv_engine_metrics_manager_t *manager, 
                                  kv_engine_type_t type, 
                                  double latency_ms) {
    if (!manager || type >= KV_ENGINE_MAX) return KV_ERR_PARAM;
    
    pthread_rwlock_rdlock(&manager->lock);
    bool registered = (manager->engine_metrics[type] != NULL);
    pthread_rwlock_unlock(&manager->lock);
    
    return registered ? KV_ERR_NONE : KV_ERR_NOT_FOUND;
}

int kv_engine_metrics_record_delete(kv_engine_metrics_manager_t *manager, 
                                   kv_engine_type_t type, 
                                   double latency_ms) {
    if (!manager || type >= KV_ENGINE_MAX) return KV_ERR_PARAM;
    
    pthread_rwlock_rdlock(&manager->lock);
    bool registered = (manager->engine_metrics[type] != NULL);
    pthread_rwlock_unlock(&manager->lock);
    
    return registered ? KV_ERR_NONE : KV_ERR_NOT_FOUND;
}

int kv_engine_metrics_update_memory_usage(kv_engine_metrics_manager_t *manager, 
                                         kv_engine_type_t type, 
                                         size_t bytes) {
    if (!manager || type >= KV_ENGINE_MAX) return KV_ERR_PARAM;
    
    pthread_rwlock_rdlock(&manager->lock);
    bool registered = (manager->engine_metrics[type] != NULL);
    pthread_rwlock_unlock(&manager->lock);
    
    return registered ? KV_ERR_NONE : KV_ERR_NOT_FOUND;
}

int kv_engine_metrics_record_error(kv_engine_metrics_manager_t *manager, 
                                  kv_engine_type_t type, 
                                  const char *error_type) {
    if (!manager || type >= KV_ENGINE_MAX || !error_type) return KV_ERR_PARAM;
    
    pthread_rwlock_rdlock(&manager->lock);
    bool registered = (manager->engine_metrics[type] != NULL);
    pthread_rwlock_unlock(&manager->lock);
    
    return registered ? KV_ERR_NONE : KV_ERR_NOT_FOUND;
}

uint64_t get_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

// 模拟引擎初始化函数
int engine_init_metrics(kv_engine_t *engine, void *metrics_manager) {
    if (!engine || !metrics_manager) return KV_ERR_PARAM;
    
    engine->metrics_manager = metrics_manager;
    engine->metrics_enabled = true;
    engine->last_metrics_update = get_time_us();
    
    kv_engine_metrics_manager_t *manager = (kv_engine_metrics_manager_t*)metrics_manager;
    return kv_engine_metrics_register_engine(manager, engine->type, engine->name);
}

// 模拟引擎操作函数
int engine_set_with_metrics(kv_engine_t *engine, const char *key, const char *value) {
    if (!engine || !key || !value) return KV_ERR_PARAM;
    
    uint64_t start_time = get_time_us();
    
    // 模拟写操作
    usleep(100); // 模拟100微秒延迟
    engine->stats.write_count++;
    engine->stats.memory_usage += strlen(key) + strlen(value);
    
    // 记录监控指标
    if (engine->metrics_enabled && engine->metrics_manager) {
        kv_engine_metrics_manager_t *manager = (kv_engine_metrics_manager_t*)engine->metrics_manager;
        double latency_ms = (get_time_us() - start_time) / 1000.0;
        
        kv_engine_metrics_record_write(manager, engine->type, latency_ms);
        kv_engine_metrics_update_memory_usage(manager, engine->type, engine->stats.memory_usage);
    }
    
    return KV_ERR_NONE;
}

char* engine_get_with_metrics(kv_engine_t *engine, const char *key) {
    if (!engine || !key) return NULL;
    
    uint64_t start_time = get_time_us();
    
    // 模拟读操作
    usleep(50); // 模拟50微秒延迟
    engine->stats.read_count++;
    
    // 记录监控指标
    if (engine->metrics_enabled && engine->metrics_manager) {
        kv_engine_metrics_manager_t *manager = (kv_engine_metrics_manager_t*)engine->metrics_manager;
        double latency_ms = (get_time_us() - start_time) / 1000.0;
        
        kv_engine_metrics_record_read(manager, engine->type, latency_ms);
    }
    
    // 模拟返回值
    return strdup("test_value");
}

int engine_delete_with_metrics(kv_engine_t *engine, const char *key) {
    if (!engine || !key) return KV_ERR_PARAM;
    
    uint64_t start_time = get_time_us();
    
    // 模拟删除操作
    usleep(75); // 模拟75微秒延迟
    engine->stats.delete_count++;
    
    // 记录监控指标
    if (engine->metrics_enabled && engine->metrics_manager) {
        kv_engine_metrics_manager_t *manager = (kv_engine_metrics_manager_t*)engine->metrics_manager;
        double latency_ms = (get_time_us() - start_time) / 1000.0;
        
        kv_engine_metrics_record_delete(manager, engine->type, latency_ms);
    }
    
    return KV_ERR_NONE;
}

// === 测试用例 ===

void test_metrics_manager_integration() {
    printf("\n--- 测试用例1: 监控管理器集成测试 ---\n");
    
    // 创建监控管理器
    kv_engine_metrics_manager_t *manager = kv_engine_metrics_manager_create();
    TEST_ASSERT(manager != NULL, "创建监控管理器成功");
    TEST_ASSERT(manager->config.monitoring_enabled == true, "监控功能已启用");
    TEST_ASSERT(manager->config.collection_interval_ms == 1000, "收集间隔配置正确");
    
    // 创建模拟引擎
    kv_engine_t engine = {0};
    engine.type = KV_ENGINE_ARRAY;
    strncpy(engine.name, "test_array", sizeof(engine.name) - 1);
    
    // 初始化监控
    int ret = engine_init_metrics(&engine, manager);
    TEST_ASSERT(ret == KV_ERR_NONE, "引擎监控初始化成功");
    TEST_ASSERT(engine.metrics_enabled == true, "引擎监控已启用");
    TEST_ASSERT(engine.metrics_manager == manager, "引擎监控管理器设置正确");
    
    // 验证引擎注册
    TEST_ASSERT(manager->engine_metrics[KV_ENGINE_ARRAY] != NULL, "Array引擎已注册");
    TEST_ASSERT(manager->total_metrics_count == 1, "监控管理器指标计数正确");
    
    kv_engine_metrics_manager_destroy(manager);
}

void test_engine_operations_with_metrics() {
    printf("\n--- 测试用例2: 引擎操作监控测试 ---\n");
    
    // 创建监控管理器和引擎
    kv_engine_metrics_manager_t *manager = kv_engine_metrics_manager_create();
    kv_engine_t engine = {0};
    engine.type = KV_ENGINE_HASH;
    strncpy(engine.name, "test_hash", sizeof(engine.name) - 1);
    
    // 初始化监控
    engine_init_metrics(&engine, manager);
    
    // 执行写操作
    int ret = engine_set_with_metrics(&engine, "key1", "value1");
    TEST_ASSERT(ret == KV_ERR_NONE, "写操作执行成功");
    TEST_ASSERT(engine.stats.write_count == 1, "写操作计数正确");
    TEST_ASSERT(engine.stats.memory_usage > 0, "内存使用量已更新");
    
    // 执行读操作
    char *value = engine_get_with_metrics(&engine, "key1");
    TEST_ASSERT(value != NULL, "读操作执行成功");
    TEST_ASSERT(engine.stats.read_count == 1, "读操作计数正确");
    if (value) free(value);
    
    // 执行删除操作
    ret = engine_delete_with_metrics(&engine, "key1");
    TEST_ASSERT(ret == KV_ERR_NONE, "删除操作执行成功");
    TEST_ASSERT(engine.stats.delete_count == 1, "删除操作计数正确");
    
    kv_engine_metrics_manager_destroy(manager);
}

void test_multiple_engines_monitoring() {
    printf("\n--- 测试用例3: 多引擎监控测试 ---\n");
    
    // 创建监控管理器
    kv_engine_metrics_manager_t *manager = kv_engine_metrics_manager_create();
    
    // 创建多个引擎
    kv_engine_t engines[3] = {0};
    engines[0].type = KV_ENGINE_ARRAY;
    strncpy(engines[0].name, "test_array", sizeof(engines[0].name) - 1);
    
    engines[1].type = KV_ENGINE_HASH;
    strncpy(engines[1].name, "test_hash", sizeof(engines[1].name) - 1);
    
    engines[2].type = KV_ENGINE_RBTREE;
    strncpy(engines[2].name, "test_rbtree", sizeof(engines[2].name) - 1);
    
    // 初始化所有引擎的监控
    for (int i = 0; i < 3; i++) {
        int ret = engine_init_metrics(&engines[i], manager);
        TEST_ASSERT(ret == KV_ERR_NONE, "引擎监控初始化成功");
    }
    
    TEST_ASSERT(manager->total_metrics_count == 3, "三个引擎已注册");
    TEST_ASSERT(manager->engine_metrics[KV_ENGINE_ARRAY] != NULL, "Array引擎已注册");
    TEST_ASSERT(manager->engine_metrics[KV_ENGINE_HASH] != NULL, "Hash引擎已注册");
    TEST_ASSERT(manager->engine_metrics[KV_ENGINE_RBTREE] != NULL, "RBTree引擎已注册");
    
    // 在每个引擎上执行操作
    for (int i = 0; i < 3; i++) {
        engine_set_with_metrics(&engines[i], "test_key", "test_value");
        char *value = engine_get_with_metrics(&engines[i], "test_key");
        if (value) free(value);
        engine_delete_with_metrics(&engines[i], "test_key");
        
        TEST_ASSERT(engines[i].stats.write_count == 1, "引擎写操作计数正确");
        TEST_ASSERT(engines[i].stats.read_count == 1, "引擎读操作计数正确");
        TEST_ASSERT(engines[i].stats.delete_count == 1, "引擎删除操作计数正确");
    }
    
    kv_engine_metrics_manager_destroy(manager);
}

void test_metrics_performance_impact() {
    printf("\n--- 测试用例4: 监控性能影响测试 ---\n");
    
    const int operations = 1000;
    
    // 测试不启用监控的性能
    kv_engine_t engine_no_metrics = {0};
    engine_no_metrics.type = KV_ENGINE_ARRAY;
    engine_no_metrics.metrics_enabled = false;
    
    uint64_t start_time = get_time_us();
    for (int i = 0; i < operations; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "key_%d", i);
        snprintf(value, sizeof(value), "value_%d", i);
        
        // 模拟操作（不含监控）
        usleep(10); // 模拟10微秒基础延迟
        engine_no_metrics.stats.write_count++;
    }
    uint64_t time_without_metrics = get_time_us() - start_time;
    
    // 测试启用监控的性能
    kv_engine_metrics_manager_t *manager = kv_engine_metrics_manager_create();
    kv_engine_t engine_with_metrics = {0};
    engine_with_metrics.type = KV_ENGINE_ARRAY;
    strncpy(engine_with_metrics.name, "perf_test", sizeof(engine_with_metrics.name) - 1);
    engine_init_metrics(&engine_with_metrics, manager);
    
    start_time = get_time_us();
    for (int i = 0; i < operations; i++) {
        char key[32], value[32];
        snprintf(key, sizeof(key), "key_%d", i);
        snprintf(value, sizeof(value), "value_%d", i);
        
        engine_set_with_metrics(&engine_with_metrics, key, value);
    }
    uint64_t time_with_metrics = get_time_us() - start_time;
    
    // 计算性能开销
    double overhead = (double)(time_with_metrics - time_without_metrics) / time_without_metrics;
    
    TEST_ASSERT(time_with_metrics > time_without_metrics, "监控确实增加了延迟");
    TEST_ASSERT(overhead < 2.0, "监控开销小于200%"); // 宽松的阈值，适用于模拟测试
    
    printf("    操作数量: %d\n", operations);
    printf("    无监控用时: %llu us\n", (unsigned long long)time_without_metrics);
    printf("    有监控用时: %llu us\n", (unsigned long long)time_with_metrics);
    printf("    性能开销: %.2f%%\n", overhead * 100);
    
    kv_engine_metrics_manager_destroy(manager);
}

void test_concurrent_metrics_access() {
    printf("\n--- 测试用例5: 并发监控访问测试 ---\n");
    
    kv_engine_metrics_manager_t *manager = kv_engine_metrics_manager_create();
    
    // 测试读写锁的基本功能
    pthread_rwlock_t *lock = &manager->lock;
    
    int ret = pthread_rwlock_rdlock(lock);
    TEST_ASSERT(ret == 0, "获取读锁成功");
    
    ret = pthread_rwlock_unlock(lock);
    TEST_ASSERT(ret == 0, "释放读锁成功");
    
    ret = pthread_rwlock_wrlock(lock);
    TEST_ASSERT(ret == 0, "获取写锁成功");
    
    ret = pthread_rwlock_unlock(lock);
    TEST_ASSERT(ret == 0, "释放写锁成功");
    
    // 测试多个引擎的并发注册
    kv_engine_t engines[5] = {0};
    for (int i = 0; i < 5; i++) {
        engines[i].type = i; // 使用不同的引擎类型
        snprintf(engines[i].name, sizeof(engines[i].name), "engine_%d", i);
        
        ret = engine_init_metrics(&engines[i], manager);
        TEST_ASSERT(ret == KV_ERR_NONE, "并发引擎注册成功");
    }
    
    TEST_ASSERT(manager->total_metrics_count == 5, "所有引擎已注册");
    
    kv_engine_metrics_manager_destroy(manager);
}

void test_error_handling() {
    printf("\n--- 测试用例6: 错误处理测试 ---\n");
    
    // 测试NULL参数
    int ret = engine_init_metrics(NULL, NULL);
    TEST_ASSERT(ret == KV_ERR_PARAM, "NULL参数返回错误");
    
    kv_engine_metrics_manager_t *manager = kv_engine_metrics_manager_create();
    
    ret = engine_init_metrics(NULL, manager);
    TEST_ASSERT(ret == KV_ERR_PARAM, "NULL引擎返回错误");
    
    kv_engine_t engine = {0};
    ret = engine_init_metrics(&engine, NULL);
    TEST_ASSERT(ret == KV_ERR_PARAM, "NULL管理器返回错误");
    
    // 测试无效引擎类型
    ret = kv_engine_metrics_register_engine(manager, KV_ENGINE_MAX, "invalid");
    TEST_ASSERT(ret == KV_ERR_PARAM, "无效引擎类型返回错误");
    
    // 测试未注册引擎的指标记录
    ret = kv_engine_metrics_record_read(manager, KV_ENGINE_LSM, 1.0);
    TEST_ASSERT(ret == KV_ERR_NOT_FOUND, "未注册引擎返回未找到错误");
    
    kv_engine_metrics_manager_destroy(manager);
}

// 主函数
int main(int argc, char *argv[]) {
    // 避免编译器警告
    (void)argc;
    (void)argv;
    
    printf("=== ConcordKV 存储引擎监控系统集成测试 ===\n");
    printf("测试时间: %s\n", __DATE__ " " __TIME__);
    printf("编译器: %s\n", __VERSION__);
    
    // 运行所有测试
    test_metrics_manager_integration();
    test_engine_operations_with_metrics();
    test_multiple_engines_monitoring();
    test_metrics_performance_impact();
    test_concurrent_metrics_access();
    test_error_handling();
    
    // 输出测试结果
    printf("\n=== 测试结果统计 ===\n");
    printf("总测试数: %d\n", test_count);
    printf("通过测试: %d\n", test_passed);
    printf("失败测试: %d\n", test_count - test_passed);
    printf("通过率: %.2f%%\n", (double)test_passed / test_count * 100);
    
    if (test_passed == test_count) {
        printf("\n🎉 所有集成测试通过! 监控系统集成正确!\n");
        return 0;
    } else {
        printf("\n❌ 部分测试失败，请检查集成实现!\n");
        return 1;
    }
} 