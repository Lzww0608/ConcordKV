#ifndef __CONCORD_NETWORK_H__
#define __CONCORD_NETWORK_H__

#include <stdint.h>
#include <stddef.h>

// 网络地址结构
typedef struct {
    char *host;              // 主机名或IP地址
    int port;                // 端口号
    int is_ipv6;             // 是否为IPv6地址
} concord_net_addr_t;

// 套接字类型
typedef enum {
    CONCORD_SOCKET_TCP = 0,  // TCP套接字
    CONCORD_SOCKET_UDP,      // UDP套接字
    CONCORD_SOCKET_UNIX      // Unix域套接字
} concord_socket_type_t;

// 套接字选项
typedef struct {
    int reuse_addr;          // 是否复用地址
    int reuse_port;          // 是否复用端口
    int keep_alive;          // 是否保持连接
    int no_delay;            // 是否禁用Nagle算法
    int non_blocking;        // 是否非阻塞
    int receive_timeout;     // 接收超时（毫秒）
    int send_timeout;        // 发送超时（毫秒）
    int receive_buffer_size; // 接收缓冲区大小
    int send_buffer_size;    // 发送缓冲区大小
} concord_socket_options_t;

// 套接字句柄
typedef struct {
    int fd;                        // 文件描述符
    concord_socket_type_t type;    // 套接字类型
    concord_net_addr_t local_addr; // 本地地址
    concord_net_addr_t peer_addr;  // 对端地址
    concord_socket_options_t opts; // 套接字选项
} concord_socket_t;

// 初始化网络库
int concord_net_init(void);

// 清理网络库
void concord_net_cleanup(void);

// 创建套接字
concord_socket_t *concord_net_socket_create(concord_socket_type_t type);

// 关闭套接字
int concord_net_socket_close(concord_socket_t *sock);

// 绑定地址
int concord_net_socket_bind(concord_socket_t *sock, const char *host, int port);

// 监听连接
int concord_net_socket_listen(concord_socket_t *sock, int backlog);

// 接受连接
concord_socket_t *concord_net_socket_accept(concord_socket_t *sock);

// 连接到服务器
int concord_net_socket_connect(concord_socket_t *sock, const char *host, int port);

// 发送数据
int concord_net_socket_send(concord_socket_t *sock, const void *data, size_t size);

// 接收数据
int concord_net_socket_recv(concord_socket_t *sock, void *buffer, size_t size);

// 发送完整数据（保证所有数据都发送）
int concord_net_socket_send_all(concord_socket_t *sock, const void *data, size_t size);

// 接收完整数据（保证接收指定大小的数据）
int concord_net_socket_recv_all(concord_socket_t *sock, void *buffer, size_t size);

// 发送数据报
int concord_net_socket_sendto(concord_socket_t *sock, const void *data, size_t size, const char *host, int port);

// 接收数据报
int concord_net_socket_recvfrom(concord_socket_t *sock, void *buffer, size_t size, char *host, size_t host_len, int *port);

// 设置选项
int concord_net_socket_set_option(concord_socket_t *sock, concord_socket_options_t *options);

// 获取选项
int concord_net_socket_get_option(concord_socket_t *sock, concord_socket_options_t *options);

// 获取本地地址
int concord_net_socket_get_local_addr(concord_socket_t *sock, char *host, size_t host_len, int *port);

// 获取对端地址
int concord_net_socket_get_peer_addr(concord_socket_t *sock, char *host, size_t host_len, int *port);

// 检查套接字是否可读
int concord_net_socket_is_readable(concord_socket_t *sock, int timeout_ms);

// 检查套接字是否可写
int concord_net_socket_is_writable(concord_socket_t *sock, int timeout_ms);

// 获取套接字错误
int concord_net_socket_get_error(concord_socket_t *sock);

// 创建网络地址
concord_net_addr_t *concord_net_addr_create(const char *host, int port);

// 销毁网络地址
void concord_net_addr_destroy(concord_net_addr_t *addr);

// 解析主机名
int concord_net_resolve_host(const char *hostname, char *ip, size_t ip_len);

// 检查端口是否可用
int concord_net_is_port_available(int port);

// 获取本机IP地址
int concord_net_get_local_ip(char *ip, size_t ip_len);

// 创建默认套接字选项
concord_socket_options_t concord_net_default_socket_options(void);

// 简单HTTP请求函数
typedef struct {
    int status_code;           // HTTP状态码
    char *content_type;        // 内容类型
    char *body;                // 响应体
    size_t body_len;           // 响应体长度
    char **headers;            // 响应头
    int header_count;          // 响应头数量
} concord_http_response_t;

// 发送HTTP GET请求
concord_http_response_t *concord_net_http_get(const char *url, const char **headers, int header_count);

// 发送HTTP POST请求
concord_http_response_t *concord_net_http_post(const char *url, const char *content_type, const void *data, size_t data_len, const char **headers, int header_count);

// 释放HTTP响应
void concord_net_http_response_free(concord_http_response_t *response);

#endif /* __CONCORD_NETWORK_H__ */ 