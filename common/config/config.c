#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// 创建新的配置项
static config_item_t *create_config_item(const char *key, config_item_type_t type) {
    config_item_t *item = (config_item_t *)malloc(sizeof(config_item_t));
    if (!item) return NULL;
    
    memset(item, 0, sizeof(config_item_t));
    if (key) {
        item->key = strdup(key);
        if (!item->key) {
            free(item);
            return NULL;
        }
    }
    
    item->type = type;
    item->parent = NULL;
    item->next = NULL;
    return item;
}

// 释放配置项资源
static void free_config_item(config_item_t *item) {
    if (!item) return;
    
    if (item->key) {
        free(item->key);
    }
    
    if (item->type == CONFIG_TYPE_STRING && item->value.str_val) {
        free(item->value.str_val);
    } else if (item->type == CONFIG_TYPE_ARRAY) {
        for (int i = 0; i < item->value.array.count; i++) {
            free_config_item(item->value.array.items[i]);
        }
        if (item->value.array.items) {
            free(item->value.array.items);
        }
    } else if (item->type == CONFIG_TYPE_OBJECT) {
        for (int i = 0; i < item->value.object.count; i++) {
            free_config_item(item->value.object.items[i]);
        }
        if (item->value.object.items) {
            free(item->value.object.items);
        }
    }
    
    free(item);
}

// 查找配置项
static config_item_t *find_config_item(config_item_t *root, const char *key) {
    if (!root || !key) return NULL;
    
    char *key_copy = strdup(key);
    if (!key_copy) return NULL;
    
    config_item_t *item = root;
    char *saveptr = NULL;
    char *token = strtok_r(key_copy, ".", &saveptr);
    
    while (token) {
        if (item->type == CONFIG_TYPE_OBJECT) {
            int found = 0;
            for (int i = 0; i < item->value.object.count; i++) {
                if (strcmp(item->value.object.items[i]->key, token) == 0) {
                    item = item->value.object.items[i];
                    found = 1;
                    break;
                }
            }
            if (!found) {
                free(key_copy);
                return NULL;
            }
        } else {
            free(key_copy);
            return NULL;
        }
        
        token = strtok_r(NULL, ".", &saveptr);
    }
    
    free(key_copy);
    return item;
}

// 创建路径上的配置项
static config_item_t *create_config_path(config_item_t *root, const char *key) {
    if (!root || !key) return NULL;
    
    char *key_copy = strdup(key);
    if (!key_copy) return NULL;
    
    config_item_t *item = root;
    char *saveptr = NULL;
    char *token = strtok_r(key_copy, ".", &saveptr);
    char *next_token = NULL;
    
    while (token) {
        next_token = strtok_r(NULL, ".", &saveptr);
        
        // 检查当前token是否已存在
        config_item_t *child = NULL;
        if (item->type == CONFIG_TYPE_OBJECT) {
            for (int i = 0; i < item->value.object.count; i++) {
                if (strcmp(item->value.object.items[i]->key, token) == 0) {
                    child = item->value.object.items[i];
                    break;
                }
            }
        }
        
        // 如果不存在，创建新的对象
        if (!child) {
            child = create_config_item(token, next_token ? CONFIG_TYPE_OBJECT : CONFIG_TYPE_NONE);
            if (!child) {
                free(key_copy);
                return NULL;
            }
            
            child->parent = item;
            
            // 将新项添加到父对象
            if (item->type != CONFIG_TYPE_OBJECT) {
                item->type = CONFIG_TYPE_OBJECT;
                item->value.object.count = 0;
                item->value.object.items = NULL;
            }
            
            config_item_t **new_items = (config_item_t **)realloc(
                item->value.object.items, 
                (item->value.object.count + 1) * sizeof(config_item_t *)
            );
            
            if (!new_items) {
                free_config_item(child);
                free(key_copy);
                return NULL;
            }
            
            item->value.object.items = new_items;
            item->value.object.items[item->value.object.count] = child;
            item->value.object.count++;
        }
        
        item = child;
        token = next_token;
    }
    
    free(key_copy);
    return item;
}

// 初始化配置
concord_config_t *concord_config_init() {
    concord_config_t *cfg = (concord_config_t *)malloc(sizeof(concord_config_t));
    if (!cfg) return NULL;
    
    memset(cfg, 0, sizeof(concord_config_t));
    cfg->root = create_config_item(NULL, CONFIG_TYPE_OBJECT);
    if (!cfg->root) {
        free(cfg);
        return NULL;
    }
    
    return cfg;
}

// 从文件加载配置（简易JSON解析）
concord_config_t *concord_config_load(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) return NULL;
    
    concord_config_t *cfg = concord_config_init();
    if (!cfg) {
        fclose(file);
        return NULL;
    }
    
    cfg->filename = strdup(filename);
    
    // 这里应实现JSON解析逻辑
    // 简易实现，仅支持基本的键值对格式
    char line[1024];
    char key[256], value[768];
    
    while (fgets(line, sizeof(line), file)) {
        // 去除注释和空行
        char *comment = strchr(line, '#');
        if (comment) *comment = '\0';
        
        // 跳过空格
        char *p = line;
        while (*p && isspace(*p)) p++;
        if (!*p) continue;
        
        // 解析键值对 (key=value 或 "key": value 格式)
        if (sscanf(p, "%255[^=]: ] = %767[^\n]", key, value) == 2 ||
            sscanf(p, "\"%255[^\"]\" : %767[^\n]", key, value) == 2) {
            
            // 去除键和值前后的空格和引号
            char *k = key;
            while (*k && isspace(*k)) k++;
            
            char *v = value;
            while (*v && isspace(*v)) v++;
            
            char *end_k = k + strlen(k) - 1;
            while (end_k > k && (isspace(*end_k) || *end_k == '"')) end_k--;
            *(end_k + 1) = '\0';
            
            char *end_v = v + strlen(v) - 1;
            while (end_v > v && (isspace(*end_v) || *end_v == '"' || *end_v == ',')) end_v--;
            *(end_v + 1) = '\0';
            
            // 确定值类型并设置
            if (isdigit(*v) || *v == '-') {
                if (strchr(v, '.')) {
                    // 浮点值
                    double d_val = atof(v);
                    concord_config_set_float(cfg, k, d_val);
                } else {
                    // 整数值
                    int64_t i_val = atoll(v);
                    concord_config_set_int(cfg, k, i_val);
                }
            } else if (strcmp(v, "true") == 0 || strcmp(v, "false") == 0) {
                // 布尔值
                int b_val = (strcmp(v, "true") == 0) ? 1 : 0;
                concord_config_set_bool(cfg, k, b_val);
            } else {
                // 字符串值
                concord_config_set_string(cfg, k, v);
            }
        }
    }
    
    fclose(file);
    return cfg;
}

// 保存配置到文件
int concord_config_save(concord_config_t *cfg, const char *filename) {
    if (!cfg || !filename) return -1;
    
    FILE *file = fopen(filename, "w");
    if (!file) return -1;
    
    // 递归函数，输出配置项
    typedef void (*write_item_func)(FILE *f, config_item_t *item, int depth);
    
    write_item_func write_item = NULL;
    write_item = (write_item_func)NULL;
    write_item = (write_item_func)^(FILE *f, config_item_t *item, int depth) {
        if (!item) return;
        
        // 缩进
        for (int i = 0; i < depth; i++) fprintf(f, "  ");
        
        if (item->key) {
            fprintf(f, "\"%s\": ", item->key);
        }
        
        switch (item->type) {
            case CONFIG_TYPE_INT:
                fprintf(f, "%lld", (long long)item->value.int_val);
                break;
            case CONFIG_TYPE_FLOAT:
                fprintf(f, "%.6f", item->value.float_val);
                break;
            case CONFIG_TYPE_BOOL:
                fprintf(f, "%s", item->value.bool_val ? "true" : "false");
                break;
            case CONFIG_TYPE_STRING:
                fprintf(f, "\"%s\"", item->value.str_val ? item->value.str_val : "");
                break;
            case CONFIG_TYPE_ARRAY:
                fprintf(f, "[\n");
                for (int i = 0; i < item->value.array.count; i++) {
                    write_item(f, item->value.array.items[i], depth + 1);
                    if (i < item->value.array.count - 1) fprintf(f, ",");
                    fprintf(f, "\n");
                }
                for (int i = 0; i < depth; i++) fprintf(f, "  ");
                fprintf(f, "]");
                break;
            case CONFIG_TYPE_OBJECT:
                fprintf(f, "{\n");
                for (int i = 0; i < item->value.object.count; i++) {
                    write_item(f, item->value.object.items[i], depth + 1);
                    if (i < item->value.object.count - 1) fprintf(f, ",");
                    fprintf(f, "\n");
                }
                for (int i = 0; i < depth; i++) fprintf(f, "  ");
                fprintf(f, "}");
                break;
            default:
                fprintf(f, "null");
                break;
        }
    };
    
    fprintf(file, "{\n");
    
    // 输出所有根对象的子项
    for (int i = 0; i < cfg->root->value.object.count; i++) {
        write_item(file, cfg->root->value.object.items[i], 1);
        if (i < cfg->root->value.object.count - 1) fprintf(file, ",");
        fprintf(file, "\n");
    }
    
    fprintf(file, "}\n");
    fclose(file);
    
    // 更新文件名并清除修改标志
    if (cfg->filename && strcmp(cfg->filename, filename) != 0) {
        free(cfg->filename);
        cfg->filename = strdup(filename);
    } else if (!cfg->filename) {
        cfg->filename = strdup(filename);
    }
    
    cfg->modified = 0;
    return 0;
}

// 销毁配置
void concord_config_destroy(concord_config_t *cfg) {
    if (!cfg) return;
    
    if (cfg->root) {
        free_config_item(cfg->root);
    }
    
    if (cfg->filename) {
        free(cfg->filename);
    }
    
    free(cfg);
}

// 获取整数值
int64_t concord_config_get_int(concord_config_t *cfg, const char *key, int64_t default_val) {
    if (!cfg || !key) return default_val;
    
    config_item_t *item = find_config_item(cfg->root, key);
    if (!item || item->type != CONFIG_TYPE_INT) return default_val;
    
    return item->value.int_val;
}

// 获取浮点值
double concord_config_get_float(concord_config_t *cfg, const char *key, double default_val) {
    if (!cfg || !key) return default_val;
    
    config_item_t *item = find_config_item(cfg->root, key);
    if (!item || item->type != CONFIG_TYPE_FLOAT) return default_val;
    
    return item->value.float_val;
}

// 获取布尔值
int concord_config_get_bool(concord_config_t *cfg, const char *key, int default_val) {
    if (!cfg || !key) return default_val;
    
    config_item_t *item = find_config_item(cfg->root, key);
    if (!item || item->type != CONFIG_TYPE_BOOL) return default_val;
    
    return item->value.bool_val;
}

// 获取字符串值
const char *concord_config_get_string(concord_config_t *cfg, const char *key, const char *default_val) {
    if (!cfg || !key) return default_val;
    
    config_item_t *item = find_config_item(cfg->root, key);
    if (!item || item->type != CONFIG_TYPE_STRING || !item->value.str_val) return default_val;
    
    return item->value.str_val;
}

// 设置整数值
int concord_config_set_int(concord_config_t *cfg, const char *key, int64_t value) {
    if (!cfg || !key) return -1;
    
    config_item_t *item = create_config_path(cfg->root, key);
    if (!item) return -1;
    
    // 如果是字符串类型，释放字符串
    if (item->type == CONFIG_TYPE_STRING && item->value.str_val) {
        free(item->value.str_val);
        item->value.str_val = NULL;
    }
    
    item->type = CONFIG_TYPE_INT;
    item->value.int_val = value;
    cfg->modified = 1;
    
    return 0;
}

// 设置浮点值
int concord_config_set_float(concord_config_t *cfg, const char *key, double value) {
    if (!cfg || !key) return -1;
    
    config_item_t *item = create_config_path(cfg->root, key);
    if (!item) return -1;
    
    // 如果是字符串类型，释放字符串
    if (item->type == CONFIG_TYPE_STRING && item->value.str_val) {
        free(item->value.str_val);
        item->value.str_val = NULL;
    }
    
    item->type = CONFIG_TYPE_FLOAT;
    item->value.float_val = value;
    cfg->modified = 1;
    
    return 0;
}

// 设置布尔值
int concord_config_set_bool(concord_config_t *cfg, const char *key, int value) {
    if (!cfg || !key) return -1;
    
    config_item_t *item = create_config_path(cfg->root, key);
    if (!item) return -1;
    
    // 如果是字符串类型，释放字符串
    if (item->type == CONFIG_TYPE_STRING && item->value.str_val) {
        free(item->value.str_val);
        item->value.str_val = NULL;
    }
    
    item->type = CONFIG_TYPE_BOOL;
    item->value.bool_val = value ? 1 : 0;
    cfg->modified = 1;
    
    return 0;
}

// 设置字符串值
int concord_config_set_string(concord_config_t *cfg, const char *key, const char *value) {
    if (!cfg || !key) return -1;
    
    config_item_t *item = create_config_path(cfg->root, key);
    if (!item) return -1;
    
    // 如果已经是字符串类型，释放旧字符串
    if (item->type == CONFIG_TYPE_STRING && item->value.str_val) {
        free(item->value.str_val);
        item->value.str_val = NULL;
    }
    
    item->type = CONFIG_TYPE_STRING;
    item->value.str_val = value ? strdup(value) : NULL;
    cfg->modified = 1;
    
    return 0;
}

// 检查配置项是否存在
int concord_config_exists(concord_config_t *cfg, const char *key) {
    if (!cfg || !key) return 0;
    
    config_item_t *item = find_config_item(cfg->root, key);
    return (item != NULL);
}

// 删除配置项
int concord_config_remove(concord_config_t *cfg, const char *key) {
    if (!cfg || !key) return -1;
    
    // 查找最后一个点分隔符
    const char *last_dot = strrchr(key, '.');
    if (!last_dot) {
        // 根级别项目
        for (int i = 0; i < cfg->root->value.object.count; i++) {
            if (strcmp(cfg->root->value.object.items[i]->key, key) == 0) {
                free_config_item(cfg->root->value.object.items[i]);
                
                // 移动后面的项目
                for (int j = i; j < cfg->root->value.object.count - 1; j++) {
                    cfg->root->value.object.items[j] = cfg->root->value.object.items[j + 1];
                }
                
                cfg->root->value.object.count--;
                cfg->modified = 1;
                return 0;
            }
        }
    } else {
        // 查找父项
        char parent_key[256];
        strncpy(parent_key, key, last_dot - key);
        parent_key[last_dot - key] = '\0';
        
        config_item_t *parent = find_config_item(cfg->root, parent_key);
        if (parent && parent->type == CONFIG_TYPE_OBJECT) {
            const char *child_key = last_dot + 1;
            
            for (int i = 0; i < parent->value.object.count; i++) {
                if (strcmp(parent->value.object.items[i]->key, child_key) == 0) {
                    free_config_item(parent->value.object.items[i]);
                    
                    // 移动后面的项目
                    for (int j = i; j < parent->value.object.count - 1; j++) {
                        parent->value.object.items[j] = parent->value.object.items[j + 1];
                    }
                    
                    parent->value.object.count--;
                    cfg->modified = 1;
                    return 0;
                }
            }
        }
    }
    
    return -1;
}

// 获取配置项类型
config_item_type_t concord_config_get_type(concord_config_t *cfg, const char *key) {
    if (!cfg || !key) return CONFIG_TYPE_NONE;
    
    config_item_t *item = find_config_item(cfg->root, key);
    return item ? item->type : CONFIG_TYPE_NONE;
}

// 获取数组大小
int concord_config_get_array_size(concord_config_t *cfg, const char *key) {
    if (!cfg || !key) return 0;
    
    config_item_t *item = find_config_item(cfg->root, key);
    if (!item || item->type != CONFIG_TYPE_ARRAY) return 0;
    
    return item->value.array.count;
}

// 获取数组中的整数值
int64_t concord_config_get_array_int(concord_config_t *cfg, const char *key, int index, int64_t default_val) {
    if (!cfg || !key || index < 0) return default_val;
    
    config_item_t *item = find_config_item(cfg->root, key);
    if (!item || item->type != CONFIG_TYPE_ARRAY || index >= item->value.array.count) return default_val;
    
    config_item_t *element = item->value.array.items[index];
    if (!element || element->type != CONFIG_TYPE_INT) return default_val;
    
    return element->value.int_val;
}

// 获取数组中的字符串值
const char *concord_config_get_array_string(concord_config_t *cfg, const char *key, int index, const char *default_val) {
    if (!cfg || !key || index < 0) return default_val;
    
    config_item_t *item = find_config_item(cfg->root, key);
    if (!item || item->type != CONFIG_TYPE_ARRAY || index >= item->value.array.count) return default_val;
    
    config_item_t *element = item->value.array.items[index];
    if (!element || element->type != CONFIG_TYPE_STRING || !element->value.str_val) return default_val;
    
    return element->value.str_val;
}

// 添加数组项
int concord_config_add_array_item(concord_config_t *cfg, const char *key) {
    if (!cfg || !key) return -1;
    
    config_item_t *item = find_config_item(cfg->root, key);
    if (!item) {
        // 创建新数组
        item = create_config_path(cfg->root, key);
        if (!item) return -1;
        item->type = CONFIG_TYPE_ARRAY;
        item->value.array.count = 0;
        item->value.array.items = NULL;
    } else if (item->type != CONFIG_TYPE_ARRAY) {
        // 转换为数组
        if (item->type == CONFIG_TYPE_STRING && item->value.str_val) {
            free(item->value.str_val);
        }
        item->type = CONFIG_TYPE_ARRAY;
        item->value.array.count = 0;
        item->value.array.items = NULL;
    }
    
    // 创建新的数组项
    config_item_t *new_item = create_config_item(NULL, CONFIG_TYPE_OBJECT);
    if (!new_item) return -1;
    
    // 扩展数组
    config_item_t **new_items = (config_item_t **)realloc(
        item->value.array.items, 
        (item->value.array.count + 1) * sizeof(config_item_t *)
    );
    
    if (!new_items) {
        free_config_item(new_item);
        return -1;
    }
    
    item->value.array.items = new_items;
    item->value.array.items[item->value.array.count] = new_item;
    item->value.array.count++;
    cfg->modified = 1;
    
    return item->value.array.count - 1;  // 返回新项的索引
}

// 合并配置
int concord_config_merge(concord_config_t *cfg, concord_config_t *src) {
    if (!cfg || !src) return -1;
    
    // TODO: 实现配置合并逻辑
    
    cfg->modified = 1;
    return 0;
}

// 从环境变量加载配置
int concord_config_load_env(concord_config_t *cfg, const char *prefix) {
    if (!cfg || !prefix) return -1;
    
    extern char **environ;
    size_t prefix_len = strlen(prefix);
    
    for (char **env = environ; *env; env++) {
        char *var = *env;
        if (strncmp(var, prefix, prefix_len) == 0 && var[prefix_len] == '_') {
            // 提取变量名和值
            char *sep = strchr(var, '=');
            if (!sep) continue;
            
            // 创建配置键
            size_t key_len = sep - var - prefix_len - 1;
            char key[256];
            strncpy(key, var + prefix_len + 1, key_len);
            key[key_len] = '\0';
            
            // 转换环境变量键名（通常为大写加下划线）为点分隔配置键
            for (char *p = key; *p; p++) {
                if (*p == '_') *p = '.';
                else *p = tolower(*p);
            }
            
            // 获取值
            char *value = sep + 1;
            
            // 确定值类型并设置
            if (isdigit(*value) || *value == '-') {
                if (strchr(value, '.')) {
                    // 浮点值
                    double d_val = atof(value);
                    concord_config_set_float(cfg, key, d_val);
                } else {
                    // 整数值
                    int64_t i_val = atoll(value);
                    concord_config_set_int(cfg, key, i_val);
                }
            } else if (strcmp(value, "true") == 0 || strcmp(value, "false") == 0) {
                // 布尔值
                int b_val = (strcmp(value, "true") == 0) ? 1 : 0;
                concord_config_set_bool(cfg, key, b_val);
            } else {
                // 字符串值
                concord_config_set_string(cfg, key, value);
            }
        }
    }
    
    return 0;
}

// 打印配置项
void concord_config_print(concord_config_t *cfg) {
    if (!cfg) return;
    
    printf("Configuration %s%s:\n", 
           cfg->filename ? "from " : "",
           cfg->filename ? cfg->filename : "");
    
    // 递归打印函数
    typedef void (*print_item_func)(config_item_t *item, int depth);
    
    print_item_func print_item = NULL;
    print_item = (print_item_func)^(config_item_t *item, int depth) {
        if (!item) return;
        
        for (int i = 0; i < depth; i++) printf("  ");
        
        if (item->key) {
            printf("%s: ", item->key);
        }
        
        switch (item->type) {
            case CONFIG_TYPE_NONE:
                printf("null\n");
                break;
            case CONFIG_TYPE_INT:
                printf("%lld\n", (long long)item->value.int_val);
                break;
            case CONFIG_TYPE_FLOAT:
                printf("%.6f\n", item->value.float_val);
                break;
            case CONFIG_TYPE_BOOL:
                printf("%s\n", item->value.bool_val ? "true" : "false");
                break;
            case CONFIG_TYPE_STRING:
                printf("\"%s\"\n", item->value.str_val ? item->value.str_val : "");
                break;
            case CONFIG_TYPE_ARRAY:
                printf("[\n");
                for (int i = 0; i < item->value.array.count; i++) {
                    print_item(item->value.array.items[i], depth + 1);
                }
                for (int i = 0; i < depth; i++) printf("  ");
                printf("]\n");
                break;
            case CONFIG_TYPE_OBJECT:
                printf("{\n");
                for (int i = 0; i < item->value.object.count; i++) {
                    print_item(item->value.object.items[i], depth + 1);
                }
                for (int i = 0; i < depth; i++) printf("  ");
                printf("}\n");
                break;
        }
    };
    
    print_item(cfg->root, 0);
} 