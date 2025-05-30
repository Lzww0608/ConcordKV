/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV storage engine - kv_cluster.h
 */
#ifndef __KV_CLUSTER_H__
#define __KV_CLUSTER_H__

#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <netinet/in.h>

// 节点状态
#define NODE_STATUS_UNKNOWN    0   // 未知状态
#define NODE_STATUS_OFFLINE    1   // 离线
#define NODE_STATUS_ONLINE     2   // 在线
#define NODE_STATUS_JOINING    3   // 正在加入
#define NODE_STATUS_LEAVING    4   // 正在离开
#define NODE_STATUS_SYNCING    5   // 同步中

// 节点角色
#define NODE_ROLE_FOLLOWER     0   // 跟随者
#define NODE_ROLE_CANDIDATE    1   // 候选者
#define NODE_ROLE_LEADER       2   // 领导者

// 集群角色
#define CLUSTER_ROLE_MASTER    0   // 主集群
#define CLUSTER_ROLE_SLAVE     1   // 从集群

// 最大节点数
#define MAX_CLUSTER_NODES      64
// 最大分片数
#define MAX_CLUSTER_SHARDS     1024

// 节点定义
typedef struct {
    char node_id[64];          // 节点ID
    char host[64];             // 主机名或IP
    int port;                  // 端口
    int status;                // 节点状态
    int role;                  // 节点角色
    time_t last_heartbeat;     // 最后心跳时间
    uint64_t term;             // 当前任期
    int shard_count;           // 拥有的分片数量
    int *shards;               // 拥有的分片列表
} cluster_node_t;

// 分片定义
typedef struct {
    int shard_id;              // 分片ID
    int primary_node;          // 主节点索引
    int replica_count;         // 副本数量
    int replicas[3];           // 副本节点索引（最多3个）
    uint64_t version;          // 版本号
} cluster_shard_t;

// 集群定义
typedef struct {
    char cluster_id[64];       // 集群ID
    int role;                  // 集群角色
    int node_count;            // 节点数量
    cluster_node_t nodes[MAX_CLUSTER_NODES]; // 节点列表
    int shard_count;           // 分片数量
    cluster_shard_t shards[MAX_CLUSTER_SHARDS]; // 分片列表
    int self_node_idx;         // 自身节点索引
    pthread_mutex_t mutex;     // 集群状态锁
    uint64_t current_term;     // 当前任期
    int current_leader;        // 当前领导者索引
    void *engine;              // 存储引擎
} cluster_t;

// 初始化集群
int cluster_init(cluster_t *cluster, const char *cluster_id, int role, const char *self_id, const char *self_host, int self_port, void *engine);

// 销毁集群
void cluster_destroy(cluster_t *cluster);

// 添加节点
int cluster_add_node(cluster_t *cluster, const char *node_id, const char *host, int port, int role);

// 删除节点
int cluster_remove_node(cluster_t *cluster, const char *node_id);

// 添加分片
int cluster_add_shard(cluster_t *cluster, int shard_id, int primary_node);

// 添加分片副本
int cluster_add_shard_replica(cluster_t *cluster, int shard_id, int replica_node);

// 获取分片主节点
int cluster_get_shard_primary(cluster_t *cluster, int shard_id);

// 通过键获取分片ID
int cluster_get_shard_by_key(cluster_t *cluster, const char *key);

// 更新节点状态
int cluster_update_node_status(cluster_t *cluster, const char *node_id, int status);

// 开始选举
int cluster_start_election(cluster_t *cluster);

// 投票给候选人
int cluster_vote_for_candidate(cluster_t *cluster, const char *candidate_id, uint64_t term);

// 成为领导者
int cluster_become_leader(cluster_t *cluster);

// 发送心跳
int cluster_send_heartbeat(cluster_t *cluster);

// 处理收到的心跳
int cluster_process_heartbeat(cluster_t *cluster, const char *leader_id, uint64_t term);

// 同步分片数据
int cluster_sync_shard(cluster_t *cluster, int shard_id, int source_node, int target_node);

// 请求转发到正确的节点
int cluster_forward_request(cluster_t *cluster, int shard_id, const char *key, const char *value, int op_type);

// 检查集群健康状态
int cluster_health_check(cluster_t *cluster);

// 加载集群配置
int cluster_load_config(cluster_t *cluster, const char *config_file);

// 保存集群配置
int cluster_save_config(cluster_t *cluster, const char *config_file);

#endif // __KV_CLUSTER_H__ 