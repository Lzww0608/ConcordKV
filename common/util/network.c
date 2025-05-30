/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV utility functions - network.c
 */
#define _GNU_SOURCE
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
#include <sys/un.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <ifaddrs.h>

// 网络库状态
static int g_net_initialized = 0;

// 初始化网络库
int concord_net_init(void) {
    if (g_net_initialized) return 0;
    g_net_initialized = 1;
    return 0;
}

// 清理网络库
void concord_net_cleanup(void) {
    g_net_initialized = 0;
}

// 创建网络地址
concord_net_addr_t *concord_net_addr_create(const char *host, int port) {
    if (!host) return NULL;
    
    concord_net_addr_t *addr = (concord_net_addr_t *)malloc(sizeof(concord_net_addr_t));
    if (!addr) return NULL;
    
    addr->host = concord_util_strdup(host);
    addr->port = port;
    
    // 检测是否为IPv6地址
    addr->is_ipv6 = 0;
    if (strchr(host, ':') != NULL) {
        addr->is_ipv6 = 1;
    }
    
    return addr;
}

// 销毁网络地址
void concord_net_addr_destroy(concord_net_addr_t *addr) {
    if (!addr) return;
    
    if (addr->host) {
        free(addr->host);
    }
    
    free(addr);
}

// 创建默认套接字选项
concord_socket_options_t concord_net_default_socket_options(void) {
    concord_socket_options_t opts;
    
    opts.reuse_addr = 1;
    opts.reuse_port = 0;
    opts.keep_alive = 1;
    opts.no_delay = 1;
    opts.non_blocking = 0;
    opts.receive_timeout = 30000;  // 30秒
    opts.send_timeout = 30000;     // 30秒
    opts.receive_buffer_size = 0;  // 默认值
    opts.send_buffer_size = 0;     // 默认值
    
    return opts;
}

// 创建套接字
concord_socket_t *concord_net_socket_create(concord_socket_type_t type) {
    // 确保网络库已初始化
    if (!g_net_initialized) {
        concord_net_init();
    }
    
    int domain = AF_INET;  // 默认IPv4
    int sock_type = SOCK_STREAM;
    int protocol = 0;
    
    // 根据套接字类型设置参数
    switch (type) {
        case CONCORD_SOCKET_TCP:
            sock_type = SOCK_STREAM;
            protocol = IPPROTO_TCP;
            break;
        case CONCORD_SOCKET_UDP:
            sock_type = SOCK_DGRAM;
            protocol = IPPROTO_UDP;
            break;
        case CONCORD_SOCKET_UNIX:
            domain = AF_UNIX;
            sock_type = SOCK_STREAM;
            break;
        default:
            return NULL;
    }
    
    // 创建套接字
    int fd = socket(domain, sock_type, protocol);
    if (fd < 0) {
        return NULL;
    }
    
    // 创建套接字结构
    concord_socket_t *sock = (concord_socket_t *)malloc(sizeof(concord_socket_t));
    if (!sock) {
        close(fd);
        return NULL;
    }
    
    memset(sock, 0, sizeof(concord_socket_t));
    sock->fd = fd;
    sock->type = type;
    sock->opts = concord_net_default_socket_options();
    
    // 应用默认选项
    if (sock->opts.reuse_addr) {
        int opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    }
    
    return sock;
}

// 关闭套接字
int concord_net_socket_close(concord_socket_t *sock) {
    if (!sock) return -1;
    
    int result = 0;
    if (sock->fd >= 0) {
        result = close(sock->fd);
    }
    
    // 释放地址资源
    if (sock->local_addr.host) {
        free(sock->local_addr.host);
    }
    
    if (sock->peer_addr.host) {
        free(sock->peer_addr.host);
    }
    
    free(sock);
    return result;
}

// 设置套接字选项
int concord_net_socket_set_option(concord_socket_t *sock, concord_socket_options_t *options) {
    if (!sock || !options || sock->fd < 0) return -1;
    
    int opt;
    struct timeval tv;
    
    // 复用地址
    if (options->reuse_addr != sock->opts.reuse_addr) {
        opt = options->reuse_addr ? 1 : 0;
        if (setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            return -1;
        }
    }
    
    // 复用端口
    if (options->reuse_port != sock->opts.reuse_port) {
        opt = options->reuse_port ? 1 : 0;
        if (setsockopt(sock->fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
            return -1;
        }
    }
    
    // 保持连接
    if (options->keep_alive != sock->opts.keep_alive) {
        opt = options->keep_alive ? 1 : 0;
        if (setsockopt(sock->fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) {
            return -1;
        }
    }
    
    // 禁用Nagle算法
    if (options->no_delay != sock->opts.no_delay && sock->type == CONCORD_SOCKET_TCP) {
        opt = options->no_delay ? 1 : 0;
        if (setsockopt(sock->fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
            return -1;
        }
    }
    
    // 接收超时
    if (options->receive_timeout != sock->opts.receive_timeout) {
        tv.tv_sec = options->receive_timeout / 1000;
        tv.tv_usec = (options->receive_timeout % 1000) * 1000;
        if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            return -1;
        }
    }
    
    // 发送超时
    if (options->send_timeout != sock->opts.send_timeout) {
        tv.tv_sec = options->send_timeout / 1000;
        tv.tv_usec = (options->send_timeout % 1000) * 1000;
        if (setsockopt(sock->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
            return -1;
        }
    }
    
    // 接收缓冲区大小
    if (options->receive_buffer_size > 0 && options->receive_buffer_size != sock->opts.receive_buffer_size) {
        if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVBUF, &options->receive_buffer_size, sizeof(options->receive_buffer_size)) < 0) {
            return -1;
        }
    }
    
    // 发送缓冲区大小
    if (options->send_buffer_size > 0 && options->send_buffer_size != sock->opts.send_buffer_size) {
        if (setsockopt(sock->fd, SOL_SOCKET, SO_SNDBUF, &options->send_buffer_size, sizeof(options->send_buffer_size)) < 0) {
            return -1;
        }
    }
    
    // 非阻塞模式
    if (options->non_blocking != sock->opts.non_blocking) {
        int flags = fcntl(sock->fd, F_GETFL, 0);
        if (flags < 0) {
            return -1;
        }
        
        if (options->non_blocking) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }
        
        if (fcntl(sock->fd, F_SETFL, flags) < 0) {
            return -1;
        }
    }
    
    // 更新选项
    sock->opts = *options;
    
    return 0;
}

// 获取套接字选项
int concord_net_socket_get_option(concord_socket_t *sock, concord_socket_options_t *options) {
    if (!sock || !options) return -1;
    
    *options = sock->opts;
    return 0;
}

// 绑定地址
int concord_net_socket_bind(concord_socket_t *sock, const char *host, int port) {
    if (!sock || sock->fd < 0) return -1;
    
    int result = -1;
    
    // 根据套接字类型使用不同的绑定方式
    if (sock->type == CONCORD_SOCKET_UNIX) {
        // Unix域套接字
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        
        if (host) {
            strncpy(addr.sun_path, host, sizeof(addr.sun_path) - 1);
        }
        
        result = bind(sock->fd, (struct sockaddr *)&addr, sizeof(addr));
        
    } else {
        // IPv4/IPv6套接字
        if (strchr(host, ':') != NULL) {
            // IPv6地址
            struct sockaddr_in6 addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin6_family = AF_INET6;
            addr.sin6_port = htons(port);
            
            if (host && strcmp(host, "0.0.0.0") != 0 && strcmp(host, "*") != 0) {
                if (inet_pton(AF_INET6, host, &addr.sin6_addr) <= 0) {
                    return -1;
                }
            } else {
                addr.sin6_addr = in6addr_any;
            }
            
            result = bind(sock->fd, (struct sockaddr *)&addr, sizeof(addr));
            
        } else {
            // IPv4地址
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            
            if (host && strcmp(host, "0.0.0.0") != 0 && strcmp(host, "*") != 0) {
                if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
                    return -1;
                }
            } else {
                addr.sin_addr.s_addr = INADDR_ANY;
            }
            
            result = bind(sock->fd, (struct sockaddr *)&addr, sizeof(addr));
        }
    }
    
    if (result == 0) {
        // 保存本地地址
        if (sock->local_addr.host) {
            free(sock->local_addr.host);
        }
        
        sock->local_addr.host = concord_util_strdup(host ? host : "0.0.0.0");
        sock->local_addr.port = port;
        sock->local_addr.is_ipv6 = (strchr(host, ':') != NULL);
    }
    
    return result;
}

// 监听连接
int concord_net_socket_listen(concord_socket_t *sock, int backlog) {
    if (!sock || sock->fd < 0) return -1;
    
    if (sock->type != CONCORD_SOCKET_TCP && sock->type != CONCORD_SOCKET_UNIX) {
        // 只有TCP和Unix域套接字才能监听
        return -1;
    }
    
    return listen(sock->fd, backlog);
}

// 接受连接
concord_socket_t *concord_net_socket_accept(concord_socket_t *sock) {
    if (!sock || sock->fd < 0) return NULL;
    
    if (sock->type != CONCORD_SOCKET_TCP && sock->type != CONCORD_SOCKET_UNIX) {
        // 只有TCP和Unix域套接字才能接受连接
        return NULL;
    }
    
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    
    int client_fd = accept(sock->fd, (struct sockaddr *)&addr, &addr_len);
    if (client_fd < 0) {
        return NULL;
    }
    
    // 创建新的套接字
    concord_socket_t *client = (concord_socket_t *)malloc(sizeof(concord_socket_t));
    if (!client) {
        close(client_fd);
        return NULL;
    }
    
    memset(client, 0, sizeof(concord_socket_t));
    client->fd = client_fd;
    client->type = sock->type;
    client->opts = sock->opts;
    
    // 设置对端地址
    char host[INET6_ADDRSTRLEN];
    int port = 0;
    
    if (addr.ss_family == AF_INET) {
        struct sockaddr_in *addr_in = (struct sockaddr_in *)&addr;
        inet_ntop(AF_INET, &addr_in->sin_addr, host, sizeof(host));
        port = ntohs(addr_in->sin_port);
        client->peer_addr.is_ipv6 = 0;
    } else if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)&addr;
        inet_ntop(AF_INET6, &addr_in6->sin6_addr, host, sizeof(host));
        port = ntohs(addr_in6->sin6_port);
        client->peer_addr.is_ipv6 = 1;
    } else if (addr.ss_family == AF_UNIX) {
        strcpy(host, "localhost");
        port = 0;
        client->peer_addr.is_ipv6 = 0;
    }
    
    client->peer_addr.host = concord_util_strdup(host);
    client->peer_addr.port = port;
    
    // 复制本地地址
    if (sock->local_addr.host) {
        client->local_addr.host = concord_util_strdup(sock->local_addr.host);
        client->local_addr.port = sock->local_addr.port;
        client->local_addr.is_ipv6 = sock->local_addr.is_ipv6;
    }
    
    return client;
}

// 连接到服务器
int concord_net_socket_connect(concord_socket_t *sock, const char *host, int port) {
    if (!sock || !host || sock->fd < 0) return -1;
    
    int result = -1;
    
    // 根据套接字类型使用不同的连接方式
    if (sock->type == CONCORD_SOCKET_UNIX) {
        // Unix域套接字
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, host, sizeof(addr.sun_path) - 1);
        
        result = connect(sock->fd, (struct sockaddr *)&addr, sizeof(addr));
        
    } else {
        // 先尝试解析主机名
        struct addrinfo hints, *res, *res0;
        memset(&hints, 0, sizeof(hints));
        
        if (sock->type == CONCORD_SOCKET_TCP) {
            hints.ai_socktype = SOCK_STREAM;
        } else {
            hints.ai_socktype = SOCK_DGRAM;
        }
        
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port);
        
        int err = getaddrinfo(host, port_str, &hints, &res0);
        if (err != 0) {
            return -1;
        }
        
        // 尝试所有可能的地址
        for (res = res0; res; res = res->ai_next) {
            result = connect(sock->fd, res->ai_addr, res->ai_addrlen);
            if (result == 0) {
                break;
            }
        }
        
        freeaddrinfo(res0);
    }
    
    if (result == 0) {
        // 保存对端地址
        if (sock->peer_addr.host) {
            free(sock->peer_addr.host);
        }
        
        sock->peer_addr.host = concord_util_strdup(host);
        sock->peer_addr.port = port;
        sock->peer_addr.is_ipv6 = (strchr(host, ':') != NULL);
    }
    
    return result;
}

// 解析主机名
int concord_net_resolve_host(const char *hostname, char *ip, size_t ip_len) {
    if (!hostname || !ip || ip_len == 0) return -1;
    
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;  // IPv4或IPv6
    hints.ai_socktype = SOCK_STREAM;
    
    int err = getaddrinfo(hostname, NULL, &hints, &res);
    if (err != 0) {
        return -1;
    }
    
    // 取第一个结果
    void *addr;
    if (res->ai_family == AF_INET) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
        addr = &(ipv4->sin_addr);
    } else {
        struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)res->ai_addr;
        addr = &(ipv6->sin6_addr);
    }
    
    // 转换为字符串
    if (inet_ntop(res->ai_family, addr, ip, ip_len) == NULL) {
        freeaddrinfo(res);
        return -1;
    }
    
    freeaddrinfo(res);
    return 0;
}

// 检查端口是否可用
int concord_net_is_port_available(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return 0;
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    int result = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    close(sock);
    
    return (result == 0);
}

// 获取本机IP地址 (返回第一个非环回地址)
int concord_net_get_local_ip(char *ip, size_t ip_len) {
    if (!ip || ip_len == 0) return -1;
    
    struct ifaddrs *ifaddr, *ifa;
    
    if (getifaddrs(&ifaddr) < 0) {
        return -1;
    }
    
    int found = 0;
    
    // 查找非环回的IPv4地址
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) continue;
        
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            
            // 跳过环回地址
            if (ntohl(addr->sin_addr.s_addr) == INADDR_LOOPBACK) continue;
            
            if (inet_ntop(AF_INET, &addr->sin_addr, ip, ip_len) != NULL) {
                found = 1;
                break;
            }
        }
    }
    
    // 如果没有找到IPv4地址，查找IPv6地址
    if (!found) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL) continue;
            
            if (ifa->ifa_addr->sa_family == AF_INET6) {
                struct sockaddr_in6 *addr = (struct sockaddr_in6 *)ifa->ifa_addr;
                
                // 跳过环回地址
                if (IN6_IS_ADDR_LOOPBACK(&addr->sin6_addr)) continue;
                // 跳过链路本地地址
                if (IN6_IS_ADDR_LINKLOCAL(&addr->sin6_addr)) continue;
                
                if (inet_ntop(AF_INET6, &addr->sin6_addr, ip, ip_len) != NULL) {
                    found = 1;
                    break;
                }
            }
        }
    }
    
    freeifaddrs(ifaddr);
    return found ? 0 : -1;
} 