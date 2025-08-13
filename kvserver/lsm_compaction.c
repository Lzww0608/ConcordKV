/*
 * @Author: Lzww0608  
 * @Date: 2025-6-2 23:26:11
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-5 21:22:56
 * @Description: ConcordKV LSM-Tree 压缩机制实现
 */

#include "lsm_sstable.h"
#include "lsm_compaction.h"
#include "lsm_memtable.h"
#include "lsm_memtable_manager.h"
#include "kv_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>

// === 工具函数实现 ===

static uint64_t get_current_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000UL + tv.tv_usec;
}

int lsm_compaction_key_compare(const char *key1, size_t key1_len,
                               const char *key2, size_t key2_len) {
    if (!key1 || !key2) {
        return key1 ? 1 : (key2 ? -1 : 0);
    }
    
    size_t min_len = key1_len < key2_len ? key1_len : key2_len;
    int result = memcmp(key1, key2, min_len);
    
    if (result == 0) {
        if (key1_len < key2_len) return -1;
        if (key1_len > key2_len) return 1;
        return 0;
    }
    
    return result;
}

int lsm_compaction_format_filename(char *buffer, size_t buffer_size,
                                   const char *data_dir, int level, uint64_t file_id) {
    if (!buffer || !data_dir) {
        return KV_ERR_INVALID_PARAM;
    }
    
    int ret = snprintf(buffer, buffer_size, "%s/level_%d_%lu.sst", 
                       data_dir, level, file_id);
    if (ret < 0 || (size_t)ret >= buffer_size) {
        return KV_ERR_INVALID_PARAM;
    }
    
    return KV_ERR_NONE;
}

// === SSTable元信息实现 ===

lsm_sstable_meta_t* lsm_sstable_meta_create(const char *filename, int level, uint64_t file_id) {
    if (!filename || level < 0 || level >= LSM_MAX_LEVELS) {
        return NULL;
    }
    
    lsm_sstable_meta_t *meta = kv_malloc(sizeof(lsm_sstable_meta_t));
    if (!meta) {
        return NULL;
    }
    
    memset(meta, 0, sizeof(lsm_sstable_meta_t));
    
    strncpy(meta->filename, filename, LSM_MAX_FILENAME - 1);
    meta->filename[LSM_MAX_FILENAME - 1] = '\0';
    meta->file_id = file_id;
    meta->level = level;
    meta->creation_time = get_current_time_us();
    
    return meta;
}

void lsm_sstable_meta_destroy(lsm_sstable_meta_t *meta) {
    if (!meta) return;
    
    if (meta->min_key) {
        kv_free(meta->min_key);
    }
    if (meta->max_key) {
        kv_free(meta->max_key);
    }
    if (meta->reader) {
        sstable_reader_destroy(meta->reader);
        meta->reader = NULL;
    }
    
    kv_free(meta);
}

int lsm_sstable_meta_load_from_file(lsm_sstable_meta_t *meta) {
    if (!meta) {
        return KV_ERR_INVALID_PARAM;
    }
    
    // 获取文件大小
    struct stat st;
    if (stat(meta->filename, &st) != 0) {
        return KV_ERR_IO_ERROR;
    }
    meta->file_size = st.st_size;
    
    // 创建读取器并从Footer加载元信息
    meta->reader = sstable_reader_create(meta->filename);
    if (!meta->reader) {
        return KV_ERR_IO_ERROR;
    }
    
    // 从Footer获取基本信息
    sstable_stats_t stats;
    int ret = sstable_reader_get_stats(meta->reader, &stats);
    if (ret != KV_ERR_NONE) {
        sstable_reader_destroy(meta->reader);
        meta->reader = NULL;
        return ret;
    }
    
    meta->entry_count = stats.total_entries;
    
    // 从索引块获取键范围（简化实现，实际可能需要读取第一个和最后一个键）
    // 这里假设SSTable已经存储了这些信息
    
    return KV_ERR_NONE;
}

bool lsm_sstable_meta_contains_key(lsm_sstable_meta_t *meta, 
                                    const char *key, size_t key_len) {
    if (!meta || !key || !meta->min_key || !meta->max_key) {
        return true; // 保守估计，如果信息不完整则认为可能包含
    }
    
    // 检查键是否在[min_key, max_key]范围内
    if (lsm_compaction_key_compare(key, key_len, meta->min_key, meta->min_key_len) < 0) {
        return false;
    }
    if (lsm_compaction_key_compare(key, key_len, meta->max_key, meta->max_key_len) > 0) {
        return false;
    }
    
    return true;
}

// === 层级管理器实现 ===

lsm_level_manager_t* lsm_level_manager_create(const char *data_dir) {
    if (!data_dir) {
        return NULL;
    }
    
    lsm_level_manager_t *manager = kv_malloc(sizeof(lsm_level_manager_t));
    if (!manager) {
        return NULL;
    }
    
    memset(manager, 0, sizeof(lsm_level_manager_t));
    
    // 初始化各层级文件数组
    for (int i = 0; i < LSM_MAX_LEVELS; i++) {
        manager->levels[i] = kv_malloc(sizeof(lsm_sstable_meta_t*) * LSM_MAX_FILES_PER_LEVEL);
        if (!manager->levels[i]) {
            // 清理已分配的内存
            for (int j = 0; j < i; j++) {
                kv_free(manager->levels[j]);
            }
            kv_free(manager);
            return NULL;
        }
        memset(manager->levels[i], 0, sizeof(lsm_sstable_meta_t*) * LSM_MAX_FILES_PER_LEVEL);
    }
    
    strncpy(manager->data_dir, data_dir, LSM_MAX_FILENAME - 1);
    manager->data_dir[LSM_MAX_FILENAME - 1] = '\0';
    
    if (pthread_rwlock_init(&manager->lock, NULL) != 0) {
        for (int i = 0; i < LSM_MAX_LEVELS; i++) {
            kv_free(manager->levels[i]);
        }
        kv_free(manager);
        return NULL;
    }
    
    manager->next_file_id = 1;
    
    // 创建数据目录
    mkdir(data_dir, 0755);
    
    return manager;
}

void lsm_level_manager_destroy(lsm_level_manager_t *manager) {
    if (!manager) return;
    
    pthread_rwlock_wrlock(&manager->lock);
    
    // 销毁所有SSTable元信息
    for (int level = 0; level < LSM_MAX_LEVELS; level++) {
        if (manager->levels[level]) {
            for (int i = 0; i < manager->level_file_count[level]; i++) {
                if (manager->levels[level][i]) {
                    lsm_sstable_meta_destroy(manager->levels[level][i]);
                }
            }
            kv_free(manager->levels[level]);
        }
    }
    
    pthread_rwlock_unlock(&manager->lock);
    pthread_rwlock_destroy(&manager->lock);
    
    kv_free(manager);
}

int lsm_level_manager_add_sstable(lsm_level_manager_t *manager, 
                                   int level, lsm_sstable_meta_t *sstable_meta) {
    if (!manager || !sstable_meta || level < 0 || level >= LSM_MAX_LEVELS) {
        return KV_ERR_INVALID_PARAM;
    }
    
    pthread_rwlock_wrlock(&manager->lock);
    
    if (manager->level_file_count[level] >= LSM_MAX_FILES_PER_LEVEL) {
        pthread_rwlock_unlock(&manager->lock);
        return KV_ERR_NO_SPACE;
    }
    
    manager->levels[level][manager->level_file_count[level]] = sstable_meta;
    manager->level_file_count[level]++;
    manager->level_size[level] += sstable_meta->file_size;
    
    pthread_rwlock_unlock(&manager->lock);
    
    return KV_ERR_NONE;
}

int lsm_level_manager_remove_sstable(lsm_level_manager_t *manager,
                                      int level, lsm_sstable_meta_t *sstable_meta) {
    if (!manager || !sstable_meta || level < 0 || level >= LSM_MAX_LEVELS) {
        return KV_ERR_INVALID_PARAM;
    }
    
    pthread_rwlock_wrlock(&manager->lock);
    
    // 查找并移除SSTable
    for (int i = 0; i < manager->level_file_count[level]; i++) {
        if (manager->levels[level][i] == sstable_meta) {
            // 将后面的元素前移
            for (int j = i; j < manager->level_file_count[level] - 1; j++) {
                manager->levels[level][j] = manager->levels[level][j + 1];
            }
            manager->level_file_count[level]--;
            manager->level_size[level] -= sstable_meta->file_size;
            manager->levels[level][manager->level_file_count[level]] = NULL;
            
            pthread_rwlock_unlock(&manager->lock);
            return KV_ERR_NONE;
        }
    }
    
    pthread_rwlock_unlock(&manager->lock);
    return KV_ERR_NOT_FOUND;
}

int lsm_level_manager_get(lsm_level_manager_t *manager,
                          const char *key, size_t key_len,
                          char **value, size_t *value_len,
                          uint64_t *seq_num, bool *deleted) {
    if (!manager || !key || !value || !value_len) {
        return KV_ERR_INVALID_PARAM;
    }
    
    pthread_rwlock_rdlock(&manager->lock);
    
    // 从Level-0开始，逐级查找
    for (int level = 0; level < LSM_MAX_LEVELS; level++) {
        for (int i = 0; i < manager->level_file_count[level]; i++) {
            lsm_sstable_meta_t *meta = manager->levels[level][i];
            if (!meta || !lsm_sstable_meta_contains_key(meta, key, key_len)) {
                continue;
            }
            
            // 确保读取器已加载
            if (!meta->reader) {
                if (lsm_sstable_meta_load_from_file(meta) != KV_ERR_NONE) {
                    continue;
                }
            }
            
            // 检查布隆过滤器
            if (!sstable_reader_may_contain(meta->reader, key, key_len)) {
                continue;
            }
            
            // 尝试获取键值对
            int ret = sstable_reader_get(meta->reader, key, key_len, 
                                         value, value_len, seq_num, deleted);
            if (ret == KV_ERR_NONE) {
                pthread_rwlock_unlock(&manager->lock);
                return KV_ERR_NONE;
            }
        }
    }
    
    pthread_rwlock_unlock(&manager->lock);
    return KV_ERR_NOT_FOUND;
}

bool lsm_level_manager_needs_compaction(lsm_level_manager_t *manager, int level) {
    if (!manager || level < 0 || level >= LSM_MAX_LEVELS) {
        return false;
    }
    
    pthread_rwlock_rdlock(&manager->lock);
    
    bool needs_compaction = false;
    
    if (level == 0) {
        // Level-0特殊处理：基于文件数量
        needs_compaction = manager->level_file_count[level] >= LSM_LEVEL0_FILE_LIMIT;
    } else {
        // 其他层级：基于大小
        uint64_t max_size = 10 * 1024 * 1024; // 10MB基础大小
        for (int i = 1; i < level; i++) {
            max_size *= LSM_LEVEL_SIZE_MULTIPLIER;
        }
        needs_compaction = manager->level_size[level] > max_size;
    }
    
    pthread_rwlock_unlock(&manager->lock);
    return needs_compaction;
}

int lsm_level_manager_get_file_count(lsm_level_manager_t *manager, int level) {
    if (!manager || level < 0 || level >= LSM_MAX_LEVELS) {
        return 0;
    }
    
    pthread_rwlock_rdlock(&manager->lock);
    int count = manager->level_file_count[level];
    pthread_rwlock_unlock(&manager->lock);
    
    return count;
}

// === 压缩任务实现 ===

lsm_compaction_task_t* lsm_compaction_task_create(lsm_compaction_task_type_t type,
                                                   lsm_compaction_priority_t priority) {
    lsm_compaction_task_t *task = kv_malloc(sizeof(lsm_compaction_task_t));
    if (!task) {
        return NULL;
    }
    
    memset(task, 0, sizeof(lsm_compaction_task_t));
    
    task->type = type;
    task->priority = priority;
    task->status = LSM_COMPACTION_STATUS_PENDING;
    task->source_level = -1;
    task->target_level = -1;
    
    if (pthread_mutex_init(&task->lock, NULL) != 0) {
        kv_free(task);
        return NULL;
    }
    
    return task;
}

void lsm_compaction_task_destroy(lsm_compaction_task_t *task) {
    if (!task) return;
    
    if (task->input_files) {
        kv_free(task->input_files);
    }
    if (task->output_files) {
        // 注意：不要释放output_files中的元信息，因为它们已经被层级管理器接管
        // 只释放数组本身
        kv_free(task->output_files);
    }
    
    pthread_mutex_destroy(&task->lock);
    kv_free(task);
}

int lsm_compaction_task_set_level0_params(lsm_compaction_task_t *task,
                                           lsm_memtable_t *memtable,
                                           int output_level) {
    if (!task || !memtable || output_level < 0 || output_level >= LSM_MAX_LEVELS) {
        return KV_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&task->lock);
    
    task->input_memtable = memtable;
    task->source_level = -1; // MemTable不属于任何层级
    task->target_level = output_level;
    task->type = LSM_COMPACTION_TASK_LEVEL0;
    
    pthread_mutex_unlock(&task->lock);
    
    return KV_ERR_NONE;
}

int lsm_compaction_task_set_level_n_params(lsm_compaction_task_t *task,
                                            int source_level, int target_level,
                                            lsm_sstable_meta_t **input_files,
                                            int input_file_count) {
    if (!task || !input_files || input_file_count <= 0 ||
        source_level < 0 || source_level >= LSM_MAX_LEVELS ||
        target_level < 0 || target_level >= LSM_MAX_LEVELS) {
        return KV_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&task->lock);
    
    task->source_level = source_level;
    task->target_level = target_level;
    task->type = LSM_COMPACTION_TASK_LEVEL_N;
    
    // 复制输入文件数组
    task->input_files = kv_malloc(sizeof(lsm_sstable_meta_t*) * input_file_count);
    if (!task->input_files) {
        pthread_mutex_unlock(&task->lock);
        return KV_ERR_OUT_OF_MEMORY;
    }
    
    memcpy(task->input_files, input_files, sizeof(lsm_sstable_meta_t*) * input_file_count);
    task->input_file_count = input_file_count;
    
    pthread_mutex_unlock(&task->lock);
    
    return KV_ERR_NONE;
}

// === 压缩配置实现 ===

lsm_compaction_config_t lsm_compaction_default_config(void) {
    lsm_compaction_config_t config;
    memset(&config, 0, sizeof(config));
    
    config.strategy = LSM_COMPACTION_LEVELED;
    config.level0_file_limit = LSM_LEVEL0_FILE_LIMIT;
    config.level_size_multiplier = LSM_LEVEL_SIZE_MULTIPLIER;
    config.enable_background_compaction = true;
    config.background_thread_count = LSM_COMPACTION_THREAD_COUNT;
    config.compression = SSTABLE_COMPRESSION_NONE;
    config.enable_bloom_filter = true;
    config.bloom_filter_bits_per_key = 10;
    
    // 设置各层级最大字节数
    uint64_t base_size = 10 * 1024 * 1024; // 10MB
    for (int i = 0; i < LSM_MAX_LEVELS; i++) {
        config.max_level_bytes[i] = base_size;
        base_size *= config.level_size_multiplier;
    }
    
    return config;
}

// === 压缩调度器实现 ===

// 工作线程函数在文件末尾实现

lsm_compaction_scheduler_t* lsm_compaction_scheduler_create(
    lsm_memtable_manager_t *memtable_manager,
    lsm_level_manager_t *level_manager,
    const lsm_compaction_config_t *config) {
    
    if (!memtable_manager || !level_manager) {
        return NULL;
    }
    
    lsm_compaction_scheduler_t *scheduler = kv_malloc(sizeof(lsm_compaction_scheduler_t));
    if (!scheduler) {
        return NULL;
    }
    
    memset(scheduler, 0, sizeof(lsm_compaction_scheduler_t));
    
    scheduler->memtable_manager = memtable_manager;
    scheduler->level_manager = level_manager;
    
    if (config) {
        scheduler->config = *config;
    } else {
        scheduler->config = lsm_compaction_default_config();
    }
    
    // 初始化锁和条件变量
    if (pthread_mutex_init(&scheduler->queue_lock, NULL) != 0 ||
        pthread_cond_init(&scheduler->queue_cond, NULL) != 0 ||
        pthread_mutex_init(&scheduler->shutdown_lock, NULL) != 0 ||
        pthread_rwlock_init(&scheduler->stats_lock, NULL) != 0) {
        
        // 清理已初始化的锁
        pthread_mutex_destroy(&scheduler->queue_lock);
        pthread_cond_destroy(&scheduler->queue_cond);
        pthread_mutex_destroy(&scheduler->shutdown_lock);
        pthread_rwlock_destroy(&scheduler->stats_lock);
        kv_free(scheduler);
        return NULL;
    }
    
    scheduler->next_task_id = 1;
    
    return scheduler;
}

void lsm_compaction_scheduler_destroy(lsm_compaction_scheduler_t *scheduler) {
    if (!scheduler) return;
    
    // 停止调度器
    lsm_compaction_scheduler_stop(scheduler);
    
    // 清理任务队列
    pthread_mutex_lock(&scheduler->queue_lock);
    lsm_compaction_task_t *task = scheduler->task_queue_head;
    while (task) {
        lsm_compaction_task_t *next = task->next;
        lsm_compaction_task_destroy(task);
        task = next;
    }
    pthread_mutex_unlock(&scheduler->queue_lock);
    
    // 销毁锁和条件变量
    pthread_mutex_destroy(&scheduler->queue_lock);
    pthread_cond_destroy(&scheduler->queue_cond);
    pthread_mutex_destroy(&scheduler->shutdown_lock);
    pthread_rwlock_destroy(&scheduler->stats_lock);
    
    if (scheduler->worker_threads) {
        kv_free(scheduler->worker_threads);
    }
    
    kv_free(scheduler);
}

int lsm_compaction_scheduler_start(lsm_compaction_scheduler_t *scheduler) {
    if (!scheduler) {
        return KV_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&scheduler->shutdown_lock);
    
    // 检查是否已经启动（通过worker_threads是否存在判断）
    if (scheduler->worker_threads) {
        // 已经启动
        pthread_mutex_unlock(&scheduler->shutdown_lock);
        return KV_ERR_NONE;
    }
    
    scheduler->shutdown = false;
    
    // 创建工作线程
    if (scheduler->config.enable_background_compaction) {
        scheduler->worker_threads = kv_malloc(sizeof(pthread_t) * scheduler->config.background_thread_count);
        if (!scheduler->worker_threads) {
            scheduler->shutdown = true;
            pthread_mutex_unlock(&scheduler->shutdown_lock);
            return KV_ERR_OUT_OF_MEMORY;
        }
        
        for (int i = 0; i < scheduler->config.background_thread_count; i++) {
            if (pthread_create(&scheduler->worker_threads[i], NULL, 
                               compaction_worker_thread, scheduler) != 0) {
                // 清理已创建的线程
                scheduler->shutdown = true;
                pthread_cond_broadcast(&scheduler->queue_cond);
                
                for (int j = 0; j < i; j++) {
                    pthread_join(scheduler->worker_threads[j], NULL);
                }
                
                kv_free(scheduler->worker_threads);
                scheduler->worker_threads = NULL;
                pthread_mutex_unlock(&scheduler->shutdown_lock);
                return KV_ERR_THREAD_ERROR;
            }
        }
    }
    
    pthread_mutex_unlock(&scheduler->shutdown_lock);
    
    return KV_ERR_NONE;
}

int lsm_compaction_scheduler_stop(lsm_compaction_scheduler_t *scheduler) {
    if (!scheduler) {
        return KV_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&scheduler->shutdown_lock);
    
    if (scheduler->shutdown) {
        // 已经停止
        pthread_mutex_unlock(&scheduler->shutdown_lock);
        return KV_ERR_NONE;
    }
    
    scheduler->shutdown = true;
    
    // 通知所有工作线程退出
    pthread_cond_broadcast(&scheduler->queue_cond);
    
    pthread_mutex_unlock(&scheduler->shutdown_lock);
    
    // 等待所有工作线程结束
    if (scheduler->worker_threads) {
        for (int i = 0; i < scheduler->config.background_thread_count; i++) {
            pthread_join(scheduler->worker_threads[i], NULL);
        }
        kv_free(scheduler->worker_threads);
        scheduler->worker_threads = NULL;
    }
    
    return KV_ERR_NONE;
}

int lsm_compaction_scheduler_submit_task(lsm_compaction_scheduler_t *scheduler,
                                         lsm_compaction_task_t *task) {
    if (!scheduler || !task) {
        return KV_ERR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&scheduler->queue_lock);
    
    // 分配任务ID
    task->task_id = scheduler->next_task_id++;
    
    // 根据优先级插入任务队列
    if (!scheduler->task_queue_head || 
        task->priority > scheduler->task_queue_head->priority) {
        // 插入队列头部
        task->next = scheduler->task_queue_head;
        scheduler->task_queue_head = task;
        if (!scheduler->task_queue_tail) {
            scheduler->task_queue_tail = task;
        }
    } else {
        // 查找合适的插入位置
        lsm_compaction_task_t *current = scheduler->task_queue_head;
        while (current->next && current->next->priority >= task->priority) {
            current = current->next;
        }
        task->next = current->next;
        current->next = task;
        if (!task->next) {
            scheduler->task_queue_tail = task;
        }
    }
    
    // 更新统计
    pthread_rwlock_wrlock(&scheduler->stats_lock);
    scheduler->stats.pending_tasks++;
    pthread_rwlock_unlock(&scheduler->stats_lock);
    
    // 通知工作线程
    pthread_cond_signal(&scheduler->queue_cond);
    
    pthread_mutex_unlock(&scheduler->queue_lock);
    
    return KV_ERR_NONE;
}

int lsm_compaction_scheduler_trigger_check(lsm_compaction_scheduler_t *scheduler) {
    if (!scheduler) {
        return KV_ERR_INVALID_PARAM;
    }
    
    // 检查是否需要Level-0压缩
    if (lsm_memtable_manager_should_flush(scheduler->memtable_manager)) {
        // 获取可用的不可变MemTable
        lsm_memtable_t *memtable = lsm_memtable_manager_get_oldest_immutable(scheduler->memtable_manager);
        if (memtable) {
            // 检查当前任务队列中是否已经有处理这个MemTable的任务
            pthread_mutex_lock(&scheduler->queue_lock);
            
            bool already_queued = false;
            lsm_compaction_task_t *current_task = scheduler->task_queue_head;
            while (current_task) {
                if (current_task->type == LSM_COMPACTION_TASK_LEVEL0 && 
                    current_task->input_memtable == memtable) {
                    already_queued = true;
                    break;
                }
                current_task = current_task->next;
            }
            
            pthread_mutex_unlock(&scheduler->queue_lock);
            
            // 如果没有重复任务，则创建新任务
            if (!already_queued) {
                lsm_compaction_task_t *task = lsm_compaction_task_create(
                    LSM_COMPACTION_TASK_LEVEL0, LSM_COMPACTION_PRIORITY_HIGH);
                if (task) {
                    int ret = lsm_compaction_task_set_level0_params(task, memtable, 0);
                    if (ret == KV_ERR_NONE) {
                        ret = lsm_compaction_scheduler_submit_task(scheduler, task);
                        if (ret != KV_ERR_NONE) {
                            lsm_compaction_task_destroy(task);
                            return ret;
                        }
                    } else {
                        lsm_compaction_task_destroy(task);
                        return ret;
                    }
                } else {
                    return KV_ERR_OUT_OF_MEMORY;
                }
            }
        }
    }
    
    // 检查各层级是否需要压缩
    for (int level = 0; level < LSM_MAX_LEVELS - 1; level++) {
        if (lsm_level_manager_needs_compaction(scheduler->level_manager, level)) {
            lsm_compaction_task_t *task = lsm_compaction_task_create(
                LSM_COMPACTION_TASK_LEVEL_N, LSM_COMPACTION_PRIORITY_NORMAL);
            if (task) {
                // 简化实现：选择所有需要压缩的文件
                // 实际实现中应该有更复杂的选择策略
                task->source_level = level;
                task->target_level = level + 1;
                lsm_compaction_scheduler_submit_task(scheduler, task);
            }
        }
    }
    
    return KV_ERR_NONE;
}

int lsm_compaction_scheduler_get_stats(lsm_compaction_scheduler_t *scheduler,
                                       lsm_compaction_stats_t *stats) {
    if (!scheduler || !stats) {
        return KV_ERR_INVALID_PARAM;
    }
    
    pthread_rwlock_rdlock(&scheduler->stats_lock);
    *stats = scheduler->stats;
    pthread_rwlock_unlock(&scheduler->stats_lock);
    
    return KV_ERR_NONE;
}

// === 压缩执行函数实现 ===

int lsm_compaction_execute_level0(lsm_compaction_scheduler_t *scheduler,
                                  lsm_compaction_task_t *task) {
    if (!scheduler || !task || !task->input_memtable) {
        return KV_ERR_INVALID_PARAM;
    }
    
    task->start_time = get_current_time_us();
    task->status = LSM_COMPACTION_STATUS_RUNNING;
    
    // 检查MemTable是否为空，避免创建空文件
    if (lsm_memtable_count(task->input_memtable) == 0) {
        // 空MemTable，直接标记为完成并移除
        int ret = lsm_memtable_manager_remove_specific_immutable(scheduler->memtable_manager, task->input_memtable);
        if (ret == KV_ERR_NONE || ret == KV_ERR_NOT_FOUND) {
            // 成功移除或已被移除，都是可接受的
            task->end_time = get_current_time_us();
            task->status = LSM_COMPACTION_STATUS_COMPLETED;
            task->bytes_written = 0;
            
            // 更新统计信息
            pthread_rwlock_wrlock(&scheduler->stats_lock);
            scheduler->stats.total_compactions++;
            scheduler->stats.level0_compactions++;
            scheduler->stats.total_compaction_time += (task->end_time - task->start_time);
            scheduler->stats.completed_tasks++;
            pthread_rwlock_unlock(&scheduler->stats_lock);
            
            return KV_ERR_NONE;
        } else {
            task->status = LSM_COMPACTION_STATUS_FAILED;
            task->error_code = ret;
            snprintf(task->error_message, sizeof(task->error_message),
                     "Failed to remove empty MemTable: %d", ret);
            return ret;
        }
    }
    
    // 更安全的文件名生成：包含线程ID和时间戳以确保唯一性
    char filename[LSM_MAX_FILENAME];
    uint64_t file_id;
    uint64_t thread_id = (uint64_t)pthread_self();
    uint64_t timestamp = get_current_time_us();
    int ret;
    
    // 生成更加唯一的文件ID
    file_id = __sync_fetch_and_add(&scheduler->level_manager->next_file_id, 1);
    
    // 包含线程ID和时间戳的文件名
    snprintf(filename, sizeof(filename), "%s/level_%d_%lu_%lx_%lu.sst", 
             scheduler->level_manager->data_dir, task->target_level, 
             file_id, thread_id, timestamp);
    
    // 检查文件是否存在，如果存在则生成新的ID
    struct stat st;
    int attempts = 0;
    while (stat(filename, &st) == 0 && attempts < 10) {
        file_id = __sync_fetch_and_add(&scheduler->level_manager->next_file_id, 1);
        timestamp = get_current_time_us();
        snprintf(filename, sizeof(filename), "%s/level_%d_%lu_%lx_%lu.sst", 
                 scheduler->level_manager->data_dir, task->target_level, 
                 file_id, thread_id, timestamp);
        attempts++;
        // 添加小延迟避免时间戳冲突
        usleep(100);
    }
    
    if (attempts >= 10) {
        task->status = LSM_COMPACTION_STATUS_FAILED;
        task->error_code = KV_ERR_IO_ERROR;
        snprintf(task->error_message, sizeof(task->error_message),
                 "Cannot generate unique filename after %d attempts", attempts);
        return KV_ERR_IO_ERROR;
    }
    
    // 创建SSTable文件，添加重试机制
    int create_attempts = 0;
    do {
        ret = sstable_create_from_memtable(task->input_memtable, filename, 
                                           scheduler->config.compression);
        if (ret == KV_ERR_NONE) {
            break;
        }
        create_attempts++;
        if (create_attempts < 3) {
            usleep(10000); // 等待10ms后重试
        }
    } while (create_attempts < 3);
    
    if (ret != KV_ERR_NONE) {
        task->status = LSM_COMPACTION_STATUS_FAILED;
        task->error_code = ret;
        snprintf(task->error_message, sizeof(task->error_message),
                 "Failed to create SSTable from MemTable after %d attempts: %d", 
                 create_attempts, ret);
        return ret;
    }
    
    // 验证文件确实创建成功
    if (stat(filename, &st) != 0) {
        task->status = LSM_COMPACTION_STATUS_FAILED;
        task->error_code = KV_ERR_IO_ERROR;
        snprintf(task->error_message, sizeof(task->error_message),
                 "SSTable file was not created successfully: %s", filename);
        return KV_ERR_IO_ERROR;
    }
    
    // 创建SSTable元信息
    lsm_sstable_meta_t *meta = lsm_sstable_meta_create(filename, task->target_level, file_id);
    if (!meta) {
        // 清理失败的文件
        unlink(filename);
        task->status = LSM_COMPACTION_STATUS_FAILED;
        task->error_code = KV_ERR_OUT_OF_MEMORY;
        snprintf(task->error_message, sizeof(task->error_message),
                 "Failed to create SSTable metadata");
        return KV_ERR_OUT_OF_MEMORY;
    }
    
    // 加载文件信息
    meta->file_size = st.st_size;
    ret = lsm_sstable_meta_load_from_file(meta);
    if (ret != KV_ERR_NONE) {
        // 清理失败的文件和元数据
        unlink(filename);
        lsm_sstable_meta_destroy(meta);
        task->status = LSM_COMPACTION_STATUS_FAILED;
        task->error_code = ret;
        snprintf(task->error_message, sizeof(task->error_message),
                 "Failed to load SSTable metadata: %d", ret);
        return ret;
    }
    
    // 安全地添加到层级管理器（这里可能会有并发竞争）
    ret = lsm_level_manager_add_sstable(scheduler->level_manager, task->target_level, meta);
    if (ret != KV_ERR_NONE) {
        // 如果添加失败，清理文件和元数据
        unlink(filename);
        lsm_sstable_meta_destroy(meta);
        task->status = LSM_COMPACTION_STATUS_FAILED;
        task->error_code = ret;
        snprintf(task->error_message, sizeof(task->error_message),
                 "Failed to add SSTable to level manager: %d", ret);
        return ret;
    }
    
    // 设置输出文件信息
    task->output_files = kv_malloc(sizeof(lsm_sstable_meta_t*));
    if (!task->output_files) {
        // 注意：这里不清理meta，因为它已经被layer_manager接管
        task->status = LSM_COMPACTION_STATUS_FAILED;
        task->error_code = KV_ERR_OUT_OF_MEMORY;
        snprintf(task->error_message, sizeof(task->error_message),
                 "Failed to allocate output files array");
        return KV_ERR_OUT_OF_MEMORY;
    }
    task->output_files[0] = meta;
    task->output_file_count = 1;
    
    // 安全地从MemTable管理器中移除已刷写的MemTable
    // 这里需要确保只有一个线程能移除特定的MemTable
    ret = lsm_memtable_manager_remove_specific_immutable(scheduler->memtable_manager, task->input_memtable);
    if (ret != KV_ERR_NONE && ret != KV_ERR_NOT_FOUND) {
        // 如果移除失败但不是"未找到"错误，记录警告但继续
        // 这通常表示另一个线程已经处理了这个MemTable
        snprintf(task->error_message, sizeof(task->error_message),
                 "Warning: Failed to remove MemTable: %d (continuing)", ret);
    }
    
    task->end_time = get_current_time_us();
    task->status = LSM_COMPACTION_STATUS_COMPLETED;
    task->bytes_written = meta->file_size;
    
    // 更新统计信息
    pthread_rwlock_wrlock(&scheduler->stats_lock);
    scheduler->stats.total_compactions++;
    scheduler->stats.level0_compactions++;
    scheduler->stats.bytes_written += task->bytes_written;
    scheduler->stats.total_compaction_time += (task->end_time - task->start_time);
    scheduler->stats.completed_tasks++;
    pthread_rwlock_unlock(&scheduler->stats_lock);
    
    return KV_ERR_NONE;
}

int lsm_compaction_execute_level_n(lsm_compaction_scheduler_t *scheduler,
                                   lsm_compaction_task_t *task) {
    if (!scheduler || !task || !task->input_files || task->input_file_count <= 0) {
        return KV_ERR_INVALID_PARAM;
    }
    
    task->start_time = get_current_time_us();
    task->status = LSM_COMPACTION_STATUS_RUNNING;
    
    // 这里实现Level-N压缩的简化版本
    // 实际实现需要合并多个SSTable文件
    
    // 暂时返回成功，实际实现会更复杂
    task->end_time = get_current_time_us();
    task->status = LSM_COMPACTION_STATUS_COMPLETED;
    
    // 更新统计信息
    pthread_rwlock_wrlock(&scheduler->stats_lock);
    scheduler->stats.total_compactions++;
    scheduler->stats.level_n_compactions++;
    scheduler->stats.total_compaction_time += (task->end_time - task->start_time);
    scheduler->stats.completed_tasks++;
    pthread_rwlock_unlock(&scheduler->stats_lock);
    
    return KV_ERR_NONE;
}

// === 工作线程实现已移至文件末尾 ===

// === 手动压缩函数 ===

int lsm_compaction_scheduler_manual_compact(lsm_compaction_scheduler_t *scheduler, int level) {
    if (!scheduler || level < 0 || level >= LSM_MAX_LEVELS) {
        return KV_ERR_INVALID_PARAM;
    }
    
    // 检查指定层级是否需要压缩
    if (!lsm_level_manager_needs_compaction(scheduler->level_manager, level)) {
        return KV_ERR_NONE; // 不需要压缩
    }
    
    // 创建手动压缩任务
    lsm_compaction_task_t *task = lsm_compaction_task_create(
        LSM_COMPACTION_TASK_MANUAL, 
        LSM_COMPACTION_PRIORITY_HIGH
    );
    
    if (!task) {
        return KV_ERR_OUT_OF_MEMORY;
    }
    
    // 设置任务参数
    task->source_level = level;
    task->target_level = level + 1;
    
    // 提交任务
    int ret = lsm_compaction_scheduler_submit_task(scheduler, task);
    if (ret != KV_ERR_NONE) {
        lsm_compaction_task_destroy(task);
        return ret;
    }
    
    return KV_ERR_NONE;
}

// === 多线程压缩实现 ===

/**
 * 后台压缩工作线程入口函数
 */
void* compaction_worker_thread(void *scheduler_ptr) {
    lsm_compaction_scheduler_t *scheduler = (lsm_compaction_scheduler_t*)scheduler_ptr;
    if (!scheduler) {
        return NULL;
    }
    
    // 设置线程标识用于调试
    char thread_name[32];
    snprintf(thread_name, sizeof(thread_name), "compaction_worker_%lu", 
             (unsigned long)pthread_self());
    pthread_setname_np(pthread_self(), thread_name);
    
    while (true) {
        // 检查是否需要停止
        pthread_mutex_lock(&scheduler->shutdown_lock);
        bool should_shutdown = scheduler->shutdown;
        pthread_mutex_unlock(&scheduler->shutdown_lock);
        
        if (should_shutdown) {
            break;
        }
        
        // 获取下一个任务，超时等待1秒
        lsm_compaction_task_t *task = lsm_compaction_scheduler_get_next_task(scheduler, 1000);
        if (!task) {
            continue; // 超时或被中断，继续循环
        }
        
        // 执行任务
        int result = KV_ERR_NONE;
        task->start_time = get_current_time_us();
        
        // 原子更新任务状态为运行中
        pthread_mutex_lock(&task->lock);
        task->status = LSM_COMPACTION_STATUS_RUNNING;
        pthread_mutex_unlock(&task->lock);
        
        // 更新统计信息
        pthread_rwlock_wrlock(&scheduler->stats_lock);
        scheduler->stats.running_tasks++;
        scheduler->stats.pending_tasks--;
        pthread_rwlock_unlock(&scheduler->stats_lock);
        
        // 根据任务类型执行不同的压缩操作
        switch (task->type) {
            case LSM_COMPACTION_TASK_LEVEL0:
                result = lsm_compaction_execute_level0(scheduler, task);
                break;
            case LSM_COMPACTION_TASK_LEVEL_N:
                result = lsm_compaction_execute_level_n(scheduler, task);
                break;
            case LSM_COMPACTION_TASK_MAJOR:
                // 主压缩任务（预留）
                result = KV_ERR_NOT_IMPLEMENTED;
                break;
            case LSM_COMPACTION_TASK_MANUAL:
                // 手动压缩任务（预留）
                result = KV_ERR_NOT_IMPLEMENTED;
                break;
            default:
                result = KV_ERR_INVALID_PARAM;
                break;
        }
        
        task->end_time = get_current_time_us();
        
        // 标记任务完成
        lsm_compaction_scheduler_complete_task(scheduler, task, result);
    }
    
    return NULL;
}

/**
 * 获取下一个待处理任务（线程安全）
 */
lsm_compaction_task_t* lsm_compaction_scheduler_get_next_task(lsm_compaction_scheduler_t *scheduler,
                                                             uint32_t timeout_ms) {
    if (!scheduler) {
        return NULL;
    }
    
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += timeout_ms / 1000;
    timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (timeout.tv_nsec >= 1000000000) {
        timeout.tv_sec++;
        timeout.tv_nsec -= 1000000000;
    }
    
    pthread_mutex_lock(&scheduler->queue_lock);
    
    while (!scheduler->task_queue_head && !scheduler->shutdown) {
        // 等待新任务或关闭信号
        int wait_result = pthread_cond_timedwait(&scheduler->queue_cond, 
                                                &scheduler->queue_lock, &timeout);
        if (wait_result == ETIMEDOUT) {
            pthread_mutex_unlock(&scheduler->queue_lock);
            return NULL;
        }
    }
    
    // 检查是否需要停止
    if (scheduler->shutdown) {
        pthread_mutex_unlock(&scheduler->queue_lock);
        return NULL;
    }
    
    // 从队列头部取出任务
    lsm_compaction_task_t *task = scheduler->task_queue_head;
    if (task) {
        scheduler->task_queue_head = task->next;
        if (!scheduler->task_queue_head) {
            scheduler->task_queue_tail = NULL;
        }
        task->next = NULL;
    }
    
    pthread_mutex_unlock(&scheduler->queue_lock);
    
    return task;
}

/**
 * 标记任务完成（线程安全）
 */
void lsm_compaction_scheduler_complete_task(lsm_compaction_scheduler_t *scheduler,
                                            lsm_compaction_task_t *task,
                                            int result) {
    if (!scheduler || !task) {
        return;
    }
    
    // 更新任务状态
    pthread_mutex_lock(&task->lock);
    task->error_code = result;
    if (result == KV_ERR_NONE) {
        task->status = LSM_COMPACTION_STATUS_COMPLETED;
    } else {
        task->status = LSM_COMPACTION_STATUS_FAILED;
        snprintf(task->error_message, sizeof(task->error_message), 
                "Compaction failed with error code: %d", result);
    }
    pthread_mutex_unlock(&task->lock);
    
    // 更新统计信息
    pthread_rwlock_wrlock(&scheduler->stats_lock);
    scheduler->stats.running_tasks--;
    if (result == KV_ERR_NONE) {
        scheduler->stats.completed_tasks++;
        scheduler->stats.total_compactions++;
        
        // 更新类型特定的统计
        switch (task->type) {
            case LSM_COMPACTION_TASK_LEVEL0:
                scheduler->stats.level0_compactions++;
                break;
            case LSM_COMPACTION_TASK_LEVEL_N:
                scheduler->stats.level_n_compactions++;
                break;
            default:
                break;
        }
        
        // 更新字节统计
        scheduler->stats.bytes_compacted += task->bytes_read;
        scheduler->stats.bytes_written += task->bytes_written;
        scheduler->stats.total_compaction_time += (task->end_time - task->start_time);
    } else {
        scheduler->stats.failed_tasks++;
    }
    pthread_rwlock_unlock(&scheduler->stats_lock);
    
    // 销毁任务
    lsm_compaction_task_destroy(task);
}

/**
 * 并行执行多个压缩任务
 */
int lsm_compaction_execute_parallel(lsm_compaction_scheduler_t *scheduler,
                                    lsm_compaction_task_t **tasks,
                                    int task_count,
                                    uint32_t timeout_ms) {
    if (!scheduler || !tasks || task_count <= 0) {
        return KV_ERR_INVALID_PARAM;
    }
    
    // 提交所有任务到队列
    for (int i = 0; i < task_count; i++) {
        if (tasks[i]) {
            int ret = lsm_compaction_scheduler_submit_task(scheduler, tasks[i]);
            if (ret != KV_ERR_NONE) {
                return ret;
            }
        }
    }
    
    // 等待所有任务完成
    uint64_t start_time = get_current_time_us();
    uint64_t timeout_us = timeout_ms * 1000UL;
    
    while (true) {
        // 检查超时
        uint64_t elapsed = get_current_time_us() - start_time;
        if (elapsed > timeout_us) {
            return KV_ERR_TIMEOUT;
        }
        
        // 检查所有任务是否完成
        bool all_completed = true;
        int failed_count = 0;
        
        for (int i = 0; i < task_count; i++) {
            if (tasks[i]) {
                pthread_mutex_lock(&tasks[i]->lock);
                lsm_compaction_status_t status = tasks[i]->status;
                pthread_mutex_unlock(&tasks[i]->lock);
                
                if (status == LSM_COMPACTION_STATUS_PENDING || 
                    status == LSM_COMPACTION_STATUS_RUNNING) {
                    all_completed = false;
                    break;
                } else if (status == LSM_COMPACTION_STATUS_FAILED) {
                    failed_count++;
                }
            }
        }
        
        if (all_completed) {
            return failed_count > 0 ? KV_ERR_COMPACTION_FAILED : KV_ERR_NONE;
        }
        
        // 短暂休眠避免CPU占用过高
        usleep(1000); // 1ms
    }
}

/**
 * 检查工作线程健康状态
 */
int lsm_compaction_scheduler_check_worker_health(lsm_compaction_scheduler_t *scheduler) {
    if (!scheduler) {
        return 0;
    }
    
    int healthy_count = 0;
    
    pthread_mutex_lock(&scheduler->shutdown_lock);
    
    if (scheduler->worker_threads && !scheduler->shutdown) {
        // 简单检查：如果线程还在运行，认为是健康的
        // 实际实现可能需要更复杂的健康检查机制
        for (int i = 0; i < scheduler->config.background_thread_count; i++) {
            // 检查线程是否仍然存在
            int result = pthread_kill(scheduler->worker_threads[i], 0);
            if (result == 0) {
                healthy_count++;
            }
        }
    }
    
    pthread_mutex_unlock(&scheduler->shutdown_lock);
    
    return healthy_count;
}



/**
 * 智能任务调度策略 - 避免死锁和资源竞争
 */
static int lsm_compaction_smart_scheduling(lsm_compaction_scheduler_t *scheduler) {
    if (!scheduler) {
        return KV_ERR_INVALID_PARAM;
    }
    
    // 1. 检查系统负载
    int active_tasks = 0;
    pthread_rwlock_rdlock(&scheduler->stats_lock);
    active_tasks = scheduler->stats.running_tasks + scheduler->stats.pending_tasks;
    pthread_rwlock_unlock(&scheduler->stats_lock);
    
    // 如果任务太多，暂停调度
    if (active_tasks > scheduler->config.background_thread_count * 2) {
        return KV_ERR_BUSY;
    }
    
    // 2. 检查资源可用性
    int healthy_workers = lsm_compaction_scheduler_check_worker_health(scheduler);
    if (healthy_workers == 0) {
        return KV_ERR_THREAD_ERROR;
    }
    
    // 3. 优先处理Level-0压缩（避免写入阻塞）
    if ((uint64_t)lsm_level_manager_get_file_count(scheduler->level_manager, 0) > 
        scheduler->config.level0_file_limit) {
        
        // 创建Level-0压缩任务
        lsm_memtable_t *memtable = lsm_memtable_manager_get_oldest_immutable(
            scheduler->memtable_manager);
        if (memtable) {
            lsm_compaction_task_t *task = lsm_compaction_task_create(
                LSM_COMPACTION_TASK_LEVEL0, LSM_COMPACTION_PRIORITY_URGENT);
            if (task) {
                lsm_compaction_task_set_level0_params(task, memtable, 0);
                lsm_compaction_scheduler_submit_task(scheduler, task);
            }
        }
    }
    
    return KV_ERR_NONE;
} 