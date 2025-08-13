/*
 * @Author: Lzww0608  
 * @Date: 2025-6-16 16:45:51
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-16 16:51:59
 * @Description: ConcordKV 主服务器 - 集成配置系统和存储引擎管理
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "kv_engine_config.h"
#include "kv_engine_interface.h"
#include "kv_store.h"
#include "kv_memory.h"
#include "kv_error.h"

// 全局变量
static kv_engine_manager_t *g_manager = NULL;
static kv_engine_global_config_t *g_config = NULL;
static volatile int g_shutdown = 0;

// 信号处理函数
static void signal_handler(int sig) {
    printf("\nReceived signal %d, shutting down gracefully...\n", sig);
    g_shutdown = 1;
}

// 创建数据目录
static int create_data_directories(kv_engine_global_config_t *config) {
    if (!config) return -1;
    
    // 创建主数据目录
    if (mkdir(config->data_directory, 0755) == -1 && errno != EEXIST) {
        fprintf(stderr, "Failed to create data directory %s: %s\n", 
                config->data_directory, strerror(errno));
        return -1;
    }
    
    // 创建各引擎数据目录
    char path[512];
    const char *engines[] = {"array", "rbtree", "hash", "btree", "lsm"};
    
    for (int i = 0; i < 5; i++) {
        snprintf(path, sizeof(path), "%s/%s", config->data_directory, engines[i]);
        if (mkdir(path, 0755) == -1 && errno != EEXIST) {
            fprintf(stderr, "Failed to create engine directory %s: %s\n", 
                    path, strerror(errno));
            return -1;
        }
    }
    
    printf("Data directories created successfully\n");
    return 0;
}

// 初始化服务器
static int init_server(const char *config_file) {
    printf("=== ConcordKV Server Initializing ===\n");
    
    // 加载配置
    if (config_file) {
        printf("Loading configuration from: %s\n", config_file);
        g_config = kv_engine_config_load_from_file(config_file);
    } else {
        printf("Using default configuration\n");
        g_config = kv_engine_global_config_create();
    }
    
    if (!g_config) {
        fprintf(stderr, "Failed to load configuration\n");
        return -1;
    }
    
    // 从环境变量加载额外配置
    kv_engine_config_load_from_env(g_config, "CONCORD_");
    
    // 验证配置
    if (kv_engine_config_validate(g_config) != 0) {
        fprintf(stderr, "Configuration validation failed\n");
        return -1;
    }
    
    // 打印配置信息
    kv_engine_config_print(g_config);
    
    // 创建数据目录
    if (create_data_directories(g_config) != 0) {
        return -1;
    }
    
    // 创建引擎管理器
    g_manager = kv_engine_manager_create(g_config);
    if (!g_manager) {
        fprintf(stderr, "Failed to create engine manager\n");
        return -1;
    }
    
    // 初始化存储引擎
    if (kv_engine_manager_init_engines(g_manager) != 0) {
        fprintf(stderr, "Failed to initialize storage engines\n");
        return -1;
    }
    
    // 设置全局管理器
    kv_engine_set_global_manager(g_manager);
    
    printf("Server initialized successfully\n");
    printf("Default engine: %s\n", kv_engine_type_to_string_ex(g_config->default_engine));
    printf("Listening on: %s:%d\n", g_config->listen_host, g_config->listen_port);
    
    return 0;
}

// 清理资源
static void cleanup_server(void) {
    printf("Cleaning up server resources...\n");
    
    if (g_manager) {
        kv_engine_manager_destroy(g_manager);
        g_manager = NULL;
    }
    
    if (g_config) {
        kv_engine_global_config_destroy(g_config);
        g_config = NULL;
    }
    
    printf("Server cleanup completed\n");
}

// 统一存储引擎接口适配器
static int unified_engine_set(const char *key, const char *value) {
    if (!g_manager) return -1;
    
    kv_engine_t *engine = kv_engine_manager_get_current(g_manager);
    if (!engine || !engine->vtable || !engine->vtable->set) {
        return -1;
    }
    
    return engine->vtable->set(engine, key, value);
}

static char* unified_engine_get(const char *key) {
    if (!g_manager) return NULL;
    
    kv_engine_t *engine = kv_engine_manager_get_current(g_manager);
    if (!engine || !engine->vtable || !engine->vtable->get) {
        return NULL;
    }
    
    return engine->vtable->get(engine, key);
}

static int unified_engine_delete(const char *key) {
    if (!g_manager) return -1;
    
    kv_engine_t *engine = kv_engine_manager_get_current(g_manager);
    if (!engine || !engine->vtable || !engine->vtable->delete) {
        return -1;
    }
    
    return engine->vtable->delete(engine, key);
}

static int unified_engine_update(const char *key, const char *value) {
    if (!g_manager) return -1;
    
    kv_engine_t *engine = kv_engine_manager_get_current(g_manager);
    if (!engine || !engine->vtable || !engine->vtable->update) {
        return -1;
    }
    
    return engine->vtable->update(engine, key, value);
}

static int unified_engine_count(void) {
    if (!g_manager) return -1;
    
    kv_engine_t *engine = kv_engine_manager_get_current(g_manager);
    if (!engine || !engine->vtable || !engine->vtable->count) {
        return -1;
    }
    
    return engine->vtable->count(engine);
}

// 命令处理器
static int handle_engine_command(const char *command) {
    if (!command || !g_manager) return -1;
    
    // 支持运行时切换引擎
    if (strncmp(command, "ENGINE ", 7) == 0) {
        const char *engine_name = command + 7;
        kv_engine_type_t type = kv_engine_type_from_string(engine_name);
        
        if (kv_engine_manager_switch_engine(g_manager, type) == 0) {
            printf("Switched to %s engine\n", engine_name);
            return 0;
        } else {
            printf("Failed to switch to %s engine\n", engine_name);
            return -1;
        }
    }
    
    // 显示引擎状态
    if (strcmp(command, "STATUS") == 0) {
        kv_engine_stats_t stats;
        if (kv_engine_manager_get_stats(g_manager, &stats) == 0) {
            printf("=== Engine Statistics ===\n");
            printf("Total Keys: %lu\n", stats.total_keys);
            printf("Total Size: %lu bytes\n", stats.total_size);
            printf("Memory Usage: %lu bytes\n", stats.memory_usage);
            printf("Read Count: %lu\n", stats.read_count);
            printf("Write Count: %lu\n", stats.write_count);
            printf("Delete Count: %lu\n", stats.delete_count);
            printf("========================\n");
        }
        return 0;
    }
    
    return -1;  // 未知命令
}

// 简化的命令行处理器（用于测试）
static void interactive_mode(void) {
    char line[1024];
    char *tokens[10];
    int token_count;
    
    printf("\n=== ConcordKV Interactive Mode ===\n");
    printf("Commands: SET key value, GET key, DEL key, COUNT, STATUS, ENGINE <type>, QUIT\n");
    printf("Engines: array, rbtree, hash, btree, lsm\n");
    printf("Current engine: %s\n", kv_engine_type_to_string_ex(g_config->default_engine));
    
    while (!g_shutdown) {
        printf("concord> ");
        fflush(stdout);
        
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        
        // 去除换行符
        line[strcspn(line, "\n")] = 0;
        
        if (strlen(line) == 0) continue;
        
        // 简单的命令解析
        token_count = 0;
        char *token = strtok(line, " ");
        while (token && token_count < 10) {
            tokens[token_count++] = token;
            token = strtok(NULL, " ");
        }
        
        if (token_count == 0) continue;
        
        // 处理命令
        if (strcasecmp(tokens[0], "QUIT") == 0) {
            break;
        } else if (strcasecmp(tokens[0], "SET") == 0 && token_count >= 3) {
            int result = unified_engine_set(tokens[1], tokens[2]);
            printf("%s\n", result == 0 ? "OK" : "ERROR");
        } else if (strcasecmp(tokens[0], "GET") == 0 && token_count >= 2) {
            char *value = unified_engine_get(tokens[1]);
            printf("%s\n", value ? value : "(null)");
        } else if (strcasecmp(tokens[0], "DEL") == 0 && token_count >= 2) {
            int result = unified_engine_delete(tokens[1]);
            printf("%s\n", result == 0 ? "OK" : "ERROR");
        } else if (strcasecmp(tokens[0], "COUNT") == 0) {
            int count = unified_engine_count();
            printf("%d\n", count);
        } else if (strcasecmp(tokens[0], "STATUS") == 0) {
            handle_engine_command("STATUS");
        } else if (strcasecmp(tokens[0], "ENGINE") == 0 && token_count >= 2) {
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "ENGINE %s", tokens[1]);
            handle_engine_command(cmd);
        } else {
            printf("Unknown command or invalid syntax\n");
        }
    }
}

// 显示帮助信息
static void show_help(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("Options:\n");
    printf("  -c, --config FILE    Configuration file path\n");
    printf("  -i, --interactive    Interactive mode\n");
    printf("  -d, --daemon         Run as daemon\n");
    printf("  -h, --help           Show this help\n");
    printf("  -v, --version        Show version\n");
    printf("\nEnvironment Variables:\n");
    printf("  CONCORD_DEFAULT_ENGINE    Default storage engine\n");
    printf("  CONCORD_LISTEN_PORT       Server listen port\n");
    printf("  CONCORD_DATA_DIR          Data directory\n");
    printf("\nExample:\n");
    printf("  %s -c engine_config.json -i\n", program_name);
}

// 主函数
int main(int argc, char *argv[]) {
    const char *config_file = NULL;
    int interactive = 0;
    int daemon_mode = 0;
    
    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--config") == 0) {
            if (i + 1 < argc) {
                config_file = argv[++i];
            } else {
                fprintf(stderr, "Error: -c requires a file path\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--interactive") == 0) {
            interactive = 1;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--daemon") == 0) {
            daemon_mode = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            show_help(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("ConcordKV Server v1.0.0\n");
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            show_help(argv[0]);
            return 1;
        }
    }
    
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化服务器
    if (init_server(config_file) != 0) {
        fprintf(stderr, "Failed to initialize server\n");
        cleanup_server();
        return 1;
    }
    
    // 运行模式
    if (interactive) {
        interactive_mode();
    } else if (daemon_mode) {
        printf("Daemon mode not implemented yet\n");
        // TODO: 实现守护进程模式
    } else {
        printf("Server running... Press Ctrl+C to stop\n");
        while (!g_shutdown) {
            sleep(1);
        }
    }
    
    // 清理资源
    cleanup_server();
    
    printf("Server shutdown complete\n");
    return 0;
} 