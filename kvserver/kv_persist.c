#include "kv_persist.h"
#include "kv_store.h"
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

// 创建目录（如果不存在）
static int ensure_directory(const char *dir) {
    struct stat st = {0};
    if (stat(dir, &st) == -1) {
        return mkdir(dir, 0755);
    }
    return 0;
}

// WAL日志文件名格式
static void get_wal_filename(char *buf, size_t buf_size, const char *dir, uint64_t seq) {
    snprintf(buf, buf_size, "%s/wal-%lu.log", dir, seq);
}

// 初始化WAL
int wal_init(wal_t *wal, const char *log_dir, int sync_write) {
    if (!wal || !log_dir) return -1;
    
    // 确保日志目录存在
    if (ensure_directory(log_dir) != 0) {
        return -1;
    }
    
    wal->log_dir = strdup(log_dir);
    wal->current_file = NULL;
    wal->fd = -1;
    wal->next_seq = 1;
    wal->sync_write = sync_write;
    
    if (pthread_mutex_init(&wal->mutex, NULL) != 0) {
        free(wal->log_dir);
        return -1;
    }
    
    // 创建新的WAL文件
    char filename[256];
    get_wal_filename(filename, sizeof(filename), wal->log_dir, wal->next_seq);
    wal->current_file = strdup(filename);
    
    wal->fd = open(wal->current_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (wal->fd < 0) {
        pthread_mutex_destroy(&wal->mutex);
        free(wal->log_dir);
        free(wal->current_file);
        return -1;
    }
    
    return 0;
}

// 销毁WAL
void wal_destroy(wal_t *wal) {
    if (!wal) return;
    
    pthread_mutex_lock(&wal->mutex);
    
    if (wal->fd >= 0) {
        close(wal->fd);
        wal->fd = -1;
    }
    
    if (wal->log_dir) {
        free(wal->log_dir);
        wal->log_dir = NULL;
    }
    
    if (wal->current_file) {
        free(wal->current_file);
        wal->current_file = NULL;
    }
    
    pthread_mutex_unlock(&wal->mutex);
    pthread_mutex_destroy(&wal->mutex);
}

// 添加WAL条目
int wal_append(wal_t *wal, uint8_t op_type, const char *key, const char *value) {
    if (!wal || !key || (op_type == WAL_OP_SET && !value)) return -1;
    
    pthread_mutex_lock(&wal->mutex);
    
    // 确保文件是打开的
    if (wal->fd < 0) {
        pthread_mutex_unlock(&wal->mutex);
        return -1;
    }
    
    uint64_t seq = wal->next_seq++;
    uint32_t key_size = strlen(key) + 1;
    uint32_t value_size = (value) ? strlen(value) + 1 : 0;
    
    // 写入序列号
    if (write(wal->fd, &seq, sizeof(seq)) != sizeof(seq)) {
        pthread_mutex_unlock(&wal->mutex);
        return -1;
    }
    
    // 写入操作类型
    if (write(wal->fd, &op_type, sizeof(op_type)) != sizeof(op_type)) {
        pthread_mutex_unlock(&wal->mutex);
        return -1;
    }
    
    // 写入键大小和键内容
    if (write(wal->fd, &key_size, sizeof(key_size)) != sizeof(key_size) ||
        write(wal->fd, key, key_size) != key_size) {
        pthread_mutex_unlock(&wal->mutex);
        return -1;
    }
    
    // 写入值大小和值内容（如果有）
    if (write(wal->fd, &value_size, sizeof(value_size)) != sizeof(value_size) ||
        (value_size > 0 && write(wal->fd, value, value_size) != value_size)) {
        pthread_mutex_unlock(&wal->mutex);
        return -1;
    }
    
    // 同步写入磁盘（如果需要）
    if (wal->sync_write) {
        fsync(wal->fd);
    }
    
    pthread_mutex_unlock(&wal->mutex);
    return 0;
}

// 恢复WAL
int wal_recover(wal_t *wal, void *engine, 
               int (*apply_log)(void *engine, uint8_t op_type, const char *key, const char *value)) {
    if (!wal || !engine || !apply_log) return -1;
    
    DIR *dir;
    struct dirent *entry;
    char path[PATH_MAX];
    
    // 打开日志目录
    if ((dir = opendir(wal->log_dir)) == NULL) {
        return -1;
    }
    
    // 扫描所有WAL文件
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "wal-", 4) == 0 && strstr(entry->d_name, ".log") != NULL) {
            snprintf(path, sizeof(path), "%s/%s", wal->log_dir, entry->d_name);
            
            int fd = open(path, O_RDONLY);
            if (fd < 0) continue;
            
            while (1) {
                uint64_t seq;
                uint8_t op_type;
                uint32_t key_size, value_size;
                char *key = NULL, *value = NULL;
                
                // 读取序列号
                if (read(fd, &seq, sizeof(seq)) != sizeof(seq)) {
                    break;
                }
                
                // 更新下一个序列号
                if (seq >= wal->next_seq) {
                    wal->next_seq = seq + 1;
                }
                
                // 读取操作类型
                if (read(fd, &op_type, sizeof(op_type)) != sizeof(op_type)) {
                    break;
                }
                
                // 读取键大小和键内容
                if (read(fd, &key_size, sizeof(key_size)) != sizeof(key_size)) {
                    break;
                }
                
                key = (char *)malloc(key_size);
                if (!key || read(fd, key, key_size) != key_size) {
                    free(key);
                    break;
                }
                
                // 读取值大小和值内容
                if (read(fd, &value_size, sizeof(value_size)) != sizeof(value_size)) {
                    free(key);
                    break;
                }
                
                if (value_size > 0) {
                    value = (char *)malloc(value_size);
                    if (!value || read(fd, value, value_size) != value_size) {
                        free(key);
                        free(value);
                        break;
                    }
                }
                
                // 应用日志
                apply_log(engine, op_type, key, value);
                
                free(key);
                if (value) free(value);
            }
            
            close(fd);
        }
    }
    
    closedir(dir);
    return 0;
}

// 初始化快照管理
int snapshot_init(snapshot_t *snap, const char *snap_dir) {
    if (!snap || !snap_dir) return -1;
    
    // 确保快照目录存在
    if (ensure_directory(snap_dir) != 0) {
        return -1;
    }
    
    snap->snap_dir = strdup(snap_dir);
    snap->snap_seq = 0;
    
    return 0;
}

// 销毁快照管理
void snapshot_destroy(snapshot_t *snap) {
    if (!snap) return;
    
    if (snap->snap_dir) {
        free(snap->snap_dir);
        snap->snap_dir = NULL;
    }
}

// 创建快照
int snapshot_create(snapshot_t *snap, void *engine, 
                   int (*save_data)(void *engine, FILE *fp)) {
    if (!snap || !engine || !save_data) return -1;
    
    char filename[256];
    snap->snap_seq++;
    snprintf(filename, sizeof(filename), "%s/snapshot-%lu.data", snap->snap_dir, snap->snap_seq);
    
    FILE *fp = fopen(filename, "wb");
    if (!fp) return -1;
    
    int ret = save_data(engine, fp);
    
    fclose(fp);
    return ret;
}

// 加载最新快照
int snapshot_load(snapshot_t *snap, void *engine,
                 int (*load_data)(void *engine, FILE *fp)) {
    if (!snap || !engine || !load_data) return -1;
    
    DIR *dir;
    struct dirent *entry;
    char latest_snapshot[PATH_MAX] = {0};
    uint64_t max_seq = 0;
    
    // 打开快照目录
    if ((dir = opendir(snap->snap_dir)) == NULL) {
        return -1;
    }
    
    // 查找最新的快照文件
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "snapshot-", 9) == 0 && strstr(entry->d_name, ".data") != NULL) {
            uint64_t seq = 0;
            sscanf(entry->d_name, "snapshot-%lu.data", &seq);
            
            if (seq > max_seq) {
                max_seq = seq;
                snprintf(latest_snapshot, sizeof(latest_snapshot), "%s/%s", snap->snap_dir, entry->d_name);
            }
        }
    }
    
    closedir(dir);
    
    if (max_seq == 0) {
        // 没有找到快照
        return 0;
    }
    
    snap->snap_seq = max_seq;
    
    FILE *fp = fopen(latest_snapshot, "rb");
    if (!fp) return -1;
    
    int ret = load_data(engine, fp);
    
    fclose(fp);
    return ret;
}

// 初始化持久化状态
int persist_init(persist_state_t *ps, const char *data_dir, int sync_write) {
    if (!ps || !data_dir) return -1;
    
    // 确保数据目录存在
    if (ensure_directory(data_dir) != 0) {
        return -1;
    }
    
    ps->data_dir = strdup(data_dir);
    ps->persistence_enabled = 1;
    
    char wal_dir[PATH_MAX], snap_dir[PATH_MAX];
    snprintf(wal_dir, sizeof(wal_dir), "%s/wal", data_dir);
    snprintf(snap_dir, sizeof(snap_dir), "%s/snapshot", data_dir);
    
    // 初始化WAL和快照
    if (wal_init(&ps->wal, wal_dir, sync_write) != 0 ||
        snapshot_init(&ps->snap, snap_dir) != 0) {
        free(ps->data_dir);
        ps->persistence_enabled = 0;
        return -1;
    }
    
    return 0;
}

// 销毁持久化状态
void persist_destroy(persist_state_t *ps) {
    if (!ps) return;
    
    wal_destroy(&ps->wal);
    snapshot_destroy(&ps->snap);
    
    if (ps->data_dir) {
        free(ps->data_dir);
        ps->data_dir = NULL;
    }
    
    ps->persistence_enabled = 0;
} 