#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/types.h>
#include <pwd.h>
#include <uuid/uuid.h>

// 字符串操作函数

// 复制字符串
char *concord_util_strdup(const char *str) {
    if (!str) return NULL;
    
    size_t len = strlen(str) + 1;
    char *new_str = (char *)malloc(len);
    if (new_str) {
        memcpy(new_str, str, len);
    }
    return new_str;
}

// 复制指定长度的字符串
char *concord_util_strndup(const char *str, size_t n) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    if (n < len) len = n;
    
    char *new_str = (char *)malloc(len + 1);
    if (new_str) {
        memcpy(new_str, str, len);
        new_str[len] = '\0';
    }
    return new_str;
}

// 修剪字符串前后的空白字符
char *concord_util_trim(char *str) {
    if (!str) return NULL;
    return concord_util_rtrim(concord_util_ltrim(str));
}

// 修剪字符串左侧的空白字符
char *concord_util_ltrim(char *str) {
    if (!str) return NULL;
    
    while (isspace((unsigned char)*str)) {
        str++;
    }
    return str;
}

// 修剪字符串右侧的空白字符
char *concord_util_rtrim(char *str) {
    if (!str) return NULL;
    
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return str;
}

// 分割字符串
int concord_util_split(char *str, char delimiter, char **parts, int max_parts) {
    if (!str || !parts || max_parts <= 0) return 0;
    
    int count = 0;
    char *p = str;
    char *start = p;
    
    while (*p && count < max_parts) {
        if (*p == delimiter) {
            *p = '\0';
            parts[count++] = start;
            start = p + 1;
        }
        p++;
    }
    
    if (*start && count < max_parts) {
        parts[count++] = start;
    }
    
    return count;
}

// 连接字符串
char *concord_util_join(char **parts, int count, const char *delimiter) {
    if (!parts || count <= 0 || !delimiter) return NULL;
    
    // 计算所需的总长度
    size_t total_len = 0;
    size_t delimiter_len = strlen(delimiter);
    
    for (int i = 0; i < count; i++) {
        if (parts[i]) {
            total_len += strlen(parts[i]);
        }
        
        if (i < count - 1) {
            total_len += delimiter_len;
        }
    }
    
    // 分配内存
    char *result = (char *)malloc(total_len + 1);
    if (!result) return NULL;
    
    // 拼接字符串
    char *p = result;
    for (int i = 0; i < count; i++) {
        if (parts[i]) {
            size_t len = strlen(parts[i]);
            memcpy(p, parts[i], len);
            p += len;
        }
        
        if (i < count - 1) {
            memcpy(p, delimiter, delimiter_len);
            p += delimiter_len;
        }
    }
    
    *p = '\0';
    return result;
}

// 替换字符串中的子串
char *concord_util_replace(const char *str, const char *old, const char *new_str) {
    if (!str || !old || !new_str) return NULL;
    
    size_t old_len = strlen(old);
    if (old_len == 0) return concord_util_strdup(str);
    
    size_t new_len = strlen(new_str);
    
    // 计算所需的总长度
    size_t len = strlen(str);
    size_t count = 0;
    const char *p = str;
    
    while ((p = strstr(p, old)) != NULL) {
        count++;
        p += old_len;
    }
    
    if (count == 0) return concord_util_strdup(str);
    
    size_t result_len = len + count * (new_len - old_len);
    char *result = (char *)malloc(result_len + 1);
    if (!result) return NULL;
    
    // 替换子串
    char *dest = result;
    p = str;
    
    while (1) {
        const char *q = strstr(p, old);
        if (!q) break;
        
        size_t prefix_len = q - p;
        memcpy(dest, p, prefix_len);
        dest += prefix_len;
        
        memcpy(dest, new_str, new_len);
        dest += new_len;
        
        p = q + old_len;
    }
    
    // 复制剩余部分
    strcpy(dest, p);
    
    return result;
}

// 文件操作函数

// 检查文件是否存在
int concord_util_file_exists(const char *path) {
    if (!path) return 0;
    
    struct stat st;
    return (stat(path, &st) == 0);
}

// 创建目录
int concord_util_create_dir(const char *path, int mode) {
    if (!path) return -1;
    
    return mkdir(path, mode);
}

// 递归创建目录
int concord_util_create_dirs(const char *path, int mode) {
    if (!path) return -1;
    
    char *path_copy = concord_util_strdup(path);
    if (!path_copy) return -1;
    
    int result = 0;
    char *p = path_copy;
    
    // 跳过根目录
    if (*p == '/') p++;
    
    while (*p) {
        // 查找下一个 '/'
        char *slash = strchr(p, '/');
        if (slash) *slash = '\0';
        
        // 创建当前目录
        char dir[PATH_MAX];
        if (path_copy[0] == '/') {
            snprintf(dir, sizeof(dir), "/%s", path_copy + 1);
        } else {
            snprintf(dir, sizeof(dir), "%s", path_copy);
        }
        
        if (strlen(dir) > 0 && !concord_util_file_exists(dir)) {
            result = mkdir(dir, mode);
            if (result != 0 && errno != EEXIST) {
                break;
            }
        }
        
        if (slash) {
            *slash = '/';
            p = slash + 1;
        } else {
            break;
        }
    }
    
    free(path_copy);
    return result;
}

// 读取文件内容
char *concord_util_read_file(const char *path, size_t *size) {
    if (!path) return NULL;
    
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    
    // 获取文件大小
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // 分配内存
    char *buffer = (char *)malloc(file_size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    
    // 读取文件内容
    size_t read_size = fread(buffer, 1, file_size, file);
    fclose(file);
    
    buffer[read_size] = '\0';
    
    if (size) *size = read_size;
    return buffer;
}

// 写入文件内容
int concord_util_write_file(const char *path, const void *data, size_t size) {
    if (!path || !data) return -1;
    
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    
    size_t written = fwrite(data, 1, size, file);
    fclose(file);
    
    return (written == size) ? 0 : -1;
}

// 追加文件内容
int concord_util_append_file(const char *path, const void *data, size_t size) {
    if (!path || !data) return -1;
    
    FILE *file = fopen(path, "ab");
    if (!file) return -1;
    
    size_t written = fwrite(data, 1, size, file);
    fclose(file);
    
    return (written == size) ? 0 : -1;
}

// 复制文件
int concord_util_copy_file(const char *src, const char *dst) {
    if (!src || !dst) return -1;
    
    // 打开源文件
    int fd_src = open(src, O_RDONLY);
    if (fd_src < 0) return -1;
    
    // 创建目标文件
    int fd_dst = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_dst < 0) {
        close(fd_src);
        return -1;
    }
    
    // 复制数据
    char buffer[8192];
    ssize_t bytes_read;
    while ((bytes_read = read(fd_src, buffer, sizeof(buffer))) > 0) {
        ssize_t bytes_written = write(fd_dst, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            close(fd_src);
            close(fd_dst);
            return -1;
        }
    }
    
    close(fd_src);
    close(fd_dst);
    
    return (bytes_read < 0) ? -1 : 0;
}

// 重命名文件
int concord_util_rename_file(const char *src, const char *dst) {
    if (!src || !dst) return -1;
    
    return rename(src, dst);
}

// 删除文件
int concord_util_remove_file(const char *path) {
    if (!path) return -1;
    
    return unlink(path);
}

// 获取文件名
char *concord_util_get_filename(const char *path) {
    if (!path) return NULL;
    
    const char *slash = strrchr(path, '/');
    if (slash) {
        return concord_util_strdup(slash + 1);
    } else {
        return concord_util_strdup(path);
    }
}

// 获取目录名
char *concord_util_get_dirname(const char *path) {
    if (!path) return NULL;
    
    const char *slash = strrchr(path, '/');
    if (!slash) return concord_util_strdup(".");
    
    if (slash == path) return concord_util_strdup("/");
    
    return concord_util_strndup(path, slash - path);
}

// 获取文件扩展名
char *concord_util_get_extension(const char *path) {
    if (!path) return NULL;
    
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path) return concord_util_strdup("");
    
    return concord_util_strdup(dot + 1);
}

// 获取绝对路径
char *concord_util_get_absolute_path(const char *path) {
    if (!path) return NULL;
    
    char abs_path[PATH_MAX];
    if (realpath(path, abs_path)) {
        return concord_util_strdup(abs_path);
    }
    
    return NULL;
}

// 时间操作函数

// 获取当前时间戳（毫秒）
uint64_t concord_util_time_now() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// 格式化时间
char *concord_util_time_format(time_t time, const char *format) {
    if (!format) format = "%Y-%m-%d %H:%M:%S";
    
    char buffer[128];
    struct tm tm_info;
    
    localtime_r(&time, &tm_info);
    size_t len = strftime(buffer, sizeof(buffer), format, &tm_info);
    
    if (len == 0) return NULL;
    
    return concord_util_strdup(buffer);
}

// 解析时间
time_t concord_util_time_parse(const char *time_str, const char *format) {
    if (!time_str || !format) return 0;
    
    struct tm tm_info;
    memset(&tm_info, 0, sizeof(struct tm));
    
    if (strptime(time_str, format, &tm_info) == NULL) {
        return 0;
    }
    
    return mktime(&tm_info);
}

// 计算时间差（毫秒）
uint64_t concord_util_time_elapsed(uint64_t start) {
    return concord_util_time_now() - start;
}

// 随机数函数

// 初始化随机数生成器
void concord_util_random_init() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    srand(tv.tv_sec ^ tv.tv_usec);
}

// 生成范围内的随机整数
int concord_util_random_int(int min, int max) {
    if (min >= max) return min;
    return min + rand() % (max - min + 1);
}

// 生成范围内的随机浮点数
double concord_util_random_float(double min, double max) {
    if (min >= max) return min;
    return min + (max - min) * ((double)rand() / RAND_MAX);
}

// 生成随机字节
void concord_util_random_bytes(void *buf, size_t len) {
    if (!buf || len == 0) return;
    
    unsigned char *bytes = (unsigned char *)buf;
    for (size_t i = 0; i < len; i++) {
        bytes[i] = (unsigned char)rand();
    }
}

// 生成随机字符串
char *concord_util_random_string(size_t len) {
    if (len == 0) return concord_util_strdup("");
    
    static const char charset[] =
        "0123456789"
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    
    char *str = (char *)malloc(len + 1);
    if (!str) return NULL;
    
    for (size_t i = 0; i < len; i++) {
        int index = rand() % (sizeof(charset) - 1);
        str[i] = charset[index];
    }
    
    str[len] = '\0';
    return str;
}

// 生成UUID
char *concord_util_generate_uuid() {
    uuid_t uuid;
    char *str = (char *)malloc(37);
    if (!str) return NULL;
    
    uuid_generate(uuid);
    uuid_unparse_lower(uuid, str);
    
    return str;
}

// 哈希函数

// DJB2哈希算法
uint32_t concord_util_hash_djb2(const void *data, size_t len) {
    if (!data || len == 0) return 0;
    
    const unsigned char *bytes = (const unsigned char *)data;
    uint32_t hash = 5381;
    
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + bytes[i];
    }
    
    return hash;
}

// FNV-1a哈希算法
uint32_t concord_util_hash_fnv1a(const void *data, size_t len) {
    if (!data || len == 0) return 0;
    
    const unsigned char *bytes = (const unsigned char *)data;
    uint32_t hash = 0x811c9dc5;
    
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 0x01000193;
    }
    
    return hash;
}

// 内存操作

// 复制内存块
void *concord_util_memdup(const void *src, size_t n) {
    if (!src || n == 0) return NULL;
    
    void *dest = malloc(n);
    if (dest) {
        memcpy(dest, src, n);
    }
    return dest;
}

// 日志函数

// 日志级别名称
static const char *log_level_names[] = {
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    "FATAL"
};

// 日志输出函数
void concord_util_log(concord_log_level_t level, const char *fmt, ...) {
    if (level < CONCORD_LOG_DEBUG || level > CONCORD_LOG_FATAL) {
        return;
    }
    
    // 获取当前时间
    time_t now;
    struct tm tm_info;
    char time_str[20];
    
    time(&now);
    localtime_r(&now, &tm_info);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_info);
    
    // 输出日志前缀
    fprintf(stderr, "[%s] [%s] ", time_str, log_level_names[level]);
    
    // 输出日志内容
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    
    fprintf(stderr, "\n");
} 