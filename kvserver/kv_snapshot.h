/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV storage engine - kv_snapshot.h
 */
#ifndef __KV_SNAPSHOT_H__
#define __KV_SNAPSHOT_H__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// 快照文件头格式版本
#define KV_SNAPSHOT_VERSION 1

// 快照数据对象类型
#define KV_SNAP_METADATA     1   // 元数据
#define KV_SNAP_KV_PAIR      2   // 键值对
#define KV_SNAP_SHARD_INFO   3   // 分片信息
#define KV_SNAP_END          4   // 结束标记

// 快照文件头
typedef struct {
    uint32_t magic;            // 魔数: 0x4B565348 ("KVSH")
    uint32_t version;          // 版本号
    uint64_t timestamp;        // 创建时间戳
    uint64_t kv_count;         // 键值对数量
    uint32_t checksum;         // 校验和
    uint32_t flags;            // 标志位
    char engine_name[32];      // 存储引擎名称
    char reserved[64];         // 预留
} kv_snapshot_header_t;

// 快照数据条目头
typedef struct {
    uint32_t type;             // 数据类型
    uint32_t data_size;        // 数据大小
    uint32_t checksum;         // 校验和
} kv_snapshot_entry_header_t;

// 快照元数据
typedef struct {
    uint64_t term;             // Raft任期
    uint64_t index;            // Raft索引
    char cluster_id[64];       // 集群ID
    char node_id[64];          // 节点ID
    uint64_t timestamp;        // 时间戳
} kv_snapshot_metadata_t;

// 快照键值对数据
typedef struct {
    uint32_t key_size;         // 键大小
    uint32_t value_size;       // 值大小
    // 键和值的数据紧随其后
} kv_snapshot_kv_t;

// 快照分片信息
typedef struct {
    uint32_t shard_id;         // 分片ID
    uint32_t primary_node;     // 主节点
    uint32_t replica_count;    // 副本数量
    uint32_t replicas[3];      // 副本节点
    uint64_t version;          // 版本号
} kv_snapshot_shard_t;

// 快照上下文
typedef struct {
    char *snapshot_dir;        // 快照目录
    uint64_t last_index;       // 最新的Raft索引
    uint64_t last_term;        // 最新的Raft任期
    time_t last_time;          // 最近快照时间
    char last_file[256];       // 最近快照文件
} kv_snapshot_ctx_t;

// 初始化快照上下文
int kv_snapshot_init(kv_snapshot_ctx_t *ctx, const char *snapshot_dir);

// 销毁快照上下文
void kv_snapshot_destroy(kv_snapshot_ctx_t *ctx);

// 创建快照
int kv_snapshot_create(kv_snapshot_ctx_t *ctx, void *engine, const char *engine_name,
                       uint64_t term, uint64_t index, const char *cluster_id, const char *node_id);

// 加载快照
int kv_snapshot_load(kv_snapshot_ctx_t *ctx, void *engine, const char *engine_name);

// 获取最新快照信息
int kv_snapshot_get_latest(kv_snapshot_ctx_t *ctx, uint64_t *term, uint64_t *index);

// 删除过期快照
int kv_snapshot_prune(kv_snapshot_ctx_t *ctx, int keep_count);

// 计算校验和
uint32_t kv_snapshot_checksum(const void *data, size_t size);

// 快照读取回调类型
typedef int (*kv_snapshot_read_callback_t)(void *engine, const char *key, size_t key_size, const char *value, size_t value_size);

// 快照写入回调类型
typedef int (*kv_snapshot_write_callback_t)(void *engine, void *user_data, char **key, size_t *key_size, char **value, size_t *value_size);

// 列出快照文件
int kv_snapshot_list(kv_snapshot_ctx_t *ctx, char ***files, int *count);

// 验证快照完整性
int kv_snapshot_verify(const char *snapshot_file);

#endif // __KV_SNAPSHOT_H__ 