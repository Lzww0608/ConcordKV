/*
 * @Author: Lzww0608  
 * @Date: 2025-6-16 17:01:27
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-6-16 17:01:30
 * @Description: ConcordKV 配置系统测试程序
 */

#define _GNU_SOURCE  // 启用GNU扩展功能
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>  // 添加pthread头文件

#include "kv_engine_config.h"
#include "kv_engine_interface.h"
#include "kv_memory.h"
#include "kv_error.h"

// 测试计数器
static int tests_run = 0;
static int tests_passed = 0;

#define TEST_ASSERT(condition, message) do { \
    tests_run++; \
    if (condition) { \
        tests_passed++; \
        printf("✅ PASS: %s\n", message); \
    } else { \
        printf("❌ FAIL: %s\n", message); \
    } \
} while(0)

// 创建测试数据目录
static int setup_test_environment(void) {
    // 创建测试数据目录
    if (mkdir("./test_data", 0755) == -1 && errno != EEXIST) {
        return -1;
    }
    
    const char *subdirs[] = {"array", "rbtree", "hash", "btree", "lsm"};
    char path[256];
    
    for (int i = 0; i < 5; i++) {
        snprintf(path, sizeof(path), "./test_data/%s", subdirs[i]);
        if (mkdir(path, 0755) == -1 && errno != EEXIST) {
            return -1;
        }
    }
    
    return 0;
}

// 测试1: 配置对象创建和销毁
static void test_config_creation(void) {
    printf("\n=== 测试1: 配置对象创建和销毁 ===\n");
    
    kv_engine_global_config_t *config = kv_engine_global_config_create();
    TEST_ASSERT(config != NULL, "配置对象创建成功");
    
    if (config) {
        TEST_ASSERT(config->strategy == KV_ENGINE_STRATEGY_FIXED, "默认策略正确");
        TEST_ASSERT(config->default_engine == KV_ENGINE_HASH, "默认引擎正确");
        TEST_ASSERT(config->worker_threads == 4, "默认工作线程数正确");
        TEST_ASSERT(config->listen_port == 6379, "默认监听端口正确");
        TEST_ASSERT(config->data_directory != NULL, "数据目录已设置");
        
        kv_engine_global_config_destroy(config);
        printf("✅ 配置对象销毁成功\n");
    }
}

// 测试2: 配置文件加载
static void test_config_file_loading(void) {
    printf("\n=== 测试2: 配置文件加载 ===\n");
    
    // 测试加载存在的配置文件
    kv_engine_global_config_t *config = kv_engine_config_load_from_file("engine_config.json");
    if (config) {
        TEST_ASSERT(config != NULL, "从JSON文件加载配置成功");
        TEST_ASSERT(config->default_engine == KV_ENGINE_HASH, "配置文件中的默认引擎正确");
        TEST_ASSERT(config->listen_port == 6379, "配置文件中的端口正确");
        
        kv_engine_global_config_destroy(config);
    } else {
        printf("⚠️  配置文件不存在，跳过文件加载测试\n");
    }
    
    // 测试加载不存在的配置文件
    kv_engine_global_config_t *bad_config = kv_engine_config_load_from_file("nonexistent.json");
    TEST_ASSERT(bad_config == NULL, "加载不存在的配置文件正确返回NULL");
}

// 测试3: 环境变量配置
static void test_env_config(void) {
    printf("\n=== 测试3: 环境变量配置 ===\n");
    
    // 设置测试环境变量
    setenv("CONCORD_DEFAULT_ENGINE", "array", 1);
    setenv("CONCORD_LISTEN_PORT", "8080", 1);
    setenv("CONCORD_DATA_DIR", "./test_data", 1);
    
    kv_engine_global_config_t *config = kv_engine_global_config_create();
    TEST_ASSERT(config != NULL, "配置对象创建成功");
    
    if (config) {
        int result = kv_engine_config_load_from_env(config, "CONCORD_");
        TEST_ASSERT(result == 0, "环境变量加载成功");
        TEST_ASSERT(config->default_engine == KV_ENGINE_ARRAY, "环境变量设置的默认引擎正确");
        TEST_ASSERT(config->listen_port == 8080, "环境变量设置的端口正确");
        TEST_ASSERT(strcmp(config->data_directory, "./test_data") == 0, "环境变量设置的数据目录正确");
        
        kv_engine_global_config_destroy(config);
    }
    
    // 清理环境变量
    unsetenv("CONCORD_DEFAULT_ENGINE");
    unsetenv("CONCORD_LISTEN_PORT");
    unsetenv("CONCORD_DATA_DIR");
}

// 测试4: 配置验证
static void test_config_validation(void) {
    printf("\n=== 测试4: 配置验证 ===\n");
    
    kv_engine_global_config_t *config = kv_engine_global_config_create();
    TEST_ASSERT(config != NULL, "配置对象创建成功");
    
    if (config) {
        // 测试有效配置
        int result = kv_engine_config_validate(config);
        TEST_ASSERT(result != 0, "默认配置验证失败（数据目录不存在）");
        
        // 修改为有效的数据目录
        free(config->data_directory);
        config->data_directory = strdup("./test_data");
        result = kv_engine_config_validate(config);
        TEST_ASSERT(result == 0, "修正后的配置验证成功");
        
        // 测试无效配置
        config->listen_port = -1;
        result = kv_engine_config_validate(config);
        TEST_ASSERT(result != 0, "无效端口配置验证失败");
        
        config->listen_port = 6379;  // 恢复有效值
        config->worker_threads = 0;
        result = kv_engine_config_validate(config);
        TEST_ASSERT(result != 0, "无效线程数配置验证失败");
        
        kv_engine_global_config_destroy(config);
    }
}

// 测试5: 引擎管理器创建和初始化
static void test_engine_manager(void) {
    printf("\n=== 测试5: 引擎管理器创建和初始化 ===\n");
    
    kv_engine_global_config_t *config = kv_engine_global_config_create();
    TEST_ASSERT(config != NULL, "配置对象创建成功");
    
    if (config) {
        // 设置有效的数据目录
        free(config->data_directory);
        config->data_directory = strdup("./test_data");
        
        kv_engine_manager_t *manager = kv_engine_manager_create(config);
        TEST_ASSERT(manager != NULL, "引擎管理器创建成功");
        
        if (manager) {
            TEST_ASSERT(manager->config == config, "管理器配置指针正确");
            TEST_ASSERT(manager->current_engine == config->default_engine, "当前引擎设置正确");
            TEST_ASSERT(manager->engines != NULL, "引擎数组已分配");
            
            // 初始化引擎
            int result = kv_engine_manager_init_engines(manager);
            TEST_ASSERT(result == 0, "引擎初始化成功");
            TEST_ASSERT(manager->engine_count > 0, "至少初始化了一个引擎");
            
            // 获取当前引擎
            kv_engine_t *current = kv_engine_manager_get_current(manager);
            TEST_ASSERT(current != NULL, "获取当前引擎成功");
            
            if (current) {
                TEST_ASSERT(current->type == config->default_engine, "当前引擎类型正确");
            }
            
            kv_engine_manager_destroy(manager);
            printf("✅ 引擎管理器销毁成功\n");
        }
        
        kv_engine_global_config_destroy(config);
    }
}

// 测试6: 引擎切换
static void test_engine_switching(void) {
    printf("\n=== 测试6: 引擎切换 ===\n");
    
    kv_engine_global_config_t *config = kv_engine_global_config_create();
    if (!config) return;
    
    free(config->data_directory);
    config->data_directory = strdup("./test_data");
    
    kv_engine_manager_t *manager = kv_engine_manager_create(config);
    if (!manager) {
        kv_engine_global_config_destroy(config);
        return;
    }
    
    int result = kv_engine_manager_init_engines(manager);
    TEST_ASSERT(result == 0, "引擎初始化成功");
    
    if (result == 0) {
        // 测试切换到Array引擎
        result = kv_engine_manager_switch_engine(manager, KV_ENGINE_ARRAY);
        TEST_ASSERT(result == 0, "切换到Array引擎成功");
        
        kv_engine_t *current = kv_engine_manager_get_current(manager);
        TEST_ASSERT(current != NULL && current->type == KV_ENGINE_ARRAY, "当前引擎确实是Array");
        
        // 测试切换到RBTree引擎
        result = kv_engine_manager_switch_engine(manager, KV_ENGINE_RBTREE);
        TEST_ASSERT(result == 0, "切换到RBTree引擎成功");
        
        current = kv_engine_manager_get_current(manager);
        TEST_ASSERT(current != NULL && current->type == KV_ENGINE_RBTREE, "当前引擎确实是RBTree");
        
        // 测试切换到Hash引擎
        result = kv_engine_manager_switch_engine(manager, KV_ENGINE_HASH);
        TEST_ASSERT(result == 0, "切换到Hash引擎成功");
        
        current = kv_engine_manager_get_current(manager);
        TEST_ASSERT(current != NULL && current->type == KV_ENGINE_HASH, "当前引擎确实是Hash");
    }
    
    kv_engine_manager_destroy(manager);
    kv_engine_global_config_destroy(config);
}

// 测试7: 基本存储操作
static void test_basic_operations(void) {
    printf("\n=== 测试7: 基本存储操作 ===\n");
    
    kv_engine_global_config_t *config = kv_engine_global_config_create();
    if (!config) return;
    
    free(config->data_directory);
    config->data_directory = strdup("./test_data");
    
    kv_engine_manager_t *manager = kv_engine_manager_create(config);
    if (!manager) {
        kv_engine_global_config_destroy(config);
        return;
    }
    
    int result = kv_engine_manager_init_engines(manager);
    if (result != 0) {
        kv_engine_manager_destroy(manager);
        kv_engine_global_config_destroy(config);
        return;
    }
    
    // 注意：这里不设置全局管理器，因为该函数不存在
    // 在实际应用中，管理器会在主程序中管理
    
    // 测试Hash引擎操作
    result = kv_engine_manager_switch_engine(manager, KV_ENGINE_HASH);
    if (result == 0) {
        kv_engine_t *engine = kv_engine_manager_get_current(manager);
        if (engine && engine->vtable) {
            // 测试SET操作
            if (engine->vtable->set) {
                result = engine->vtable->set(engine, "test_key", "test_value");
                TEST_ASSERT(result == 0, "Hash引擎SET操作成功");
            }
            
            // 测试GET操作
            if (engine->vtable->get) {
                char *value = engine->vtable->get(engine, "test_key");
                TEST_ASSERT(value != NULL, "Hash引擎GET操作成功");
                if (value) {
                    TEST_ASSERT(strcmp(value, "test_value") == 0, "Hash引擎GET值正确");
                }
            }
            
            // 测试COUNT操作
            if (engine->vtable->count) {
                int count = engine->vtable->count(engine);
                TEST_ASSERT(count >= 1, "Hash引擎COUNT操作成功");
            }
            
            // 测试DELETE操作
            if (engine->vtable->delete) {
                result = engine->vtable->delete(engine, "test_key");
                TEST_ASSERT(result == 0, "Hash引擎DELETE操作成功");
            }
        }
    }
    
    // 测试Array引擎操作
    result = kv_engine_manager_switch_engine(manager, KV_ENGINE_ARRAY);
    if (result == 0) {
        kv_engine_t *engine = kv_engine_manager_get_current(manager);
        if (engine && engine->vtable) {
            // 测试SET操作
            if (engine->vtable->set) {
                result = engine->vtable->set(engine, "array_key", "array_value");
                TEST_ASSERT(result == 0, "Array引擎SET操作成功");
            }
            
            // 测试GET操作
            if (engine->vtable->get) {
                char *value = engine->vtable->get(engine, "array_key");
                TEST_ASSERT(value != NULL, "Array引擎GET操作成功");
                if (value) {
                    TEST_ASSERT(strcmp(value, "array_value") == 0, "Array引擎GET值正确");
                }
            }
        }
    }
    
    kv_engine_manager_destroy(manager);
    kv_engine_global_config_destroy(config);
}

// 测试8: 字符串转换函数
static void test_string_conversions(void) {
    printf("\n=== 测试8: 字符串转换函数 ===\n");
    
    // 测试引擎类型转换
    TEST_ASSERT(kv_engine_type_from_string("array") == KV_ENGINE_ARRAY, "字符串转Array引擎类型");
    TEST_ASSERT(kv_engine_type_from_string("hash") == KV_ENGINE_HASH, "字符串转Hash引擎类型");
    TEST_ASSERT(kv_engine_type_from_string("rbtree") == KV_ENGINE_RBTREE, "字符串转RBTree引擎类型");
    TEST_ASSERT(kv_engine_type_from_string("btree") == KV_ENGINE_BTREE, "字符串转BTree引擎类型");
    TEST_ASSERT(kv_engine_type_from_string("lsm") == KV_ENGINE_LSM, "字符串转LSM引擎类型");
    TEST_ASSERT(kv_engine_type_from_string("unknown") == KV_ENGINE_HASH, "未知字符串返回默认值");
    
    // 测试引擎类型转字符串
    TEST_ASSERT(strcmp(kv_engine_type_to_string_ex(KV_ENGINE_ARRAY), "array") == 0, "Array引擎类型转字符串");
    TEST_ASSERT(strcmp(kv_engine_type_to_string_ex(KV_ENGINE_HASH), "hash") == 0, "Hash引擎类型转字符串");
    TEST_ASSERT(strcmp(kv_engine_type_to_string_ex(KV_ENGINE_RBTREE), "rbtree") == 0, "RBTree引擎类型转字符串");
    
    // 测试策略类型转换
    TEST_ASSERT(kv_engine_strategy_from_string("fixed") == KV_ENGINE_STRATEGY_FIXED, "字符串转固定策略");
    TEST_ASSERT(kv_engine_strategy_from_string("adaptive") == KV_ENGINE_STRATEGY_ADAPTIVE, "字符串转自适应策略");
    TEST_ASSERT(kv_engine_strategy_from_string("load_balanced") == KV_ENGINE_STRATEGY_LOAD_BALANCED, "字符串转负载均衡策略");
    
    // 测试策略类型转字符串
    TEST_ASSERT(strcmp(kv_engine_strategy_to_string(KV_ENGINE_STRATEGY_FIXED), "fixed") == 0, "固定策略转字符串");
    TEST_ASSERT(strcmp(kv_engine_strategy_to_string(KV_ENGINE_STRATEGY_ADAPTIVE), "adaptive") == 0, "自适应策略转字符串");
}

// 清理测试环境
static void cleanup_test_environment(void) {
    // 简单清理，实际项目中可能需要更复杂的清理逻辑
    system("rm -rf ./test_data");
}

// 主测试函数
int main(void) {
    printf("🔧 ConcordKV 配置系统测试开始\n");
    printf("================================\n");
    
    // 设置测试环境
    if (setup_test_environment() != 0) {
        fprintf(stderr, "❌ 测试环境设置失败\n");
        return 1;
    }
    
    // 运行所有测试
    test_config_creation();
    test_config_file_loading();
    test_env_config();
    test_config_validation();
    test_engine_manager();
    test_engine_switching();
    test_basic_operations();
    test_string_conversions();
    
    // 清理测试环境
    cleanup_test_environment();
    
    // 输出测试结果
    printf("\n================================\n");
    printf("🔧 配置系统测试完成\n");
    printf("总测试数: %d\n", tests_run);
    printf("通过测试: %d\n", tests_passed);
    printf("失败测试: %d\n", tests_run - tests_passed);
    
    if (tests_passed == tests_run) {
        printf("✅ 所有测试通过！\n");
        return 0;
    } else {
        printf("❌ 部分测试失败！\n");
        return 1;
    }
} 