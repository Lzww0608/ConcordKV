#ifndef __CONCORD_UTIL_H__
#define __CONCORD_UTIL_H__

#include <stdint.h>
#include <stddef.h>
#include <time.h>

// 字符串操作
char *concord_util_strdup(const char *str);
char *concord_util_strndup(const char *str, size_t n);
char *concord_util_trim(char *str);
char *concord_util_ltrim(char *str);
char *concord_util_rtrim(char *str);
int concord_util_split(char *str, char delimiter, char **parts, int max_parts);
char *concord_util_join(char **parts, int count, const char *delimiter);
char *concord_util_replace(const char *str, const char *old, const char *new_str);

// 文件操作
int concord_util_file_exists(const char *path);
int concord_util_create_dir(const char *path, int mode);
int concord_util_create_dirs(const char *path, int mode);
char *concord_util_read_file(const char *path, size_t *size);
int concord_util_write_file(const char *path, const void *data, size_t size);
int concord_util_append_file(const char *path, const void *data, size_t size);
int concord_util_copy_file(const char *src, const char *dst);
int concord_util_rename_file(const char *src, const char *dst);
int concord_util_remove_file(const char *path);
char *concord_util_get_filename(const char *path);
char *concord_util_get_dirname(const char *path);
char *concord_util_get_extension(const char *path);
char *concord_util_get_absolute_path(const char *path);

// 时间操作
uint64_t concord_util_time_now();  // 返回毫秒级时间戳
char *concord_util_time_format(time_t time, const char *format);
time_t concord_util_time_parse(const char *time_str, const char *format);
uint64_t concord_util_time_elapsed(uint64_t start);  // 计算时间差（毫秒）

// 随机数
void concord_util_random_init();  // 初始化随机数生成器
int concord_util_random_int(int min, int max);
double concord_util_random_float(double min, double max);
void concord_util_random_bytes(void *buf, size_t len);
char *concord_util_random_string(size_t len);
char *concord_util_generate_uuid();

// 哈希和加密
uint32_t concord_util_hash_djb2(const void *data, size_t len);
uint32_t concord_util_hash_fnv1a(const void *data, size_t len);
uint64_t concord_util_hash_murmur3(const void *data, size_t len, uint64_t seed);
char *concord_util_md5(const void *data, size_t len);
char *concord_util_sha1(const void *data, size_t len);
char *concord_util_sha256(const void *data, size_t len);
char *concord_util_base64_encode(const void *data, size_t len);
void *concord_util_base64_decode(const char *str, size_t *out_len);

// 压缩与解压缩
void *concord_util_compress(const void *data, size_t len, size_t *out_len);
void *concord_util_decompress(const void *data, size_t len, size_t *out_len);

// 编码转换
char *concord_util_utf8_to_gbk(const char *utf8);
char *concord_util_gbk_to_utf8(const char *gbk);

// 网络操作
int concord_util_is_ipv4(const char *ip);
int concord_util_is_ipv6(const char *ip);
uint32_t concord_util_ipv4_to_int(const char *ip);
char *concord_util_int_to_ipv4(uint32_t ip);
int concord_util_hostname_to_ip(const char *hostname, char *ip, size_t len);
int concord_util_port_is_available(int port);

// 系统信息
char *concord_util_get_hostname();
int concord_util_get_cpu_count();
uint64_t concord_util_get_memory_total();
uint64_t concord_util_get_memory_used();
uint64_t concord_util_get_disk_total(const char *path);
uint64_t concord_util_get_disk_used(const char *path);
char *concord_util_get_process_name();
int concord_util_get_process_id();
uint64_t concord_util_get_process_memory();
float concord_util_get_process_cpu();

// 内存操作
void *concord_util_memdup(const void *src, size_t n);

// 日志相关 (简单接口，详细日志功能另外实现)
typedef enum {
    CONCORD_LOG_DEBUG = 0,
    CONCORD_LOG_INFO,
    CONCORD_LOG_WARN,
    CONCORD_LOG_ERROR,
    CONCORD_LOG_FATAL
} concord_log_level_t;

void concord_util_log(concord_log_level_t level, const char *fmt, ...);

#define concord_util_debug(fmt, ...) concord_util_log(CONCORD_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define concord_util_info(fmt, ...) concord_util_log(CONCORD_LOG_INFO, fmt, ##__VA_ARGS__)
#define concord_util_warn(fmt, ...) concord_util_log(CONCORD_LOG_WARN, fmt, ##__VA_ARGS__)
#define concord_util_error(fmt, ...) concord_util_log(CONCORD_LOG_ERROR, fmt, ##__VA_ARGS__)
#define concord_util_fatal(fmt, ...) concord_util_log(CONCORD_LOG_FATAL, fmt, ##__VA_ARGS__)

#endif /* __CONCORD_UTIL_H__ */ 