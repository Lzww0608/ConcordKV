/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV configuration management header
 */

#ifndef __CONCORD_CONFIG_H__
#define __CONCORD_CONFIG_H__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 配置项类型
typedef enum {
    CONFIG_TYPE_NONE = 0,
    CONFIG_TYPE_INT,
    CONFIG_TYPE_FLOAT,
    CONFIG_TYPE_BOOL,
    CONFIG_TYPE_STRING,
    CONFIG_TYPE_ARRAY,
    CONFIG_TYPE_OBJECT
} config_item_type_t;

// 配置项结构
typedef struct config_item {
    char *key;                 // 配置键名
    config_item_type_t type;   // 配置类型
    union {
        int64_t int_val;       // 整数值
        double float_val;      // 浮点值
        int bool_val;          // 布尔值
        char *str_val;         // 字符串值
        struct {
            struct config_item **items;  // 数组项
            int count;                   // 数组大小
        } array;
        struct {
            struct config_item **items;  // 对象字段
            int count;                   // 字段数量
        } object;
    } value;
    struct config_item *parent;  // 父节点
    struct config_item *next;    // 链表下一个节点
} config_item_t;

// 配置上下文
typedef struct {
    config_item_t *root;       // 配置根节点
    char *filename;            // 配置文件名
    int modified;              // 是否被修改
} concord_config_t;

// 初始化配置
concord_config_t *concord_config_init();

// 从文件加载配置
concord_config_t *concord_config_load(const char *filename);

// 保存配置到文件
int concord_config_save(concord_config_t *cfg, const char *filename);

// 销毁配置
void concord_config_destroy(concord_config_t *cfg);

// 获取整数值
int64_t concord_config_get_int(concord_config_t *cfg, const char *key, int64_t default_val);

// 获取浮点值
double concord_config_get_float(concord_config_t *cfg, const char *key, double default_val);

// 获取布尔值
int concord_config_get_bool(concord_config_t *cfg, const char *key, int default_val);

// 获取字符串值
const char *concord_config_get_string(concord_config_t *cfg, const char *key, const char *default_val);

// 设置整数值
int concord_config_set_int(concord_config_t *cfg, const char *key, int64_t value);

// 设置浮点值
int concord_config_set_float(concord_config_t *cfg, const char *key, double value);

// 设置布尔值
int concord_config_set_bool(concord_config_t *cfg, const char *key, int value);

// 设置字符串值
int concord_config_set_string(concord_config_t *cfg, const char *key, const char *value);

// 检查配置项是否存在
int concord_config_exists(concord_config_t *cfg, const char *key);

// 删除配置项
int concord_config_remove(concord_config_t *cfg, const char *key);

// 获取配置项类型
config_item_type_t concord_config_get_type(concord_config_t *cfg, const char *key);

// 获取数组大小
int concord_config_get_array_size(concord_config_t *cfg, const char *key);

// 获取数组中的整数值
int64_t concord_config_get_array_int(concord_config_t *cfg, const char *key, int index, int64_t default_val);

// 获取数组中的字符串值
const char *concord_config_get_array_string(concord_config_t *cfg, const char *key, int index, const char *default_val);

// 添加数组项
int concord_config_add_array_item(concord_config_t *cfg, const char *key);

// 合并配置
int concord_config_merge(concord_config_t *cfg, concord_config_t *src);

// 从环境变量加载配置
int concord_config_load_env(concord_config_t *cfg, const char *prefix);

// 打印配置项
void concord_config_print(concord_config_t *cfg);

#endif /* __CONCORD_CONFIG_H__ */ 