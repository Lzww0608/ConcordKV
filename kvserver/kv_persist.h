#ifndef __KV_PERSIST_H__
#define __KV_PERSIST_H__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

// WAL操作类型
#define WAL_OP_SET    1
#define WAL_OP_DEL    2
#define WAL_OP_MOD    3

// 日志条目结构
typedef struct {
    uint64_t seq;        // 序列号
    uint8_t op_type;     // 操作类型：SET/DEL/MOD
    uint32_t key_size;   // 键长度
    uint32_t value_size; // 值长度
    char *key;           // 键
    char *value;         // 值
} wal_entry_t;

// WAL管理结构
typedef struct {
    char *log_dir;       // 日志目录
    char *current_file;  // 当前日志文件路径
    int fd;              // 当前日志文件描述符
    uint64_t next_seq;   // 下一个序列号
    pthread_mutex_t mutex; // 写入锁
    int sync_write;      // 是否同步写入
} wal_t;

// WAL相关操作
int wal_init(wal_t *wal, const char *log_dir, int sync_write);
void wal_destroy(wal_t *wal);
int wal_append(wal_t *wal, uint8_t op_type, const char *key, const char *value);
int wal_recover(wal_t *wal, void *engine, 
                int (*apply_log)(void *engine, uint8_t op_type, const char *key, const char *value));

// 快照管理
typedef struct {
    char *snap_dir;      // 快照目录
    uint64_t snap_seq;   // 快照序列号
} snapshot_t;

// 快照相关操作
int snapshot_init(snapshot_t *snap, const char *snap_dir);
void snapshot_destroy(snapshot_t *snap);
int snapshot_create(snapshot_t *snap, void *engine, 
                   int (*save_data)(void *engine, FILE *fp));
int snapshot_load(snapshot_t *snap, void *engine,
                 int (*load_data)(void *engine, FILE *fp));

// 持久化状态结构
typedef struct {
    wal_t wal;
    snapshot_t snap;
    char *data_dir;
    int persistence_enabled;
} persist_state_t;

// 持久化状态操作
int persist_init(persist_state_t *ps, const char *data_dir, int sync_write);
void persist_destroy(persist_state_t *ps);

#endif // __KV_PERSIST_H__ 