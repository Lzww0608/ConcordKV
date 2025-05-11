#include "kv_cluster.h"
#include "kv_store.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

// 哈希函数，用于基于键的值确定分片
static int hash_key_for_shard(const char *key, int shard_count) {
    unsigned long hash = 0;
    int c;
    
    while ((c = *key++)) {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }
    
    return hash % shard_count;
}

// 获取当前时间戳
static uint64_t get_timestamp() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 初始化集群
int cluster_init(cluster_t *cluster, const char *cluster_id, int role, const char *self_id, const char *self_host, int self_port, void *engine) {
    if (!cluster || !cluster_id || !self_id || !self_host || !engine) return -1;
    
    memset(cluster, 0, sizeof(cluster_t));
    
    strncpy(cluster->cluster_id, cluster_id, sizeof(cluster->cluster_id) - 1);
    cluster->role = role;
    cluster->engine = engine;
    cluster->current_term = 0;
    cluster->current_leader = -1;
    
    // 初始化互斥锁
    if (pthread_mutex_init(&cluster->mutex, NULL) != 0) {
        return -1;
    }
    
    // 添加自身作为第一个节点
    cluster->self_node_idx = 0;
    cluster_add_node(cluster, self_id, self_host, self_port, NODE_ROLE_FOLLOWER);
    
    // 初始时所有分片都分配给自己
    for (int i = 0; i < MAX_CLUSTER_SHARDS; i++) {
        cluster_add_shard(cluster, i, 0);
    }
    
    return 0;
}

// 销毁集群
void cluster_destroy(cluster_t *cluster) {
    if (!cluster) return;
    
    pthread_mutex_lock(&cluster->mutex);
    
    // 释放所有节点的分片列表内存
    for (int i = 0; i < cluster->node_count; i++) {
        if (cluster->nodes[i].shards) {
            free(cluster->nodes[i].shards);
            cluster->nodes[i].shards = NULL;
        }
    }
    
    pthread_mutex_unlock(&cluster->mutex);
    pthread_mutex_destroy(&cluster->mutex);
}

// 添加节点
int cluster_add_node(cluster_t *cluster, const char *node_id, const char *host, int port, int role) {
    if (!cluster || !node_id || !host || cluster->node_count >= MAX_CLUSTER_NODES) return -1;
    
    pthread_mutex_lock(&cluster->mutex);
    
    // 检查节点是否已存在
    for (int i = 0; i < cluster->node_count; i++) {
        if (strcmp(cluster->nodes[i].node_id, node_id) == 0) {
            pthread_mutex_unlock(&cluster->mutex);
            return -1; // 节点已存在
        }
    }
    
    int idx = cluster->node_count;
    
    strncpy(cluster->nodes[idx].node_id, node_id, sizeof(cluster->nodes[idx].node_id) - 1);
    strncpy(cluster->nodes[idx].host, host, sizeof(cluster->nodes[idx].host) - 1);
    cluster->nodes[idx].port = port;
    cluster->nodes[idx].status = NODE_STATUS_JOINING;
    cluster->nodes[idx].role = role;
    cluster->nodes[idx].last_heartbeat = time(NULL);
    cluster->nodes[idx].term = cluster->current_term;
    cluster->nodes[idx].shard_count = 0;
    cluster->nodes[idx].shards = NULL;
    
    cluster->node_count++;
    
    pthread_mutex_unlock(&cluster->mutex);
    return 0;
}

// 删除节点
int cluster_remove_node(cluster_t *cluster, const char *node_id) {
    if (!cluster || !node_id) return -1;
    
    pthread_mutex_lock(&cluster->mutex);
    
    int idx = -1;
    // 查找节点索引
    for (int i = 0; i < cluster->node_count; i++) {
        if (strcmp(cluster->nodes[i].node_id, node_id) == 0) {
            idx = i;
            break;
        }
    }
    
    if (idx < 0) {
        pthread_mutex_unlock(&cluster->mutex);
        return -1; // 节点不存在
    }
    
    // 如果是自己，不允许删除
    if (idx == cluster->self_node_idx) {
        pthread_mutex_unlock(&cluster->mutex);
        return -1;
    }
    
    // 释放节点的分片列表
    if (cluster->nodes[idx].shards) {
        free(cluster->nodes[idx].shards);
    }
    
    // 重新分配该节点的分片
    for (int i = 0; i < cluster->shard_count; i++) {
        if (cluster->shards[i].primary_node == idx) {
            // 找一个副本作为新的主节点
            if (cluster->shards[i].replica_count > 0) {
                cluster->shards[i].primary_node = cluster->shards[i].replicas[0];
                // 移除第一个副本
                for (int j = 0; j < cluster->shards[i].replica_count - 1; j++) {
                    cluster->shards[i].replicas[j] = cluster->shards[i].replicas[j + 1];
                }
                cluster->shards[i].replica_count--;
            } else {
                // 没有副本，分配给自己
                cluster->shards[i].primary_node = cluster->self_node_idx;
            }
        }
        
        // 从副本列表中移除
        for (int j = 0; j < cluster->shards[i].replica_count; j++) {
            if (cluster->shards[i].replicas[j] == idx) {
                // 移除这个副本
                for (int k = j; k < cluster->shards[i].replica_count - 1; k++) {
                    cluster->shards[i].replicas[k] = cluster->shards[i].replicas[k + 1];
                }
                cluster->shards[i].replica_count--;
                break;
            }
        }
    }
    
    // 移除节点
    for (int i = idx; i < cluster->node_count - 1; i++) {
        cluster->nodes[i] = cluster->nodes[i + 1];
    }
    
    cluster->node_count--;
    
    // 如果删除的节点在自己前面，需要更新自己的索引
    if (idx < cluster->self_node_idx) {
        cluster->self_node_idx--;
    }
    
    // 如果删除的是当前领导者
    if (idx == cluster->current_leader) {
        cluster->current_leader = -1;
    }
    
    pthread_mutex_unlock(&cluster->mutex);
    return 0;
}

// 添加分片
int cluster_add_shard(cluster_t *cluster, int shard_id, int primary_node) {
    if (!cluster || shard_id < 0 || shard_id >= MAX_CLUSTER_SHARDS || primary_node < 0 || primary_node >= cluster->node_count) return -1;
    
    pthread_mutex_lock(&cluster->mutex);
    
    // 检查分片是否已存在
    for (int i = 0; i < cluster->shard_count; i++) {
        if (cluster->shards[i].shard_id == shard_id) {
            pthread_mutex_unlock(&cluster->mutex);
            return -1; // 分片已存在
        }
    }
    
    int idx = cluster->shard_count;
    
    cluster->shards[idx].shard_id = shard_id;
    cluster->shards[idx].primary_node = primary_node;
    cluster->shards[idx].replica_count = 0;
    cluster->shards[idx].version = 1;
    
    cluster->shard_count++;
    
    // 更新节点的分片列表
    cluster_node_t *node = &cluster->nodes[primary_node];
    node->shard_count++;
    node->shards = (int *)realloc(node->shards, node->shard_count * sizeof(int));
    if (node->shards) {
        node->shards[node->shard_count - 1] = shard_id;
    }
    
    pthread_mutex_unlock(&cluster->mutex);
    return 0;
}

// 添加分片副本
int cluster_add_shard_replica(cluster_t *cluster, int shard_id, int replica_node) {
    if (!cluster || shard_id < 0 || shard_id >= MAX_CLUSTER_SHARDS || replica_node < 0 || replica_node >= cluster->node_count) return -1;
    
    pthread_mutex_lock(&cluster->mutex);
    
    // 查找分片
    int shard_idx = -1;
    for (int i = 0; i < cluster->shard_count; i++) {
        if (cluster->shards[i].shard_id == shard_id) {
            shard_idx = i;
            break;
        }
    }
    
    if (shard_idx < 0) {
        pthread_mutex_unlock(&cluster->mutex);
        return -1; // 分片不存在
    }
    
    // 检查副本是否已存在或是否是主节点
    if (cluster->shards[shard_idx].primary_node == replica_node) {
        pthread_mutex_unlock(&cluster->mutex);
        return -1; // 不能将主节点添加为副本
    }
    
    for (int i = 0; i < cluster->shards[shard_idx].replica_count; i++) {
        if (cluster->shards[shard_idx].replicas[i] == replica_node) {
            pthread_mutex_unlock(&cluster->mutex);
            return -1; // 副本已存在
        }
    }
    
    // 检查副本数量限制
    if (cluster->shards[shard_idx].replica_count >= 3) {
        pthread_mutex_unlock(&cluster->mutex);
        return -1; // 副本数量已达上限
    }
    
    // 添加副本
    cluster->shards[shard_idx].replicas[cluster->shards[shard_idx].replica_count++] = replica_node;
    
    // 更新版本号
    cluster->shards[shard_idx].version++;
    
    // 更新节点的分片列表
    cluster_node_t *node = &cluster->nodes[replica_node];
    node->shard_count++;
    node->shards = (int *)realloc(node->shards, node->shard_count * sizeof(int));
    if (node->shards) {
        node->shards[node->shard_count - 1] = shard_id;
    }
    
    pthread_mutex_unlock(&cluster->mutex);
    return 0;
}

// 获取分片主节点
int cluster_get_shard_primary(cluster_t *cluster, int shard_id) {
    if (!cluster || shard_id < 0 || shard_id >= MAX_CLUSTER_SHARDS) return -1;
    
    pthread_mutex_lock(&cluster->mutex);
    
    // 查找分片
    for (int i = 0; i < cluster->shard_count; i++) {
        if (cluster->shards[i].shard_id == shard_id) {
            int primary = cluster->shards[i].primary_node;
            pthread_mutex_unlock(&cluster->mutex);
            return primary;
        }
    }
    
    pthread_mutex_unlock(&cluster->mutex);
    return -1; // 分片不存在
}

// 通过键获取分片ID
int cluster_get_shard_by_key(cluster_t *cluster, const char *key) {
    if (!cluster || !key) return -1;
    
    // 简单地基于键的哈希值确定分片
    return hash_key_for_shard(key, cluster->shard_count);
}

// 更新节点状态
int cluster_update_node_status(cluster_t *cluster, const char *node_id, int status) {
    if (!cluster || !node_id) return -1;
    
    pthread_mutex_lock(&cluster->mutex);
    
    // 查找节点
    for (int i = 0; i < cluster->node_count; i++) {
        if (strcmp(cluster->nodes[i].node_id, node_id) == 0) {
            cluster->nodes[i].status = status;
            pthread_mutex_unlock(&cluster->mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&cluster->mutex);
    return -1; // 节点不存在
}

// 开始选举 (Raft算法)
int cluster_start_election(cluster_t *cluster) {
    if (!cluster) return -1;
    
    pthread_mutex_lock(&cluster->mutex);
    
    // 只有跟随者可以启动选举
    if (cluster->nodes[cluster->self_node_idx].role != NODE_ROLE_FOLLOWER) {
        pthread_mutex_unlock(&cluster->mutex);
        return -1;
    }
    
    // 增加任期并投票给自己
    cluster->current_term++;
    cluster->nodes[cluster->self_node_idx].term = cluster->current_term;
    cluster->nodes[cluster->self_node_idx].role = NODE_ROLE_CANDIDATE;
    
    // 记录投票数量（自己已投票）
    int votes = 1;
    int needed_votes = (cluster->node_count / 2) + 1;
    
    pthread_mutex_unlock(&cluster->mutex);
    
    // 向其他节点发送投票请求
    // 这里简单模拟，实际需要通过网络通信请求其他节点投票
    // ...
    
    // 如果获得足够的选票，成为领导者
    if (votes >= needed_votes) {
        return cluster_become_leader(cluster);
    }
    
    return 0;
}

// 投票给候选人
int cluster_vote_for_candidate(cluster_t *cluster, const char *candidate_id, uint64_t term) {
    if (!cluster || !candidate_id) return -1;
    
    pthread_mutex_lock(&cluster->mutex);
    
    // 如果候选人的任期小于当前任期，拒绝投票
    if (term < cluster->current_term) {
        pthread_mutex_unlock(&cluster->mutex);
        return -1;
    }
    
    // 如果候选人的任期大于当前任期，更新自己的任期并投票
    if (term > cluster->current_term) {
        cluster->current_term = term;
        cluster->nodes[cluster->self_node_idx].term = term;
        
        // 如果自己是领导者或候选人，变回跟随者
        if (cluster->nodes[cluster->self_node_idx].role != NODE_ROLE_FOLLOWER) {
            cluster->nodes[cluster->self_node_idx].role = NODE_ROLE_FOLLOWER;
        }
        
        // 查找候选人索引并投票
        for (int i = 0; i < cluster->node_count; i++) {
            if (strcmp(cluster->nodes[i].node_id, candidate_id) == 0) {
                // 这里简单返回投票结果，实际需要记录已投票的候选人
                pthread_mutex_unlock(&cluster->mutex);
                return 0;
            }
        }
    }
    
    pthread_mutex_unlock(&cluster->mutex);
    return -1; // 候选人不存在或任期不对
}

// 成为领导者
int cluster_become_leader(cluster_t *cluster) {
    if (!cluster) return -1;
    
    pthread_mutex_lock(&cluster->mutex);
    
    // 只有候选人可以成为领导者
    if (cluster->nodes[cluster->self_node_idx].role != NODE_ROLE_CANDIDATE) {
        pthread_mutex_unlock(&cluster->mutex);
        return -1;
    }
    
    // 成为领导者
    cluster->nodes[cluster->self_node_idx].role = NODE_ROLE_LEADER;
    cluster->current_leader = cluster->self_node_idx;
    
    pthread_mutex_unlock(&cluster->mutex);
    
    // 立即发送心跳以建立权威
    return cluster_send_heartbeat(cluster);
}

// 发送心跳
int cluster_send_heartbeat(cluster_t *cluster) {
    if (!cluster) return -1;
    
    pthread_mutex_lock(&cluster->mutex);
    
    // 只有领导者可以发送心跳
    if (cluster->nodes[cluster->self_node_idx].role != NODE_ROLE_LEADER) {
        pthread_mutex_unlock(&cluster->mutex);
        return -1;
    }
    
    // 更新心跳时间
    cluster->nodes[cluster->self_node_idx].last_heartbeat = time(NULL);
    
    pthread_mutex_unlock(&cluster->mutex);
    
    // 向其他节点发送心跳
    // 这里简单模拟，实际需要通过网络通信发送心跳
    // ...
    
    return 0;
}

// 处理收到的心跳
int cluster_process_heartbeat(cluster_t *cluster, const char *leader_id, uint64_t term) {
    if (!cluster || !leader_id) return -1;
    
    pthread_mutex_lock(&cluster->mutex);
    
    // 如果领导者的任期小于当前任期，拒绝心跳
    if (term < cluster->current_term) {
        pthread_mutex_unlock(&cluster->mutex);
        return -1;
    }
    
    // 如果领导者的任期大于当前任期，更新自己的任期
    if (term > cluster->current_term) {
        cluster->current_term = term;
        cluster->nodes[cluster->self_node_idx].term = term;
    }
    
    // 如果自己是候选人或领导者，变回跟随者
    if (cluster->nodes[cluster->self_node_idx].role != NODE_ROLE_FOLLOWER) {
        cluster->nodes[cluster->self_node_idx].role = NODE_ROLE_FOLLOWER;
    }
    
    // 查找领导者索引
    int leader_idx = -1;
    for (int i = 0; i < cluster->node_count; i++) {
        if (strcmp(cluster->nodes[i].node_id, leader_id) == 0) {
            leader_idx = i;
            break;
        }
    }
    
    if (leader_idx < 0) {
        pthread_mutex_unlock(&cluster->mutex);
        return -1; // 领导者不存在
    }
    
    // 更新领导者信息
    cluster->current_leader = leader_idx;
    cluster->nodes[leader_idx].last_heartbeat = time(NULL);
    
    pthread_mutex_unlock(&cluster->mutex);
    return 0;
}

// 同步分片数据
int cluster_sync_shard(cluster_t *cluster, int shard_id, int source_node, int target_node) {
    if (!cluster || shard_id < 0 || shard_id >= MAX_CLUSTER_SHARDS ||
        source_node < 0 || source_node >= cluster->node_count ||
        target_node < 0 || target_node >= cluster->node_count) {
        return -1;
    }
    
    // 这里简单模拟，实际需要通过网络通信同步数据
    // ...
    
    return 0;
}

// 请求转发到正确的节点
int cluster_forward_request(cluster_t *cluster, int shard_id, const char *key, const char *value, int op_type) {
    if (!cluster || shard_id < 0 || shard_id >= MAX_CLUSTER_SHARDS || !key) return -1;
    
    pthread_mutex_lock(&cluster->mutex);
    
    // 查找分片主节点
    int primary_node = -1;
    for (int i = 0; i < cluster->shard_count; i++) {
        if (cluster->shards[i].shard_id == shard_id) {
            primary_node = cluster->shards[i].primary_node;
            break;
        }
    }
    
    if (primary_node < 0) {
        pthread_mutex_unlock(&cluster->mutex);
        return -1; // 分片不存在
    }
    
    // 如果自己就是主节点，直接处理
    if (primary_node == cluster->self_node_idx) {
        pthread_mutex_unlock(&cluster->mutex);
        // 处理请求...
        return 0;
    }
    
    // 获取主节点信息
    char host[64];
    int port;
    strncpy(host, cluster->nodes[primary_node].host, sizeof(host) - 1);
    port = cluster->nodes[primary_node].port;
    
    pthread_mutex_unlock(&cluster->mutex);
    
    // 转发请求到主节点
    // 这里简单模拟，实际需要通过网络通信转发请求
    // ...
    
    return 0;
}

// 检查集群健康状态
int cluster_health_check(cluster_t *cluster) {
    if (!cluster) return -1;
    
    pthread_mutex_lock(&cluster->mutex);
    
    time_t now = time(NULL);
    int offline_count = 0;
    
    // 检查所有节点的心跳
    for (int i = 0; i < cluster->node_count; i++) {
        if (i != cluster->self_node_idx) {
            // 如果超过30秒没有心跳，标记为离线
            if (now - cluster->nodes[i].last_heartbeat > 30) {
                if (cluster->nodes[i].status != NODE_STATUS_OFFLINE) {
                    cluster->nodes[i].status = NODE_STATUS_OFFLINE;
                    offline_count++;
                }
            }
        }
    }
    
    pthread_mutex_unlock(&cluster->mutex);
    
    // 如果一半以上节点离线，集群不健康
    return (offline_count > cluster->node_count / 2) ? -1 : 0;
}

// 加载集群配置
int cluster_load_config(cluster_t *cluster, const char *config_file) {
    if (!cluster || !config_file) return -1;
    
    FILE *fp = fopen(config_file, "r");
    if (!fp) return -1;
    
    // 这里简单模拟，实际需要解析配置文件
    // ...
    
    fclose(fp);
    return 0;
}

// 保存集群配置
int cluster_save_config(cluster_t *cluster, const char *config_file) {
    if (!cluster || !config_file) return -1;
    
    FILE *fp = fopen(config_file, "w");
    if (!fp) return -1;
    
    // 这里简单模拟，实际需要序列化集群配置
    // ...
    
    fclose(fp);
    return 0;
} 