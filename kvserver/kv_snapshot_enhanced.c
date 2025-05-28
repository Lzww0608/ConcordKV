#define _GNU_SOURCE  // 为了使用strdup
#include "kv_persist.h"
#include "kv_store.h"
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

// 快照文件名格式
static void get_snapshot_filename(char *buf, size_t buf_size, const char *dir, uint64_t seq) {
    snprintf(buf, buf_size, "%s/snapshot-%lu.snap", dir, seq);
}

// 增量快照文件名格式
static void get_incremental_snapshot_filename(char *buf, size_t buf_size, const char *dir, 
                                            uint64_t from_seq, uint64_t to_seq) {
    snprintf(buf, buf_size, "%s/incremental-%lu-%lu.snap", dir, from_seq, to_seq);
}

// 创建目录（如果不存在）
static int ensure_directory(const char *dir) {
    struct stat st = {0};
    if (stat(dir, &st) == -1) {
        return mkdir(dir, 0755);
    }
    return 0;
}

// 初始化快照
int snapshot_init(snapshot_t *snap, const char *snap_dir) {
    if (!snap || !snap_dir) return -1;
    
    // 确保快照目录存在
    if (ensure_directory(snap_dir) != 0) {
        return -1;
    }
    
    snap->snap_dir = strdup(snap_dir);
    snap->snap_seq = 0;
    snap->snap_time = time(NULL);
    snap->snap_file = NULL;
    
    return 0;
}

// 销毁快照
void snapshot_destroy(snapshot_t *snap) {
    if (!snap) return;
    
    if (snap->snap_dir) {
        free(snap->snap_dir);
        snap->snap_dir = NULL;
    }
    
    if (snap->snap_file) {
        free(snap->snap_file);
        snap->snap_file = NULL;
    }
}

// 创建快照
int snapshot_create(snapshot_t *snap, void *engine, 
                   int (*save_data)(void *engine, FILE *fp)) {
    if (!snap || !engine || !save_data) return -1;
    
    // 生成快照文件名
    char filename[256];
    snap->snap_seq++;
    get_snapshot_filename(filename, sizeof(filename), snap->snap_dir, snap->snap_seq);
    
    FILE *fp = fopen(filename, "wb");
    if (!fp) return -1;
    
    // 写入快照头信息
    uint32_t magic = 0x534E4150; // "SNAP"
    uint32_t version = 1;
    time_t timestamp = time(NULL);
    
    fwrite(&magic, sizeof(magic), 1, fp);
    fwrite(&version, sizeof(version), 1, fp);
    fwrite(&timestamp, sizeof(timestamp), 1, fp);
    fwrite(&snap->snap_seq, sizeof(snap->snap_seq), 1, fp);
    
    // 保存数据
    int result = save_data(engine, fp);
    
    fclose(fp);
    
    if (result == 0) {
        snap->snap_time = timestamp;
        if (snap->snap_file) {
            free(snap->snap_file);
        }
        snap->snap_file = strdup(filename);
        printf("[SNAPSHOT] Created snapshot: %s (seq: %lu)\n", filename, snap->snap_seq);
    } else {
        unlink(filename); // 删除失败的快照文件
    }
    
    return result;
}

// 加载快照
int snapshot_load(snapshot_t *snap, void *engine,
                 int (*load_data)(void *engine, FILE *fp)) {
    if (!snap || !engine || !load_data) return -1;
    
    DIR *dir;
    struct dirent *entry;
    char latest_file[256] = {0};
    uint64_t latest_seq = 0;
    
    // 找到最新的快照文件
    if ((dir = opendir(snap->snap_dir)) == NULL) {
        return -1;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "snapshot-", 9) == 0 && strstr(entry->d_name, ".snap") != NULL) {
            uint64_t seq;
            if (sscanf(entry->d_name, "snapshot-%lu.snap", &seq) == 1) {
                if (seq > latest_seq) {
                    latest_seq = seq;
                    snprintf(latest_file, sizeof(latest_file), "%s/%s", snap->snap_dir, entry->d_name);
                }
            }
        }
    }
    closedir(dir);
    
    if (latest_seq == 0) {
        return -1; // 没有找到快照文件
    }
    
    FILE *fp = fopen(latest_file, "rb");
    if (!fp) return -1;
    
    // 读取快照头信息
    uint32_t magic, version;
    time_t timestamp;
    uint64_t seq;
    
    if (fread(&magic, sizeof(magic), 1, fp) != 1 || magic != 0x534E4150 ||
        fread(&version, sizeof(version), 1, fp) != 1 || version != 1 ||
        fread(&timestamp, sizeof(timestamp), 1, fp) != 1 ||
        fread(&seq, sizeof(seq), 1, fp) != 1) {
        fclose(fp);
        return -1;
    }
    
    // 加载数据
    int result = load_data(engine, fp);
    
    fclose(fp);
    
    if (result == 0) {
        snap->snap_seq = seq;
        snap->snap_time = timestamp;
        if (snap->snap_file) {
            free(snap->snap_file);
        }
        snap->snap_file = strdup(latest_file);
        printf("[SNAPSHOT] Loaded snapshot: %s (seq: %lu)\n", latest_file, seq);
    }
    
    return result;
}

// 创建增量快照
int snapshot_create_incremental(snapshot_t *snap, void *engine,
                               uint64_t from_seq, uint64_t to_seq,
                               int (*save_incremental)(void *engine, FILE *fp, uint64_t from_seq, uint64_t to_seq)) {
    if (!snap || !engine || !save_incremental || from_seq >= to_seq) return -1;
    
    // 生成增量快照文件名
    char filename[256];
    get_incremental_snapshot_filename(filename, sizeof(filename), snap->snap_dir, from_seq, to_seq);
    
    FILE *fp = fopen(filename, "wb");
    if (!fp) return -1;
    
    // 写入增量快照头信息
    uint32_t magic = 0x494E4352; // "INCR"
    uint32_t version = 1;
    time_t timestamp = time(NULL);
    
    fwrite(&magic, sizeof(magic), 1, fp);
    fwrite(&version, sizeof(version), 1, fp);
    fwrite(&timestamp, sizeof(timestamp), 1, fp);
    fwrite(&from_seq, sizeof(from_seq), 1, fp);
    fwrite(&to_seq, sizeof(to_seq), 1, fp);
    
    // 保存增量数据
    int result = save_incremental(engine, fp, from_seq, to_seq);
    
    fclose(fp);
    
    if (result == 0) {
        printf("[INCREMENTAL_SNAPSHOT] Created incremental snapshot: %s (seq: %lu-%lu)\n", 
               filename, from_seq, to_seq);
    } else {
        unlink(filename); // 删除失败的快照文件
    }
    
    return result;
}

// 应用增量快照
int snapshot_apply_incremental(snapshot_t *snap, void *engine,
                              int (*apply_incremental)(void *engine, FILE *fp)) {
    if (!snap || !engine || !apply_incremental) return -1;
    
    DIR *dir;
    struct dirent *entry;
    char incremental_files[100][256];
    int file_count = 0;
    
    // 收集所有增量快照文件
    if ((dir = opendir(snap->snap_dir)) == NULL) {
        return -1;
    }
    
    while ((entry = readdir(dir)) != NULL && file_count < 100) {
        if (strncmp(entry->d_name, "incremental-", 12) == 0 && strstr(entry->d_name, ".snap") != NULL) {
            snprintf(incremental_files[file_count], sizeof(incremental_files[file_count]), 
                    "%s/%s", snap->snap_dir, entry->d_name);
            file_count++;
        }
    }
    closedir(dir);
    
    // 按序列号排序（简单的冒泡排序）
    for (int i = 0; i < file_count - 1; i++) {
        for (int j = 0; j < file_count - i - 1; j++) {
            uint64_t seq1, seq2, end1, end2;
            char *name1 = strrchr(incremental_files[j], '/') + 1;
            char *name2 = strrchr(incremental_files[j + 1], '/') + 1;
            
            sscanf(name1, "incremental-%lu-%lu.snap", &seq1, &end1);
            sscanf(name2, "incremental-%lu-%lu.snap", &seq2, &end2);
            
            if (seq1 > seq2) {
                char temp[256];
                strcpy(temp, incremental_files[j]);
                strcpy(incremental_files[j], incremental_files[j + 1]);
                strcpy(incremental_files[j + 1], temp);
            }
        }
    }
    
    // 依次应用增量快照
    for (int i = 0; i < file_count; i++) {
        FILE *fp = fopen(incremental_files[i], "rb");
        if (!fp) continue;
        
        // 读取增量快照头信息
        uint32_t magic, version;
        time_t timestamp;
        uint64_t from_seq, to_seq;
        
        if (fread(&magic, sizeof(magic), 1, fp) != 1 || magic != 0x494E4352 ||
            fread(&version, sizeof(version), 1, fp) != 1 || version != 1 ||
            fread(&timestamp, sizeof(timestamp), 1, fp) != 1 ||
            fread(&from_seq, sizeof(from_seq), 1, fp) != 1 ||
            fread(&to_seq, sizeof(to_seq), 1, fp) != 1) {
            fclose(fp);
            continue;
        }
        
        // 应用增量数据
        int result = apply_incremental(engine, fp);
        fclose(fp);
        
        if (result == 0) {
            printf("[INCREMENTAL_SNAPSHOT] Applied incremental snapshot: %s (seq: %lu-%lu)\n", 
                   incremental_files[i], from_seq, to_seq);
        } else {
            printf("[ERROR] Failed to apply incremental snapshot: %s\n", incremental_files[i]);
            return -1;
        }
    }
    
    return 0;
}

// 清理旧的快照文件
int snapshot_cleanup_old(snapshot_t *snap, int keep_count) {
    if (!snap || keep_count <= 0) return -1;
    
    DIR *dir;
    struct dirent *entry;
    char snapshot_files[100][256];
    uint64_t snapshot_seqs[100];
    int file_count = 0;
    
    // 收集所有快照文件
    if ((dir = opendir(snap->snap_dir)) == NULL) {
        return -1;
    }
    
    while ((entry = readdir(dir)) != NULL && file_count < 100) {
        if (strncmp(entry->d_name, "snapshot-", 9) == 0 && strstr(entry->d_name, ".snap") != NULL) {
            uint64_t seq;
            if (sscanf(entry->d_name, "snapshot-%lu.snap", &seq) == 1) {
                snprintf(snapshot_files[file_count], sizeof(snapshot_files[file_count]), 
                        "%s/%s", snap->snap_dir, entry->d_name);
                snapshot_seqs[file_count] = seq;
                file_count++;
            }
        }
    }
    closedir(dir);
    
    if (file_count <= keep_count) {
        return 0; // 不需要清理
    }
    
    // 按序列号排序
    for (int i = 0; i < file_count - 1; i++) {
        for (int j = 0; j < file_count - i - 1; j++) {
            if (snapshot_seqs[j] > snapshot_seqs[j + 1]) {
                // 交换序列号
                uint64_t temp_seq = snapshot_seqs[j];
                snapshot_seqs[j] = snapshot_seqs[j + 1];
                snapshot_seqs[j + 1] = temp_seq;
                
                // 交换文件名
                char temp_file[256];
                strcpy(temp_file, snapshot_files[j]);
                strcpy(snapshot_files[j], snapshot_files[j + 1]);
                strcpy(snapshot_files[j + 1], temp_file);
            }
        }
    }
    
    // 删除旧的快照文件（保留最新的keep_count个）
    int deleted_count = 0;
    for (int i = 0; i < file_count - keep_count; i++) {
        if (unlink(snapshot_files[i]) == 0) {
            printf("[CLEANUP] Removed old snapshot: %s\n", snapshot_files[i]);
            deleted_count++;
        }
    }
    
    return deleted_count;
}

// 获取快照统计信息
int snapshot_get_stats(snapshot_t *snap, int *total_snapshots, int *total_incremental, 
                      size_t *total_size) {
    if (!snap || !total_snapshots || !total_incremental || !total_size) return -1;
    
    DIR *dir;
    struct dirent *entry;
    *total_snapshots = 0;
    *total_incremental = 0;
    *total_size = 0;
    
    if ((dir = opendir(snap->snap_dir)) == NULL) {
        return -1;
    }
    
    while ((entry = readdir(dir)) != NULL) {
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "%s/%s", snap->snap_dir, entry->d_name);
        
        struct stat st;
        if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
            *total_size += st.st_size;
            
            if (strncmp(entry->d_name, "snapshot-", 9) == 0) {
                (*total_snapshots)++;
            } else if (strncmp(entry->d_name, "incremental-", 12) == 0) {
                (*total_incremental)++;
            }
        }
    }
    
    closedir(dir);
    return 0;
} 