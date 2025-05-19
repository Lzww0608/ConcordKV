#include "network.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

// 发送数据
int concord_net_socket_send(concord_socket_t *sock, const void *data, size_t size) {
    if (!sock || !data || sock->fd < 0) return -1;
    
    return send(sock->fd, data, size, 0);
}

// 接收数据
int concord_net_socket_recv(concord_socket_t *sock, void *buffer, size_t size) {
    if (!sock || !buffer || sock->fd < 0) return -1;
    
    return recv(sock->fd, buffer, size, 0);
}

// 发送完整数据（保证所有数据都发送）
int concord_net_socket_send_all(concord_socket_t *sock, const void *data, size_t size) {
    if (!sock || !data || sock->fd < 0) return -1;
    
    const char *ptr = (const char *)data;
    size_t left = size;
    int sent = 0;
    
    while (left > 0) {
        sent = send(sock->fd, ptr, left, 0);
        if (sent <= 0) {
            if (sent < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
                // 被信号中断或非阻塞模式下资源暂时不可用，继续尝试
                if (sock->opts.non_blocking) {
                    // 非阻塞模式下，等待可写
                    if (concord_net_socket_is_writable(sock, sock->opts.send_timeout) <= 0) {
                        return -1;  // 超时或错误
                    }
                }
                continue;
            }
            // 发生错误
            return sent;
        }
        
        // 更新已发送数据
        left -= sent;
        ptr += sent;
    }
    
    return size;
}

// 接收完整数据（保证接收指定大小的数据）
int concord_net_socket_recv_all(concord_socket_t *sock, void *buffer, size_t size) {
    if (!sock || !buffer || sock->fd < 0) return -1;
    
    char *ptr = (char *)buffer;
    size_t left = size;
    int received = 0;
    
    while (left > 0) {
        received = recv(sock->fd, ptr, left, 0);
        if (received <= 0) {
            if (received < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
                // 被信号中断或非阻塞模式下资源暂时不可用，继续尝试
                if (sock->opts.non_blocking) {
                    // 非阻塞模式下，等待可读
                    if (concord_net_socket_is_readable(sock, sock->opts.receive_timeout) <= 0) {
                        return -1;  // 超时或错误
                    }
                }
                continue;
            }
            // 连接关闭或发生错误
            return received;
        }
        
        // 更新已接收数据
        left -= received;
        ptr += received;
    }
    
    return size;
}

// 发送数据报
int concord_net_socket_sendto(concord_socket_t *sock, const void *data, size_t size, const char *host, int port) {
    if (!sock || !data || !host || sock->fd < 0) return -1;
    
    if (sock->type != CONCORD_SOCKET_UDP) {
        // 只有UDP套接字才能发送数据报
        return -1;
    }
    
    struct sockaddr_storage addr;
    socklen_t addr_len;
    
    // 准备地址
    if (strchr(host, ':') != NULL) {
        // IPv6地址
        struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&addr;
        memset(addr6, 0, sizeof(*addr6));
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons(port);
        
        if (inet_pton(AF_INET6, host, &addr6->sin6_addr) <= 0) {
            return -1;
        }
        
        addr_len = sizeof(*addr6);
    } else {
        // IPv4地址
        struct sockaddr_in *addr4 = (struct sockaddr_in *)&addr;
        memset(addr4, 0, sizeof(*addr4));
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons(port);
        
        if (inet_pton(AF_INET, host, &addr4->sin_addr) <= 0) {
            return -1;
        }
        
        addr_len = sizeof(*addr4);
    }
    
    return sendto(sock->fd, data, size, 0, (struct sockaddr *)&addr, addr_len);
}

// 接收数据报
int concord_net_socket_recvfrom(concord_socket_t *sock, void *buffer, size_t size, char *host, size_t host_len, int *port) {
    if (!sock || !buffer || sock->fd < 0) return -1;
    
    if (sock->type != CONCORD_SOCKET_UDP) {
        // 只有UDP套接字才能接收数据报
        return -1;
    }
    
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    
    int received = recvfrom(sock->fd, buffer, size, 0, (struct sockaddr *)&addr, &addr_len);
    if (received < 0) {
        return received;
    }
    
    // 获取发送方地址
    if (host && host_len > 0) {
        if (addr.ss_family == AF_INET) {
            struct sockaddr_in *addr4 = (struct sockaddr_in *)&addr;
            inet_ntop(AF_INET, &addr4->sin_addr, host, host_len);
            if (port) {
                *port = ntohs(addr4->sin_port);
            }
        } else if (addr.ss_family == AF_INET6) {
            struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&addr;
            inet_ntop(AF_INET6, &addr6->sin6_addr, host, host_len);
            if (port) {
                *port = ntohs(addr6->sin6_port);
            }
        }
    }
    
    return received;
}

// 获取本地地址
int concord_net_socket_get_local_addr(concord_socket_t *sock, char *host, size_t host_len, int *port) {
    if (!sock || sock->fd < 0) return -1;
    
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    
    if (getsockname(sock->fd, (struct sockaddr *)&addr, &addr_len) < 0) {
        return -1;
    }
    
    if (host && host_len > 0) {
        if (addr.ss_family == AF_INET) {
            struct sockaddr_in *addr4 = (struct sockaddr_in *)&addr;
            inet_ntop(AF_INET, &addr4->sin_addr, host, host_len);
            if (port) {
                *port = ntohs(addr4->sin_port);
            }
        } else if (addr.ss_family == AF_INET6) {
            struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&addr;
            inet_ntop(AF_INET6, &addr6->sin6_addr, host, host_len);
            if (port) {
                *port = ntohs(addr6->sin6_port);
            }
        } else {
            strncpy(host, "unknown", host_len - 1);
            host[host_len - 1] = '\0';
            if (port) {
                *port = 0;
            }
        }
    }
    
    return 0;
}

// 获取对端地址
int concord_net_socket_get_peer_addr(concord_socket_t *sock, char *host, size_t host_len, int *port) {
    if (!sock || sock->fd < 0) return -1;
    
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    
    if (getpeername(sock->fd, (struct sockaddr *)&addr, &addr_len) < 0) {
        return -1;
    }
    
    if (host && host_len > 0) {
        if (addr.ss_family == AF_INET) {
            struct sockaddr_in *addr4 = (struct sockaddr_in *)&addr;
            inet_ntop(AF_INET, &addr4->sin_addr, host, host_len);
            if (port) {
                *port = ntohs(addr4->sin_port);
            }
        } else if (addr.ss_family == AF_INET6) {
            struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)&addr;
            inet_ntop(AF_INET6, &addr6->sin6_addr, host, host_len);
            if (port) {
                *port = ntohs(addr6->sin6_port);
            }
        } else {
            strncpy(host, "unknown", host_len - 1);
            host[host_len - 1] = '\0';
            if (port) {
                *port = 0;
            }
        }
    }
    
    return 0;
}

// 检查套接字是否可读
int concord_net_socket_is_readable(concord_socket_t *sock, int timeout_ms) {
    if (!sock || sock->fd < 0) return -1;
    
    struct pollfd pfd;
    pfd.fd = sock->fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    
    return poll(&pfd, 1, timeout_ms);
}

// 检查套接字是否可写
int concord_net_socket_is_writable(concord_socket_t *sock, int timeout_ms) {
    if (!sock || sock->fd < 0) return -1;
    
    struct pollfd pfd;
    pfd.fd = sock->fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    
    return poll(&pfd, 1, timeout_ms);
}

// 获取套接字错误
int concord_net_socket_get_error(concord_socket_t *sock) {
    if (!sock || sock->fd < 0) return -1;
    
    int error = 0;
    socklen_t len = sizeof(error);
    
    if (getsockopt(sock->fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
        return errno;
    }
    
    return error;
}

// ----- HTTP简易实现 -----

// 解析URL
static int parse_url(const char *url, char *scheme, size_t scheme_len,
                    char *host, size_t host_len, int *port, char *path, size_t path_len) {
    if (!url || !host || !port || !path) return -1;
    
    // 默认值
    if (scheme) scheme[0] = '\0';
    host[0] = '\0';
    *port = 80;
    path[0] = '\0';
    
    // 解析协议
    const char *p = strstr(url, "://");
    if (p) {
        if (scheme && scheme_len > 0) {
            size_t len = p - url;
            if (len >= scheme_len) len = scheme_len - 1;
            strncpy(scheme, url, len);
            scheme[len] = '\0';
        }
        p += 3;
    } else {
        p = url;
    }
    
    // 解析主机和端口
    const char *path_start = strchr(p, '/');
    if (!path_start) {
        path_start = p + strlen(p);
    }
    
    const char *port_start = strchr(p, ':');
    if (port_start && port_start < path_start) {
        // 有端口号
        size_t host_len_actual = port_start - p;
        if (host_len_actual >= host_len) host_len_actual = host_len - 1;
        strncpy(host, p, host_len_actual);
        host[host_len_actual] = '\0';
        
        *port = atoi(port_start + 1);
    } else {
        // 无端口号
        size_t host_len_actual = path_start - p;
        if (host_len_actual >= host_len) host_len_actual = host_len - 1;
        strncpy(host, p, host_len_actual);
        host[host_len_actual] = '\0';
        
        // 根据协议设置默认端口
        if (scheme && strcmp(scheme, "https") == 0) {
            *port = 443;
        } else {
            *port = 80;
        }
    }
    
    // 解析路径
    if (*path_start) {
        strncpy(path, path_start, path_len - 1);
        path[path_len - 1] = '\0';
    } else {
        strncpy(path, "/", path_len - 1);
        path[path_len - 1] = '\0';
    }
    
    return 0;
}

// 简单的HTTP响应解析
static concord_http_response_t *parse_http_response(const char *response, size_t length) {
    if (!response || length == 0) return NULL;
    
    concord_http_response_t *http_res = (concord_http_response_t *)malloc(sizeof(concord_http_response_t));
    if (!http_res) return NULL;
    
    memset(http_res, 0, sizeof(concord_http_response_t));
    
    // 查找头部和正文的分隔行
    const char *body_start = strstr(response, "\r\n\r\n");
    if (!body_start) {
        free(http_res);
        return NULL;
    }
    
    // 计算头部长度和正文长度
    size_t headers_len = body_start - response + 2;  // 包含第一个\r\n
    size_t body_len = length - headers_len - 2;      // 减去第二个\r\n
    
    // 分配和复制正文
    http_res->body = (char *)malloc(body_len + 1);
    if (!http_res->body) {
        free(http_res);
        return NULL;
    }
    
    memcpy(http_res->body, body_start + 4, body_len);
    http_res->body[body_len] = '\0';
    http_res->body_len = body_len;
    
    // 解析状态行和头部
    char headers[16384];
    if (headers_len >= sizeof(headers)) {
        headers_len = sizeof(headers) - 1;
    }
    
    memcpy(headers, response, headers_len);
    headers[headers_len] = '\0';
    
    // 按行分割头部
    char *saveptr = NULL;
    char *line = strtok_r(headers, "\r\n", &saveptr);
    
    if (line) {
        // 解析状态行
        if (strncmp(line, "HTTP/", 5) == 0) {
            char *status_start = strchr(line, ' ');
            if (status_start) {
                http_res->status_code = atoi(status_start + 1);
            }
        }
        
        // 计算头部数量
        int header_count = 0;
        char *header_line = strtok_r(NULL, "\r\n", &saveptr);
        while (header_line) {
            header_count++;
            header_line = strtok_r(NULL, "\r\n", &saveptr);
        }
        
        // 分配头部数组
        if (header_count > 0) {
            http_res->headers = (char **)malloc(header_count * sizeof(char *));
            if (!http_res->headers) {
                free(http_res->body);
                free(http_res);
                return NULL;
            }
            
            // 重新解析头部
            memcpy(headers, response, headers_len);
            headers[headers_len] = '\0';
            
            saveptr = NULL;
            line = strtok_r(headers, "\r\n", &saveptr);  // 跳过状态行
            
            int index = 0;
            header_line = strtok_r(NULL, "\r\n", &saveptr);
            while (header_line && index < header_count) {
                http_res->headers[index] = concord_util_strdup(header_line);
                
                // 提取Content-Type
                if (strncasecmp(header_line, "Content-Type:", 13) == 0) {
                    char *value = header_line + 13;
                    while (*value && (*value == ' ' || *value == '\t')) value++;
                    http_res->content_type = concord_util_strdup(value);
                }
                
                index++;
                header_line = strtok_r(NULL, "\r\n", &saveptr);
            }
            
            http_res->header_count = index;
        }
    }
    
    return http_res;
}

// 释放HTTP响应
void concord_net_http_response_free(concord_http_response_t *response) {
    if (!response) return;
    
    if (response->content_type) {
        free(response->content_type);
    }
    
    if (response->body) {
        free(response->body);
    }
    
    if (response->headers) {
        for (int i = 0; i < response->header_count; i++) {
            if (response->headers[i]) {
                free(response->headers[i]);
            }
        }
        free(response->headers);
    }
    
    free(response);
}

// 发送HTTP GET请求
concord_http_response_t *concord_net_http_get(const char *url, const char **headers, int header_count) {
    if (!url) return NULL;
    
    char scheme[16];
    char host[256];
    char path[2048];
    int port;
    
    // 解析URL
    if (parse_url(url, scheme, sizeof(scheme), host, sizeof(host), &port, path, sizeof(path)) != 0) {
        return NULL;
    }
    
    // 创建套接字
    concord_socket_t *sock = concord_net_socket_create(CONCORD_SOCKET_TCP);
    if (!sock) {
        return NULL;
    }
    
    // 连接服务器
    if (concord_net_socket_connect(sock, host, port) != 0) {
        concord_net_socket_close(sock);
        return NULL;
    }
    
    // 构建请求
    char request[4096];
    int req_len = snprintf(request, sizeof(request),
                         "GET %s HTTP/1.1\r\n"
                         "Host: %s\r\n"
                         "Connection: close\r\n",
                         path, host);
    
    // 添加自定义头部
    for (int i = 0; i < header_count; i++) {
        if (headers[i]) {
            req_len += snprintf(request + req_len, sizeof(request) - req_len,
                              "%s\r\n", headers[i]);
        }
    }
    
    // 添加空行结束请求头
    req_len += snprintf(request + req_len, sizeof(request) - req_len, "\r\n");
    
    // 发送请求
    if (concord_net_socket_send_all(sock, request, req_len) != req_len) {
        concord_net_socket_close(sock);
        return NULL;
    }
    
    // 接收响应
    char response[65536];
    int resp_len = 0;
    int received;
    
    while ((received = concord_net_socket_recv(sock, response + resp_len, sizeof(response) - resp_len - 1)) > 0) {
        resp_len += received;
        response[resp_len] = '\0';
        
        // 防止缓冲区溢出
        if (resp_len >= sizeof(response) - 1) {
            break;
        }
    }
    
    // 关闭连接
    concord_net_socket_close(sock);
    
    // 解析响应
    if (resp_len > 0) {
        return parse_http_response(response, resp_len);
    }
    
    return NULL;
}

// 发送HTTP POST请求
concord_http_response_t *concord_net_http_post(const char *url, const char *content_type, const void *data, size_t data_len, const char **headers, int header_count) {
    if (!url || !data) return NULL;
    
    char scheme[16];
    char host[256];
    char path[2048];
    int port;
    
    // 解析URL
    if (parse_url(url, scheme, sizeof(scheme), host, sizeof(host), &port, path, sizeof(path)) != 0) {
        return NULL;
    }
    
    // 创建套接字
    concord_socket_t *sock = concord_net_socket_create(CONCORD_SOCKET_TCP);
    if (!sock) {
        return NULL;
    }
    
    // 连接服务器
    if (concord_net_socket_connect(sock, host, port) != 0) {
        concord_net_socket_close(sock);
        return NULL;
    }
    
    // 构建请求
    char request[4096];
    int req_len = snprintf(request, sizeof(request),
                         "POST %s HTTP/1.1\r\n"
                         "Host: %s\r\n"
                         "Connection: close\r\n"
                         "Content-Length: %zu\r\n",
                         path, host, data_len);
    
    // 添加Content-Type
    if (content_type) {
        req_len += snprintf(request + req_len, sizeof(request) - req_len,
                          "Content-Type: %s\r\n", content_type);
    } else {
        req_len += snprintf(request + req_len, sizeof(request) - req_len,
                          "Content-Type: application/x-www-form-urlencoded\r\n");
    }
    
    // 添加自定义头部
    for (int i = 0; i < header_count; i++) {
        if (headers[i]) {
            req_len += snprintf(request + req_len, sizeof(request) - req_len,
                              "%s\r\n", headers[i]);
        }
    }
    
    // 添加空行结束请求头
    req_len += snprintf(request + req_len, sizeof(request) - req_len, "\r\n");
    
    // 发送请求头
    if (concord_net_socket_send_all(sock, request, req_len) != req_len) {
        concord_net_socket_close(sock);
        return NULL;
    }
    
    // 发送请求体
    if (concord_net_socket_send_all(sock, data, data_len) != data_len) {
        concord_net_socket_close(sock);
        return NULL;
    }
    
    // 接收响应
    char response[65536];
    int resp_len = 0;
    int received;
    
    while ((received = concord_net_socket_recv(sock, response + resp_len, sizeof(response) - resp_len - 1)) > 0) {
        resp_len += received;
        response[resp_len] = '\0';
        
        // 防止缓冲区溢出
        if (resp_len >= sizeof(response) - 1) {
            break;
        }
    }
    
    // 关闭连接
    concord_net_socket_close(sock);
    
    // 解析响应
    if (resp_len > 0) {
        return parse_http_response(response, resp_len);
    }
    
    return NULL;
} 