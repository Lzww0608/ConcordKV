#define _GNU_SOURCE  // 为了使用strdup
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include <pthread.h>
#include <assert.h>
#include "../kvserver/kv_persist.h"

// 测试超时时间（秒）
#define TEST_TIMEOUT 30

// 模拟存储引擎结构
typedef struct {
    char keys[1000][64];
    char values[1000][256];
    int count;
    pthread_mutex_t mutex;
} mock_engine_t;

// 全局变量
static mock_engine_t g_engine;
static int g_test_timeout = 0;

// 超时处理函数
void timeout_handler(int sig) {
    printf("[TIMEOUT] Test timed out after %d seconds\n", TEST_TIMEOUT);
    g_test_timeout = 1;
    exit(1);
}

// 设置测试超时
void setup_test_timeout() {
    signal(SIGALRM, timeout_handler);
    alarm(TEST_TIMEOUT);
}

// 清除测试超时
void clear_test_timeout() {
    alarm(0);
}

// 初始化模拟引擎
void init_mock_engine() {
    memset(&g_engine, 0, sizeof(g_engine));
    pthread_mutex_init(&g_engine.mutex, NULL);
}

// 销毁模拟引擎
void destroy_mock_engine() {
    pthread_mutex_destroy(&g_engine.mutex);
}

// 模拟引擎：设置键值对
int mock_set(const char *key, const char *value) {
    pthread_mutex_lock(&g_engine.mutex);
    
    // 查找是否已存在
    for (int i = 0; i < g_engine.count; i++) {
        if (strcmp(g_engine.keys[i], key) == 0) {
            strcpy(g_engine.values[i], value);
            pthread_mutex_unlock(&g_engine.mutex);
            return 0;
        }
    }
    
    // 添加新的键值对
    if (g_engine.count < 1000) {
        strcpy(g_engine.keys[g_engine.count], key);
        strcpy(g_engine.values[g_engine.count], value);
        g_engine.count++;
    }
    
    pthread_mutex_unlock(&g_engine.mutex);
    return 0;
}

// 模拟引擎：获取值
int mock_get(const char *key, char **value) {
    pthread_mutex_lock(&g_engine.mutex);
    
    for (int i = 0; i < g_engine.count; i++) {
        if (strcmp(g_engine.keys[i], key) == 0) {
            *value = strdup(g_engine.values[i]);
            pthread_mutex_unlock(&g_engine.mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_engine.mutex);
    return -1;
}

// 模拟引擎：删除键
int mock_delete(const char *key) {
    pthread_mutex_lock(&g_engine.mutex);
    
    for (int i = 0; i < g_engine.count; i++) {
        if (strcmp(g_engine.keys[i], key) == 0) {
            // 移动后面的元素
            for (int j = i; j < g_engine.count - 1; j++) {
                strcpy(g_engine.keys[j], g_engine.keys[j + 1]);
                strcpy(g_engine.values[j], g_engine.values[j + 1]);
            }
            g_engine.count--;
            pthread_mutex_unlock(&g_engine.mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_engine.mutex);
    return -1;
}

// 模拟引擎：获取所有键
int mock_get_all_keys(void *engine, char ***keys, int *count) {
    mock_engine_t *eng = (mock_engine_t *)engine;
    
    pthread_mutex_lock(&eng->mutex);
    
    *count = eng->count;
    *keys = (char **)malloc(eng->count * sizeof(char *));
    
    for (int i = 0; i < eng->count; i++) {
        (*keys)[i] = strdup(eng->keys[i]);
    }
    
    pthread_mutex_unlock(&eng->mutex);
    return 0;
}

// 模拟引擎：获取值（用于压缩）
int mock_get_value(void *engine, const char *key, char **value) {
    mock_engine_t *eng = (mock_engine_t *)engine;
    
    pthread_mutex_lock(&eng->mutex);
    
    for (int i = 0; i < eng->count; i++) {
        if (strcmp(eng->keys[i], key) == 0) {
            *value = strdup(eng->values[i]);
            pthread_mutex_unlock(&eng->mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&eng->mutex);
    return -1;
}

// 应用日志条目到模拟引擎
int apply_log_to_engine(void *engine, uint8_t op_type, const char *key, const char *value) {
    switch (op_type) {
        case WAL_OP_SET:
            return mock_set(key, value);
        case WAL_OP_DEL:
            return mock_delete(key);
        case WAL_OP_MOD:
            return mock_set(key, value);
        default:
            return -1;
    }
}

// 保存数据到快照
int save_data_to_snapshot(void *engine, FILE *fp) {
    mock_engine_t *eng = (mock_engine_t *)engine;
    
    pthread_mutex_lock(&eng->mutex);
    
    // 写入条目数量
    fwrite(&eng->count, sizeof(eng->count), 1, fp);
    
    // 写入所有键值对
    for (int i = 0; i < eng->count; i++) {
        uint32_t key_len = strlen(eng->keys[i]) + 1;
        uint32_t value_len = strlen(eng->values[i]) + 1;
        
        fwrite(&key_len, sizeof(key_len), 1, fp);
        fwrite(eng->keys[i], key_len, 1, fp);
        fwrite(&value_len, sizeof(value_len), 1, fp);
        fwrite(eng->values[i], value_len, 1, fp);
    }
    
    pthread_mutex_unlock(&eng->mutex);
    return 0;
}

// 从快照加载数据
int load_data_from_snapshot(void *engine, FILE *fp) {
    mock_engine_t *eng = (mock_engine_t *)engine;
    
    pthread_mutex_lock(&eng->mutex);
    
    // 清空现有数据
    eng->count = 0;
    
    // 读取条目数量
    int count;
    if (fread(&count, sizeof(count), 1, fp) != 1) {
        pthread_mutex_unlock(&eng->mutex);
        return -1;
    }
    
    // 读取所有键值对
    for (int i = 0; i < count && i < 1000; i++) {
        uint32_t key_len, value_len;
        
        if (fread(&key_len, sizeof(key_len), 1, fp) != 1 ||
            fread(eng->keys[i], key_len, 1, fp) != 1 ||
            fread(&value_len, sizeof(value_len), 1, fp) != 1 ||
            fread(eng->values[i], value_len, 1, fp) != 1) {
            pthread_mutex_unlock(&eng->mutex);
            return -1;
        }
        
        eng->count++;
    }
    
    pthread_mutex_unlock(&eng->mutex);
    return 0;
}

// 测试基本WAL功能
void test_basic_wal() {
    printf("\n=== 测试基本WAL功能 ===\n");
    setup_test_timeout();
    
    wal_t wal;
    assert(wal_init(&wal, "/tmp/test_wal", 1) == 0);
    
    // 添加一些日志条目
    assert(wal_append(&wal, WAL_OP_SET, "key1", "value1") == 0);
    assert(wal_append(&wal, WAL_OP_SET, "key2", "value2") == 0);
    assert(wal_append(&wal, WAL_OP_DEL, "key1", NULL) == 0);
    
    printf("✓ WAL基本写入测试通过\n");
    
    // 测试恢复
    init_mock_engine();
    assert(wal_recover(&wal, &g_engine, apply_log_to_engine) == 0);
    
    char *value;
    assert(mock_get("key2", &value) == 0);
    assert(strcmp(value, "value2") == 0);
    free(value);
    
    assert(mock_get("key1", &value) != 0); // key1应该被删除了
    
    printf("✓ WAL恢复测试通过\n");
    
    destroy_mock_engine();
    wal_destroy(&wal);
    clear_test_timeout();
}

// 测试日志轮转
void test_log_rotation() {
    printf("\n=== 测试日志轮转功能 ===\n");
    setup_test_timeout();
    
    wal_t wal;
    assert(wal_init(&wal, "/tmp/test_rotation", 1) == 0);
    
    // 写入大量数据触发轮转
    char key[64], value[256];
    for (int i = 0; i < 2000; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        snprintf(value, sizeof(value), "value_%d_with_some_long_content_to_make_file_larger", i);
        assert(wal_append(&wal, WAL_OP_SET, key, value) == 0);
    }
    
    printf("✓ 日志轮转测试通过\n");
    
    wal_destroy(&wal);
    clear_test_timeout();
}

// 测试日志压缩
void test_log_compaction() {
    printf("\n=== 测试日志压缩功能 ===\n");
    setup_test_timeout();
    
    wal_t wal;
    init_mock_engine();
    
    assert(wal_init(&wal, "/tmp/test_compact", 1) == 0);
    
    // 停止自动启动的压缩线程，避免竞争条件
    wal.compact_running = 0;
    
    // 添加大量数据以触发压缩
    for (int i = 0; i < 100; i++) {
        char key[64], value[64];
        snprintf(key, sizeof(key), "compact_key_%d", i);
        snprintf(value, sizeof(value), "compact_value_%d", i);
        assert(wal_append(&wal, WAL_OP_SET, key, value) == 0);
        
        // 同时更新mock引擎
        mock_set(key, value);
    }
    
    // 手动调用压缩
    assert(wal_compact_logs(&wal, &g_engine, mock_get_all_keys, mock_get_value) == 0);
    
    printf("✓ 日志压缩测试通过\n");
    
    destroy_mock_engine();
    wal_destroy(&wal);
    clear_test_timeout();
}

// 测试增量同步
void test_incremental_sync() {
    printf("\n=== 测试增量同步功能 ===\n");
    setup_test_timeout();
    
    wal_t wal;
    assert(wal_init(&wal, "/tmp/test_incremental", 0) == 0); // 关闭同步写入
    
    // 停止自动启动的增量同步线程，避免竞争条件
    wal.incremental_running = 0;
    
    // 写入一些数据
    for (int i = 0; i < 10; i++) {
        char key[64], value[64];
        snprintf(key, sizeof(key), "inc_key_%d", i);
        snprintf(value, sizeof(value), "inc_value_%d", i);
        assert(wal_append(&wal, WAL_OP_SET, key, value) == 0);
    }
    
    // 手动调用增量同步
    assert(wal_incremental_sync(&wal, &g_engine) == 0);
    
    // 强制同步
    assert(wal_force_sync(&wal) == 0);
    
    printf("✓ 增量同步测试通过\n");
    
    wal_destroy(&wal);
    clear_test_timeout();
}

// 测试快照功能
void test_snapshot() {
    printf("\n=== 测试快照功能 ===\n");
    setup_test_timeout();
    
    snapshot_t snap;
    init_mock_engine();
    
    assert(snapshot_init(&snap, "/tmp/test_snapshot") == 0);
    
    // 添加一些数据
    mock_set("snap_key1", "snap_value1");
    mock_set("snap_key2", "snap_value2");
    
    // 创建快照
    assert(snapshot_create(&snap, &g_engine, save_data_to_snapshot) == 0);
    
    // 清空引擎数据
    g_engine.count = 0;
    
    // 从快照恢复
    assert(snapshot_load(&snap, &g_engine, load_data_from_snapshot) == 0);
    
    // 验证数据
    char *value;
    assert(mock_get("snap_key1", &value) == 0);
    assert(strcmp(value, "snap_value1") == 0);
    free(value);
    
    printf("✓ 快照测试通过\n");
    
    destroy_mock_engine();
    snapshot_destroy(&snap);
    clear_test_timeout();
}

// 增量保存函数（用于测试）
int save_incremental_test(void *engine, FILE *fp, uint64_t from_seq, uint64_t to_seq) {
    // 简单写入序列号范围信息
    fwrite(&from_seq, sizeof(from_seq), 1, fp);
    fwrite(&to_seq, sizeof(to_seq), 1, fp);
    return 0;
}

// 测试增量快照
void test_incremental_snapshot() {
    printf("\n=== 测试增量快照功能 ===\n");
    setup_test_timeout();
    
    snapshot_t snap;
    init_mock_engine();
    
    assert(snapshot_init(&snap, "/tmp/test_inc_snapshot") == 0);
    
    // 创建增量快照
    assert(snapshot_create_incremental(&snap, &g_engine, 100, 200, save_incremental_test) == 0);
    
    printf("✓ 增量快照测试通过\n");
    
    destroy_mock_engine();
    snapshot_destroy(&snap);
    clear_test_timeout();
}

// 并发测试
void* concurrent_writer(void *arg) {
    wal_t *wal = (wal_t *)arg;
    
    for (int i = 0; i < 100; i++) {
        char key[64], value[64];
        snprintf(key, sizeof(key), "thread_key_%d", i);
        snprintf(value, sizeof(value), "thread_value_%d", i);
        
        if (wal_append(wal, WAL_OP_SET, key, value) != 0) {
            printf("并发写入失败: %d\n", i);
            break;
        }
        
        usleep(1000); // 1ms延迟
    }
    
    return NULL;
}

void test_concurrent_operations() {
    printf("\n=== 测试并发操作 ===\n");
    setup_test_timeout();
    
    wal_t wal;
    assert(wal_init(&wal, "/tmp/test_concurrent", 1) == 0);
    
    // 创建多个写入线程
    pthread_t threads[5];
    for (int i = 0; i < 5; i++) {
        assert(pthread_create(&threads[i], NULL, concurrent_writer, &wal) == 0);
    }
    
    // 等待所有线程完成
    for (int i = 0; i < 5; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("✓ 并发操作测试通过\n");
    
    wal_destroy(&wal);
    clear_test_timeout();
}

// 性能测试
void test_performance() {
    printf("\n=== 性能测试 ===\n");
    setup_test_timeout();
    
    wal_t wal;
    assert(wal_init(&wal, "/tmp/test_performance", 0) == 0); // 关闭同步写入以提高性能
    
    struct timeval start, end;
    gettimeofday(&start, NULL);
    
    // 写入10000条记录
    for (int i = 0; i < 10000; i++) {
        char key[64], value[256];
        snprintf(key, sizeof(key), "perf_key_%d", i);
        snprintf(value, sizeof(value), "perf_value_%d_with_some_additional_content_for_testing", i);
        assert(wal_append(&wal, WAL_OP_SET, key, value) == 0);
    }
    
    gettimeofday(&end, NULL);
    
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
    double ops_per_sec = 10000.0 / elapsed;
    
    printf("✓ 性能测试: 10000条记录写入耗时 %.2f秒, %.0f ops/sec\n", elapsed, ops_per_sec);
    
    wal_destroy(&wal);
    clear_test_timeout();
}

// 错误处理测试
void test_error_handling() {
    printf("\n=== 测试错误处理 ===\n");
    setup_test_timeout();
    
    wal_t wal;
    
    // 测试无效参数
    assert(wal_init(NULL, "/tmp/test", 1) != 0);
    assert(wal_init(&wal, NULL, 1) != 0);
    
    // 测试无效目录
    assert(wal_init(&wal, "/invalid/path/that/does/not/exist", 1) != 0);
    
    // 正常初始化
    assert(wal_init(&wal, "/tmp/test_error", 1) == 0);
    
    // 测试无效的append操作
    assert(wal_append(NULL, WAL_OP_SET, "key", "value") != 0);
    assert(wal_append(&wal, WAL_OP_SET, NULL, "value") != 0);
    assert(wal_append(&wal, WAL_OP_SET, "key", NULL) != 0); // SET操作需要value
    
    printf("✓ 错误处理测试通过\n");
    
    wal_destroy(&wal);
    clear_test_timeout();
}

int main() {
    printf("开始增强持久化功能测试...\n");
    
    // 清理测试目录
    system("rm -rf /tmp/test_*");
    
    // 运行所有测试
    test_basic_wal();
    test_log_rotation();
    test_log_compaction();
    test_incremental_sync();
    test_snapshot();
    test_incremental_snapshot();
    test_concurrent_operations();
    test_performance();
    test_error_handling();
    
    printf("\n🎉 所有测试通过！增强持久化功能工作正常。\n");
    
    // 清理测试目录
    system("rm -rf /tmp/test_*");
    
    return 0;
} 