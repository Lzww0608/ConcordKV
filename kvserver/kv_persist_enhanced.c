#define _GNU_SOURCE  // 为了使用strdup
#include "kv_persist.h"
#include "kv_store.h"
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <limits.h>

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

// 获取当前时间戳
static time_t get_current_time() {
    return time(NULL);
}

// 添加WAL文件到链表
static int add_wal_file(wal_t *wal, const char *filename, uint64_t start_seq, uint64_t end_seq) {
    wal_file_t *file = (wal_file_t *)malloc(sizeof(wal_file_t));
    if (!file) return -1;
    
    file->filename = strdup(filename);
    file->start_seq = start_seq;
    file->end_seq = end_seq;
    
    struct stat st;
    if (stat(filename, &st) == 0) {
        file->file_size = st.st_size;
    } else {
        file->file_size = 0;
    }
    
    file->next = wal->file_list;
    wal->file_list = file;
    
    return 0;
}

// 清理WAL文件链表
static void cleanup_wal_file_list(wal_file_t *head) {
    while (head) {
        wal_file_t *next = head->next;
        free(head->filename);
        free(head);
        head = next;
    }
}

// 日志压缩线程函数
static void* compaction_thread(void *arg) {
    wal_t *wal = (wal_t *)arg;
    
    while (wal->compact_running) {
        sleep(10); // 每10秒检查一次
        
        pthread_mutex_lock(&wal->mutex);
        
        // 检查是否需要压缩
        uint64_t total_entries = wal->next_seq - wal->compact_seq;
        if (total_entries > WAL_COMPACT_THRESHOLD) {
            // 触发压缩
            printf("[COMPACTION] Starting log compaction, total entries: %lu\n", total_entries);
            // 这里需要调用具体的压缩逻辑
        }
        
        pthread_mutex_unlock(&wal->mutex);
    }
    
    return NULL;
}

// 增量同步线程函数
static void* incremental_sync_thread(void *arg) {
    wal_t *wal = (wal_t *)arg;
    
    while (wal->incremental_running) {
        sleep(INCREMENTAL_SYNC_INTERVAL);
        
        pthread_mutex_lock(&wal->mutex);
        
        time_t current_time = get_current_time();
        if (current_time - wal->last_sync_time >= INCREMENTAL_SYNC_INTERVAL) {
            // 执行增量同步
            if (wal->fd >= 0) {
                fsync(wal->fd);
                wal->last_sync_time = current_time;
                printf("[INCREMENTAL] Incremental sync completed at seq: %lu\n", wal->next_seq);
            }
        }
        
        pthread_mutex_unlock(&wal->mutex);
    }
    
    return NULL;
}

// 初始化增强版WAL
int wal_init(wal_t *wal, const char *log_dir, int sync_write) {
    if (!wal || !log_dir) return -1;
    
    // 确保日志目录存在
    if (ensure_directory(log_dir) != 0) {
        return -1;
    }
    
    memset(wal, 0, sizeof(wal_t));
    wal->log_dir = strdup(log_dir);
    wal->current_file = NULL;
    wal->fd = -1;
    wal->next_seq = 1;
    wal->sync_write = sync_write;
    wal->compact_seq = 0;
    wal->last_sync_seq = 0;
    wal->last_sync_time = get_current_time();
    wal->file_list = NULL;
    wal->compact_running = 0;
    wal->incremental_running = 0;
    
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
    
    // 启动压缩线程
    wal->compact_running = 1;
    if (pthread_create(&wal->compact_thread, NULL, compaction_thread, wal) != 0) {
        printf("[WARNING] Failed to start compaction thread\n");
        wal->compact_running = 0;
    }
    
    // 启动增量同步线程
    wal->incremental_running = 1;
    if (pthread_create(&wal->incremental_thread, NULL, incremental_sync_thread, wal) != 0) {
        printf("[WARNING] Failed to start incremental sync thread\n");
        wal->incremental_running = 0;
    }
    
    return 0;
}

// 销毁增强版WAL
void wal_destroy(wal_t *wal) {
    if (!wal) return;
    
    // 停止后台线程
    int was_compact_running = wal->compact_running;
    int was_incremental_running = wal->incremental_running;
    
    wal->compact_running = 0;
    wal->incremental_running = 0;
    
    // 等待线程结束
    if (was_compact_running) {
        pthread_join(wal->compact_thread, NULL);
    }
    
    if (was_incremental_running) {
        pthread_join(wal->incremental_thread, NULL);
    }
    
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
    
    cleanup_wal_file_list(wal->file_list);
    wal->file_list = NULL;
    
    pthread_mutex_unlock(&wal->mutex);
    pthread_mutex_destroy(&wal->mutex);
}

// 检查是否需要轮转日志文件
static int check_log_rotation(wal_t *wal) {
    if (wal->fd < 0) return 0;
    
    struct stat st;
    if (fstat(wal->fd, &st) != 0) return 0;
    
    if (st.st_size >= WAL_MAX_FILE_SIZE) {
        // 需要轮转日志文件
        close(wal->fd);
        
        // 添加当前文件到文件列表
        add_wal_file(wal, wal->current_file, wal->last_sync_seq, wal->next_seq - 1);
        
        // 创建新的日志文件
        char new_filename[256];
        get_wal_filename(new_filename, sizeof(new_filename), wal->log_dir, wal->next_seq);
        
        free(wal->current_file);
        wal->current_file = strdup(new_filename);
        
        wal->fd = open(wal->current_file, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (wal->fd < 0) {
            return -1;
        }
        
        wal->last_sync_seq = wal->next_seq;
        printf("[LOG_ROTATION] Rotated to new log file: %s\n", new_filename);
    }
    
    return 0;
}

// 增强版WAL添加条目
int wal_append(wal_t *wal, uint8_t op_type, const char *key, const char *value) {
    if (!wal || !key || (op_type == WAL_OP_SET && !value)) return -1;
    
    pthread_mutex_lock(&wal->mutex);
    
    // 检查日志轮转
    if (check_log_rotation(wal) != 0) {
        pthread_mutex_unlock(&wal->mutex);
        return -1;
    }
    
    // 确保文件是打开的
    if (wal->fd < 0) {
        pthread_mutex_unlock(&wal->mutex);
        return -1;
    }
    
    uint64_t seq = wal->next_seq++;
    uint32_t key_size = strlen(key) + 1;
    uint32_t value_size = (value) ? strlen(value) + 1 : 0;
    time_t timestamp = get_current_time();
    
    // 写入序列号
    if (write(wal->fd, &seq, sizeof(seq)) != sizeof(seq)) {
        pthread_mutex_unlock(&wal->mutex);
        return -1;
    }
    
    // 写入时间戳
    if (write(wal->fd, &timestamp, sizeof(timestamp)) != sizeof(timestamp)) {
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

// 日志压缩实现
int wal_compact_logs(wal_t *wal, void *engine,
                    int (*get_all_keys)(void *engine, char ***keys, int *count),
                    int (*get_value)(void *engine, const char *key, char **value)) {
    if (!wal || !engine || !get_all_keys || !get_value) return -1;
    
    pthread_mutex_lock(&wal->mutex);
    
    char **keys = NULL;
    int key_count = 0;
    
    // 获取所有当前有效的键
    if (get_all_keys(engine, &keys, &key_count) != 0) {
        pthread_mutex_unlock(&wal->mutex);
        return -1;
    }
    
    // 创建压缩后的日志文件
    char compact_filename[256];
    snprintf(compact_filename, sizeof(compact_filename), "%s/wal-compact-%lu.log", 
             wal->log_dir, wal->next_seq);
    
    int compact_fd = open(compact_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (compact_fd < 0) {
        pthread_mutex_unlock(&wal->mutex);
        return -1;
    }
    
    uint64_t compact_seq = wal->next_seq++;
    
    // 写入所有有效的键值对
    for (int i = 0; i < key_count; i++) {
        char *value = NULL;
        if (get_value(engine, keys[i], &value) == 0 && value) {
            uint32_t key_size = strlen(keys[i]) + 1;
            uint32_t value_size = strlen(value) + 1;
            uint8_t op_type = WAL_OP_SET;
            time_t timestamp = get_current_time();
            
            // 写入压缩条目
            write(compact_fd, &compact_seq, sizeof(compact_seq));
            write(compact_fd, &timestamp, sizeof(timestamp));
            write(compact_fd, &op_type, sizeof(op_type));
            write(compact_fd, &key_size, sizeof(key_size));
            write(compact_fd, keys[i], key_size);
            write(compact_fd, &value_size, sizeof(value_size));
            write(compact_fd, value, value_size);
            
            free(value);
        }
        free(keys[i]);
    }
    free(keys);
    
    fsync(compact_fd);
    close(compact_fd);
    
    // 更新压缩序列号
    wal->compact_seq = compact_seq;
    
    printf("[COMPACTION] Log compaction completed, new compact file: %s\n", compact_filename);
    
    pthread_mutex_unlock(&wal->mutex);
    return 0;
}

// 清理旧日志文件
void wal_cleanup_old_logs(wal_t *wal, uint64_t before_seq) {
    if (!wal) return;
    
    pthread_mutex_lock(&wal->mutex);
    
    wal_file_t *current = wal->file_list;
    wal_file_t *prev = NULL;
    
    while (current) {
        if (current->end_seq < before_seq) {
            // 删除这个文件
            if (unlink(current->filename) == 0) {
                printf("[CLEANUP] Removed old log file: %s\n", current->filename);
            }
            
            // 从链表中移除
            if (prev) {
                prev->next = current->next;
            } else {
                wal->file_list = current->next;
            }
            
            wal_file_t *to_delete = current;
            current = current->next;
            
            free(to_delete->filename);
            free(to_delete);
        } else {
            prev = current;
            current = current->next;
        }
    }
    
    pthread_mutex_unlock(&wal->mutex);
}

// 强制同步
int wal_force_sync(wal_t *wal) {
    if (!wal) return -1;
    
    pthread_mutex_lock(&wal->mutex);
    
    int result = 0;
    if (wal->fd >= 0) {
        result = fsync(wal->fd);
        wal->last_sync_time = get_current_time();
    }
    
    pthread_mutex_unlock(&wal->mutex);
    return result;
}

// 增量同步
int wal_incremental_sync(wal_t *wal, void *engine) {
    if (!wal || !engine) return -1;
    
    pthread_mutex_lock(&wal->mutex);
    
    // 检查是否有新的数据需要同步
    if (wal->next_seq > wal->last_sync_seq + INCREMENTAL_BATCH_SIZE) {
        if (wal->fd >= 0) {
            fsync(wal->fd);
            wal->last_sync_seq = wal->next_seq - 1;
            wal->last_sync_time = get_current_time();
            printf("[INCREMENTAL] Batch sync completed, synced to seq: %lu\n", wal->last_sync_seq);
        }
    }
    
    pthread_mutex_unlock(&wal->mutex);
    return 0;
}

// 启动增量同步
int wal_start_incremental_sync(wal_t *wal, void *engine) {
    if (!wal || !engine) return -1;
    
    if (!wal->incremental_running) {
        wal->incremental_running = 1;
        if (pthread_create(&wal->incremental_thread, NULL, incremental_sync_thread, wal) != 0) {
            wal->incremental_running = 0;
            return -1;
        }
    }
    
    return 0;
}

// 启动压缩
int wal_start_compaction(wal_t *wal, void *engine,
                        int (*get_all_keys)(void *engine, char ***keys, int *count)) {
    if (!wal || !engine || !get_all_keys) return -1;
    
    if (!wal->compact_running) {
        wal->compact_running = 1;
        if (pthread_create(&wal->compact_thread, NULL, compaction_thread, wal) != 0) {
            wal->compact_running = 0;
            return -1;
        }
    }
    
    return 0;
}

// WAL恢复功能
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
                time_t timestamp;
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
                
                // 读取时间戳
                if (read(fd, &timestamp, sizeof(timestamp)) != sizeof(timestamp)) {
                    break;
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
                
                // 应用日志条目
                apply_log(engine, op_type, key, value);
                
                free(key);
                free(value);
            }
            
            close(fd);
        }
    }
    
    closedir(dir);
    return 0;
} 