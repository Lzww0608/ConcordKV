#ifndef __CONCORD_LOG_H__
#define __CONCORD_LOG_H__

#include <stdarg.h>

// 日志级别
typedef enum {
    CONCORD_LOG_TRACE = 0,
    CONCORD_LOG_DEBUG,
    CONCORD_LOG_INFO,
    CONCORD_LOG_WARN,
    CONCORD_LOG_ERROR,
    CONCORD_LOG_FATAL,
    CONCORD_LOG_OFF
} concord_log_level_t;

// 日志处理器类型
typedef enum {
    CONCORD_LOG_HANDLER_CONSOLE = 0,  // 控制台输出
    CONCORD_LOG_HANDLER_FILE,         // 文件输出
    CONCORD_LOG_HANDLER_SYSLOG,       // 系统日志
    CONCORD_LOG_HANDLER_CALLBACK      // 回调函数
} concord_log_handler_type_t;

// 日志属性
typedef struct {
    concord_log_level_t level;        // 日志级别
    char *name;                       // 日志名称
    int enabled;                      // 是否启用
    int colored;                      // 是否使用颜色
    int show_timestamp;               // 是否显示时间戳
    int show_level;                   // 是否显示级别
    int show_file_line;               // 是否显示文件和行号
    int show_function;                // 是否显示函数名
    char *timestamp_format;           // 时间戳格式
    int max_file_size;                // 日志文件最大大小（字节）
    int max_backup_count;             // 日志文件最大备份数量
    char *file_path;                  // 日志文件路径
    void (*callback)(concord_log_level_t level, const char *message); // 回调函数
} concord_log_options_t;

// 日志处理器
typedef struct {
    concord_log_handler_type_t type;  // 处理器类型
    concord_log_options_t options;    // 处理器选项
    struct concord_log_handler *next; // 链接到下一个处理器
} concord_log_handler_t;

// 日志上下文
typedef struct {
    char *name;                       // 日志名称
    concord_log_level_t level;        // 日志级别
    concord_log_handler_t *handlers;  // 处理器链表
    struct concord_log_context *next; // 链接到下一个上下文
} concord_log_context_t;

// 初始化日志系统
int concord_log_init(void);

// 关闭日志系统
void concord_log_shutdown(void);

// 创建日志上下文
concord_log_context_t *concord_log_create_context(const char *name);

// 设置日志级别
void concord_log_set_level(concord_log_context_t *ctx, concord_log_level_t level);

// 添加控制台处理器
int concord_log_add_console_handler(concord_log_context_t *ctx, concord_log_options_t *options);

// 添加文件处理器
int concord_log_add_file_handler(concord_log_context_t *ctx, const char *file_path, concord_log_options_t *options);

// 添加系统日志处理器
int concord_log_add_syslog_handler(concord_log_context_t *ctx, const char *ident, concord_log_options_t *options);

// 添加回调处理器
int concord_log_add_callback_handler(concord_log_context_t *ctx, void (*callback)(concord_log_level_t level, const char *message), concord_log_options_t *options);

// 删除处理器
int concord_log_remove_handler(concord_log_context_t *ctx, concord_log_handler_type_t type);

// 日志记录函数
void concord_log_log(concord_log_context_t *ctx, concord_log_level_t level, const char *file, int line, const char *func, const char *fmt, ...);

void concord_log_vlog(concord_log_context_t *ctx, concord_log_level_t level, const char *file, int line, const char *func, const char *fmt, va_list ap);

// 获取默认日志上下文
concord_log_context_t *concord_log_get_default_context(void);

// 便捷宏
#define CONCORD_LOG_TRACE(ctx, ...) \
    concord_log_log(ctx, CONCORD_LOG_TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define CONCORD_LOG_DEBUG(ctx, ...) \
    concord_log_log(ctx, CONCORD_LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define CONCORD_LOG_INFO(ctx, ...) \
    concord_log_log(ctx, CONCORD_LOG_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define CONCORD_LOG_WARN(ctx, ...) \
    concord_log_log(ctx, CONCORD_LOG_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define CONCORD_LOG_ERROR(ctx, ...) \
    concord_log_log(ctx, CONCORD_LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define CONCORD_LOG_FATAL(ctx, ...) \
    concord_log_log(ctx, CONCORD_LOG_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)

// 默认上下文便捷宏
#define LOG_TRACE(...) \
    CONCORD_LOG_TRACE(concord_log_get_default_context(), __VA_ARGS__)

#define LOG_DEBUG(...) \
    CONCORD_LOG_DEBUG(concord_log_get_default_context(), __VA_ARGS__)

#define LOG_INFO(...) \
    CONCORD_LOG_INFO(concord_log_get_default_context(), __VA_ARGS__)

#define LOG_WARN(...) \
    CONCORD_LOG_WARN(concord_log_get_default_context(), __VA_ARGS__)

#define LOG_ERROR(...) \
    CONCORD_LOG_ERROR(concord_log_get_default_context(), __VA_ARGS__)

#define LOG_FATAL(...) \
    CONCORD_LOG_FATAL(concord_log_get_default_context(), __VA_ARGS__)

#endif /* __CONCORD_LOG_H__ */ 