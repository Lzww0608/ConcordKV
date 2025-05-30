/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV test suite - simple_test.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <signal.h>
#include "kv_persist.h"

// 简单的模拟引擎
typedef struct {
    char keys[100][64];
    char values[100][256];
    int count;
} simple_engine_t;

simple_engine_t g_engine;

void init_engine() {
    memset(&g_engine, 0, sizeof(g_engine));
}

int apply_log(void *engine, uint8_t op_type, const char *key, const char *value) {
    simple_engine_t *eng = (simple_engine_t *)engine;
    
    if (op_type == WAL_OP_SET && eng->count < 100) {
        strcpy(eng->keys[eng->count], key);
        strcpy(eng->values[eng->count], value ? value : "");
        eng->count++;
    }
    
    return 0;
}

int get_all_keys(void *engine, char ***keys, int *count) {
    simple_engine_t *eng = (simple_engine_t *)engine;
    
    *keys = (char **)malloc(eng->count * sizeof(char *));
    for (int i = 0; i < eng->count; i++) {
        (*keys)[i] = strdup(eng->keys[i]);
    }
    *count = eng->count;
    
    return 0;
}

int get_value(void *engine, const char *key, char **value) {
    simple_engine_t *eng = (simple_engine_t *)engine;
    
    for (int i = 0; i < eng->count; i++) {
        if (strcmp(eng->keys[i], key) == 0) {
            *value = strdup(eng->values[i]);
            return 0;
        }
    }
    
    return -1;
}

void timeout_handler(int sig) {
    printf("测试超时！\n");
    exit(1);
}

int main() {
    printf("开始简单持久化功能测试...\n");
    
    // 设置超时
    signal(SIGALRM, timeout_handler);
    alarm(30);
    
    // 清理测试目录
    system("rm -rf /tmp/simple_test");
    
    init_engine();
    
    // 测试基本WAL功能
    printf("\n=== 测试基本WAL功能 ===\n");
    wal_t wal;
    assert(wal_init(&wal, "/tmp/simple_test", 1) == 0);
    
    // 停止后台线程避免竞争
    wal.compact_running = 0;
    wal.incremental_running = 0;
    
    // 写入一些数据
    assert(wal_append(&wal, WAL_OP_SET, "key1", "value1") == 0);
    assert(wal_append(&wal, WAL_OP_SET, "key2", "value2") == 0);
    assert(wal_append(&wal, WAL_OP_DEL, "key1", NULL) == 0);
    
    printf("✓ WAL写入测试通过\n");
    
    // 测试恢复
    wal_destroy(&wal);
    
    assert(wal_init(&wal, "/tmp/simple_test", 1) == 0);
    wal.compact_running = 0;
    wal.incremental_running = 0;
    
    assert(wal_recover(&wal, &g_engine, apply_log) == 0);
    
    printf("✓ WAL恢复测试通过，恢复了 %d 条记录\n", g_engine.count);
    
    // 测试压缩
    printf("\n=== 测试日志压缩 ===\n");
    
    // 添加一些数据到引擎
    strcpy(g_engine.keys[0], "compress_key1");
    strcpy(g_engine.values[0], "compress_value1");
    strcpy(g_engine.keys[1], "compress_key2");
    strcpy(g_engine.values[1], "compress_value2");
    g_engine.count = 2;
    
    assert(wal_compact_logs(&wal, &g_engine, get_all_keys, get_value) == 0);
    
    printf("✓ 日志压缩测试通过\n");
    
    // 测试增量同步
    printf("\n=== 测试增量同步 ===\n");
    
    assert(wal_incremental_sync(&wal, &g_engine) == 0);
    assert(wal_force_sync(&wal) == 0);
    
    printf("✓ 增量同步测试通过\n");
    
    wal_destroy(&wal);
    
    printf("\n🎉 所有简单测试通过！\n");
    
    // 清理
    system("rm -rf /tmp/simple_test");
    
    return 0;
} 