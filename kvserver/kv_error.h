/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-11 17:09:22
* @Description: ConcordKV storage engine - kv_error.h
 */
#ifndef __KV_ERROR_H__
#define __KV_ERROR_H__

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

// 错误级别
#define KV_LOG_DEBUG     0
#define KV_LOG_INFO      1
#define KV_LOG_WARN      2
#define KV_LOG_ERROR     3
#define KV_LOG_FATAL     4

// 错误类型
#define KV_ERR_NONE              0   // 无错误
#define KV_ERR_SYS               1   // 系统错误
#define KV_ERR_MEM               2   // 内存错误
#define KV_ERR_PARAM             3   // 参数错误
#define KV_ERR_FILE              4   // 文件错误
#define KV_ERR_NET               5   // 网络错误
#define KV_ERR_NOT_FOUND         6   // 未找到
#define KV_ERR_EXIST             7   // 已存在
#define KV_ERR_TIMEOUT           8   // 超时
#define KV_ERR_LOCK              9   // 锁错误
#define KV_ERR_BUSY              10  // 资源忙
#define KV_ERR_NOT_SUPPORTED     11  // 不支持
#define KV_ERR_CORRUPTED         12  // 数据损坏
#define KV_ERR_DISK_FULL         13  // 磁盘满
#define KV_ERR_TXN_CONFLICT      14  // 事务冲突
#define KV_ERR_TXN_ABORT         15  // 事务中止
#define KV_ERR_CLUSTER_SYNC      16  // 集群同步错误
#define KV_ERR_NETWORK_PARTITION 17  // 网络分区
#define KV_ERR_LEADER_CHANGED    18  // 领导者变更

// LSM-Tree 相关错误码
#define KV_ERR_INVALID_PARAM     19  // 无效参数
#define KV_ERR_OUT_OF_MEMORY     20  // 内存不足
#define KV_ERR_IO_ERROR          21  // I/O错误
#define KV_ERR_NO_SPACE          22  // 空间不足
#define KV_ERR_THREAD_ERROR      23  // 线程错误
#define KV_ERR_INVALID_STATE     24  // 无效状态
#define KV_ERR_NOT_IMPLEMENTED   25  // 功能未实现
#define KV_ERR_COMPACTION_FAILED 26  // 压缩失败

// 批量操作相关错误码
#define KV_ERR_BATCH_FULL        27  // 批次已满
#define KV_ERR_BATCH_TOO_LARGE   28  // 批次过大

// 缓存相关错误码
#define KV_SUCCESS               0   // 成功
#define KV_ERR_KEY_NOT_FOUND     29  // 键未找到
#define KV_ERR_KEY_EXISTS        30  // 键已存在
#define KV_ERR_KEY_TOO_LONG      31  // 键过长
#define KV_ERR_MEMORY_ALLOC      32  // 内存分配失败
#define KV_ERR_LOCK_FAILED       33  // 锁操作失败

// I/O优化器相关错误码
#define KV_ERR_IO                34  // I/O操作错误
#define KV_ERR_OVERFLOW          35  // 缓冲区溢出
#define KV_ERR_UNDERFLOW         36  // 缓冲区下溢
#define KV_ERR_ALIGNMENT         37  // 内存对齐错误
#define KV_ERR_DEVICE            38  // 设备错误
#define KV_ERR_ASYNC_FAILED      39  // 异步操作失败
#define KV_ERR_BATCH_FAILED      40  // 批量操作失败
#define KV_ERR_MMAP_FAILED       41  // 内存映射失败
#define KV_ERR_STATE             42  // 状态错误
#define KV_ERR_CONFIG            43  // 配置错误

// 异步批量I/O相关错误码
#define KV_ERR_NULL_POINTER      44  // 空指针错误
#define KV_ERR_THREAD_CREATE     45  // 线程创建失败
#define KV_ERR_CANCELLED         46  // 操作已取消

#define KV_ERR_UNKNOWN           100 // 未知错误

// 错误信息结构
typedef struct {
    int code;               // 错误码
    char message[256];      // 错误消息
    char func[64];          // 函数名
    char file[128];         // 文件名
    int line;               // 行号
    time_t timestamp;       // 时间戳
} kv_error_t;

// 错误上下文
typedef struct {
    kv_error_t last_error;  // 最近的错误
    int log_level;          // 日志级别
    FILE *log_file;         // 日志文件
    int log_to_stdout;      // 是否输出到标准输出
} kv_error_ctx_t;

// 全局错误上下文
extern kv_error_ctx_t g_error_ctx;

// 初始化错误处理
int kv_error_init(int log_level, const char *log_file, int log_to_stdout);

// 设置错误
void kv_set_error(int code, const char *file, int line, const char *func, const char *fmt, ...);

// 获取最近的错误
const kv_error_t *kv_get_last_error(void);

// 清除错误
void kv_clear_error(void);

// 记录日志
void kv_log(int level, const char *file, int line, const char *func, const char *fmt, ...);

// 错误处理宏
#define KV_ERROR(code, fmt, ...) \
    kv_set_error(code, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define KV_LOG_DEBUG_MSG(fmt, ...) \
    kv_log(KV_LOG_DEBUG, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define KV_LOG_INFO_MSG(fmt, ...) \
    kv_log(KV_LOG_INFO, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define KV_LOG_WARN_MSG(fmt, ...) \
    kv_log(KV_LOG_WARN, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define KV_LOG_ERROR_MSG(fmt, ...) \
    kv_log(KV_LOG_ERROR, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define KV_LOG_FATAL_MSG(fmt, ...) \
    kv_log(KV_LOG_FATAL, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

// 检查条件并设置错误
#define KV_CHECK(cond, code, fmt, ...) \
    do { \
        if (!(cond)) { \
            KV_ERROR(code, fmt, ##__VA_ARGS__); \
            return code; \
        } \
    } while (0)

// 检查条件并设置错误，返回NULL
#define KV_CHECK_NULL(cond, code, fmt, ...) \
    do { \
        if (!(cond)) { \
            KV_ERROR(code, fmt, ##__VA_ARGS__); \
            return NULL; \
        } \
    } while (0)

// 检查条件并记录错误
#define KV_CHECK_LOG(cond, level, fmt, ...) \
    do { \
        if (!(cond)) { \
            kv_log(level, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
        } \
    } while (0)

#endif // __KV_ERROR_H__ 