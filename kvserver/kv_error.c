/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV storage engine - kv_error.c
 */
#include "kv_error.h"
#include <pthread.h>

// 全局错误上下文
kv_error_ctx_t g_error_ctx = {0};

// 锁保护错误上下文
static pthread_mutex_t g_error_mutex = PTHREAD_MUTEX_INITIALIZER;

// 日志级别名称
static const char *log_level_names[] = {
    "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

// 错误码名称
static const char *error_code_names[] = {
    "KV_ERR_NONE",
    "KV_ERR_SYS",
    "KV_ERR_MEM",
    "KV_ERR_PARAM",
    "KV_ERR_FILE",
    "KV_ERR_NET",
    "KV_ERR_NOT_FOUND",
    "KV_ERR_EXIST",
    "KV_ERR_TIMEOUT",
    "KV_ERR_LOCK",
    "KV_ERR_BUSY",
    "KV_ERR_NOT_SUPPORTED",
    "KV_ERR_CORRUPTED",
    "KV_ERR_DISK_FULL",
    "KV_ERR_TXN_CONFLICT",
    "KV_ERR_TXN_ABORT",
    "KV_ERR_CLUSTER_SYNC",
    "KV_ERR_NETWORK_PARTITION",
    "KV_ERR_LEADER_CHANGED",
    "KV_ERR_UNKNOWN"
};

// 初始化错误处理
int kv_error_init(int log_level, const char *log_file, int log_to_stdout) {
    pthread_mutex_lock(&g_error_mutex);
    
    g_error_ctx.log_level = log_level;
    g_error_ctx.log_to_stdout = log_to_stdout;
    
    // 清除上一次可能的错误
    memset(&g_error_ctx.last_error, 0, sizeof(kv_error_t));
    
    // 关闭之前的日志文件
    if (g_error_ctx.log_file) {
        fclose(g_error_ctx.log_file);
        g_error_ctx.log_file = NULL;
    }
    
    // 如果指定了日志文件，打开它
    if (log_file) {
        g_error_ctx.log_file = fopen(log_file, "a");
        if (!g_error_ctx.log_file) {
            pthread_mutex_unlock(&g_error_mutex);
            return -1;
        }
        
        // 设置无缓冲模式，确保日志即时写入
        setbuf(g_error_ctx.log_file, NULL);
    }
    
    pthread_mutex_unlock(&g_error_mutex);
    return 0;
}

// 设置错误
void kv_set_error(int code, const char *file, int line, const char *func, const char *fmt, ...) {
    pthread_mutex_lock(&g_error_mutex);
    
    kv_error_t *error = &g_error_ctx.last_error;
    
    error->code = code;
    strncpy(error->file, file, sizeof(error->file) - 1);
    error->line = line;
    strncpy(error->func, func, sizeof(error->func) - 1);
    error->timestamp = time(NULL);
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(error->message, sizeof(error->message), fmt, args);
    va_end(args);
    
    // 同时记录错误日志
    kv_log(KV_LOG_ERROR, file, line, func, "Error %d: %s", code, error->message);
    
    pthread_mutex_unlock(&g_error_mutex);
}

// 获取最近的错误
const kv_error_t *kv_get_last_error(void) {
    return &g_error_ctx.last_error;
}

// 清除错误
void kv_clear_error(void) {
    pthread_mutex_lock(&g_error_mutex);
    memset(&g_error_ctx.last_error, 0, sizeof(kv_error_t));
    pthread_mutex_unlock(&g_error_mutex);
}

// 记录日志
void kv_log(int level, const char *file, int line, const char *func, const char *fmt, ...) {
    if (level < g_error_ctx.log_level) {
        return;
    }
    
    pthread_mutex_lock(&g_error_mutex);
    
    // 格式化时间
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_now);
    
    // 准备日志消息
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    
    // 构建完整日志条目
    char log_entry[1536];
    int thread_id = (int)pthread_self();
    snprintf(log_entry, sizeof(log_entry), 
             "[%s] [%s] [%s:%d] [%s] [T-%d] %s\n",
             time_str, log_level_names[level], file, line, func, thread_id, message);
    
    // 写入日志文件
    if (g_error_ctx.log_file) {
        fputs(log_entry, g_error_ctx.log_file);
    }
    
    // 输出到标准输出
    if (g_error_ctx.log_to_stdout) {
        fputs(log_entry, stdout);
    }
    
    pthread_mutex_unlock(&g_error_mutex);
}

// 获取错误码名称
const char *kv_get_error_name(int code) {
    if (code < 0 || code >= (int)(sizeof(error_code_names)/sizeof(error_code_names[0]))) {
        return "UNKNOWN_ERROR";
    }
    return error_code_names[code];
}

// 获取错误描述
const char *kv_get_error_desc(int code) {
    switch (code) {
        case KV_ERR_NONE:
            return "No error";
        case KV_ERR_SYS:
            return "System error";
        case KV_ERR_MEM:
            return "Memory allocation error";
        case KV_ERR_PARAM:
            return "Invalid parameter";
        case KV_ERR_FILE:
            return "File operation error";
        case KV_ERR_NET:
            return "Network error";
        case KV_ERR_NOT_FOUND:
            return "Resource not found";
        case KV_ERR_EXIST:
            return "Resource already exists";
        case KV_ERR_TIMEOUT:
            return "Operation timeout";
        case KV_ERR_LOCK:
            return "Lock error";
        case KV_ERR_BUSY:
            return "Resource busy";
        case KV_ERR_NOT_SUPPORTED:
            return "Operation not supported";
        case KV_ERR_CORRUPTED:
            return "Data corrupted";
        case KV_ERR_DISK_FULL:
            return "Disk full";
        case KV_ERR_TXN_CONFLICT:
            return "Transaction conflict";
        case KV_ERR_TXN_ABORT:
            return "Transaction aborted";
        case KV_ERR_CLUSTER_SYNC:
            return "Cluster synchronization error";
        case KV_ERR_NETWORK_PARTITION:
            return "Network partition detected";
        case KV_ERR_LEADER_CHANGED:
            return "Cluster leader changed";
        case KV_ERR_UNKNOWN:
        default:
            return "Unknown error";
    }
}

// 格式化错误信息
void kv_format_error(const kv_error_t *error, char *buffer, size_t buffer_size) {
    if (!error || !buffer || buffer_size == 0) {
        return;
    }
    
    struct tm tm_time;
    localtime_r(&error->timestamp, &tm_time);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_time);
    
    snprintf(buffer, buffer_size,
             "Error %d (%s) at %s:%d in %s: %s [%s]",
             error->code, kv_get_error_name(error->code),
             error->file, error->line, error->func,
             error->message, time_str);
}

// 打印错误信息
void kv_print_error(const kv_error_t *error) {
    if (!error) {
        return;
    }
    
    char buffer[1024];
    kv_format_error(error, buffer, sizeof(buffer));
    fprintf(stderr, "%s\n", buffer);
} 