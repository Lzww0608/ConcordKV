/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV metrics system - metrics_format.c
 */
/**
 * @file metrics_format.c
 * @brief 指标格式化输出和HTTP服务器实现
 */

#include "metrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdarg.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <poll.h>

// 写入格式化输出的缓冲区
typedef struct {
    char *buffer;      // 缓冲区
    size_t size;       // 缓冲区大小
    size_t used;       // 已使用大小
    int overflow;      // 是否溢出
} output_buffer_t;

// HTTP服务器配置
typedef struct {
    concord_metrics_repo_t *repo;  // 指标仓库
    int running;                   // 运行状态
    int server_fd;                 // 服务器socket
    pthread_t thread;              // 服务线程
    char *host;                    // 主机地址
    int port;                      // 端口
    char *path;                    // 指标路径
    int wakeup_fd;                 // 用于唤醒阻塞的accept()调用
} http_server_t;

// 全局HTTP服务器实例
static http_server_t *g_http_server = NULL;

// 初始化输出缓冲区
static void output_buffer_init(output_buffer_t *out, char *buffer, size_t size) {
    out->buffer = buffer;
    out->size = size;
    out->used = 0;
    out->overflow = 0;
}

// 向输出缓冲区写入字符串
static void output_buffer_write(output_buffer_t *out, const char *str) {
    if (!str || out->overflow) {
        return;
    }

    size_t len = strlen(str);
    if (out->used + len >= out->size) {
        out->overflow = 1;
        return;
    }

    memcpy(out->buffer + out->used, str, len);
    out->used += len;
    out->buffer[out->used] = '\0';
}

// 向输出缓冲区写入格式化字符串
static void output_buffer_printf(output_buffer_t *out, const char *format, ...) {
    if (out->overflow) {
        return;
    }

    va_list args;
    va_start(args, format);
    
    size_t remaining = out->size - out->used;
    int written = vsnprintf(out->buffer + out->used, remaining, format, args);
    
    va_end(args);
    
    if (written < 0 || (size_t)written >= remaining) {
        out->overflow = 1;
        return;
    }
    
    out->used += written;
}

// 获取指标类型的字符串表示
static const char *get_metric_type_str(concord_metric_type_t type) {
    switch (type) {
        case CONCORD_METRIC_COUNTER:
            return "counter";
        case CONCORD_METRIC_GAUGE:
            return "gauge";
        case CONCORD_METRIC_HISTOGRAM:
            return "histogram";
        case CONCORD_METRIC_METER:
            return "meter";
        default:
            return "unknown";
    }
}

// 格式化输出计数器指标
static void format_counter(concord_metric_t *metric, output_buffer_t *out, concord_metrics_format_t format) {
    const char *name = concord_metrics_get_name(metric);
    const char *desc = concord_metrics_get_description(metric);
    int64_t value = concord_metrics_counter_get(metric);
    
    switch (format) {
        case CONCORD_METRICS_FORMAT_TEXT:
            output_buffer_printf(out, "# TYPE %s counter\n", name);
            if (desc) {
                output_buffer_printf(out, "# HELP %s %s\n", name, desc);
            }
            output_buffer_printf(out, "%s %lld\n", name, (long long)value);
            break;
            
        case CONCORD_METRICS_FORMAT_JSON:
            output_buffer_printf(out, "{\n");
            output_buffer_printf(out, "  \"name\": \"%s\",\n", name);
            if (desc) {
                output_buffer_printf(out, "  \"description\": \"%s\",\n", desc);
            }
            output_buffer_printf(out, "  \"type\": \"counter\",\n");
            output_buffer_printf(out, "  \"value\": %lld\n", (long long)value);
            output_buffer_printf(out, "}");
            break;
            
        case CONCORD_METRICS_FORMAT_PROMETHEUS:
            if (desc) {
                output_buffer_printf(out, "# HELP %s %s\n", name, desc);
            }
            output_buffer_printf(out, "# TYPE %s counter\n", name);
            output_buffer_printf(out, "%s %lld\n", name, (long long)value);
            break;
    }
}

// 格式化输出仪表盘指标
static void format_gauge(concord_metric_t *metric, output_buffer_t *out, concord_metrics_format_t format) {
    const char *name = concord_metrics_get_name(metric);
    const char *desc = concord_metrics_get_description(metric);
    double value = concord_metrics_gauge_get(metric);
    
    switch (format) {
        case CONCORD_METRICS_FORMAT_TEXT:
            output_buffer_printf(out, "# TYPE %s gauge\n", name);
            if (desc) {
                output_buffer_printf(out, "# HELP %s %s\n", name, desc);
            }
            output_buffer_printf(out, "%s %g\n", name, value);
            break;
            
        case CONCORD_METRICS_FORMAT_JSON:
            output_buffer_printf(out, "{\n");
            output_buffer_printf(out, "  \"name\": \"%s\",\n", name);
            if (desc) {
                output_buffer_printf(out, "  \"description\": \"%s\",\n", desc);
            }
            output_buffer_printf(out, "  \"type\": \"gauge\",\n");
            output_buffer_printf(out, "  \"value\": %g\n", value);
            output_buffer_printf(out, "}");
            break;
            
        case CONCORD_METRICS_FORMAT_PROMETHEUS:
            if (desc) {
                output_buffer_printf(out, "# HELP %s %s\n", name, desc);
            }
            output_buffer_printf(out, "# TYPE %s gauge\n", name);
            output_buffer_printf(out, "%s %g\n", name, value);
            break;
    }
}

// 格式化输出直方图指标
static void format_histogram(concord_metric_t *metric, output_buffer_t *out, concord_metrics_format_t format) {
    const char *name = concord_metrics_get_name(metric);
    const char *desc = concord_metrics_get_description(metric);
    int64_t count = concord_metrics_histogram_count(metric);
    double mean = concord_metrics_histogram_mean(metric);
    double p50 = concord_metrics_histogram_percentile(metric, 0.5);
    double p90 = concord_metrics_histogram_percentile(metric, 0.9);
    double p95 = concord_metrics_histogram_percentile(metric, 0.95);
    double p99 = concord_metrics_histogram_percentile(metric, 0.99);
    
    switch (format) {
        case CONCORD_METRICS_FORMAT_TEXT:
            output_buffer_printf(out, "# TYPE %s histogram\n", name);
            if (desc) {
                output_buffer_printf(out, "# HELP %s %s\n", name, desc);
            }
            output_buffer_printf(out, "%s.count %lld\n", name, (long long)count);
            output_buffer_printf(out, "%s.mean %g\n", name, mean);
            output_buffer_printf(out, "%s.p50 %g\n", name, p50);
            output_buffer_printf(out, "%s.p90 %g\n", name, p90);
            output_buffer_printf(out, "%s.p95 %g\n", name, p95);
            output_buffer_printf(out, "%s.p99 %g\n", name, p99);
            break;
            
        case CONCORD_METRICS_FORMAT_JSON:
            output_buffer_printf(out, "{\n");
            output_buffer_printf(out, "  \"name\": \"%s\",\n", name);
            if (desc) {
                output_buffer_printf(out, "  \"description\": \"%s\",\n", desc);
            }
            output_buffer_printf(out, "  \"type\": \"histogram\",\n");
            output_buffer_printf(out, "  \"count\": %lld,\n", (long long)count);
            output_buffer_printf(out, "  \"mean\": %g,\n", mean);
            output_buffer_printf(out, "  \"p50\": %g,\n", p50);
            output_buffer_printf(out, "  \"p90\": %g,\n", p90);
            output_buffer_printf(out, "  \"p95\": %g,\n", p95);
            output_buffer_printf(out, "  \"p99\": %g\n", p99);
            output_buffer_printf(out, "}");
            break;
            
        case CONCORD_METRICS_FORMAT_PROMETHEUS:
            if (desc) {
                output_buffer_printf(out, "# HELP %s %s\n", name, desc);
            }
            output_buffer_printf(out, "# TYPE %s histogram\n", name);
            output_buffer_printf(out, "%s_count %lld\n", name, (long long)count);
            output_buffer_printf(out, "%s_sum %g\n", name, mean * count);
            output_buffer_printf(out, "%s_bucket{le=\"0.5\"} %g\n", name, p50);
            output_buffer_printf(out, "%s_bucket{le=\"0.9\"} %g\n", name, p90);
            output_buffer_printf(out, "%s_bucket{le=\"0.95\"} %g\n", name, p95);
            output_buffer_printf(out, "%s_bucket{le=\"0.99\"} %g\n", name, p99);
            output_buffer_printf(out, "%s_bucket{le=\"+Inf\"} %lld\n", name, (long long)count);
            break;
    }
}

// 格式化输出计量表指标
static void format_meter(concord_metric_t *metric, output_buffer_t *out, concord_metrics_format_t format) {
    const char *name = concord_metrics_get_name(metric);
    const char *desc = concord_metrics_get_description(metric);
    double rate1m = concord_metrics_meter_rate1(metric);
    double rate5m = concord_metrics_meter_rate5(metric);
    double rate15m = concord_metrics_meter_rate15(metric);
    double rate_mean = concord_metrics_meter_rate_mean(metric);
    
    switch (format) {
        case CONCORD_METRICS_FORMAT_TEXT:
            output_buffer_printf(out, "# TYPE %s meter\n", name);
            if (desc) {
                output_buffer_printf(out, "# HELP %s %s\n", name, desc);
            }
            output_buffer_printf(out, "%s.rate1m %g\n", name, rate1m);
            output_buffer_printf(out, "%s.rate5m %g\n", name, rate5m);
            output_buffer_printf(out, "%s.rate15m %g\n", name, rate15m);
            output_buffer_printf(out, "%s.rate_mean %g\n", name, rate_mean);
            break;
            
        case CONCORD_METRICS_FORMAT_JSON:
            output_buffer_printf(out, "{\n");
            output_buffer_printf(out, "  \"name\": \"%s\",\n", name);
            if (desc) {
                output_buffer_printf(out, "  \"description\": \"%s\",\n", desc);
            }
            output_buffer_printf(out, "  \"type\": \"meter\",\n");
            output_buffer_printf(out, "  \"rate1m\": %g,\n", rate1m);
            output_buffer_printf(out, "  \"rate5m\": %g,\n", rate5m);
            output_buffer_printf(out, "  \"rate15m\": %g,\n", rate15m);
            output_buffer_printf(out, "  \"rate_mean\": %g\n", rate_mean);
            output_buffer_printf(out, "}");
            break;
            
        case CONCORD_METRICS_FORMAT_PROMETHEUS:
            if (desc) {
                output_buffer_printf(out, "# HELP %s %s\n", name, desc);
            }
            output_buffer_printf(out, "# TYPE %s gauge\n", name);
            output_buffer_printf(out, "%s_rate1m %g\n", name, rate1m);
            output_buffer_printf(out, "%s_rate5m %g\n", name, rate5m);
            output_buffer_printf(out, "%s_rate15m %g\n", name, rate15m);
            output_buffer_printf(out, "%s_rate_mean %g\n", name, rate_mean);
            break;
    }
}

// 指标格式化回调函数参数
typedef struct {
    output_buffer_t *out;
    concord_metrics_format_t format;
    int count;
} format_callback_args_t;

// 指标格式化回调函数
static void format_metric_callback(concord_metric_t *metric, void *user_data) {
    format_callback_args_t *args = (format_callback_args_t *)user_data;
    
    if (args->count > 0 && args->format == CONCORD_METRICS_FORMAT_JSON) {
        output_buffer_write(args->out, ",\n");
    }
    
    switch (concord_metrics_get_type(metric)) {
        case CONCORD_METRIC_COUNTER:
            format_counter(metric, args->out, args->format);
            break;
        case CONCORD_METRIC_GAUGE:
            format_gauge(metric, args->out, args->format);
            break;
        case CONCORD_METRIC_HISTOGRAM:
            format_histogram(metric, args->out, args->format);
            break;
        case CONCORD_METRIC_METER:
            format_meter(metric, args->out, args->format);
            break;
    }
    
    args->count++;
}

// 将指标导出为指定格式的字符串
int concord_metrics_dump(
    concord_metrics_repo_t *repo,
    concord_metrics_format_t format,
    char *buffer,
    size_t size
) {
    if (!repo || !buffer || size == 0) {
        return -1;
    }
    
    output_buffer_t out;
    output_buffer_init(&out, buffer, size);
    
    format_callback_args_t args = {
        .out = &out,
        .format = format,
        .count = 0
    };
    
    if (format == CONCORD_METRICS_FORMAT_JSON) {
        output_buffer_write(&out, "[\n");
    }
    
    concord_metrics_foreach(repo, format_metric_callback, &args);
    
    if (format == CONCORD_METRICS_FORMAT_JSON) {
        output_buffer_write(&out, "\n]");
    }
    
    if (out.overflow) {
        return -1;
    }
    
    return (int)out.used;
}

// HTTP响应状态码
typedef enum {
    HTTP_OK = 200,
    HTTP_BAD_REQUEST = 400,
    HTTP_NOT_FOUND = 404,
    HTTP_METHOD_NOT_ALLOWED = 405,
    HTTP_INTERNAL_ERROR = 500
} http_status_t;

// 获取内容类型
static const char *get_content_type(concord_metrics_format_t format) {
    switch (format) {
        case CONCORD_METRICS_FORMAT_JSON:
            return "application/json";
        case CONCORD_METRICS_FORMAT_PROMETHEUS:
            return "text/plain; version=0.0.4";
        default:
            return "text/plain";
    }
}

// 发送HTTP响应
static void send_http_response(
    int client_fd,
    http_status_t status,
    const char *content_type,
    const char *body,
    size_t body_len
) {
    char header[1024];
    int header_len;
    
    const char *status_text;
    switch (status) {
        case HTTP_OK:
            status_text = "OK";
            break;
        case HTTP_BAD_REQUEST:
            status_text = "Bad Request";
            break;
        case HTTP_NOT_FOUND:
            status_text = "Not Found";
            break;
        case HTTP_METHOD_NOT_ALLOWED:
            status_text = "Method Not Allowed";
            break;
        default:
            status_text = "Internal Server Error";
            break;
    }
    
    // 获取当前时间
    char time_buf[64];
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    strftime(time_buf, sizeof(time_buf), "%a, %d %b %Y %H:%M:%S GMT", tm_info);
    
    // 构造HTTP头
    header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Server: ConcordKV-Metrics\r\n"
        "Date: %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, status_text, time_buf, content_type, body_len);
    
    // 发送头部
    send(client_fd, header, header_len, 0);
    
    // 发送主体
    if (body && body_len > 0) {
        send(client_fd, body, body_len, 0);
    }
}

// 处理HTTP请求
static void handle_http_request(int client_fd, concord_metrics_repo_t *repo, const char *path) {
    char buffer[8192];
    ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    
    if (bytes_received <= 0) {
        return;
    }
    
    buffer[bytes_received] = '\0';
    
    // 解析HTTP请求
    char method[16] = {0};
    char request_path[256] = {0};
    char protocol[16] = {0};
    
    if (sscanf(buffer, "%15s %255s %15s", method, request_path, protocol) != 3) {
        send_http_response(client_fd, HTTP_BAD_REQUEST, "text/plain", "Bad Request", 11);
        return;
    }
    
    // 只支持GET方法
    if (strcmp(method, "GET") != 0) {
        send_http_response(client_fd, HTTP_METHOD_NOT_ALLOWED, "text/plain", "Method Not Allowed", 19);
        return;
    }
    
    // 检查路径
    if (strcmp(request_path, path) != 0) {
        send_http_response(client_fd, HTTP_NOT_FOUND, "text/plain", "Not Found", 9);
        return;
    }
    
    // 分配足够大的缓冲区来存储指标
    char *metrics_buffer = (char *)malloc(1024 * 1024); // 1MB
    if (!metrics_buffer) {
        send_http_response(client_fd, HTTP_INTERNAL_ERROR, "text/plain", "Internal Server Error", 21);
        return;
    }
    
    // 导出指标
    int len = concord_metrics_dump(repo, CONCORD_METRICS_FORMAT_PROMETHEUS, metrics_buffer, 1024 * 1024);
    if (len < 0) {
        free(metrics_buffer);
        send_http_response(client_fd, HTTP_INTERNAL_ERROR, "text/plain", "Internal Server Error", 21);
        return;
    }
    
    // 发送响应
    send_http_response(
        client_fd,
        HTTP_OK,
        get_content_type(CONCORD_METRICS_FORMAT_PROMETHEUS),
        metrics_buffer,
        len
    );
    
    free(metrics_buffer);
}

// HTTP服务器线程函数
static void *http_server_thread(void *arg) {
    http_server_t *server = (http_server_t *)arg;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // 忽略SIGPIPE信号
    signal(SIGPIPE, SIG_IGN);
    
    // 设置epoll以监听server_fd和wakeup_fd
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("创建epoll失败");
        return NULL;
    }
    
    // 添加server_fd到epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = server->server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server->server_fd, &ev) < 0) {
        perror("添加server_fd到epoll失败");
        close(epoll_fd);
        return NULL;
    }
    
    // 添加wakeup_fd到epoll
    ev.events = EPOLLIN;
    ev.data.fd = server->wakeup_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server->wakeup_fd, &ev) < 0) {
        perror("添加wakeup_fd到epoll失败");
        close(epoll_fd);
        return NULL;
    }
    
    // 监听事件循环
    struct epoll_event events[2];
    while (server->running) {
        int nfds = epoll_wait(epoll_fd, events, 2, 1000);  // 最多等待1秒
        
        // 检查运行状态
        if (!server->running) {
            break;
        }
        
        if (nfds < 0) {
            if (errno == EINTR) {
                // 被信号中断，继续
                continue;
            }
            perror("epoll_wait失败");
            break;
        }
        
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == server->wakeup_fd) {
                // 读取wakeup_fd数据
                uint64_t value;
                if (read(server->wakeup_fd, &value, sizeof(value)) < 0) {
                    if (errno != EAGAIN && errno != EINTR) {
                        perror("读取wakeup_fd失败");
                    }
                }
                // 检查运行状态
                if (!server->running) {
                    break;
                }
            } else if (events[i].data.fd == server->server_fd) {
                // 接受连接
                int client_fd = accept(server->server_fd, (struct sockaddr *)&client_addr, &client_len);
                if (client_fd < 0) {
                    if (errno != EAGAIN && errno != EINTR) {
                        perror("accept失败");
                    }
                    continue;
                }
                
                // 处理请求
                handle_http_request(client_fd, server->repo, server->path);
                
                // 关闭连接
                close(client_fd);
            }
        }
    }
    
    close(epoll_fd);
    return NULL;
}

// 启动HTTP指标服务器
int concord_metrics_start_server(
    concord_metrics_repo_t *repo,
    const char *host,
    int port,
    const char *path
) {
    if (!repo || !host || port <= 0 || port > 65535 || !path) {
        return -1;
    }
    
    // 如果服务器已经在运行，则先停止
    if (g_http_server) {
        concord_metrics_stop_server(repo);
    }
    
    // 创建服务器实例
    g_http_server = (http_server_t *)calloc(1, sizeof(http_server_t));
    if (!g_http_server) {
        return -1;
    }
    
    g_http_server->repo = repo;
    g_http_server->host = strdup(host);
    g_http_server->port = port;
    g_http_server->path = strdup(path);
    
    if (!g_http_server->host || !g_http_server->path) {
        free(g_http_server->host);
        free(g_http_server->path);
        free(g_http_server);
        g_http_server = NULL;
        return -1;
    }
    
    // 创建用于唤醒的eventfd
    g_http_server->wakeup_fd = eventfd(0, EFD_NONBLOCK);
    if (g_http_server->wakeup_fd < 0) {
        free(g_http_server->host);
        free(g_http_server->path);
        free(g_http_server);
        g_http_server = NULL;
        return -1;
    }
    
    // 创建socket
    g_http_server->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_http_server->server_fd < 0) {
        close(g_http_server->wakeup_fd);
        free(g_http_server->host);
        free(g_http_server->path);
        free(g_http_server);
        g_http_server = NULL;
        return -1;
    }
    
    // 设置地址重用
    int opt = 1;
    if (setsockopt(g_http_server->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        close(g_http_server->wakeup_fd);
        close(g_http_server->server_fd);
        free(g_http_server->host);
        free(g_http_server->path);
        free(g_http_server);
        g_http_server = NULL;
        return -1;
    }
    
    // 设置非阻塞模式
    int flags = fcntl(g_http_server->server_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(g_http_server->server_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        close(g_http_server->wakeup_fd);
        close(g_http_server->server_fd);
        free(g_http_server->host);
        free(g_http_server->path);
        free(g_http_server);
        g_http_server = NULL;
        return -1;
    }
    
    // 绑定地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    
    if (strcmp(host, "0.0.0.0") == 0) {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        server_addr.sin_addr.s_addr = inet_addr(host);
    }
    
    if (bind(g_http_server->server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(g_http_server->wakeup_fd);
        close(g_http_server->server_fd);
        free(g_http_server->host);
        free(g_http_server->path);
        free(g_http_server);
        g_http_server = NULL;
        return -1;
    }
    
    // 监听
    if (listen(g_http_server->server_fd, 10) < 0) {
        close(g_http_server->wakeup_fd);
        close(g_http_server->server_fd);
        free(g_http_server->host);
        free(g_http_server->path);
        free(g_http_server);
        g_http_server = NULL;
        return -1;
    }
    
    // 标记为运行中
    g_http_server->running = 1;
    
    // 创建服务线程
    if (pthread_create(&g_http_server->thread, NULL, http_server_thread, g_http_server) != 0) {
        close(g_http_server->wakeup_fd);
        close(g_http_server->server_fd);
        free(g_http_server->host);
        free(g_http_server->path);
        free(g_http_server);
        g_http_server = NULL;
        return -1;
    }
    
    return 0;
}

// 停止HTTP指标服务器
int concord_metrics_stop_server(concord_metrics_repo_t *repo) {
    if (!g_http_server || !repo) {
        return -1;
    }
    
    // 标记为停止
    g_http_server->running = 0;
    
    // 发送唤醒信号，中断阻塞的epoll_wait
    uint64_t value = 1;
    if (write(g_http_server->wakeup_fd, &value, sizeof(value)) < 0) {
        perror("唤醒HTTP服务器线程失败");
    }
    
    // 使用更通用的线程等待方式
    pthread_t thread_id = g_http_server->thread;
    
    // 创建条件变量和互斥锁用于线程超时控制
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);
    
    // 创建线程状态结构
    typedef struct {
        int done;
        pthread_t thread_id;
    } join_data_t;
    
    join_data_t join_data = {0, thread_id};
    
    // 创建一个辅助线程来等待目标线程结束
    pthread_t waiter_thread;
    void *wait_for_thread(void *arg) {
        join_data_t *data = (join_data_t *)arg;
        pthread_join(data->thread_id, NULL);
        
        // 获取互斥锁，设置完成标志，发送信号
        pthread_mutex_lock(&mutex);
        data->done = 1;
        pthread_cond_signal(&cond);
        pthread_mutex_unlock(&mutex);
        
        return NULL;
    }
    
    // 启动辅助线程
    pthread_create(&waiter_thread, NULL, wait_for_thread, &join_data);
    
    // 设置超时时间（2秒）
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += 2;
    
    // 等待条件变量（有超时控制）
    int joined = 0;
    pthread_mutex_lock(&mutex);
    while (!join_data.done) {
        int rc = pthread_cond_timedwait(&cond, &mutex, &timeout);
        if (rc == ETIMEDOUT) {
            // 超时，跳出循环
            break;
        } else if (rc == 0) {
            joined = 1;
            break;
        }
    }
    pthread_mutex_unlock(&mutex);
    
    // 如果无法正常加入，强制取消线程
    if (!joined) {
        fprintf(stderr, "等待HTTP服务器线程超时，强制取消\n");
        pthread_cancel(thread_id);
        pthread_join(thread_id, NULL);
    }
    
    // 清理辅助线程和同步原语
    pthread_cancel(waiter_thread);
    pthread_join(waiter_thread, NULL);
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond);
    
    // 关闭socket
    close(g_http_server->server_fd);
    close(g_http_server->wakeup_fd);
    
    // 清理资源
    free(g_http_server->host);
    free(g_http_server->path);
    free(g_http_server);
    g_http_server = NULL;
    
    return 0;
} 