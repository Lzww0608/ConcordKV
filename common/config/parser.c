/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV configuration parser implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#include "parser.h"
#include "config.h"

// 创建默认解析选项
config_parser_options_t concord_config_parser_default_options() {
    config_parser_options_t options;
    options.format = CONFIG_FORMAT_AUTO;
    options.case_sensitive = 1;  // 默认区分大小写
    options.allow_comments = 1;  // 默认允许注释
    options.allow_includes = 0;  // 默认不允许包含其他文件
    return options;
}

// 根据文件扩展名猜测配置格式
config_format_t concord_config_guess_format(const char *filename) {
    if (!filename) return CONFIG_FORMAT_JSON;  // 默认为JSON
    
    const char *ext = strrchr(filename, '.');
    if (!ext) return CONFIG_FORMAT_JSON;
    
    ext++;  // 跳过点号
    
    if (strcasecmp(ext, "json") == 0) return CONFIG_FORMAT_JSON;
    if (strcasecmp(ext, "ini") == 0) return CONFIG_FORMAT_INI;
    if (strcasecmp(ext, "yaml") == 0 || strcasecmp(ext, "yml") == 0) return CONFIG_FORMAT_YAML;
    if (strcasecmp(ext, "toml") == 0) return CONFIG_FORMAT_TOML;
    
    return CONFIG_FORMAT_JSON;  // 未知扩展名默认使用JSON
}

// 从文件加载配置（使用指定格式解析器）
concord_config_t *concord_config_load_with_format(const char *filename, config_format_t format) {
    FILE *fp;
    long file_size;
    char *buffer;
    concord_config_t *cfg = NULL;
    
    // 如果格式是自动，则根据文件扩展名猜测
    if (format == CONFIG_FORMAT_AUTO) {
        format = concord_config_guess_format(filename);
    }
    
    // 打开文件
    fp = fopen(filename, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open file %s\n", filename);
        return NULL;
    }
    
    // 获取文件大小
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    // 分配内存
    buffer = (char *)malloc(file_size + 1);
    if (!buffer) {
        fclose(fp);
        fprintf(stderr, "Error: Memory allocation failed\n");
        return NULL;
    }
    
    // 读取文件内容
    if (fread(buffer, 1, file_size, fp) != file_size) {
        free(buffer);
        fclose(fp);
        fprintf(stderr, "Error: Failed to read file %s\n", filename);
        return NULL;
    }
    
    buffer[file_size] = '\0';  // 添加字符串结束符
    fclose(fp);
    
    // 根据格式解析内容
    switch (format) {
        case CONFIG_FORMAT_JSON:
            cfg = concord_config_parse_json(buffer);
            break;
        case CONFIG_FORMAT_INI:
            cfg = concord_config_parse_ini(buffer);
            break;
        case CONFIG_FORMAT_YAML:
            cfg = concord_config_parse_yaml(buffer);
            break;
        case CONFIG_FORMAT_TOML:
            cfg = concord_config_parse_toml(buffer);
            break;
        default:
            fprintf(stderr, "Error: Unsupported format\n");
            free(buffer);
            return NULL;
    }
    
    free(buffer);
    
    if (cfg) {
        // 保存文件名
        cfg->filename = strdup(filename);
    }
    
    return cfg;
}

// 保存配置到文件（使用指定格式）
int concord_config_save_with_format(concord_config_t *cfg, const char *filename, config_format_t format) {
    FILE *fp;
    char *content = NULL;
    int result = -1;
    
    if (!cfg || !filename) {
        return -1;
    }
    
    // 如果格式是自动，则根据文件扩展名猜测
    if (format == CONFIG_FORMAT_AUTO) {
        format = concord_config_guess_format(filename);
    }
    
    // 根据格式生成内容
    switch (format) {
        case CONFIG_FORMAT_JSON:
            content = concord_config_to_json(cfg, 1);  // 使用美化格式
            break;
        case CONFIG_FORMAT_INI:
            content = concord_config_to_ini(cfg);
            break;
        case CONFIG_FORMAT_YAML:
            content = concord_config_to_yaml(cfg);
            break;
        case CONFIG_FORMAT_TOML:
            content = concord_config_to_toml(cfg);
            break;
        default:
            fprintf(stderr, "Error: Unsupported format\n");
            return -1;
    }
    
    if (!content) {
        return -1;
    }
    
    // 写入文件
    fp = fopen(filename, "wb");
    if (!fp) {
        free(content);
        fprintf(stderr, "Error: Cannot open file %s for writing\n", filename);
        return -1;
    }
    
    if (fputs(content, fp) >= 0) {
        result = 0;  // 成功
    }
    
    fclose(fp);
    free(content);
    
    return result;
}

// JSON 解析器实现
concord_config_t *concord_config_parse_json(const char *json_str) {
    // 实现简单的JSON解析器
    concord_config_t *cfg = concord_config_init();
    if (!cfg || !json_str) {
        return NULL;
    }
    
    const char *p = json_str;
    // 跳过空白
    while (*p && isspace(*p)) p++;
    
    // 检查是否是一个JSON对象
    if (*p != '{') {
        fprintf(stderr, "Error: JSON must start with '{'\n");
        concord_config_destroy(cfg);
        return NULL;
    }
    
    p++; // 跳过 '{'
    
    // 解析对象内容
    while (*p) {
        // 跳过空白
        while (*p && isspace(*p)) p++;
        
        if (*p == '}') {
            p++;  // 对象结束
            break;
        }
        
        // 必须是双引号开始的键名
        if (*p != '"') {
            fprintf(stderr, "Error: Expected '\"' for key name\n");
            concord_config_destroy(cfg);
            return NULL;
        }
        
        p++; // 跳过引号
        
        // 读取键名
        const char *key_start = p;
        while (*p && *p != '"') p++;
        
        if (*p != '"') {
            fprintf(stderr, "Error: Unterminated key name\n");
            concord_config_destroy(cfg);
            return NULL;
        }
        
        // 构造键名
        int key_len = p - key_start;
        char *key = (char *)malloc(key_len + 1);
        if (!key) {
            concord_config_destroy(cfg);
            return NULL;
        }
        
        strncpy(key, key_start, key_len);
        key[key_len] = '\0';
        
        p++; // 跳过结束引号
        
        // 跳过空白
        while (*p && isspace(*p)) p++;
        
        // 必须有冒号
        if (*p != ':') {
            fprintf(stderr, "Error: Expected ':' after key\n");
            free(key);
            concord_config_destroy(cfg);
            return NULL;
        }
        
        p++; // 跳过冒号
        
        // 跳过空白
        while (*p && isspace(*p)) p++;
        
        // 解析值
        if (*p == '"') {
            // 字符串值
            p++; // 跳过开始引号
            const char *str_start = p;
            
            // 查找结束引号
            while (*p && *p != '"') {
                if (*p == '\\' && *(p+1)) {
                    p += 2; // 跳过转义字符
                } else {
                    p++;
                }
            }
            
            if (*p != '"') {
                fprintf(stderr, "Error: Unterminated string value\n");
                free(key);
                concord_config_destroy(cfg);
                return NULL;
            }
            
            // 构造字符串值
            int str_len = p - str_start;
            char *str_val = (char *)malloc(str_len + 1);
            if (!str_val) {
                free(key);
                concord_config_destroy(cfg);
                return NULL;
            }
            
            // 简单处理字符串，不处理转义
            strncpy(str_val, str_start, str_len);
            str_val[str_len] = '\0';
            
            // 设置配置值
            concord_config_set_string(cfg, key, str_val);
            
            free(str_val);
            p++; // 跳过结束引号
        } else if (isdigit(*p) || *p == '-') {
            // 数字值
            const char *num_start = p;
            bool is_float = false;
            
            // 跳过可能的负号
            if (*p == '-') p++;
            
            // 读取整数部分
            while (*p && isdigit(*p)) p++;
            
            // 检查是否有小数部分
            if (*p == '.') {
                is_float = true;
                p++;
                while (*p && isdigit(*p)) p++;
            }
            
            // 检查是否有科学计数法
            if (*p == 'e' || *p == 'E') {
                is_float = true;
                p++;
                if (*p == '+' || *p == '-') p++;
                while (*p && isdigit(*p)) p++;
            }
            
            // 构造数字字符串
            int num_len = p - num_start;
            char *num_str = (char *)malloc(num_len + 1);
            if (!num_str) {
                free(key);
                concord_config_destroy(cfg);
                return NULL;
            }
            
            strncpy(num_str, num_start, num_len);
            num_str[num_len] = '\0';
            
            // 设置配置值
            if (is_float) {
                double val = atof(num_str);
                concord_config_set_float(cfg, key, val);
            } else {
                int64_t val = atoll(num_str);
                concord_config_set_int(cfg, key, val);
            }
            
            free(num_str);
        } else if (strncmp(p, "true", 4) == 0) {
            // 布尔值 true
            concord_config_set_bool(cfg, key, 1);
            p += 4;
        } else if (strncmp(p, "false", 5) == 0) {
            // 布尔值 false
            concord_config_set_bool(cfg, key, 0);
            p += 5;
        } else if (strncmp(p, "null", 4) == 0) {
            // null 值，在我们的系统中可以忽略或设置为特殊值
            // 这里简单地跳过
            p += 4;
        } else if (*p == '[') {
            // 数组，简单处理，只支持字符串数组
            p++; // 跳过 '['
            
            int array_index = 0;
            
            // 添加数组项
            concord_config_set_string(cfg, key, ""); // 创建空数组
            
            while (*p) {
                // 跳过空白
                while (*p && isspace(*p)) p++;
                
                if (*p == ']') {
                    p++; // 数组结束
                    break;
                }
                
                // 添加数组项
                char array_key[256];
                snprintf(array_key, sizeof(array_key), "%s[%d]", key, array_index);
                
                // 解析数组值
                if (*p == '"') {
                    // 字符串值
                    p++; // 跳过开始引号
                    const char *str_start = p;
                    
                    // 查找结束引号
                    while (*p && *p != '"') {
                        if (*p == '\\' && *(p+1)) {
                            p += 2; // 跳过转义字符
                        } else {
                            p++;
                        }
                    }
                    
                    if (*p != '"') {
                        fprintf(stderr, "Error: Unterminated string value in array\n");
                        free(key);
                        concord_config_destroy(cfg);
                        return NULL;
                    }
                    
                    // 构造字符串值
                    int str_len = p - str_start;
                    char *str_val = (char *)malloc(str_len + 1);
                    if (!str_val) {
                        free(key);
                        concord_config_destroy(cfg);
                        return NULL;
                    }
                    
                    strncpy(str_val, str_start, str_len);
                    str_val[str_len] = '\0';
                    
                    // 设置配置值
                    concord_config_set_string(cfg, array_key, str_val);
                    
                    free(str_val);
                    p++; // 跳过结束引号
                } else {
                    // 暂时不支持其他类型的数组项
                    fprintf(stderr, "Error: Only string arrays are supported\n");
                    free(key);
                    concord_config_destroy(cfg);
                    return NULL;
                }
                
                array_index++;
                
                // 跳过空白
                while (*p && isspace(*p)) p++;
                
                // 逗号或结束括号
                if (*p == ',') {
                    p++; // 跳过逗号
                } else if (*p == ']') {
                    p++; // 数组结束
                    break;
                } else {
                    fprintf(stderr, "Error: Expected ',' or ']' in array\n");
                    free(key);
                    concord_config_destroy(cfg);
                    return NULL;
                }
            }
        } else if (*p == '{') {
            // 嵌套对象，简单处理，只支持一级嵌套
            p++; // 跳过 '{'
            
            while (*p) {
                // 跳过空白
                while (*p && isspace(*p)) p++;
                
                if (*p == '}') {
                    p++; // 对象结束
                    break;
                }
                
                // 必须是双引号开始的键名
                if (*p != '"') {
                    fprintf(stderr, "Error: Expected '\"' for nested key name\n");
                    free(key);
                    concord_config_destroy(cfg);
                    return NULL;
                }
                
                p++; // 跳过引号
                
                // 读取嵌套键名
                const char *nested_key_start = p;
                while (*p && *p != '"') p++;
                
                if (*p != '"') {
                    fprintf(stderr, "Error: Unterminated nested key name\n");
                    free(key);
                    concord_config_destroy(cfg);
                    return NULL;
                }
                
                // 构造嵌套完整键名
                int nested_key_len = p - nested_key_start;
                char *nested_full_key = (char *)malloc(key_len + nested_key_len + 2);
                if (!nested_full_key) {
                    free(key);
                    concord_config_destroy(cfg);
                    return NULL;
                }
                
                strcpy(nested_full_key, key);
                strcat(nested_full_key, ".");
                strncat(nested_full_key, nested_key_start, nested_key_len);
                
                p++; // 跳过结束引号
                
                // 跳过空白
                while (*p && isspace(*p)) p++;
                
                // 必须有冒号
                if (*p != ':') {
                    fprintf(stderr, "Error: Expected ':' after nested key\n");
                    free(key);
                    free(nested_full_key);
                    concord_config_destroy(cfg);
                    return NULL;
                }
                
                p++; // 跳过冒号
                
                // 跳过空白
                while (*p && isspace(*p)) p++;
                
                // 解析嵌套值
                if (*p == '"') {
                    // 字符串值
                    p++; // 跳过开始引号
                    const char *str_start = p;
                    
                    // 查找结束引号
                    while (*p && *p != '"') {
                        if (*p == '\\' && *(p+1)) {
                            p += 2; // 跳过转义字符
                        } else {
                            p++;
                        }
                    }
                    
                    if (*p != '"') {
                        fprintf(stderr, "Error: Unterminated string value in nested object\n");
                        free(key);
                        free(nested_full_key);
                        concord_config_destroy(cfg);
                        return NULL;
                    }
                    
                    // 构造字符串值
                    int str_len = p - str_start;
                    char *str_val = (char *)malloc(str_len + 1);
                    if (!str_val) {
                        free(key);
                        free(nested_full_key);
                        concord_config_destroy(cfg);
                        return NULL;
                    }
                    
                    strncpy(str_val, str_start, str_len);
                    str_val[str_len] = '\0';
                    
                    // 设置配置值
                    concord_config_set_string(cfg, nested_full_key, str_val);
                    
                    free(str_val);
                    p++; // 跳过结束引号
                } else if (isdigit(*p) || *p == '-') {
                    // 数字值
                    const char *num_start = p;
                    bool is_float = false;
                    
                    // 跳过可能的负号
                    if (*p == '-') p++;
                    
                    // 读取整数部分
                    while (*p && isdigit(*p)) p++;
                    
                    // 检查是否有小数部分
                    if (*p == '.') {
                        is_float = true;
                        p++;
                        while (*p && isdigit(*p)) p++;
                    }
                    
                    // 构造数字字符串
                    int num_len = p - num_start;
                    char *num_str = (char *)malloc(num_len + 1);
                    if (!num_str) {
                        free(key);
                        free(nested_full_key);
                        concord_config_destroy(cfg);
                        return NULL;
                    }
                    
                    strncpy(num_str, num_start, num_len);
                    num_str[num_len] = '\0';
                    
                    // 设置配置值
                    if (is_float) {
                        double val = atof(num_str);
                        concord_config_set_float(cfg, nested_full_key, val);
                    } else {
                        int64_t val = atoll(num_str);
                        concord_config_set_int(cfg, nested_full_key, val);
                    }
                    
                    free(num_str);
                } else if (strncmp(p, "true", 4) == 0) {
                    // 布尔值 true
                    concord_config_set_bool(cfg, nested_full_key, 1);
                    p += 4;
                } else if (strncmp(p, "false", 5) == 0) {
                    // 布尔值 false
                    concord_config_set_bool(cfg, nested_full_key, 0);
                    p += 5;
                } else if (strncmp(p, "null", 4) == 0) {
                    // null 值，简单地跳过
                    p += 4;
                } else {
                    fprintf(stderr, "Error: Unsupported value type in nested object\n");
                    free(key);
                    free(nested_full_key);
                    concord_config_destroy(cfg);
                    return NULL;
                }
                
                free(nested_full_key);
                
                // 跳过空白
                while (*p && isspace(*p)) p++;
                
                // 逗号或结束括号
                if (*p == ',') {
                    p++; // 跳过逗号
                } else if (*p == '}') {
                    p++; // 对象结束
                    break;
                } else {
                    fprintf(stderr, "Error: Expected ',' or '}' in nested object\n");
                    free(key);
                    concord_config_destroy(cfg);
                    return NULL;
                }
            }
        } else {
            fprintf(stderr, "Error: Unsupported value type\n");
            free(key);
            concord_config_destroy(cfg);
            return NULL;
        }
        
        free(key);
        
        // 跳过空白
        while (*p && isspace(*p)) p++;
        
        // 逗号或结束括号
        if (*p == ',') {
            p++; // 跳过逗号
        } else if (*p == '}') {
            p++; // 对象结束
            break;
        } else {
            fprintf(stderr, "Error: Expected ',' or '}'\n");
            concord_config_destroy(cfg);
            return NULL;
        }
    }
    
    return cfg;
}

// 将配置转换为JSON字符串
char *concord_config_to_json(concord_config_t *cfg, int pretty) {
    if (!cfg || !cfg->root) {
        return strdup("{}");
    }
    
    // 构建JSON字符串的缓冲区
    int buffer_size = 1024;
    char *buffer = (char *)malloc(buffer_size);
    if (!buffer) {
        return NULL;
    }
    
    int length = 0;
    buffer[0] = '{';
    length = 1;
    
    if (pretty) {
        buffer[length++] = '\n';
    }
    
    // 遍历所有配置项
    config_item_t *item = cfg->root;
    int first_item = 1;
    
    while (item) {
        // 如果不是第一项，添加逗号
        if (!first_item) {
            buffer[length++] = ',';
            if (pretty) {
                buffer[length++] = '\n';
            }
        } else {
            first_item = 0;
        }
        
        // 检查缓冲区大小
        if (length + 1024 > buffer_size) {
            buffer_size *= 2;
            buffer = (char *)realloc(buffer, buffer_size);
            if (!buffer) {
                return NULL;
            }
        }
        
        // 添加缩进
        if (pretty) {
            buffer[length++] = ' ';
            buffer[length++] = ' ';
        }
        
        // 添加键名
        buffer[length++] = '"';
        strcpy(buffer + length, item->key);
        length += strlen(item->key);
        buffer[length++] = '"';
        buffer[length++] = ':';
        
        if (pretty) {
            buffer[length++] = ' ';
        }
        
        // 根据类型添加值
        switch (item->type) {
            case CONFIG_TYPE_INT:
                length += snprintf(buffer + length, buffer_size - length, "%lld", (long long)item->value.int_val);
                break;
            case CONFIG_TYPE_FLOAT:
                length += snprintf(buffer + length, buffer_size - length, "%g", item->value.float_val);
                break;
            case CONFIG_TYPE_BOOL:
                if (item->value.bool_val) {
                    strcpy(buffer + length, "true");
                    length += 4;
                } else {
                    strcpy(buffer + length, "false");
                    length += 5;
                }
                break;
            case CONFIG_TYPE_STRING:
                buffer[length++] = '"';
                // 简单JSON字符串转义
                for (const char *p = item->value.str_val; *p; p++) {
                    // 检查缓冲区大小
                    if (length + 10 > buffer_size) {
                        buffer_size *= 2;
                        buffer = (char *)realloc(buffer, buffer_size);
                        if (!buffer) {
                            return NULL;
                        }
                    }
                    
                    // 转义特殊字符
                    if (*p == '"' || *p == '\\') {
                        buffer[length++] = '\\';
                        buffer[length++] = *p;
                    } else if (*p == '\b') {
                        buffer[length++] = '\\';
                        buffer[length++] = 'b';
                    } else if (*p == '\f') {
                        buffer[length++] = '\\';
                        buffer[length++] = 'f';
                    } else if (*p == '\n') {
                        buffer[length++] = '\\';
                        buffer[length++] = 'n';
                    } else if (*p == '\r') {
                        buffer[length++] = '\\';
                        buffer[length++] = 'r';
                    } else if (*p == '\t') {
                        buffer[length++] = '\\';
                        buffer[length++] = 't';
                    } else {
                        buffer[length++] = *p;
                    }
                }
                buffer[length++] = '"';
                break;
            case CONFIG_TYPE_ARRAY:
                // 简单数组处理，这里只处理简单类型
                buffer[length++] = '[';
                if (pretty) {
                    buffer[length++] = '\n';
                }
                
                for (int i = 0; i < item->value.array.count; i++) {
                    config_item_t *array_item = item->value.array.items[i];
                    
                    // 检查缓冲区大小
                    if (length + 1024 > buffer_size) {
                        buffer_size *= 2;
                        buffer = (char *)realloc(buffer, buffer_size);
                        if (!buffer) {
                            return NULL;
                        }
                    }
                    
                    // 添加缩进
                    if (pretty) {
                        buffer[length++] = ' ';
                        buffer[length++] = ' ';
                        buffer[length++] = ' ';
                        buffer[length++] = ' ';
                    }
                    
                    // 根据类型添加值
                    switch (array_item->type) {
                        case CONFIG_TYPE_STRING:
                            buffer[length++] = '"';
                            strcpy(buffer + length, array_item->value.str_val);
                            length += strlen(array_item->value.str_val);
                            buffer[length++] = '"';
                            break;
                        case CONFIG_TYPE_INT:
                            length += snprintf(buffer + length, buffer_size - length, "%lld", (long long)array_item->value.int_val);
                            break;
                        case CONFIG_TYPE_FLOAT:
                            length += snprintf(buffer + length, buffer_size - length, "%g", array_item->value.float_val);
                            break;
                        case CONFIG_TYPE_BOOL:
                            if (array_item->value.bool_val) {
                                strcpy(buffer + length, "true");
                                length += 4;
                            } else {
                                strcpy(buffer + length, "false");
                                length += 5;
                            }
                            break;
                        default:
                            strcpy(buffer + length, "null");
                            length += 4;
                            break;
                    }
                    
                    if (i < item->value.array.count - 1) {
                        buffer[length++] = ',';
                        if (pretty) {
                            buffer[length++] = '\n';
                        }
                    } else if (pretty) {
                        buffer[length++] = '\n';
                        buffer[length++] = ' ';
                        buffer[length++] = ' ';
                    }
                }
                
                buffer[length++] = ']';
                break;
            case CONFIG_TYPE_OBJECT:
                // 对象处理，简化实现
                buffer[length++] = '{';
                if (pretty) {
                    buffer[length++] = '\n';
                }
                
                for (int i = 0; i < item->value.object.count; i++) {
                    config_item_t *obj_item = item->value.object.items[i];
                    
                    // 检查缓冲区大小
                    if (length + 1024 > buffer_size) {
                        buffer_size *= 2;
                        buffer = (char *)realloc(buffer, buffer_size);
                        if (!buffer) {
                            return NULL;
                        }
                    }
                    
                    // 添加缩进
                    if (pretty) {
                        buffer[length++] = ' ';
                        buffer[length++] = ' ';
                        buffer[length++] = ' ';
                        buffer[length++] = ' ';
                    }
                    
                    // 添加键名
                    buffer[length++] = '"';
                    strcpy(buffer + length, obj_item->key);
                    length += strlen(obj_item->key);
                    buffer[length++] = '"';
                    buffer[length++] = ':';
                    
                    if (pretty) {
                        buffer[length++] = ' ';
                    }
                    
                    // 根据类型添加值
                    switch (obj_item->type) {
                        case CONFIG_TYPE_STRING:
                            buffer[length++] = '"';
                            strcpy(buffer + length, obj_item->value.str_val);
                            length += strlen(obj_item->value.str_val);
                            buffer[length++] = '"';
                            break;
                        case CONFIG_TYPE_INT:
                            length += snprintf(buffer + length, buffer_size - length, "%lld", (long long)obj_item->value.int_val);
                            break;
                        case CONFIG_TYPE_FLOAT:
                            length += snprintf(buffer + length, buffer_size - length, "%g", obj_item->value.float_val);
                            break;
                        case CONFIG_TYPE_BOOL:
                            if (obj_item->value.bool_val) {
                                strcpy(buffer + length, "true");
                                length += 4;
                            } else {
                                strcpy(buffer + length, "false");
                                length += 5;
                            }
                            break;
                        default:
                            strcpy(buffer + length, "null");
                            length += 4;
                            break;
                    }
                    
                    if (i < item->value.object.count - 1) {
                        buffer[length++] = ',';
                        if (pretty) {
                            buffer[length++] = '\n';
                        }
                    } else if (pretty) {
                        buffer[length++] = '\n';
                        buffer[length++] = ' ';
                        buffer[length++] = ' ';
                    }
                }
                
                buffer[length++] = '}';
                break;
            default:
                strcpy(buffer + length, "null");
                length += 4;
                break;
        }
        
        item = item->next;
    }
    
    if (pretty) {
        buffer[length++] = '\n';
    }
    
    buffer[length++] = '}';
    buffer[length] = '\0';
    
    return buffer;
}

// INI 解析器实现
concord_config_t *concord_config_parse_ini(const char *ini_str) {
    concord_config_t *cfg = concord_config_init();
    if (!cfg || !ini_str) {
        return NULL;
    }
    
    const char *p = ini_str;
    char current_section[256] = "";  // 当前节名称
    
    // 逐行解析
    while (*p) {
        // 跳过空白
        while (*p && isspace(*p)) p++;
        
        // 空行或注释
        if (*p == '\0' || *p == ';' || *p == '#') {
            // 跳到下一行
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }
        
        // 节定义 [section]
        if (*p == '[') {
            p++; // 跳过 '['
            
            // 读取节名
            const char *section_start = p;
            while (*p && *p != ']' && *p != '\n') p++;
            
            if (*p != ']') {
                fprintf(stderr, "Error: Unterminated section name\n");
                concord_config_destroy(cfg);
                return NULL;
            }
            
            // 保存节名
            int section_len = p - section_start;
            if (section_len >= sizeof(current_section) - 1) {
                section_len = sizeof(current_section) - 1;
            }
            
            strncpy(current_section, section_start, section_len);
            current_section[section_len] = '\0';
            
            p++; // 跳过 ']'
        } else {
            // 键值对
            const char *key_start = p;
            while (*p && *p != '=' && *p != '\n') p++;
            
            if (*p != '=') {
                fprintf(stderr, "Error: Invalid key-value pair\n");
                concord_config_destroy(cfg);
                return NULL;
            }
            
            // 提取键名并去除尾部空白
            int key_len = p - key_start;
            while (key_len > 0 && isspace(key_start[key_len - 1])) {
                key_len--;
            }
            
            char *key = (char *)malloc(key_len + 1);
            if (!key) {
                concord_config_destroy(cfg);
                return NULL;
            }
            
            strncpy(key, key_start, key_len);
            key[key_len] = '\0';
            
            p++; // 跳过 '='
            
            // 跳过值前面的空白
            while (*p && isspace(*p) && *p != '\n') p++;
            
            // 读取值
            const char *value_start = p;
            while (*p && *p != '\n') p++;
            
            // 去除值后面的空白
            int value_len = p - value_start;
            while (value_len > 0 && isspace(value_start[value_len - 1])) {
                value_len--;
            }
            
            char *value = (char *)malloc(value_len + 1);
            if (!value) {
                free(key);
                concord_config_destroy(cfg);
                return NULL;
            }
            
            strncpy(value, value_start, value_len);
            value[value_len] = '\0';
            
            // 构造完整键名（带节名）
            char *full_key;
            if (current_section[0] != '\0') {
                full_key = (char *)malloc(strlen(current_section) + strlen(key) + 2);
                if (!full_key) {
                    free(key);
                    free(value);
                    concord_config_destroy(cfg);
                    return NULL;
                }
                
                sprintf(full_key, "%s.%s", current_section, key);
            } else {
                full_key = strdup(key);
                if (!full_key) {
                    free(key);
                    free(value);
                    concord_config_destroy(cfg);
                    return NULL;
                }
            }
            
            // 尝试识别值类型并设置
            if (isdigit(value[0]) || (value[0] == '-' && isdigit(value[1]))) {
                // 检查是否为浮点数
                int is_float = 0;
                for (int i = 0; value[i]; i++) {
                    if (value[i] == '.' || value[i] == 'e' || value[i] == 'E') {
                        is_float = 1;
                        break;
                    }
                }
                
                if (is_float) {
                    double val = atof(value);
                    concord_config_set_float(cfg, full_key, val);
                } else {
                    int64_t val = atoll(value);
                    concord_config_set_int(cfg, full_key, val);
                }
            } else if (strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 || strcasecmp(value, "on") == 0 || strcmp(value, "1") == 0) {
                concord_config_set_bool(cfg, full_key, 1);
            } else if (strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0 || strcasecmp(value, "off") == 0 || strcmp(value, "0") == 0) {
                concord_config_set_bool(cfg, full_key, 0);
            } else {
                concord_config_set_string(cfg, full_key, value);
            }
            
            free(key);
            free(value);
            free(full_key);
        }
        
        // 跳到下一行
        if (*p == '\n') p++;
    }
    
    return cfg;
}

// 将配置转换为INI字符串
char *concord_config_to_ini(concord_config_t *cfg) {
    if (!cfg || !cfg->root) {
        return strdup("; Empty INI file\n");
    }
    
    // 初始化缓冲区
    int buffer_size = 1024;
    char *buffer = (char *)malloc(buffer_size);
    if (!buffer) {
        return NULL;
    }
    
    int length = 0;
    
    // 收集所有节和键
    struct section_entry {
        char *name;
        struct section_entry *next;
    };
    
    // 创建一个默认节
    struct section_entry *sections = (struct section_entry *)malloc(sizeof(struct section_entry));
    if (!sections) {
        free(buffer);
        return NULL;
    }
    
    sections->name = strdup("");
    sections->next = NULL;
    
    // 遍历所有配置项，收集节
    config_item_t *item = cfg->root;
    while (item) {
        char *dot = strchr(item->key, '.');
        if (dot) {
            // 有节名
            char section_name[256] = {0};
            strncpy(section_name, item->key, dot - item->key);
            
            // 检查节是否已存在
            int found = 0;
            struct section_entry *current = sections;
            while (current) {
                if (strcmp(current->name, section_name) == 0) {
                    found = 1;
                    break;
                }
                current = current->next;
            }
            
            // 如果节不存在，添加
            if (!found) {
                struct section_entry *new_section = (struct section_entry *)malloc(sizeof(struct section_entry));
                if (!new_section) {
                    // 清理
                    current = sections;
                    while (current) {
                        struct section_entry *next = current->next;
                        free(current->name);
                        free(current);
                        current = next;
                    }
                    free(buffer);
                    return NULL;
                }
                
                new_section->name = strdup(section_name);
                new_section->next = sections->next;
                sections->next = new_section;
            }
        }
        
        item = item->next;
    }
    
    // 按节写入配置
    struct section_entry *current_section = sections;
    while (current_section) {
        // 写入节名
        if (current_section->name[0] != '\0') {
            // 确保缓冲区足够大
            if (length + strlen(current_section->name) + 32 > buffer_size) {
                buffer_size *= 2;
                buffer = (char *)realloc(buffer, buffer_size);
                if (!buffer) {
                    // 清理
                    struct section_entry *current = sections;
                    while (current) {
                        struct section_entry *next = current->next;
                        free(current->name);
                        free(current);
                        current = next;
                    }
                    return NULL;
                }
            }
            
            // 写入节标题
            length += snprintf(buffer + length, buffer_size - length, "\n[%s]\n", current_section->name);
        }
        
        // 写入该节的所有键值对
        item = cfg->root;
        while (item) {
            char *dot = strchr(item->key, '.');
            int in_current_section = 0;
            
            if (dot && current_section->name[0] != '\0') {
                // 检查是否属于当前节
                if (strncmp(item->key, current_section->name, dot - item->key) == 0 &&
                    strlen(current_section->name) == (size_t)(dot - item->key)) {
                    in_current_section = 1;
                }
            } else if (!dot && current_section->name[0] == '\0') {
                // 无节的键值对
                in_current_section = 1;
            }
            
            if (in_current_section) {
                const char *key_name = (dot) ? dot + 1 : item->key;
                
                // 确保缓冲区足够大
                if (length + strlen(key_name) + 256 > buffer_size) {
                    buffer_size *= 2;
                    buffer = (char *)realloc(buffer, buffer_size);
                    if (!buffer) {
                        // 清理
                        struct section_entry *current = sections;
                        while (current) {
                            struct section_entry *next = current->next;
                            free(current->name);
                            free(current);
                            current = next;
                        }
                        return NULL;
                    }
                }
                
                // 写入键名
                length += snprintf(buffer + length, buffer_size - length, "%s = ", key_name);
                
                // 根据类型写入值
                switch (item->type) {
                    case CONFIG_TYPE_INT:
                        length += snprintf(buffer + length, buffer_size - length, "%lld\n", (long long)item->value.int_val);
                        break;
                    case CONFIG_TYPE_FLOAT:
                        length += snprintf(buffer + length, buffer_size - length, "%g\n", item->value.float_val);
                        break;
                    case CONFIG_TYPE_BOOL:
                        length += snprintf(buffer + length, buffer_size - length, "%s\n", item->value.bool_val ? "true" : "false");
                        break;
                    case CONFIG_TYPE_STRING:
                        length += snprintf(buffer + length, buffer_size - length, "%s\n", item->value.str_val);
                        break;
                    default:
                        length += snprintf(buffer + length, buffer_size - length, "\n");
                        break;
                }
            }
            
            item = item->next;
        }
        
        current_section = current_section->next;
    }
    
    // 清理节列表
    current_section = sections;
    while (current_section) {
        struct section_entry *next = current_section->next;
        free(current_section->name);
        free(current_section);
        current_section = next;
    }
    
    return buffer;
}

// YAML 解析器实现
concord_config_t *concord_config_parse_yaml(const char *yaml_str) {
    concord_config_t *cfg = concord_config_init();
    if (!cfg || !yaml_str) {
        return NULL;
    }
    
    const char *p = yaml_str;
    char current_path[1024] = "";  // 当前路径
    int indent_stack[32] = {0};    // 缩进堆栈
    int indent_level = 0;          // 当前缩进级别
    
    // 逐行解析
    while (*p) {
        // 跳过空行
        while (*p && isspace(*p) && *p != '\n') p++;
        
        // 如果是空行或注释，跳过
        if (*p == '\0' || *p == '#' || *p == '\n') {
            // 跳到下一行
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }
        
        // 计算当前行的缩进
        int current_indent = 0;
        const char *indent_start = p;
        while (*p == ' ' || *p == '\t') {
            current_indent += (*p == '\t') ? 4 : 1;
            p++;
        }
        
        // 如果缩进减少，则回到适当的级别
        while (indent_level > 0 && current_indent < indent_stack[indent_level]) {
            // 找到键名中的最后一个点，并截断当前路径
            char *last_dot = strrchr(current_path, '.');
            if (last_dot) {
                *last_dot = '\0';
            } else {
                current_path[0] = '\0';
            }
            indent_level--;
        }
        
        // 读取键名
        const char *key_start = p;
        while (*p && *p != ':' && *p != '\n') p++;
        
        if (*p != ':') {
            fprintf(stderr, "Error: Invalid YAML key-value pair\n");
            concord_config_destroy(cfg);
            return NULL;
        }
        
        // 提取键名并去除尾部空白
        int key_len = p - key_start;
        while (key_len > 0 && isspace(key_start[key_len - 1])) {
            key_len--;
        }
        
        char key[256] = {0};
        if (key_len >= sizeof(key)) {
            key_len = sizeof(key) - 1;
        }
        
        strncpy(key, key_start, key_len);
        key[key_len] = '\0';
        
        p++; // 跳过冒号
        
        // 更新当前路径
        if (current_path[0] != '\0') {
            strcat(current_path, ".");
        }
        strcat(current_path, key);
        
        // 跳过值前面的空白
        while (*p && isspace(*p) && *p != '\n') p++;
        
        if (*p == '\n') {
            // 空值或下一行是一个嵌套对象
            p++;
            
            // 保存当前缩进级别
            indent_stack[++indent_level] = current_indent;
        } else {
            // 值在同一行上
            
            // 读取值
            const char *value_start = p;
            while (*p && *p != '\n') p++;
            
            // 去除值后面的空白
            int value_len = p - value_start;
            while (value_len > 0 && isspace(value_start[value_len - 1])) {
                value_len--;
            }
            
            char value[1024] = {0};
            if (value_len >= sizeof(value)) {
                value_len = sizeof(value) - 1;
            }
            
            strncpy(value, value_start, value_len);
            value[value_len] = '\0';
            
            // 尝试识别值类型并设置
            if (isdigit(value[0]) || (value[0] == '-' && isdigit(value[1]))) {
                // 检查是否为浮点数
                int is_float = 0;
                for (int i = 0; value[i]; i++) {
                    if (value[i] == '.' || value[i] == 'e' || value[i] == 'E') {
                        is_float = 1;
                        break;
                    }
                }
                
                if (is_float) {
                    double val = atof(value);
                    concord_config_set_float(cfg, current_path, val);
                } else {
                    int64_t val = atoll(value);
                    concord_config_set_int(cfg, current_path, val);
                }
            } else if (strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 || strcmp(value, "on") == 0) {
                concord_config_set_bool(cfg, current_path, 1);
            } else if (strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0 || strcmp(value, "off") == 0) {
                concord_config_set_bool(cfg, current_path, 0);
            } else if (value[0] == '[' && value[value_len - 1] == ']') {
                // 简单的数组处理
                char array_str[1024];
                strncpy(array_str, value + 1, value_len - 2);
                array_str[value_len - 2] = '\0';
                
                char *token, *saveptr;
                int array_index = 0;
                
                token = strtok_r(array_str, ",", &saveptr);
                while (token) {
                    // 去除前后空白
                    while (*token && isspace(*token)) token++;
                    char *end = token + strlen(token) - 1;
                    while (end > token && isspace(*end)) {
                        *end = '\0';
                        end--;
                    }
                    
                    // 构造数组键
                    char array_key[1024];
                    snprintf(array_key, sizeof(array_key), "%s[%d]", current_path, array_index);
                    
                    // 检查是否是字符串（被引号包围）
                    if ((token[0] == '"' && token[strlen(token) - 1] == '"') ||
                        (token[0] == '\'' && token[strlen(token) - 1] == '\'')) {
                        char *str_val = token + 1;
                        token[strlen(token) - 1] = '\0';
                        concord_config_set_string(cfg, array_key, str_val);
                    } else if (isdigit(token[0]) || (token[0] == '-' && isdigit(token[1]))) {
                        // 数字
                        int is_float = 0;
                        for (int i = 0; token[i]; i++) {
                            if (token[i] == '.' || token[i] == 'e' || token[i] == 'E') {
                                is_float = 1;
                                break;
                            }
                        }
                        
                        if (is_float) {
                            double val = atof(token);
                            concord_config_set_float(cfg, array_key, val);
                        } else {
                            int64_t val = atoll(token);
                            concord_config_set_int(cfg, array_key, val);
                        }
                    } else if (strcasecmp(token, "true") == 0 || strcasecmp(token, "yes") == 0) {
                        concord_config_set_bool(cfg, array_key, 1);
                    } else if (strcasecmp(token, "false") == 0 || strcasecmp(token, "no") == 0) {
                        concord_config_set_bool(cfg, array_key, 0);
                    } else {
                        // 其他值当作字符串处理
                        concord_config_set_string(cfg, array_key, token);
                    }
                    
                    array_index++;
                    token = strtok_r(NULL, ",", &saveptr);
                }
            } else if ((value[0] == '"' && value[value_len - 1] == '"') ||
                       (value[0] == '\'' && value[value_len - 1] == '\'')) {
                // 字符串值（被引号包围）
                value[value_len - 1] = '\0';
                concord_config_set_string(cfg, current_path, value + 1);
            } else {
                // 其他值当作字符串处理
                concord_config_set_string(cfg, current_path, value);
            }
            
            // 跳到下一行
            if (*p == '\n') p++;
            
            // 回到上一级路径
            char *last_dot = strrchr(current_path, '.');
            if (last_dot) {
                *last_dot = '\0';
            } else {
                current_path[0] = '\0';
            }
        }
    }
    
    return cfg;
}

// 将配置转换为YAML字符串
char *concord_config_to_yaml(concord_config_t *cfg) {
    if (!cfg || !cfg->root) {
        return strdup("# Empty YAML file\n");
    }
    
    // 初始化缓冲区
    int buffer_size = 1024;
    char *buffer = (char *)malloc(buffer_size);
    if (!buffer) {
        return NULL;
    }
    
    buffer[0] = '\0';
    int length = 0;
    
    // 构建嵌套结构
    struct yaml_node {
        char *key;                // 键名
        config_item_type_t type;  // 值类型
        void *value;              // 值
        struct yaml_node **children;  // 子节点
        int child_count;          // 子节点数量
        int is_array;             // 是否是数组
    };
    
    // 创建根节点
    struct yaml_node *root = (struct yaml_node *)malloc(sizeof(struct yaml_node));
    if (!root) {
        free(buffer);
        return NULL;
    }
    
    root->key = strdup("");
    root->type = CONFIG_TYPE_OBJECT;
    root->value = NULL;
    root->children = NULL;
    root->child_count = 0;
    root->is_array = 0;
    
    // 遍历所有配置项，构建嵌套结构
    config_item_t *item = cfg->root;
    while (item) {
        char *key_copy = strdup(item->key);
        char *token, *saveptr;
        struct yaml_node *current = root;
        
        // 按点分隔键名
        token = strtok_r(key_copy, ".", &saveptr);
        while (token) {
            // 检查是否是数组索引
            char *array_start = strchr(token, '[');
            int is_array_item = 0;
            int array_index = 0;
            
            if (array_start) {
                *array_start = '\0';
                char *array_end = strchr(array_start + 1, ']');
                if (array_end) {
                    *array_end = '\0';
                    array_index = atoi(array_start + 1);
                    is_array_item = 1;
                }
            }
            
            // 查找或创建子节点
            int found = 0;
            for (int i = 0; i < current->child_count; i++) {
                if (strcmp(current->children[i]->key, token) == 0) {
                    current = current->children[i];
                    found = 1;
                    break;
                }
            }
            
            if (!found) {
                // 创建新子节点
                struct yaml_node *new_node = (struct yaml_node *)malloc(sizeof(struct yaml_node));
                if (!new_node) {
                    // 应该释放整个结构，但这里简化处理
                    free(key_copy);
                    free(buffer);
                    return NULL;
                }
                
                new_node->key = strdup(token);
                new_node->type = CONFIG_TYPE_OBJECT;
                new_node->value = NULL;
                new_node->children = NULL;
                new_node->child_count = 0;
                new_node->is_array = is_array_item;
                
                // 添加到父节点
                current->children = (struct yaml_node **)realloc(current->children, 
                                                               (current->child_count + 1) * sizeof(struct yaml_node *));
                current->children[current->child_count++] = new_node;
                current = new_node;
            }
            
            token = strtok_r(NULL, ".", &saveptr);
            
            // 如果没有更多的键名部分，设置值
            if (!token) {
                current->type = item->type;
                
                switch (item->type) {
                    case CONFIG_TYPE_INT:
                        current->value = malloc(sizeof(int64_t));
                        *((int64_t *)current->value) = item->value.int_val;
                        break;
                    case CONFIG_TYPE_FLOAT:
                        current->value = malloc(sizeof(double));
                        *((double *)current->value) = item->value.float_val;
                        break;
                    case CONFIG_TYPE_BOOL:
                        current->value = malloc(sizeof(int));
                        *((int *)current->value) = item->value.bool_val;
                        break;
                    case CONFIG_TYPE_STRING:
                        current->value = strdup(item->value.str_val);
                        break;
                    default:
                        current->value = NULL;
                        break;
                }
            }
        }
        
        free(key_copy);
        item = item->next;
    }
    
    // 递归生成YAML
    // (这里应该实现一个递归函数来生成YAML文本，但简化处理)
    
    // 输出简单的平铺结构
    item = cfg->root;
    while (item) {
        // 确保缓冲区足够大
        if (length + strlen(item->key) + 256 > buffer_size) {
            buffer_size *= 2;
            buffer = (char *)realloc(buffer, buffer_size);
            if (!buffer) {
                return NULL;
            }
        }
        
        // 输出键名
        length += snprintf(buffer + length, buffer_size - length, "%s: ", item->key);
        
        // 根据类型输出值
        switch (item->type) {
            case CONFIG_TYPE_INT:
                length += snprintf(buffer + length, buffer_size - length, "%lld\n", (long long)item->value.int_val);
                break;
            case CONFIG_TYPE_FLOAT:
                length += snprintf(buffer + length, buffer_size - length, "%g\n", item->value.float_val);
                break;
            case CONFIG_TYPE_BOOL:
                length += snprintf(buffer + length, buffer_size - length, "%s\n", item->value.bool_val ? "true" : "false");
                break;
            case CONFIG_TYPE_STRING:
                // 检查是否需要引号
                if (strchr(item->value.str_val, ':') || strchr(item->value.str_val, '#') || 
                    strchr(item->value.str_val, '\'') || strchr(item->value.str_val, '"')) {
                    length += snprintf(buffer + length, buffer_size - length, "\"%s\"\n", item->value.str_val);
                } else {
                    length += snprintf(buffer + length, buffer_size - length, "%s\n", item->value.str_val);
                }
                break;
            default:
                length += snprintf(buffer + length, buffer_size - length, "null\n");
                break;
        }
        
        item = item->next;
    }
    
    // 释放嵌套结构（应该递归释放整个结构，但这里简化处理）
    
    return buffer;
}

// TOML 解析器实现
concord_config_t *concord_config_parse_toml(const char *toml_str) {
    concord_config_t *cfg = concord_config_init();
    if (!cfg || !toml_str) {
        return NULL;
    }
    
    const char *p = toml_str;
    char current_table[256] = "";  // 当前表名
    
    // 逐行解析
    while (*p) {
        // 跳过空白
        while (*p && isspace(*p)) p++;
        
        // 空行或注释
        if (*p == '\0' || *p == '#' || *p == '\n') {
            // 跳到下一行
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            continue;
        }
        
        // 表定义 [table] 或 [[array_table]]
        if (*p == '[') {
            int is_array_table = 0;
            p++; // 跳过第一个 '['
            
            if (*p == '[') {
                is_array_table = 1;
                p++; // 跳过第二个 '['
            }
            
            // 读取表名
            const char *table_start = p;
            while (*p && *p != ']' && *p != '\n') p++;
            
            if (*p != ']') {
                fprintf(stderr, "Error: Unterminated table name\n");
                concord_config_destroy(cfg);
                return NULL;
            }
            
            // 保存表名
            int table_len = p - table_start;
            if (table_len >= sizeof(current_table) - 1) {
                table_len = sizeof(current_table) - 1;
            }
            
            strncpy(current_table, table_start, table_len);
            current_table[table_len] = '\0';
            
            p++; // 跳过第一个 ']'
            
            if (is_array_table && *p == ']') {
                p++; // 跳过第二个 ']'
                
                // 数组表处理（简化，不实现完整功能）
                // 实际TOML中，这表示数组的新元素
                // 这里简单地添加一个索引
                static int array_index = 0;
                char indexed_table[512];
                snprintf(indexed_table, sizeof(indexed_table), "%s[%d]", current_table, array_index++);
                strcpy(current_table, indexed_table);
            }
        } else {
            // 键值对
            const char *key_start = p;
            while (*p && *p != '=' && *p != '\n') p++;
            
            if (*p != '=') {
                fprintf(stderr, "Error: Invalid key-value pair\n");
                concord_config_destroy(cfg);
                return NULL;
            }
            
            // 提取键名并去除尾部空白
            int key_len = p - key_start;
            while (key_len > 0 && isspace(key_start[key_len - 1])) {
                key_len--;
            }
            
            char key[256] = {0};
            if (key_len >= sizeof(key)) {
                key_len = sizeof(key) - 1;
            }
            
            strncpy(key, key_start, key_len);
            key[key_len] = '\0';
            
            // 处理引号包裹的键
            if ((key[0] == '"' && key[key_len - 1] == '"') || 
                (key[0] == '\'' && key[key_len - 1] == '\'')) {
                memmove(key, key + 1, key_len - 2);
                key[key_len - 2] = '\0';
            }
            
            p++; // 跳过 '='
            
            // 跳过值前面的空白
            while (*p && isspace(*p) && *p != '\n') p++;
            
            // 读取值
            const char *value_start = p;
            
            // 处理不同类型的值
            if (*p == '"' || *p == '\'') {
                // 字符串值
                char quote = *p++;
                value_start = p;
                
                // 查找结束引号
                while (*p && *p != quote) {
                    if (*p == '\\' && *(p+1)) {
                        p += 2; // 跳过转义字符
                    } else {
                        p++;
                    }
                }
                
                if (*p != quote) {
                    fprintf(stderr, "Error: Unterminated string value\n");
                    concord_config_destroy(cfg);
                    return NULL;
                }
                
                // 构造字符串值
                int value_len = p - value_start;
                char *value = (char *)malloc(value_len + 1);
                if (!value) {
                    concord_config_destroy(cfg);
                    return NULL;
                }
                
                strncpy(value, value_start, value_len);
                value[value_len] = '\0';
                
                // 构造完整键名
                char full_key[512] = {0};
                if (current_table[0] != '\0') {
                    snprintf(full_key, sizeof(full_key), "%s.%s", current_table, key);
                } else {
                    strcpy(full_key, key);
                }
                
                // 设置配置值
                concord_config_set_string(cfg, full_key, value);
                
                free(value);
                p++; // 跳过结束引号
            } else if (*p == '[') {
                // 数组值
                p++; // 跳过 '['
                
                // 构造完整键名
                char base_key[512] = {0};
                if (current_table[0] != '\0') {
                    snprintf(base_key, sizeof(base_key), "%s.%s", current_table, key);
                } else {
                    strcpy(base_key, key);
                }
                
                int array_index = 0;
                
                // 解析数组元素
                while (*p) {
                    // 跳过空白
                    while (*p && isspace(*p)) p++;
                    
                    if (*p == ']') {
                        p++; // 数组结束
                        break;
                    }
                    
                    // 构造元素键名
                    char element_key[768];
                    snprintf(element_key, sizeof(element_key), "%s[%d]", base_key, array_index++);
                    
                    // 解析元素值
                    if (*p == '"' || *p == '\'') {
                        // 字符串值
                        char quote = *p++;
                        const char *str_start = p;
                        
                        // 查找结束引号
                        while (*p && *p != quote) {
                            if (*p == '\\' && *(p+1)) {
                                p += 2; // 跳过转义字符
                            } else {
                                p++;
                            }
                        }
                        
                        if (*p != quote) {
                            fprintf(stderr, "Error: Unterminated string value in array\n");
                            concord_config_destroy(cfg);
                            return NULL;
                        }
                        
                        // 构造字符串值
                        int str_len = p - str_start;
                        char *str_val = (char *)malloc(str_len + 1);
                        if (!str_val) {
                            concord_config_destroy(cfg);
                            return NULL;
                        }
                        
                        strncpy(str_val, str_start, str_len);
                        str_val[str_len] = '\0';
                        
                        // 设置配置值
                        concord_config_set_string(cfg, element_key, str_val);
                        
                        free(str_val);
                        p++; // 跳过结束引号
                    } else if (isdigit(*p) || *p == '-' || *p == '+') {
                        // 数字值
                        const char *num_start = p;
                        int is_float = 0;
                        
                        // 跳过符号
                        if (*p == '-' || *p == '+') p++;
                        
                        // 读取整数部分
                        while (*p && isdigit(*p)) p++;
                        
                        // 检查是否有小数部分
                        if (*p == '.') {
                            is_float = 1;
                            p++;
                            while (*p && isdigit(*p)) p++;
                        }
                        
                        // 检查科学计数法
                        if (*p == 'e' || *p == 'E') {
                            is_float = 1;
                            p++;
                            if (*p == '+' || *p == '-') p++;
                            while (*p && isdigit(*p)) p++;
                        }
                        
                        // 构造数字字符串
                        int num_len = p - num_start;
                        char *num_str = (char *)malloc(num_len + 1);
                        if (!num_str) {
                            concord_config_destroy(cfg);
                            return NULL;
                        }
                        
                        strncpy(num_str, num_start, num_len);
                        num_str[num_len] = '\0';
                        
                        // 设置配置值
                        if (is_float) {
                            double val = atof(num_str);
                            concord_config_set_float(cfg, element_key, val);
                        } else {
                            int64_t val = atoll(num_str);
                            concord_config_set_int(cfg, element_key, val);
                        }
                        
                        free(num_str);
                    } else if (strncmp(p, "true", 4) == 0) {
                        // 布尔值 true
                        concord_config_set_bool(cfg, element_key, 1);
                        p += 4;
                    } else if (strncmp(p, "false", 5) == 0) {
                        // 布尔值 false
                        concord_config_set_bool(cfg, element_key, 0);
                        p += 5;
                    } else {
                        // 未知值类型
                        fprintf(stderr, "Error: Unknown value type in array\n");
                        concord_config_destroy(cfg);
                        return NULL;
                    }
                    
                    // 跳过空白
                    while (*p && isspace(*p)) p++;
                    
                    // 逗号或结束方括号
                    if (*p == ',') {
                        p++; // 跳过逗号
                    } else if (*p == ']') {
                        p++; // 数组结束
                        break;
                    } else {
                        fprintf(stderr, "Error: Expected ',' or ']' in array\n");
                        concord_config_destroy(cfg);
                        return NULL;
                    }
                }
            } else if (*p == '{') {
                // 内联表（简化处理）
                p++; // 跳过 '{'
                
                // 构造基础键名
                char base_key[512] = {0};
                if (current_table[0] != '\0') {
                    snprintf(base_key, sizeof(base_key), "%s.%s", current_table, key);
                } else {
                    strcpy(base_key, key);
                }
                
                // 解析内联表键值对
                while (*p) {
                    // 跳过空白
                    while (*p && isspace(*p)) p++;
                    
                    if (*p == '}') {
                        p++; // 表结束
                        break;
                    }
                    
                    // 读取键名
                    const char *subkey_start = p;
                    while (*p && *p != '=' && *p != '\n' && *p != '}') p++;
                    
                    if (*p != '=') {
                        fprintf(stderr, "Error: Invalid key-value pair in inline table\n");
                        concord_config_destroy(cfg);
                        return NULL;
                    }
                    
                    // 提取键名并去除空白
                    int subkey_len = p - subkey_start;
                    while (subkey_len > 0 && isspace(subkey_start[subkey_len - 1])) {
                        subkey_len--;
                    }
                    
                    char subkey[256] = {0};
                    if (subkey_len >= sizeof(subkey)) {
                        subkey_len = sizeof(subkey) - 1;
                    }
                    
                    strncpy(subkey, subkey_start, subkey_len);
                    subkey[subkey_len] = '\0';
                    
                    // 处理引号包裹的键
                    if ((subkey[0] == '"' && subkey[subkey_len - 1] == '"') ||
                        (subkey[0] == '\'' && subkey[subkey_len - 1] == '\'')) {
                        memmove(subkey, subkey + 1, subkey_len - 2);
                        subkey[subkey_len - 2] = '\0';
                    }
                    
                    p++; // 跳过 '='
                    
                    // 跳过值前面的空白
                    while (*p && isspace(*p) && *p != '\n') p++;
                    
                    // 构造完整键名
                    char full_key[768] = {0};
                    snprintf(full_key, sizeof(full_key), "%s.%s", base_key, subkey);
                    
                    // 解析值
                    if (*p == '"' || *p == '\'') {
                        // 字符串值
                        char quote = *p++;
                        const char *str_start = p;
                        
                        // 查找结束引号
                        while (*p && *p != quote) {
                            if (*p == '\\' && *(p+1)) {
                                p += 2; // 跳过转义字符
                            } else {
                                p++;
                            }
                        }
                        
                        if (*p != quote) {
                            fprintf(stderr, "Error: Unterminated string value in inline table\n");
                            concord_config_destroy(cfg);
                            return NULL;
                        }
                        
                        // 构造字符串值
                        int str_len = p - str_start;
                        char *str_val = (char *)malloc(str_len + 1);
                        if (!str_val) {
                            concord_config_destroy(cfg);
                            return NULL;
                        }
                        
                        strncpy(str_val, str_start, str_len);
                        str_val[str_len] = '\0';
                        
                        // 设置配置值
                        concord_config_set_string(cfg, full_key, str_val);
                        
                        free(str_val);
                        p++; // 跳过结束引号
                    } else if (isdigit(*p) || *p == '-' || *p == '+') {
                        // 数字值
                        const char *num_start = p;
                        int is_float = 0;
                        
                        // 跳过符号
                        if (*p == '-' || *p == '+') p++;
                        
                        // 读取整数部分
                        while (*p && isdigit(*p)) p++;
                        
                        // 检查是否有小数部分
                        if (*p == '.') {
                            is_float = 1;
                            p++;
                            while (*p && isdigit(*p)) p++;
                        }
                        
                        // 检查科学计数法
                        if (*p == 'e' || *p == 'E') {
                            is_float = 1;
                            p++;
                            if (*p == '+' || *p == '-') p++;
                            while (*p && isdigit(*p)) p++;
                        }
                        
                        // 构造数字字符串
                        int num_len = p - num_start;
                        char *num_str = (char *)malloc(num_len + 1);
                        if (!num_str) {
                            concord_config_destroy(cfg);
                            return NULL;
                        }
                        
                        strncpy(num_str, num_start, num_len);
                        num_str[num_len] = '\0';
                        
                        // 设置配置值
                        if (is_float) {
                            double val = atof(num_str);
                            concord_config_set_float(cfg, full_key, val);
                        } else {
                            int64_t val = atoll(num_str);
                            concord_config_set_int(cfg, full_key, val);
                        }
                        
                        free(num_str);
                    } else if (strncmp(p, "true", 4) == 0) {
                        // 布尔值 true
                        concord_config_set_bool(cfg, full_key, 1);
                        p += 4;
                    } else if (strncmp(p, "false", 5) == 0) {
                        // 布尔值 false
                        concord_config_set_bool(cfg, full_key, 0);
                        p += 5;
                    } else {
                        // 未知值类型
                        fprintf(stderr, "Error: Unknown value type in inline table\n");
                        concord_config_destroy(cfg);
                        return NULL;
                    }
                    
                    // 跳过空白
                    while (*p && isspace(*p)) p++;
                    
                    // 逗号或结束花括号
                    if (*p == ',') {
                        p++; // 跳过逗号
                    } else if (*p == '}') {
                        p++; // 表结束
                        break;
                    } else {
                        fprintf(stderr, "Error: Expected ',' or '}' in inline table\n");
                        concord_config_destroy(cfg);
                        return NULL;
                    }
                }
            } else if (isdigit(*p) || *p == '-' || *p == '+') {
                // 数字值
                const char *num_start = p;
                int is_float = 0;
                
                // 跳过符号
                if (*p == '-' || *p == '+') p++;
                
                // 读取整数部分
                while (*p && isdigit(*p)) p++;
                
                // 检查是否有小数部分
                if (*p == '.') {
                    is_float = 1;
                    p++;
                    while (*p && isdigit(*p)) p++;
                }
                
                // 检查科学计数法
                if (*p == 'e' || *p == 'E') {
                    is_float = 1;
                    p++;
                    if (*p == '+' || *p == '-') p++;
                    while (*p && isdigit(*p)) p++;
                }
                
                // 构造数字字符串
                int num_len = p - num_start;
                char *num_str = (char *)malloc(num_len + 1);
                if (!num_str) {
                    concord_config_destroy(cfg);
                    return NULL;
                }
                
                strncpy(num_str, num_start, num_len);
                num_str[num_len] = '\0';
                
                // 构造完整键名
                char full_key[512] = {0};
                if (current_table[0] != '\0') {
                    snprintf(full_key, sizeof(full_key), "%s.%s", current_table, key);
                } else {
                    strcpy(full_key, key);
                }
                
                // 设置配置值
                if (is_float) {
                    double val = atof(num_str);
                    concord_config_set_float(cfg, full_key, val);
                } else {
                    int64_t val = atoll(num_str);
                    concord_config_set_int(cfg, full_key, val);
                }
                
                free(num_str);
            } else if (strncmp(p, "true", 4) == 0) {
                // 布尔值 true
                char full_key[512] = {0};
                if (current_table[0] != '\0') {
                    snprintf(full_key, sizeof(full_key), "%s.%s", current_table, key);
                } else {
                    strcpy(full_key, key);
                }
                
                concord_config_set_bool(cfg, full_key, 1);
                p += 4;
            } else if (strncmp(p, "false", 5) == 0) {
                // 布尔值 false
                char full_key[512] = {0};
                if (current_table[0] != '\0') {
                    snprintf(full_key, sizeof(full_key), "%s.%s", current_table, key);
                } else {
                    strcpy(full_key, key);
                }
                
                concord_config_set_bool(cfg, full_key, 0);
                p += 5;
            } else {
                // 未知值类型
                fprintf(stderr, "Error: Unknown value type\n");
                concord_config_destroy(cfg);
                return NULL;
            }
        }
        
        // 跳到下一行
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    
    return cfg;
}

// 将配置转换为TOML字符串
char *concord_config_to_toml(concord_config_t *cfg) {
    if (!cfg || !cfg->root) {
        return strdup("# Empty TOML file\n");
    }
    
    // 初始化缓冲区
    int buffer_size = 1024;
    char *buffer = (char *)malloc(buffer_size);
    if (!buffer) {
        return NULL;
    }
    
    int length = 0;
    buffer[0] = '\0';
    
    // 收集所有表和键
    struct table_entry {
        char *name;
        struct table_entry *next;
    };
    
    // 创建一个默认表
    struct table_entry *tables = (struct table_entry *)malloc(sizeof(struct table_entry));
    if (!tables) {
        free(buffer);
        return NULL;
    }
    
    tables->name = strdup("");
    tables->next = NULL;
    
    // 遍历所有配置项，收集表
    config_item_t *item = cfg->root;
    while (item) {
        char *dot = strchr(item->key, '.');
        if (dot) {
            // 有表名
            char table_name[256] = {0};
            strncpy(table_name, item->key, dot - item->key);
            
            // 检查表是否已存在
            int found = 0;
            struct table_entry *current = tables;
            while (current) {
                if (strcmp(current->name, table_name) == 0) {
                    found = 1;
                    break;
                }
                current = current->next;
            }
            
            // 如果表不存在，添加
            if (!found) {
                struct table_entry *new_table = (struct table_entry *)malloc(sizeof(struct table_entry));
                if (!new_table) {
                    // 清理
                    current = tables;
                    while (current) {
                        struct table_entry *next = current->next;
                        free(current->name);
                        free(current);
                        current = next;
                    }
                    free(buffer);
                    return NULL;
                }
                
                new_table->name = strdup(table_name);
                new_table->next = NULL;
                
                // 添加到表列表末尾
                current = tables;
                while (current->next) {
                    current = current->next;
                }
                current->next = new_table;
            }
        }
        
        item = item->next;
    }
    
    // 按表写入配置
    struct table_entry *current_table = tables;
    while (current_table) {
        // 写入表名
        if (current_table->name[0] != '\0') {
            // 确保缓冲区足够大
            if (length + strlen(current_table->name) + 32 > buffer_size) {
                buffer_size *= 2;
                buffer = (char *)realloc(buffer, buffer_size);
                if (!buffer) {
                    // 清理
                    struct table_entry *current = tables;
                    while (current) {
                        struct table_entry *next = current->next;
                        free(current->name);
                        free(current);
                        current = next;
                    }
                    return NULL;
                }
            }
            
            // 写入表标题
            length += snprintf(buffer + length, buffer_size - length, "\n[%s]\n", current_table->name);
        }
        
        // 写入该表的所有键值对
        item = cfg->root;
        while (item) {
            char *dot = strchr(item->key, '.');
            int in_current_table = 0;
            
            if (dot && current_table->name[0] != '\0') {
                // 检查是否属于当前表
                if (strncmp(item->key, current_table->name, dot - item->key) == 0 &&
                    strlen(current_table->name) == (size_t)(dot - item->key)) {
                    in_current_table = 1;
                    dot++;
                }
            } else if (!dot && current_table->name[0] == '\0') {
                // 无表前缀的键值对
                in_current_table = 1;
            }
            
            if (in_current_table) {
                const char *key_name = (dot) ? dot : item->key;
                
                // 确保缓冲区足够大
                if (length + strlen(key_name) + 256 > buffer_size) {
                    buffer_size *= 2;
                    buffer = (char *)realloc(buffer, buffer_size);
                    if (!buffer) {
                        // 清理
                        struct table_entry *current = tables;
                        while (current) {
                            struct table_entry *next = current->next;
                            free(current->name);
                            free(current);
                            current = next;
                        }
                        return NULL;
                    }
                }
                
                // 写入键名
                length += snprintf(buffer + length, buffer_size - length, "%s = ", key_name);
                
                // 根据类型写入值
                switch (item->type) {
                    case CONFIG_TYPE_INT:
                        length += snprintf(buffer + length, buffer_size - length, "%lld\n", (long long)item->value.int_val);
                        break;
                    case CONFIG_TYPE_FLOAT:
                        length += snprintf(buffer + length, buffer_size - length, "%g\n", item->value.float_val);
                        break;
                    case CONFIG_TYPE_BOOL:
                        length += snprintf(buffer + length, buffer_size - length, "%s\n", item->value.bool_val ? "true" : "false");
                        break;
                    case CONFIG_TYPE_STRING:
                        // 确保字符串正确地使用引号包围，并处理特殊字符
                        length += snprintf(buffer + length, buffer_size - length, "\"%s\"\n", item->value.str_val);
                        break;
                    default:
                        length += snprintf(buffer + length, buffer_size - length, "\n");
                        break;
                }
            }
            
            item = item->next;
        }
        
        current_table = current_table->next;
    }
    
    // 清理表列表
    current_table = tables;
    while (current_table) {
        struct table_entry *next = current_table->next;
        free(current_table->name);
        free(current_table);
        current_table = next;
    }
    
    return buffer;
}

// 注册自定义解析器
// 全局变量存储自定义解析器
static config_parser_func custom_parsers[CONFIG_FORMAT_AUTO];
static config_serializer_func custom_serializers[CONFIG_FORMAT_AUTO];

int concord_config_register_parser(config_format_t format, config_parser_func parser, config_serializer_func serializer) {
    if (format >= CONFIG_FORMAT_AUTO) {
        return -1; // 格式无效
    }
    
    if (!parser || !serializer) {
        return -1; // 函数指针无效
    }
    
    custom_parsers[format] = parser;
    custom_serializers[format] = serializer;
    
    return 0;
} 