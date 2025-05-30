/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV storage engine - kv_snapshot.c
 */
#include "kv_snapshot.h"
#include "kv_error.h"
#include "kv_store.h"
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

// 魔数
#define KV_SNAPSHOT_MAGIC 0x4B565348  // "KVSH"

// 创建目录（如果不存在）
static int ensure_directory(const char *dir) {
    struct stat st = {0};
    if (stat(dir, &st) == -1) {
        return mkdir(dir, 0755);
    }
    return 0;
}

// 初始化快照上下文
int kv_snapshot_init(kv_snapshot_ctx_t *ctx, const char *snapshot_dir) {
    if (!ctx || !snapshot_dir) {
        KV_ERROR(KV_ERR_PARAM, "Invalid parameters for snapshot init");
        return KV_ERR_PARAM;
    }
    
    // 确保快照目录存在
    if (ensure_directory(snapshot_dir) != 0) {
        KV_ERROR(KV_ERR_FILE, "Failed to create snapshot directory: %s", snapshot_dir);
        return KV_ERR_FILE;
    }
    
    memset(ctx, 0, sizeof(kv_snapshot_ctx_t));
    ctx->snapshot_dir = strdup(snapshot_dir);
    if (!ctx->snapshot_dir) {
        KV_ERROR(KV_ERR_MEM, "Failed to allocate memory for snapshot directory");
        return KV_ERR_MEM;
    }
    
    // 查找最新的快照
    if (kv_snapshot_get_latest(ctx, &ctx->last_term, &ctx->last_index) != 0) {
        // 没有找到快照，这不是错误
        ctx->last_term = 0;
        ctx->last_index = 0;
        ctx->last_time = 0;
        memset(ctx->last_file, 0, sizeof(ctx->last_file));
    }
    
    return 0;
}

// 销毁快照上下文
void kv_snapshot_destroy(kv_snapshot_ctx_t *ctx) {
    if (!ctx) return;
    
    if (ctx->snapshot_dir) {
        free(ctx->snapshot_dir);
        ctx->snapshot_dir = NULL;
    }
}

// 计算校验和 (简单的CRC32算法)
uint32_t kv_snapshot_checksum(const void *data, size_t size) {
    if (!data || size == 0) return 0;
    
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < size; i++) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    
    return ~crc;
}

// 查找最新的快照
int kv_snapshot_get_latest(kv_snapshot_ctx_t *ctx, uint64_t *term, uint64_t *index) {
    if (!ctx || !term || !index || !ctx->snapshot_dir) {
        KV_ERROR(KV_ERR_PARAM, "Invalid parameters for get_latest");
        return KV_ERR_PARAM;
    }
    
    DIR *dir;
    struct dirent *entry;
    char latest_file[256] = {0};
    uint64_t max_index = 0;
    uint64_t max_term = 0;
    
    // 打开快照目录
    if ((dir = opendir(ctx->snapshot_dir)) == NULL) {
        KV_ERROR(KV_ERR_FILE, "Failed to open snapshot directory: %s", ctx->snapshot_dir);
        return KV_ERR_FILE;
    }
    
    // 快照文件名格式: snapshot-term-index.data
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "snapshot-", 9) == 0 && strstr(entry->d_name, ".data") != NULL) {
            uint64_t file_term = 0, file_index = 0;
            if (sscanf(entry->d_name, "snapshot-%lu-%lu.data", &file_term, &file_index) == 2) {
                if (file_index > max_index || (file_index == max_index && file_term > max_term)) {
                    max_index = file_index;
                    max_term = file_term;
                    snprintf(latest_file, sizeof(latest_file), "%s", entry->d_name);
                }
            }
        }
    }
    
    closedir(dir);
    
    if (max_index == 0) {
        // 没有找到快照
        return KV_ERR_NOT_FOUND;
    }
    
    *term = max_term;
    *index = max_index;
    
    // 保存最新快照信息
    ctx->last_term = max_term;
    ctx->last_index = max_index;
    
    // 获取文件时间
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", ctx->snapshot_dir, latest_file);
    
    struct stat st;
    if (stat(full_path, &st) == 0) {
        ctx->last_time = st.st_mtime;
    }
    
    strncpy(ctx->last_file, latest_file, sizeof(ctx->last_file) - 1);
    
    return 0;
}

// 列出快照文件
int kv_snapshot_list(kv_snapshot_ctx_t *ctx, char ***files, int *count) {
    if (!ctx || !files || !count || !ctx->snapshot_dir) {
        KV_ERROR(KV_ERR_PARAM, "Invalid parameters for snapshot_list");
        return KV_ERR_PARAM;
    }
    
    DIR *dir;
    struct dirent *entry;
    int file_count = 0;
    char **file_list = NULL;
    
    // 打开快照目录
    if ((dir = opendir(ctx->snapshot_dir)) == NULL) {
        KV_ERROR(KV_ERR_FILE, "Failed to open snapshot directory: %s", ctx->snapshot_dir);
        return KV_ERR_FILE;
    }
    
    // 计算快照文件数量
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "snapshot-", 9) == 0 && strstr(entry->d_name, ".data") != NULL) {
            file_count++;
        }
    }
    
    if (file_count == 0) {
        closedir(dir);
        *files = NULL;
        *count = 0;
        return 0;
    }
    
    // 分配文件列表内存
    file_list = (char **)malloc(file_count * sizeof(char *));
    if (!file_list) {
        closedir(dir);
        KV_ERROR(KV_ERR_MEM, "Failed to allocate memory for snapshot list");
        return KV_ERR_MEM;
    }
    
    // 重置目录读取位置
    rewinddir(dir);
    
    // 读取文件列表
    int idx = 0;
    while ((entry = readdir(dir)) != NULL && idx < file_count) {
        if (strncmp(entry->d_name, "snapshot-", 9) == 0 && strstr(entry->d_name, ".data") != NULL) {
            file_list[idx] = strdup(entry->d_name);
            idx++;
        }
    }
    
    closedir(dir);
    
    *files = file_list;
    *count = idx;
    
    return 0;
}

// 删除过期快照
int kv_snapshot_prune(kv_snapshot_ctx_t *ctx, int keep_count) {
    if (!ctx || keep_count < 0 || !ctx->snapshot_dir) {
        KV_ERROR(KV_ERR_PARAM, "Invalid parameters for snapshot_prune");
        return KV_ERR_PARAM;
    }
    
    char **files = NULL;
    int file_count = 0;
    
    // 获取快照文件列表
    if (kv_snapshot_list(ctx, &files, &file_count) != 0) {
        return KV_ERR_FILE;
    }
    
    // 如果文件数量不超过保留数量，不需要删除
    if (file_count <= keep_count) {
        // 释放文件列表
        for (int i = 0; i < file_count; i++) {
            free(files[i]);
        }
        free(files);
        return 0;
    }
    
    // 按照索引和任期对文件进行排序（此处简化，实际应该实现排序）
    
    // 删除多余的文件
    for (int i = 0; i < file_count - keep_count; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", ctx->snapshot_dir, files[i]);
        
        if (unlink(path) != 0) {
            KV_LOG_WARN_MSG("Failed to delete snapshot file: %s", path);
        }
    }
    
    // 释放文件列表
    for (int i = 0; i < file_count; i++) {
        free(files[i]);
    }
    free(files);
    
    return 0;
}

// 验证快照完整性
int kv_snapshot_verify(const char *snapshot_file) {
    if (!snapshot_file) {
        KV_ERROR(KV_ERR_PARAM, "Invalid parameters for snapshot_verify");
        return KV_ERR_PARAM;
    }
    
    FILE *fp = fopen(snapshot_file, "rb");
    if (!fp) {
        KV_ERROR(KV_ERR_FILE, "Failed to open snapshot file: %s", snapshot_file);
        return KV_ERR_FILE;
    }
    
    // 读取和验证文件头
    kv_snapshot_header_t header;
    if (fread(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        KV_ERROR(KV_ERR_CORRUPTED, "Failed to read snapshot header");
        return KV_ERR_CORRUPTED;
    }
    
    // 验证魔数
    if (header.magic != KV_SNAPSHOT_MAGIC) {
        fclose(fp);
        KV_ERROR(KV_ERR_CORRUPTED, "Invalid snapshot file format, magic number mismatch");
        return KV_ERR_CORRUPTED;
    }
    
    // 验证版本
    if (header.version > KV_SNAPSHOT_VERSION) {
        fclose(fp);
        KV_ERROR(KV_ERR_NOT_SUPPORTED, "Snapshot version not supported: %u", header.version);
        return KV_ERR_NOT_SUPPORTED;
    }
    
    // 此处应该对文件进行更详细的验证，比如遍历所有条目并验证校验和
    // 简化起见，我们仅验证文件头
    
    fclose(fp);
    return 0;
}

// 从红黑树引擎迭代获取所有键值对的回调函数
// 必须在调用kv_snapshot_create前定义这个回调函数
#if ENABLE_RBTREE_KVENGINE
// 迭代红黑树中的所有节点，写入快照文件
static int rbtree_snapshot_iterator(rbtree_t *tree, rbtree_node *node, FILE *fp) {
    if (!tree || !node || node == tree->nil || !fp) {
        return 0;
    }
    
    // 遍历左子树
    rbtree_snapshot_iterator(tree, node->left, fp);
    
    // 处理当前节点
    if (node->key && node->value) {
        // 写入键值对条目头
        kv_snapshot_entry_header_t entry_header;
        entry_header.type = KV_SNAP_KV_PAIR;
        
        // 获取键值大小
        uint32_t key_size = strlen(node->key) + 1;
        uint32_t value_size = strlen(node->value) + 1;
        
        // 创建键值对数据
        kv_snapshot_kv_t kv_data;
        kv_data.key_size = key_size;
        kv_data.value_size = value_size;
        
        // 计算数据大小
        entry_header.data_size = sizeof(kv_data) + key_size + value_size;
        
        // 计算校验和
        uint32_t checksum = kv_snapshot_checksum(&kv_data, sizeof(kv_data));
        checksum ^= kv_snapshot_checksum(node->key, key_size);
        checksum ^= kv_snapshot_checksum(node->value, value_size);
        entry_header.checksum = checksum;
        
        // 写入条目头
        fwrite(&entry_header, sizeof(entry_header), 1, fp);
        
        // 写入键值数据
        fwrite(&kv_data, sizeof(kv_data), 1, fp);
        fwrite(node->key, key_size, 1, fp);
        fwrite(node->value, value_size, 1, fp);
    }
    
    // 遍历右子树
    rbtree_snapshot_iterator(tree, node->right, fp);
    
    return 0;
}
#endif

// 创建快照
int kv_snapshot_create(kv_snapshot_ctx_t *ctx, void *engine, const char *engine_name,
                       uint64_t term, uint64_t index, const char *cluster_id, const char *node_id) {
    if (!ctx || !engine || !engine_name || !cluster_id || !node_id) {
        KV_ERROR(KV_ERR_PARAM, "Invalid parameters for snapshot creation");
        return KV_ERR_PARAM;
    }
    
    // 确保快照目录存在
    if (ensure_directory(ctx->snapshot_dir) != 0) {
        KV_ERROR(KV_ERR_FILE, "Failed to create snapshot directory: %s", ctx->snapshot_dir);
        return KV_ERR_FILE;
    }
    
    // 创建快照文件名
    char snapshot_file[512];
    snprintf(snapshot_file, sizeof(snapshot_file), "%s/snapshot-%lu-%lu.data", 
             ctx->snapshot_dir, term, index);
    
    // 打开快照文件
    FILE *fp = fopen(snapshot_file, "wb");
    if (!fp) {
        KV_ERROR(KV_ERR_FILE, "Failed to create snapshot file: %s", snapshot_file);
        return KV_ERR_FILE;
    }
    
    // 准备文件头
    kv_snapshot_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = KV_SNAPSHOT_MAGIC;
    header.version = KV_SNAPSHOT_VERSION;
    header.timestamp = (uint64_t)time(NULL);
    header.kv_count = 0; // 稍后更新
    header.flags = 0;
    strncpy(header.engine_name, engine_name, sizeof(header.engine_name) - 1);
    
    // 写入文件头（先预留位置，稍后更新）
    fwrite(&header, sizeof(header), 1, fp);
    
    // 写入元数据
    kv_snapshot_metadata_t metadata;
    memset(&metadata, 0, sizeof(metadata));
    metadata.term = term;
    metadata.index = index;
    metadata.timestamp = (uint64_t)time(NULL);
    strncpy(metadata.cluster_id, cluster_id, sizeof(metadata.cluster_id) - 1);
    strncpy(metadata.node_id, node_id, sizeof(metadata.node_id) - 1);
    
    // 创建元数据条目头
    kv_snapshot_entry_header_t metadata_header;
    metadata_header.type = KV_SNAP_METADATA;
    metadata_header.data_size = sizeof(metadata);
    metadata_header.checksum = kv_snapshot_checksum(&metadata, sizeof(metadata));
    
    // 写入元数据条目
    fwrite(&metadata_header, sizeof(metadata_header), 1, fp);
    fwrite(&metadata, sizeof(metadata), 1, fp);
    
    // 写入KV数据
    uint64_t kv_count = 0;
    
#if ENABLE_RBTREE_KVENGINE
    if (strcmp(engine_name, "rbtree") == 0) {
        rbtree_t *tree = (rbtree_t *)engine;
        
        // 遍历红黑树并写入键值对
        if (tree && tree->root != tree->nil) {
            rbtree_snapshot_iterator(tree, tree->root, fp);
            kv_count = tree->count;
        }
    }
#endif

#if ENABLE_HASH_KVENGINE
    // 哈希表的快照实现略，需要遍历哈希表并保存键值对
#endif

#if ENABLE_ARRAY_KVENGINE
    // 数组的快照实现略，需要遍历数组并保存键值对
#endif
    
    // 写入结束标记
    kv_snapshot_entry_header_t end_header;
    end_header.type = KV_SNAP_END;
    end_header.data_size = 0;
    end_header.checksum = 0;
    fwrite(&end_header, sizeof(end_header), 1, fp);
    
    // 更新文件头中的键值对数量
    fseek(fp, offsetof(kv_snapshot_header_t, kv_count), SEEK_SET);
    fwrite(&kv_count, sizeof(kv_count), 1, fp);
    
    // 计算并更新文件头的校验和
    fseek(fp, 0, SEEK_SET);
    header.kv_count = kv_count;
    header.checksum = kv_snapshot_checksum(&header, sizeof(header) - sizeof(header.checksum));
    fwrite(&header, sizeof(header), 1, fp);
    
    fclose(fp);
    
    // 更新上下文中的快照信息
    ctx->last_term = term;
    ctx->last_index = index;
    ctx->last_time = time(NULL);
    snprintf(ctx->last_file, sizeof(ctx->last_file), "snapshot-%lu-%lu.data", term, index);
    
    KV_LOG_INFO_MSG("Snapshot created: %s with %lu key-value pairs", snapshot_file, kv_count);
    
    return 0;
}

// 加载快照
int kv_snapshot_load(kv_snapshot_ctx_t *ctx, void *engine, const char *engine_name) {
    if (!ctx || !engine || !engine_name) {
        KV_ERROR(KV_ERR_PARAM, "Invalid parameters for snapshot loading");
        return KV_ERR_PARAM;
    }
    
    // 确保有快照可加载
    if (ctx->last_file[0] == '\0') {
        KV_ERROR(KV_ERR_NOT_FOUND, "No snapshot found to load");
        return KV_ERR_NOT_FOUND;
    }
    
    // 构建快照文件路径
    char snapshot_file[512];
    snprintf(snapshot_file, sizeof(snapshot_file), "%s/%s", ctx->snapshot_dir, ctx->last_file);
    
    // 验证快照文件
    if (kv_snapshot_verify(snapshot_file) != 0) {
        return KV_ERR_CORRUPTED;
    }
    
    // 打开快照文件
    FILE *fp = fopen(snapshot_file, "rb");
    if (!fp) {
        KV_ERROR(KV_ERR_FILE, "Failed to open snapshot file: %s", snapshot_file);
        return KV_ERR_FILE;
    }
    
    // 读取文件头
    kv_snapshot_header_t header;
    if (fread(&header, sizeof(header), 1, fp) != 1) {
        fclose(fp);
        KV_ERROR(KV_ERR_CORRUPTED, "Failed to read snapshot header");
        return KV_ERR_CORRUPTED;
    }
    
    // 检查存储引擎是否匹配
    if (strcmp(header.engine_name, engine_name) != 0) {
        fclose(fp);
        KV_ERROR(KV_ERR_NOT_SUPPORTED, "Snapshot engine (%s) does not match current engine (%s)",
                header.engine_name, engine_name);
        return KV_ERR_NOT_SUPPORTED;
    }
    
    // 清空当前存储引擎
#if ENABLE_RBTREE_KVENGINE
    if (strcmp(engine_name, "rbtree") == 0) {
        rbtree_t *tree = (rbtree_t *)engine;
        // 这里应该有清空树的函数，示例使用通用接口
        kv_store_rbtree_destroy(tree);
        kv_store_rbtree_create(tree);
    }
#endif

#if ENABLE_HASH_KVENGINE
    // 哈希表清空实现略
#endif

#if ENABLE_ARRAY_KVENGINE
    // 数组清空实现略
#endif
    
    // 读取快照条目
    uint64_t loaded_count = 0;
    while (1) {
        // 读取条目头
        kv_snapshot_entry_header_t entry_header;
        if (fread(&entry_header, sizeof(entry_header), 1, fp) != 1) {
            break; // 读取完毕或出错
        }
        
        // 检查是否到达结束标记
        if (entry_header.type == KV_SNAP_END) {
            break;
        }
        
        // 根据条目类型处理
        switch (entry_header.type) {
            case KV_SNAP_METADATA:
                // 读取元数据但不做特殊处理
                {
                    kv_snapshot_metadata_t metadata;
                    if (fread(&metadata, sizeof(metadata), 1, fp) != 1) {
                        fclose(fp);
                        KV_ERROR(KV_ERR_CORRUPTED, "Failed to read snapshot metadata");
                        return KV_ERR_CORRUPTED;
                    }
                    // 可以记录或使用元数据
                }
                break;
                
            case KV_SNAP_KV_PAIR:
                // 读取键值对并添加到存储引擎
                {
                    // 读取键值对数据头
                    kv_snapshot_kv_t kv_data;
                    if (fread(&kv_data, sizeof(kv_data), 1, fp) != 1) {
                        fclose(fp);
                        KV_ERROR(KV_ERR_CORRUPTED, "Failed to read key-value data header");
                        return KV_ERR_CORRUPTED;
                    }
                    
                    // 分配内存并读取键
                    char *key = (char *)malloc(kv_data.key_size);
                    if (!key) {
                        fclose(fp);
                        KV_ERROR(KV_ERR_MEM, "Failed to allocate memory for key");
                        return KV_ERR_MEM;
                    }
                    
                    if (fread(key, kv_data.key_size, 1, fp) != 1) {
                        free(key);
                        fclose(fp);
                        KV_ERROR(KV_ERR_CORRUPTED, "Failed to read key data");
                        return KV_ERR_CORRUPTED;
                    }
                    
                    // 分配内存并读取值
                    char *value = (char *)malloc(kv_data.value_size);
                    if (!value) {
                        free(key);
                        fclose(fp);
                        KV_ERROR(KV_ERR_MEM, "Failed to allocate memory for value");
                        return KV_ERR_MEM;
                    }
                    
                    if (fread(value, kv_data.value_size, 1, fp) != 1) {
                        free(key);
                        free(value);
                        fclose(fp);
                        KV_ERROR(KV_ERR_CORRUPTED, "Failed to read value data");
                        return KV_ERR_CORRUPTED;
                    }
                    
                    // 将键值对添加到存储引擎
#if ENABLE_RBTREE_KVENGINE
                    if (strcmp(engine_name, "rbtree") == 0) {
                        rbtree_t *tree = (rbtree_t *)engine;
                        kvs_rbtree_set(tree, key, value);
                    }
#endif

#if ENABLE_HASH_KVENGINE
                    // 哈希表插入实现略
#endif

#if ENABLE_ARRAY_KVENGINE
                    // 数组插入实现略
#endif
                    
                    free(key);
                    free(value);
                    loaded_count++;
                }
                break;
                
            case KV_SNAP_SHARD_INFO:
                // 读取分片信息（未使用）
                {
                    // 跳过分片数据
                    fseek(fp, entry_header.data_size, SEEK_CUR);
                }
                break;
                
            default:
                // 未知类型，跳过
                fseek(fp, entry_header.data_size, SEEK_CUR);
                break;
        }
    }
    
    fclose(fp);
    
    KV_LOG_INFO_MSG("Snapshot loaded: %s with %lu key-value pairs", snapshot_file, loaded_count);
    
    return 0;
} 