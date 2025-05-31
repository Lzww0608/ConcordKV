#include <gtest/gtest.h>
#include <string>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
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
kv_error_ctx_t g_error_ctx = {0};

// 函数声明
const char *kv_get_error_name(int code);
const char *kv_get_error_desc(int code);
void kv_format_error(const kv_error_t *error, char *buffer, size_t size);
void kv_log(int level, const char *file, int line, const char *func, const char *fmt, ...);
int kv_error_init(int log_level, const char *log_file, int log_to_stdout);
void kv_set_error(int code, const char *file, int line, const char *func, const char *fmt, ...);
const kv_error_t *kv_get_last_error(void);
void kv_clear_error(void);

// 错误处理宏
#define KV_ERROR(code, fmt, ...) \
    kv_set_error(code, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define KV_CHECK(cond, code, fmt, ...) \
    do { \
        if (!(cond)) { \
            KV_ERROR(code, fmt, ##__VA_ARGS__); \
            return code; \
        } \
    } while (0)

// 函数实现
int kv_error_init(int log_level, const char *log_file, int log_to_stdout) {
    if (log_level < KV_LOG_DEBUG || log_level > KV_LOG_FATAL) {
        return -1;
    }
    
    g_error_ctx.log_level = log_level;
    g_error_ctx.log_to_stdout = log_to_stdout;
    
    // 初始化last_error
    g_error_ctx.last_error.code = KV_ERR_NONE;
    g_error_ctx.last_error.message[0] = '\0';
    g_error_ctx.last_error.file[0] = '\0';
    g_error_ctx.last_error.func[0] = '\0';
    g_error_ctx.last_error.line = 0;
    g_error_ctx.last_error.timestamp = 0;
    
    // 打开日志文件
    if (log_file) {
        g_error_ctx.log_file = fopen(log_file, "w");
        if (!g_error_ctx.log_file) {
            return -1;
        }
    } else {
        g_error_ctx.log_file = NULL;
    }
    
    return 0;
}

void kv_set_error(int code, const char *file, int line, const char *func, const char *fmt, ...) {
    kv_error_t *error = &g_error_ctx.last_error;
    
    error->code = code;
    error->line = line;
    error->timestamp = time(NULL);
    
    if (file) {
        strncpy(error->file, file, sizeof(error->file) - 1);
        error->file[sizeof(error->file) - 1] = '\0';
    } else {
        error->file[0] = '\0';
    }
    
    if (func) {
        strncpy(error->func, func, sizeof(error->func) - 1);
        error->func[sizeof(error->func) - 1] = '\0';
    } else {
        error->func[0] = '\0';
    }
    
    if (fmt) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(error->message, sizeof(error->message), fmt, args);
        va_end(args);
    } else {
        error->message[0] = '\0';
    }
    
    // 记录错误
    kv_log(KV_LOG_ERROR, file, line, func, "%s", error->message);
}

const kv_error_t *kv_get_last_error(void) {
    return &g_error_ctx.last_error;
}

void kv_clear_error(void) {
    g_error_ctx.last_error.code = KV_ERR_NONE;
    g_error_ctx.last_error.message[0] = '\0';
    g_error_ctx.last_error.file[0] = '\0';
    g_error_ctx.last_error.func[0] = '\0';
    g_error_ctx.last_error.line = 0;
    g_error_ctx.last_error.timestamp = 0;
}

void kv_log(int level, const char *file, int line, const char *func, const char *fmt, ...) {
    if (level < g_error_ctx.log_level) {
        return;
    }
    
    const char *level_str = "UNKNOWN";
    switch (level) {
        case KV_LOG_DEBUG: level_str = "DEBUG"; break;
        case KV_LOG_INFO: level_str = "INFO"; break;
        case KV_LOG_WARN: level_str = "WARN"; break;
        case KV_LOG_ERROR: level_str = "ERROR"; break;
        case KV_LOG_FATAL: level_str = "FATAL"; break;
    }
    
    char timestamp[32];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    char message[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    
    char log_line[1280];
    snprintf(log_line, sizeof(log_line), "[%s] %s %s:%d %s: %s\n",
             level_str, timestamp, file, line, func, message);
    
    // 写入日志文件
    if (g_error_ctx.log_file) {
        fputs(log_line, g_error_ctx.log_file);
        fflush(g_error_ctx.log_file);
    }
    
    // 输出到标准输出
    if (g_error_ctx.log_to_stdout) {
        fputs(log_line, stdout);
        fflush(stdout);
    }
}

const char *kv_get_error_name(int code) {
    switch (code) {
        case KV_ERR_NONE: return "KV_ERR_NONE";
        case KV_ERR_SYS: return "KV_ERR_SYS";
        case KV_ERR_MEM: return "KV_ERR_MEM";
        case KV_ERR_PARAM: return "KV_ERR_PARAM";
        case KV_ERR_FILE: return "KV_ERR_FILE";
        case KV_ERR_NET: return "KV_ERR_NET";
        case KV_ERR_NOT_FOUND: return "KV_ERR_NOT_FOUND";
        case KV_ERR_EXIST: return "KV_ERR_EXIST";
        case KV_ERR_TIMEOUT: return "KV_ERR_TIMEOUT";
        case KV_ERR_LOCK: return "KV_ERR_LOCK";
        case KV_ERR_BUSY: return "KV_ERR_BUSY";
        case KV_ERR_NOT_SUPPORTED: return "KV_ERR_NOT_SUPPORTED";
        case KV_ERR_CORRUPTED: return "KV_ERR_CORRUPTED";
        case KV_ERR_DISK_FULL: return "KV_ERR_DISK_FULL";
        case KV_ERR_TXN_CONFLICT: return "KV_ERR_TXN_CONFLICT";
        case KV_ERR_TXN_ABORT: return "KV_ERR_TXN_ABORT";
        case KV_ERR_CLUSTER_SYNC: return "KV_ERR_CLUSTER_SYNC";
        case KV_ERR_NETWORK_PARTITION: return "KV_ERR_NETWORK_PARTITION";
        case KV_ERR_LEADER_CHANGED: return "KV_ERR_LEADER_CHANGED";
        default: return "UNKNOWN_ERROR";
    }
}

const char *kv_get_error_desc(int code) {
    switch (code) {
        case KV_ERR_NONE: return "No error";
        case KV_ERR_SYS: return "System error";
        case KV_ERR_MEM: return "Memory error";
        case KV_ERR_PARAM: return "Parameter error";
        case KV_ERR_FILE: return "File error";
        case KV_ERR_NET: return "Network error";
        case KV_ERR_NOT_FOUND: return "Resource not found";
        case KV_ERR_EXIST: return "Resource already exists";
        case KV_ERR_TIMEOUT: return "Operation timed out";
        case KV_ERR_LOCK: return "Lock error";
        case KV_ERR_BUSY: return "Resource busy";
        case KV_ERR_NOT_SUPPORTED: return "Operation not supported";
        case KV_ERR_CORRUPTED: return "Data corrupted";
        case KV_ERR_DISK_FULL: return "Disk full";
        case KV_ERR_TXN_CONFLICT: return "Transaction conflict";
        case KV_ERR_TXN_ABORT: return "Transaction aborted";
        case KV_ERR_CLUSTER_SYNC: return "Cluster synchronization error";
        case KV_ERR_NETWORK_PARTITION: return "Network partition detected";
        case KV_ERR_LEADER_CHANGED: return "Cluster leader changed";
        default: return "Unknown error";
    }
}

void kv_format_error(const kv_error_t *error, char *buffer, size_t size) {
    if (!error || !buffer || size == 0) return;
    
    snprintf(buffer, size, "Error %d (%s) in %s:%d [%s]: %s",
             error->code,
             kv_get_error_name(error->code),
             error->file,
             error->line,
             error->func,
             error->message);
}

// 错误处理模块测试类
class KvErrorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 清理可能的临时文件
        remove("test_log.txt");
        
        // 初始化错误处理
        ASSERT_EQ(0, kv_error_init(KV_LOG_DEBUG, "test_log.txt", 0));
        
        // 清除可能的上一次错误
        kv_clear_error();
    }
    
    void TearDown() override {
        // 关闭日志文件
        if (g_error_ctx.log_file) {
            fclose(g_error_ctx.log_file);
            g_error_ctx.log_file = NULL;
        }
        
        // 清理临时文件
        remove("test_log.txt");
    }
    
    // 读取日志文件内容
    std::string readLogFile() {
        // 确保日志被写入文件
        if (g_error_ctx.log_file) {
            fflush(g_error_ctx.log_file);
        }
        
        std::ifstream file("test_log.txt");
        if (!file.is_open()) {
            return "";
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        
        return buffer.str();
    }
};

// 测试错误设置和获取
TEST_F(KvErrorTest, SetAndGetError) {
    // 设置一个错误
    kv_set_error(KV_ERR_NOT_FOUND, "test_file.c", 123, "test_func", "Test error message: %s", "key123");
    
    // 获取最近的错误
    const kv_error_t *error = kv_get_last_error();
    
    // 验证错误信息
    ASSERT_NE(nullptr, error);
    ASSERT_EQ(KV_ERR_NOT_FOUND, error->code);
    ASSERT_STREQ("test_file.c", error->file);
    ASSERT_EQ(123, error->line);
    ASSERT_STREQ("test_func", error->func);
    ASSERT_STREQ("Test error message: key123", error->message);
    
    // 验证日志文件是否包含错误信息
    std::string log_content = readLogFile();
    ASSERT_TRUE(log_content.find("ERROR") != std::string::npos);
    ASSERT_TRUE(log_content.find("Test error message: key123") != std::string::npos);
}

// 测试清除错误
TEST_F(KvErrorTest, ClearError) {
    // 设置一个错误
    kv_set_error(KV_ERR_MEM, "test_file.c", 123, "test_func", "Memory allocation failed");
    
    // 验证错误已设置
    const kv_error_t *error = kv_get_last_error();
    ASSERT_EQ(KV_ERR_MEM, error->code);
    
    // 清除错误
    kv_clear_error();
    
    // 验证错误已清除
    error = kv_get_last_error();
    ASSERT_EQ(KV_ERR_NONE, error->code);
}

// 测试不同级别的日志
TEST_F(KvErrorTest, LogLevels) {
    // 关闭当前日志文件
    if (g_error_ctx.log_file) {
        fclose(g_error_ctx.log_file);
        g_error_ctx.log_file = NULL;
    }
    
    // 重新初始化，设置日志级别为INFO
    kv_error_init(KV_LOG_INFO, "test_log.txt", 0);
    
    // 写入不同级别的日志
    kv_log(KV_LOG_DEBUG, "test_file.c", 100, "test_func", "Debug message");
    kv_log(KV_LOG_INFO, "test_file.c", 101, "test_func", "Info message");
    kv_log(KV_LOG_WARN, "test_file.c", 102, "test_func", "Warning message");
    kv_log(KV_LOG_ERROR, "test_file.c", 103, "test_func", "Error message");
    
    // 读取日志内容
    std::string log_content = readLogFile();
    
    // DEBUG级别消息不应该记录
    ASSERT_TRUE(log_content.find("Debug message") == std::string::npos);
    
    // INFO及以上级别消息应该记录
    ASSERT_TRUE(log_content.find("Info message") != std::string::npos);
    ASSERT_TRUE(log_content.find("Warning message") != std::string::npos);
    ASSERT_TRUE(log_content.find("Error message") != std::string::npos);
}

// 测试错误宏
TEST_F(KvErrorTest, ErrorMacros) {
    // 使用宏设置错误
    KV_ERROR(KV_ERR_PARAM, "Invalid parameter: %s", "param1");
    
    // 验证错误已设置
    const kv_error_t *error = kv_get_last_error();
    ASSERT_EQ(KV_ERR_PARAM, error->code);
    ASSERT_STREQ("Invalid parameter: param1", error->message);
    
    // 测试检查条件宏
    int result = 0;
    int param = 0;
    
    // 条件不满足，应设置错误并返回错误码
    result = [&]() -> int {
        KV_CHECK(param > 0, KV_ERR_PARAM, "Parameter must be positive");
        return 0;
    }();
    
    ASSERT_EQ(KV_ERR_PARAM, result);
    error = kv_get_last_error();
    ASSERT_STREQ("Parameter must be positive", error->message);
    
    // 条件满足，应正常执行
    param = 1;
    result = [&]() -> int {
        KV_CHECK(param > 0, KV_ERR_PARAM, "Parameter must be positive");
        return 0;
    }();
    
    ASSERT_EQ(0, result);
}

// 测试错误格式化
TEST_F(KvErrorTest, ErrorFormatting) {
    // 设置一个错误
    kv_set_error(KV_ERR_FILE, "file_ops.c", 42, "open_file", "Cannot open file: %s", "data.txt");
    
    // 获取最近的错误
    const kv_error_t *error = kv_get_last_error();
    
    // 格式化错误信息
    char buffer[256];
    kv_format_error(error, buffer, sizeof(buffer));
    
    // 验证格式化的错误信息
    ASSERT_TRUE(strstr(buffer, "Error 4 (KV_ERR_FILE)") != nullptr);
    ASSERT_TRUE(strstr(buffer, "file_ops.c:42") != nullptr);
    ASSERT_TRUE(strstr(buffer, "open_file") != nullptr);
    ASSERT_TRUE(strstr(buffer, "Cannot open file: data.txt") != nullptr);
}

// 测试错误码和描述
TEST_F(KvErrorTest, ErrorCodesAndDescriptions) {
    // 测试一些常见错误码的名称和描述
    ASSERT_STREQ("KV_ERR_NONE", kv_get_error_name(KV_ERR_NONE));
    ASSERT_STREQ("No error", kv_get_error_desc(KV_ERR_NONE));
    
    ASSERT_STREQ("KV_ERR_NOT_FOUND", kv_get_error_name(KV_ERR_NOT_FOUND));
    ASSERT_STREQ("Resource not found", kv_get_error_desc(KV_ERR_NOT_FOUND));
    
    ASSERT_STREQ("KV_ERR_TXN_CONFLICT", kv_get_error_name(KV_ERR_TXN_CONFLICT));
    ASSERT_STREQ("Transaction conflict", kv_get_error_desc(KV_ERR_TXN_CONFLICT));
    
    // 测试无效错误码
    ASSERT_STREQ("UNKNOWN_ERROR", kv_get_error_name(999));
    ASSERT_STREQ("Unknown error", kv_get_error_desc(999));
}

// 测试日志到标准输出
TEST_F(KvErrorTest, LogToStdout) {
    // 关闭当前日志文件
    if (g_error_ctx.log_file) {
        fclose(g_error_ctx.log_file);
        g_error_ctx.log_file = NULL;
    }
    
    // 重定向标准输出进行测试
    testing::internal::CaptureStdout();
    
    // 初始化错误处理，启用标准输出
    kv_error_init(KV_LOG_INFO, "test_log.txt", 1);
    
    // 记录一条日志
    kv_log(KV_LOG_INFO, "test_file.c", 100, "test_func", "Test stdout logging");
    
    // 获取标准输出内容
    std::string output = testing::internal::GetCapturedStdout();
    
    // 验证日志内容
    ASSERT_TRUE(output.find("INFO") != std::string::npos);
    ASSERT_TRUE(output.find("Test stdout logging") != std::string::npos);
} 