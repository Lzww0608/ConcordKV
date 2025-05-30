/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV storage engine - kv_persist.h
 */
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
#include <time.h>

// WAL操作类型
#define WAL_OP_SET    1
#define WAL_OP_DEL    2
#define WAL_OP_MOD    3

// 日志压缩配置
#define WAL_COMPACT_THRESHOLD    1000    // 触发压缩的日志条目数
#define WAL_COMPACT_RATIO        0.3     // 压缩比例阈值
#define WAL_MAX_FILE_SIZE        (64 * 1024 * 1024)  // 64MB

// 增量持久化配置
#define INCREMENTAL_SYNC_INTERVAL  5     // 增量同步间隔(秒)
#define INCREMENTAL_BATCH_SIZE     100   // 增量批处理大小

// 日志条目结构
typedef struct {
    uint64_t seq;        // 序列号
    uint8_t op_type;     // 操作类型：SET/DEL/MOD
    uint32_t key_size;   // 键长度
    uint32_t value_size; // 值长度
    char *key;           // 键
    char *value;         // 值
    time_t timestamp;    // 时间戳
} wal_entry_t;

// WAL文件信息
typedef struct wal_file {
    char *filename;      // 文件名
    uint64_t start_seq;  // 起始序列号
    uint64_t end_seq;    // 结束序列号
    size_t file_size;    // 文件大小
    struct wal_file *next;
} wal_file_t;

// WAL管理结构
typedef struct {
    char *log_dir;       // 日志目录
    char *current_file;  // 当前日志文件路径
    int fd;              // 当前日志文件描述符
    uint64_t next_seq;   // 下一个序列号
    pthread_mutex_t mutex; // 写入锁
    int sync_write;      // 是否同步写入
    
    // 日志压缩相关
    wal_file_t *file_list;     // WAL文件链表
    uint64_t compact_seq;      // 已压缩的序列号
    pthread_t compact_thread;  // 压缩线程
    int compact_running;       // 压缩线程运行标志
    
    // 增量持久化相关
    uint64_t last_sync_seq;    // 上次同步的序列号
    pthread_t incremental_thread; // 增量同步线程
    int incremental_running;   // 增量同步线程运行标志
    time_t last_sync_time;     // 上次同步时间
} wal_t;

// WAL相关操作
int wal_init(wal_t *wal, const char *log_dir, int sync_write);
void wal_destroy(wal_t *wal);
int wal_append(wal_t *wal, uint8_t op_type, const char *key, const char *value);
int wal_recover(wal_t *wal, void *engine, 
                int (*apply_log)(void *engine, uint8_t op_type, const char *key, const char *value));

// 日志压缩功能
int wal_start_compaction(wal_t *wal, void *engine,
                        int (*get_all_keys)(void *engine, char ***keys, int *count));
int wal_compact_logs(wal_t *wal, void *engine,
                    int (*get_all_keys)(void *engine, char ***keys, int *count),
                    int (*get_value)(void *engine, const char *key, char **value));
void wal_cleanup_old_logs(wal_t *wal, uint64_t before_seq);

// 增量持久化功能
int wal_start_incremental_sync(wal_t *wal, void *engine);
int wal_incremental_sync(wal_t *wal, void *engine);
int wal_force_sync(wal_t *wal);

// 快照管理
typedef struct {
    char *snap_dir;      // 快照目录
    uint64_t snap_seq;   // 快照序列号
    time_t snap_time;    // 快照时间
    char *snap_file;     // 快照文件路径
} snapshot_t;

// 快照相关操作
int snapshot_init(snapshot_t *snap, const char *snap_dir);
void snapshot_destroy(snapshot_t *snap);
int snapshot_create(snapshot_t *snap, void *engine, 
                   int (*save_data)(void *engine, FILE *fp));
int snapshot_load(snapshot_t *snap, void *engine,
                 int (*load_data)(void *engine, FILE *fp));

// 增量快照功能
int snapshot_create_incremental(snapshot_t *snap, void *engine,
                               uint64_t from_seq, uint64_t to_seq,
                               int (*save_incremental)(void *engine, FILE *fp, uint64_t from_seq, uint64_t to_seq));
int snapshot_apply_incremental(snapshot_t *snap, void *engine,
                              int (*apply_incremental)(void *engine, FILE *fp));

// 持久化状态结构
typedef struct {
    wal_t wal;
    snapshot_t snap;
    char *data_dir;
    int persistence_enabled;
    
    // 性能统计
    uint64_t total_writes;
    uint64_t total_syncs;
    uint64_t total_compactions;
    time_t last_compact_time;
} persist_state_t;

// 持久化状态操作
int persist_init(persist_state_t *ps, const char *data_dir, int sync_write);
void persist_destroy(persist_state_t *ps);

// 持久化性能监控
typedef struct {
    uint64_t write_ops;
    uint64_t sync_ops;
    uint64_t compact_ops;
    double avg_write_latency;
    double avg_sync_latency;
    size_t total_log_size;
    size_t compressed_log_size;
} persist_stats_t;

int persist_get_stats(persist_state_t *ps, persist_stats_t *stats);
void persist_reset_stats(persist_state_t *ps);

#endif // __KV_PERSIST_H__ 