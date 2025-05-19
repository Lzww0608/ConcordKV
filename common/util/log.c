#include "log.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <syslog.h>

// 控制台颜色
#define COLOR_RESET   "\x1b[0m"
#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_WHITE   "\x1b[37m"

// 颜色映射
static const char *level_colors[] = {
    COLOR_BLUE,    // TRACE
    COLOR_CYAN,    // DEBUG
    COLOR_GREEN,   // INFO
    COLOR_YELLOW,  // WARN
    COLOR_MAGENTA, // ERROR
    COLOR_RED,     // FATAL
    COLOR_RESET    // OFF
};

// 日志级别名称
static const char *level_names[] = {
    "TRACE",
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL",
    "OFF"
};

// 系统日志级别映射
static const int syslog_levels[] = {
    LOG_DEBUG,    // TRACE
    LOG_DEBUG,    // DEBUG
    LOG_INFO,     // INFO
    LOG_WARNING,  // WARN
    LOG_ERR,      // ERROR
    LOG_CRIT,     // FATAL
    LOG_DEBUG     // OFF
};

// 全局日志上下文
static concord_log_context_t *g_contexts = NULL;
static concord_log_context_t *g_default_context = NULL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_initialized = 0;

// 默认日志选项
static concord_log_options_t g_default_options = {
    .level = CONCORD_LOG_INFO,
    .name = NULL,
    .enabled = 1,
    .colored = 1,
    .show_timestamp = 1,
    .show_level = 1,
    .show_file_line = 0,
    .show_function = 0,
    .timestamp_format = "%Y-%m-%d %H:%M:%S",
    .max_file_size = 10 * 1024 * 1024, // 10MB
    .max_backup_count = 5,
    .file_path = NULL,
    .callback = NULL
};

// 辅助函数 - 复制日志选项
static void copy_log_options(concord_log_options_t *dst, concord_log_options_t *src) {
    if (!dst || !src) return;
    
    dst->level = src->level;
    dst->enabled = src->enabled;
    dst->colored = src->colored;
    dst->show_timestamp = src->show_timestamp;
    dst->show_level = src->show_level;
    dst->show_file_line = src->show_file_line;
    dst->show_function = src->show_function;
    
    if (dst->name) free(dst->name);
    dst->name = src->name ? concord_util_strdup(src->name) : NULL;
    
    if (dst->timestamp_format) free(dst->timestamp_format);
    dst->timestamp_format = src->timestamp_format ? concord_util_strdup(src->timestamp_format) : NULL;
    
    dst->max_file_size = src->max_file_size;
    dst->max_backup_count = src->max_backup_count;
    
    if (dst->file_path) free(dst->file_path);
    dst->file_path = src->file_path ? concord_util_strdup(src->file_path) : NULL;
    
    dst->callback = src->callback;
}

// 辅助函数 - 创建默认日志选项
static concord_log_options_t *create_default_options() {
    concord_log_options_t *options = (concord_log_options_t *)malloc(sizeof(concord_log_options_t));
    if (!options) return NULL;
    
    memset(options, 0, sizeof(concord_log_options_t));
    options->level = g_default_options.level;
    options->enabled = g_default_options.enabled;
    options->colored = g_default_options.colored;
    options->show_timestamp = g_default_options.show_timestamp;
    options->show_level = g_default_options.show_level;
    options->show_file_line = g_default_options.show_file_line;
    options->show_function = g_default_options.show_function;
    options->name = g_default_options.name ? concord_util_strdup(g_default_options.name) : NULL;
    options->timestamp_format = g_default_options.timestamp_format ? concord_util_strdup(g_default_options.timestamp_format) : NULL;
    options->max_file_size = g_default_options.max_file_size;
    options->max_backup_count = g_default_options.max_backup_count;
    options->file_path = g_default_options.file_path ? concord_util_strdup(g_default_options.file_path) : NULL;
    options->callback = g_default_options.callback;
    
    return options;
}

// 辅助函数 - 释放日志选项
static void free_log_options(concord_log_options_t *options) {
    if (!options) return;
    
    if (options->name) free(options->name);
    if (options->timestamp_format) free(options->timestamp_format);
    if (options->file_path) free(options->file_path);
}

// 辅助函数 - 创建日志处理器
static concord_log_handler_t *create_log_handler(concord_log_handler_type_t type, concord_log_options_t *options) {
    concord_log_handler_t *handler = (concord_log_handler_t *)malloc(sizeof(concord_log_handler_t));
    if (!handler) return NULL;
    
    memset(handler, 0, sizeof(concord_log_handler_t));
    handler->type = type;
    
    if (options) {
        copy_log_options(&handler->options, options);
    } else {
        concord_log_options_t *default_options = create_default_options();
        if (default_options) {
            copy_log_options(&handler->options, default_options);
            free_log_options(default_options);
            free(default_options);
        }
    }
    
    handler->next = NULL;
    
    return handler;
}

// 辅助函数 - 释放日志处理器
static void free_log_handler(concord_log_handler_t *handler) {
    if (!handler) return;
    
    free_log_options(&handler->options);
    free(handler);
}

// 辅助函数 - 格式化日志消息
static char *format_log_message(concord_log_context_t *ctx, concord_log_handler_t *handler, 
                               concord_log_level_t level, const char *file, int line, 
                               const char *func, const char *fmt, va_list args) {
    if (!ctx || !handler || !fmt) return NULL;
    
    char timestamp[64] = {0};
    char level_str[16] = {0};
    char file_line_str[256] = {0};
    char function_str[128] = {0};
    char message[4096] = {0};
    char prefix[512] = {0};
    char full_message[4096] = {0};
    
    // 格式化消息内容
    vsnprintf(message, sizeof(message), fmt, args);
    
    // 格式化时间戳
    if (handler->options.show_timestamp) {
        time_t now;
        struct tm tm_info;
        
        time(&now);
        localtime_r(&now, &tm_info);
        
        const char *timestamp_format = handler->options.timestamp_format ? 
                                      handler->options.timestamp_format : "%Y-%m-%d %H:%M:%S";
        
        strftime(timestamp, sizeof(timestamp), timestamp_format, &tm_info);
    }
    
    // 格式化日志级别
    if (handler->options.show_level) {
        snprintf(level_str, sizeof(level_str), "%s", level_names[level]);
    }
    
    // 格式化文件和行号
    if (handler->options.show_file_line && file) {
        snprintf(file_line_str, sizeof(file_line_str), "%s:%d", file, line);
    }
    
    // 格式化函数名
    if (handler->options.show_function && func) {
        snprintf(function_str, sizeof(function_str), "%s()", func);
    }
    
    // 组合前缀
    if (handler->options.show_timestamp && timestamp[0]) {
        strcat(prefix, "[");
        strcat(prefix, timestamp);
        strcat(prefix, "] ");
    }
    
    if (handler->options.show_level && level_str[0]) {
        strcat(prefix, "[");
        strcat(prefix, level_str);
        strcat(prefix, "] ");
    }
    
    if (ctx->name) {
        strcat(prefix, "[");
        strcat(prefix, ctx->name);
        strcat(prefix, "] ");
    }
    
    if (handler->options.show_file_line && file_line_str[0]) {
        strcat(prefix, "[");
        strcat(prefix, file_line_str);
        strcat(prefix, "] ");
    }
    
    if (handler->options.show_function && function_str[0]) {
        strcat(prefix, "[");
        strcat(prefix, function_str);
        strcat(prefix, "] ");
    }
    
    // 组合完整消息
    if (prefix[0]) {
        snprintf(full_message, sizeof(full_message), "%s%s", prefix, message);
    } else {
        snprintf(full_message, sizeof(full_message), "%s", message);
    }
    
    return concord_util_strdup(full_message);
}

// 辅助函数 - 轮转日志文件
static int rotate_log_file(const char *file_path, int max_backup_count) {
    if (!file_path || max_backup_count <= 0) return -1;
    
    // 检查文件是否存在
    if (!concord_util_file_exists(file_path)) {
        return 0;  // 文件不存在，不需要轮转
    }
    
    // 删除最老的备份
    char old_path[PATH_MAX];
    snprintf(old_path, sizeof(old_path), "%s.%d", file_path, max_backup_count);
    unlink(old_path);
    
    // 重命名现有的备份
    for (int i = max_backup_count - 1; i >= 1; i--) {
        char src_path[PATH_MAX];
        char dst_path[PATH_MAX];
        
        snprintf(src_path, sizeof(src_path), "%s.%d", file_path, i);
        snprintf(dst_path, sizeof(dst_path), "%s.%d", file_path, i + 1);
        
        if (concord_util_file_exists(src_path)) {
            rename(src_path, dst_path);
        }
    }
    
    // 重命名当前日志文件
    char new_path[PATH_MAX];
    snprintf(new_path, sizeof(new_path), "%s.1", file_path);
    rename(file_path, new_path);
    
    return 0;
}

// 辅助函数 - 检查日志文件大小
static int check_log_file_size(const char *file_path, int max_size, int max_backup_count) {
    if (!file_path || max_size <= 0) return 0;
    
    // 检查文件是否存在
    if (!concord_util_file_exists(file_path)) {
        return 0;  // 文件不存在，不需要轮转
    }
    
    // 获取文件大小
    struct stat st;
    if (stat(file_path, &st) != 0) {
        return -1;
    }
    
    // 检查是否需要轮转
    if (st.st_size >= max_size) {
        return rotate_log_file(file_path, max_backup_count);
    }
    
    return 0;
}

// 初始化日志系统
int concord_log_init(void) {
    pthread_mutex_lock(&g_log_mutex);
    
    if (g_initialized) {
        pthread_mutex_unlock(&g_log_mutex);
        return 0;
    }
    
    // 创建默认上下文
    g_default_context = concord_log_create_context("default");
    if (!g_default_context) {
        pthread_mutex_unlock(&g_log_mutex);
        return -1;
    }
    
    // 添加默认控制台处理器
    if (concord_log_add_console_handler(g_default_context, NULL) != 0) {
        free(g_default_context);
        g_default_context = NULL;
        pthread_mutex_unlock(&g_log_mutex);
        return -1;
    }
    
    g_initialized = 1;
    
    pthread_mutex_unlock(&g_log_mutex);
    return 0;
}

// 关闭日志系统
void concord_log_shutdown(void) {
    pthread_mutex_lock(&g_log_mutex);
    
    if (!g_initialized) {
        pthread_mutex_unlock(&g_log_mutex);
        return;
    }
    
    // 释放所有上下文
    concord_log_context_t *ctx = g_contexts;
    while (ctx) {
        concord_log_context_t *next = ctx->next;
        
        // 释放所有处理器
        concord_log_handler_t *handler = ctx->handlers;
        while (handler) {
            concord_log_handler_t *next_handler = handler->next;
            free_log_handler(handler);
            handler = next_handler;
        }
        
        if (ctx->name) free(ctx->name);
        free(ctx);
        
        ctx = next;
    }
    
    g_contexts = NULL;
    g_default_context = NULL;
    g_initialized = 0;
    
    pthread_mutex_unlock(&g_log_mutex);
}

// 创建日志上下文
concord_log_context_t *concord_log_create_context(const char *name) {
    if (!name) name = "unnamed";
    
    pthread_mutex_lock(&g_log_mutex);
    
    // 检查是否已存在同名上下文
    concord_log_context_t *ctx = g_contexts;
    while (ctx) {
        if (ctx->name && strcmp(ctx->name, name) == 0) {
            pthread_mutex_unlock(&g_log_mutex);
            return ctx;
        }
        ctx = ctx->next;
    }
    
    // 创建新上下文
    ctx = (concord_log_context_t *)malloc(sizeof(concord_log_context_t));
    if (!ctx) {
        pthread_mutex_unlock(&g_log_mutex);
        return NULL;
    }
    
    memset(ctx, 0, sizeof(concord_log_context_t));
    ctx->name = concord_util_strdup(name);
    ctx->level = g_default_options.level;
    ctx->handlers = NULL;
    ctx->next = NULL;
    
    // 添加到全局链表
    if (!g_contexts) {
        g_contexts = ctx;
    } else {
        concord_log_context_t *last = g_contexts;
        while (last->next) {
            last = last->next;
        }
        last->next = ctx;
    }
    
    // 如果没有默认上下文，设置为默认
    if (!g_default_context) {
        g_default_context = ctx;
    }
    
    pthread_mutex_unlock(&g_log_mutex);
    return ctx;
}

// 设置日志级别
void concord_log_set_level(concord_log_context_t *ctx, concord_log_level_t level) {
    if (!ctx) return;
    
    pthread_mutex_lock(&g_log_mutex);
    ctx->level = level;
    pthread_mutex_unlock(&g_log_mutex);
}

// 添加控制台处理器
int concord_log_add_console_handler(concord_log_context_t *ctx, concord_log_options_t *options) {
    if (!ctx) return -1;
    
    pthread_mutex_lock(&g_log_mutex);
    
    // 创建处理器
    concord_log_handler_t *handler = create_log_handler(CONCORD_LOG_HANDLER_CONSOLE, options);
    if (!handler) {
        pthread_mutex_unlock(&g_log_mutex);
        return -1;
    }
    
    // 设置默认值
    if (!options) {
        handler->options.colored = 1;  // 默认启用颜色
    }
    
    // 添加到处理器链表
    if (!ctx->handlers) {
        ctx->handlers = handler;
    } else {
        concord_log_handler_t *last = ctx->handlers;
        while (last->next) {
            last = last->next;
        }
        last->next = handler;
    }
    
    pthread_mutex_unlock(&g_log_mutex);
    return 0;
}

// 添加文件处理器
int concord_log_add_file_handler(concord_log_context_t *ctx, const char *file_path, concord_log_options_t *options) {
    if (!ctx || !file_path) return -1;
    
    pthread_mutex_lock(&g_log_mutex);
    
    // 创建处理器
    concord_log_handler_t *handler = create_log_handler(CONCORD_LOG_HANDLER_FILE, options);
    if (!handler) {
        pthread_mutex_unlock(&g_log_mutex);
        return -1;
    }
    
    // 设置文件路径
    if (handler->options.file_path) free(handler->options.file_path);
    handler->options.file_path = concord_util_strdup(file_path);
    
    // 添加到处理器链表
    if (!ctx->handlers) {
        ctx->handlers = handler;
    } else {
        concord_log_handler_t *last = ctx->handlers;
        while (last->next) {
            last = last->next;
        }
        last->next = handler;
    }
    
    pthread_mutex_unlock(&g_log_mutex);
    return 0;
}

// 添加系统日志处理器
int concord_log_add_syslog_handler(concord_log_context_t *ctx, const char *ident, concord_log_options_t *options) {
    if (!ctx) return -1;
    
    pthread_mutex_lock(&g_log_mutex);
    
    // 创建处理器
    concord_log_handler_t *handler = create_log_handler(CONCORD_LOG_HANDLER_SYSLOG, options);
    if (!handler) {
        pthread_mutex_unlock(&g_log_mutex);
        return -1;
    }
    
    // 设置标识
    if (handler->options.name) free(handler->options.name);
    handler->options.name = ident ? concord_util_strdup(ident) : (ctx->name ? concord_util_strdup(ctx->name) : concord_util_strdup("concordkv"));
    
    // 打开系统日志
    openlog(handler->options.name, LOG_PID, LOG_USER);
    
    // 添加到处理器链表
    if (!ctx->handlers) {
        ctx->handlers = handler;
    } else {
        concord_log_handler_t *last = ctx->handlers;
        while (last->next) {
            last = last->next;
        }
        last->next = handler;
    }
    
    pthread_mutex_unlock(&g_log_mutex);
    return 0;
}

// 添加回调处理器
int concord_log_add_callback_handler(concord_log_context_t *ctx, void (*callback)(concord_log_level_t level, const char *message), concord_log_options_t *options) {
    if (!ctx || !callback) return -1;
    
    pthread_mutex_lock(&g_log_mutex);
    
    // 创建处理器
    concord_log_handler_t *handler = create_log_handler(CONCORD_LOG_HANDLER_CALLBACK, options);
    if (!handler) {
        pthread_mutex_unlock(&g_log_mutex);
        return -1;
    }
    
    // 设置回调函数
    handler->options.callback = callback;
    
    // 添加到处理器链表
    if (!ctx->handlers) {
        ctx->handlers = handler;
    } else {
        concord_log_handler_t *last = ctx->handlers;
        while (last->next) {
            last = last->next;
        }
        last->next = handler;
    }
    
    pthread_mutex_unlock(&g_log_mutex);
    return 0;
}

// 删除处理器
int concord_log_remove_handler(concord_log_context_t *ctx, concord_log_handler_type_t type) {
    if (!ctx) return -1;
    
    pthread_mutex_lock(&g_log_mutex);
    
    concord_log_handler_t *handler = ctx->handlers;
    concord_log_handler_t *prev = NULL;
    
    while (handler) {
        if (handler->type == type) {
            if (prev) {
                prev->next = handler->next;
            } else {
                ctx->handlers = handler->next;
            }
            
            concord_log_handler_t *to_free = handler;
            handler = handler->next;
            
            free_log_handler(to_free);
        } else {
            prev = handler;
            handler = handler->next;
        }
    }
    
    pthread_mutex_unlock(&g_log_mutex);
    return 0;
}

// 日志记录函数
void concord_log_log(concord_log_context_t *ctx, concord_log_level_t level, const char *file, int line, const char *func, const char *fmt, ...) {
    if (!ctx || !fmt || level < CONCORD_LOG_TRACE || level > CONCORD_LOG_FATAL) return;
    
    // 检查日志级别
    if (level < ctx->level) return;
    
    va_list args;
    va_start(args, fmt);
    concord_log_vlog(ctx, level, file, line, func, fmt, args);
    va_end(args);
}

// 日志记录函数 (va_list版本)
void concord_log_vlog(concord_log_context_t *ctx, concord_log_level_t level, const char *file, int line, const char *func, const char *fmt, va_list ap) {
    if (!ctx || !fmt || level < CONCORD_LOG_TRACE || level > CONCORD_LOG_FATAL) return;
    
    // 检查日志级别
    if (level < ctx->level) return;
    
    pthread_mutex_lock(&g_log_mutex);
    
    // 遍历所有处理器
    concord_log_handler_t *handler = ctx->handlers;
    while (handler) {
        // 检查处理器是否启用且级别匹配
        if (handler->options.enabled && level >= handler->options.level) {
            // 拷贝va_list，因为每个处理器都需要使用
            va_list args_copy;
            va_copy(args_copy, ap);
            
            // 格式化消息
            char *message = format_log_message(ctx, handler, level, file, line, func, fmt, args_copy);
            va_end(args_copy);
            
            if (message) {
                // 根据处理器类型处理日志
                switch (handler->type) {
                    case CONCORD_LOG_HANDLER_CONSOLE: {
                        FILE *stream = (level >= CONCORD_LOG_WARN) ? stderr : stdout;
                        
                        if (handler->options.colored) {
                            fprintf(stream, "%s%s%s\n", level_colors[level], message, COLOR_RESET);
                        } else {
                            fprintf(stream, "%s\n", message);
                        }
                        fflush(stream);
                        break;
                    }
                    
                    case CONCORD_LOG_HANDLER_FILE: {
                        if (handler->options.file_path) {
                            // 检查是否需要轮转日志
                            check_log_file_size(handler->options.file_path, 
                                              handler->options.max_file_size, 
                                              handler->options.max_backup_count);
                            
                            // 追加到日志文件
                            FILE *file = fopen(handler->options.file_path, "a");
                            if (file) {
                                fprintf(file, "%s\n", message);
                                fclose(file);
                            }
                        }
                        break;
                    }
                    
                    case CONCORD_LOG_HANDLER_SYSLOG: {
                        syslog(syslog_levels[level], "%s", message);
                        break;
                    }
                    
                    case CONCORD_LOG_HANDLER_CALLBACK: {
                        if (handler->options.callback) {
                            handler->options.callback(level, message);
                        }
                        break;
                    }
                }
                
                free(message);
            }
        }
        
        handler = handler->next;
    }
    
    pthread_mutex_unlock(&g_log_mutex);
}

// 获取默认日志上下文
concord_log_context_t *concord_log_get_default_context(void) {
    // 如果未初始化，进行初始化
    if (!g_initialized) {
        concord_log_init();
    }
    
    return g_default_context;
} 