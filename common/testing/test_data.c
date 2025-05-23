/**
 * @file test_data.c
 * @brief 测试数据生成器实现
 */

#define _GNU_SOURCE  // 为了使用strdup等函数
#include "testing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>  // 为了使用usleep

// 测试数据生成器结构
struct concord_test_data_gen_t {
    concord_test_data_config_t config;  // 配置
    char **key_cache;                   // 键缓存
    char **value_cache;                 // 值缓存
    size_t cache_size;                  // 缓存大小
    double *zipf_probs;                 // Zipf分布概率数组
    int initialized;                    // 是否已初始化
};

// 默认字符集
static const char DEFAULT_CHARSET[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
static const char ALPHA_CHARSET[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char NUMERIC_CHARSET[] = "0123456789";

// 生成随机整数 [min, max]
static int random_int(int min, int max) {
    if (min >= max) return min;
    return min + rand() % (max - min + 1);
}

// 生成随机字符串
static char *generate_random_string(size_t min_len, size_t max_len, const char *charset) {
    if (!charset) charset = DEFAULT_CHARSET;
    
    size_t charset_len = strlen(charset);
    size_t len = random_int(min_len, max_len);
    
    char *str = malloc(len + 1);
    if (!str) return NULL;
    
    for (size_t i = 0; i < len; i++) {
        str[i] = charset[rand() % charset_len];
    }
    str[len] = '\0';
    
    return str;
}

// 生成模式字符串
static char *generate_pattern_string(const char *pattern, size_t len, int index) {
    // 严格的参数验证
    if (!pattern || len == 0) {
        return generate_random_string(len > 0 ? len : 16, len > 0 ? len : 16, DEFAULT_CHARSET);
    }
    
    char *str = malloc(len + 1);
    if (!str) return NULL;
    
    size_t pattern_len = strlen(pattern);
    if (pattern_len == 0) {
        free(str);
        return generate_random_string(len, len, DEFAULT_CHARSET);
    }
    
    size_t pos = 0;
    
    for (size_t i = 0; i < pattern_len && pos < len; i++) {
        switch (pattern[i]) {
            case 'n': // 数字
                if (pos < len) str[pos++] = '0' + (index % 10);
                break;
            case 'a': // 小写字母
                if (pos < len) str[pos++] = 'a' + (index % 26);
                break;
            case 'A': // 大写字母
                if (pos < len) str[pos++] = 'A' + (index % 26);
                break;
            case 'x': // 随机字符
                if (pos < len) str[pos++] = DEFAULT_CHARSET[rand() % (sizeof(DEFAULT_CHARSET) - 1)];
                break;
            case '#': // 索引号
                {
                    char num_str[32];
                    snprintf(num_str, sizeof(num_str), "%d", index);
                    size_t num_len = strlen(num_str);
                    for (size_t j = 0; j < num_len && pos < len; j++) {
                        str[pos++] = num_str[j];
                    }
                }
                break;
            default:
                if (pos < len) str[pos++] = pattern[i];
                break;
        }
    }
    
    // 如果还有剩余长度，用随机字符填充
    while (pos < len) {
        str[pos++] = DEFAULT_CHARSET[rand() % (sizeof(DEFAULT_CHARSET) - 1)];
    }
    
    str[len] = '\0';
    return str;
}

// 计算Zipf分布概率
static void calculate_zipf_probabilities(concord_test_data_gen_t *generator) {
    if (!generator->config.use_zipf || generator->config.total_keys == 0) return;
    
    generator->zipf_probs = malloc(generator->config.total_keys * sizeof(double));
    if (!generator->zipf_probs) return;
    
    double s = generator->config.zipf_s;
    double sum = 0.0;
    
    // 计算归一化常数
    for (size_t i = 1; i <= generator->config.total_keys; i++) {
        sum += 1.0 / pow(i, s);
    }
    
    // 计算每个键的概率
    for (size_t i = 0; i < generator->config.total_keys; i++) {
        generator->zipf_probs[i] = (1.0 / pow(i + 1, s)) / sum;
    }
}

// 根据Zipf分布选择键索引
static size_t select_zipf_key_index(concord_test_data_gen_t *generator) {
    if (!generator->zipf_probs) return rand() % generator->config.total_keys;
    
    double r = (double)rand() / RAND_MAX;
    double cumulative = 0.0;
    
    for (size_t i = 0; i < generator->config.total_keys; i++) {
        cumulative += generator->zipf_probs[i];
        if (r <= cumulative) {
            return i;
        }
    }
    
    return generator->config.total_keys - 1;
}

// 初始化缓存
static int initialize_cache(concord_test_data_gen_t *generator) {
    if (generator->initialized) return 0;
    
    generator->cache_size = generator->config.total_keys;
    
    // 分配键缓存
    generator->key_cache = malloc(generator->cache_size * sizeof(char*));
    if (!generator->key_cache) return -1;
    
    // 分配值缓存
    generator->value_cache = malloc(generator->cache_size * sizeof(char*));
    if (!generator->value_cache) {
        free(generator->key_cache);
        return -1;
    }
    
    // 生成所有键值对
    for (size_t i = 0; i < generator->cache_size; i++) {
        // 生成键
        if (generator->config.key_pattern) {
            size_t key_len = random_int(generator->config.key_min_size, generator->config.key_max_size);
            generator->key_cache[i] = generate_pattern_string(generator->config.key_pattern, key_len, i);
        } else {
            generator->key_cache[i] = generate_random_string(
                generator->config.key_min_size, 
                generator->config.key_max_size, 
                NULL
            );
        }
        
        // 生成值
        if (generator->config.value_pattern) {
            size_t value_len = random_int(generator->config.value_min_size, generator->config.value_max_size);
            generator->value_cache[i] = generate_pattern_string(generator->config.value_pattern, value_len, i);
        } else {
            generator->value_cache[i] = generate_random_string(
                generator->config.value_min_size, 
                generator->config.value_max_size, 
                NULL
            );
        }
        
        if (!generator->key_cache[i] || !generator->value_cache[i]) {
            // 清理已分配的内存
            for (size_t j = 0; j <= i; j++) {
                free(generator->key_cache[j]);
                free(generator->value_cache[j]);
            }
            free(generator->key_cache);
            free(generator->value_cache);
            return -1;
        }
    }
    
    // 计算Zipf分布概率
    if (generator->config.use_zipf) {
        calculate_zipf_probabilities(generator);
    }
    
    generator->initialized = 1;
    return 0;
}

// 创建测试数据生成器
concord_test_data_gen_t *concord_test_data_gen_create(concord_test_data_config_t *config) {
    if (!config) return NULL;
    
    // 验证配置参数的合理性
    if (config->total_keys == 0 || 
        config->key_min_size == 0 || config->key_max_size == 0 ||
        config->value_min_size == 0 || config->value_max_size == 0 ||
        config->key_min_size > config->key_max_size ||
        config->value_min_size > config->value_max_size) {
        return NULL;
    }
    
    concord_test_data_gen_t *generator = calloc(1, sizeof(concord_test_data_gen_t));
    if (!generator) return NULL;
    
    // 复制配置
    generator->config = *config;
    
    // 安全地复制字符串指针
    if (config->key_pattern && strlen(config->key_pattern) > 0) {
        generator->config.key_pattern = strdup(config->key_pattern);
        if (!generator->config.key_pattern) {
            free(generator);
            return NULL;
        }
    } else {
        generator->config.key_pattern = NULL;
    }
    
    if (config->value_pattern && strlen(config->value_pattern) > 0) {
        generator->config.value_pattern = strdup(config->value_pattern);
        if (!generator->config.value_pattern) {
            free(generator->config.key_pattern);
            free(generator);
            return NULL;
        }
    } else {
        generator->config.value_pattern = NULL;
    }
    
    // 设置随机种子
    srand(time(NULL));
    
    return generator;
}

// 销毁测试数据生成器
void concord_test_data_gen_destroy(concord_test_data_gen_t *generator) {
    if (!generator) return;
    
    // 清理缓存
    if (generator->key_cache) {
        for (size_t i = 0; i < generator->cache_size; i++) {
            free(generator->key_cache[i]);
        }
        free(generator->key_cache);
    }
    
    if (generator->value_cache) {
        for (size_t i = 0; i < generator->cache_size; i++) {
            free(generator->value_cache[i]);
        }
        free(generator->value_cache);
    }
    
    free(generator->zipf_probs);
    free(generator->config.key_pattern);
    free(generator->config.value_pattern);
    free(generator);
}

// 生成随机键
char *concord_test_data_gen_key(concord_test_data_gen_t *generator) {
    if (!generator) return NULL;
    
    // 如果有缓存且启用了Zipf分布，从缓存中选择
    if (generator->config.use_zipf && generator->config.total_keys > 0) {
        if (initialize_cache(generator) != 0) return NULL;
        
        size_t index = select_zipf_key_index(generator);
        return strdup(generator->key_cache[index]);
    }
    
    // 否则生成新的随机键
    if (generator->config.key_pattern) {
        size_t key_len = random_int(generator->config.key_min_size, generator->config.key_max_size);
        return generate_pattern_string(generator->config.key_pattern, key_len, rand());
    } else {
        return generate_random_string(
            generator->config.key_min_size, 
            generator->config.key_max_size, 
            NULL
        );
    }
}

// 生成随机值
char *concord_test_data_gen_value(concord_test_data_gen_t *generator) {
    if (!generator) return NULL;
    
    if (generator->config.value_pattern) {
        size_t value_len = random_int(generator->config.value_min_size, generator->config.value_max_size);
        return generate_pattern_string(generator->config.value_pattern, value_len, rand());
    } else {
        return generate_random_string(
            generator->config.value_min_size, 
            generator->config.value_max_size, 
            NULL
        );
    }
}

// 生成键值对
int concord_test_data_gen_kv_pair(concord_test_data_gen_t *generator, char **key, char **value) {
    if (!generator || !key || !value) return -1;
    
    *key = concord_test_data_gen_key(generator);
    *value = concord_test_data_gen_value(generator);
    
    if (!*key || !*value) {
        free(*key);
        free(*value);
        *key = NULL;
        *value = NULL;
        return -1;
    }
    
    return 0;
}

// 生成测试数据集
int concord_test_data_gen_dataset(concord_test_data_gen_t *generator, const char *output_file) {
    if (!generator || !output_file) return -1;
    
    FILE *fp = fopen(output_file, "w");
    if (!fp) return -1;
    
    printf("生成测试数据集: %s\n", output_file);
    printf("键数量: %zu\n", generator->config.total_keys);
    printf("键大小: %zu - %zu\n", generator->config.key_min_size, generator->config.key_max_size);
    printf("值大小: %zu - %zu\n", generator->config.value_min_size, generator->config.value_max_size);
    
    if (generator->config.use_zipf) {
        printf("使用Zipf分布 (s=%.2f)\n", generator->config.zipf_s);
        if (initialize_cache(generator) != 0) {
            fclose(fp);
            return -1;
        }
    }
    
    // 写入文件头
    fprintf(fp, "# ConcordKV Test Dataset\n");
    fprintf(fp, "# Total Keys: %zu\n", generator->config.total_keys);
    fprintf(fp, "# Key Size: %zu - %zu\n", generator->config.key_min_size, generator->config.key_max_size);
    fprintf(fp, "# Value Size: %zu - %zu\n", generator->config.value_min_size, generator->config.value_max_size);
    if (generator->config.use_zipf) {
        fprintf(fp, "# Zipf Distribution: s=%.2f\n", generator->config.zipf_s);
    }
    fprintf(fp, "# Format: KEY<TAB>VALUE\n");
    fprintf(fp, "\n");
    
    // 生成数据
    for (size_t i = 0; i < generator->config.total_keys; i++) {
        char *key, *value;
        
        if (generator->initialized && generator->key_cache && generator->value_cache) {
            // 使用缓存数据
            key = strdup(generator->key_cache[i]);
            value = strdup(generator->value_cache[i]);
        } else {
            // 动态生成
            if (concord_test_data_gen_kv_pair(generator, &key, &value) != 0) {
                continue;
            }
        }
        
        if (key && value) {
            fprintf(fp, "%s\t%s\n", key, value);
        }
        
        free(key);
        free(value);
        
        // 进度显示
        if ((i + 1) % 10000 == 0 || i == generator->config.total_keys - 1) {
            printf("已生成: %zu/%zu (%.1f%%)\n", 
                   i + 1, generator->config.total_keys, 
                   (double)(i + 1) / generator->config.total_keys * 100.0);
        }
    }
    
    fclose(fp);
    printf("测试数据集生成完成: %s\n", output_file);
    return 0;
}

// 加载测试数据集
int concord_test_data_load_dataset(const char *input_file, char ***keys, char ***values, size_t *count) {
    if (!input_file || !keys || !values || !count) return -1;
    
    FILE *fp = fopen(input_file, "r");
    if (!fp) return -1;
    
    printf("加载测试数据集: %s\n", input_file);
    
    // 第一次扫描，统计行数
    size_t line_count = 0;
    char line[8192];
    
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] != '#' && strlen(line) > 1) {
            line_count++;
        }
    }
    
    if (line_count == 0) {
        fclose(fp);
        return -1;
    }
    
    // 分配内存
    *keys = malloc(line_count * sizeof(char*));
    *values = malloc(line_count * sizeof(char*));
    
    if (!*keys || !*values) {
        free(*keys);
        free(*values);
        fclose(fp);
        return -1;
    }
    
    // 第二次扫描，读取数据
    rewind(fp);
    size_t loaded = 0;
    
    while (fgets(line, sizeof(line), fp) && loaded < line_count) {
        if (line[0] == '#' || strlen(line) <= 1) continue;
        
        // 移除换行符
        char *newline = strchr(line, '\n');
        if (newline) *newline = '\0';
        
        // 查找制表符分隔符
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        
        *tab = '\0';
        char *key = line;
        char *value = tab + 1;
        
        (*keys)[loaded] = strdup(key);
        (*values)[loaded] = strdup(value);
        
        if (!(*keys)[loaded] || !(*values)[loaded]) {
            // 清理已分配的内存
            for (size_t i = 0; i <= loaded; i++) {
                free((*keys)[i]);
                free((*values)[i]);
            }
            free(*keys);
            free(*values);
            fclose(fp);
            return -1;
        }
        
        loaded++;
    }
    
    fclose(fp);
    *count = loaded;
    
    printf("加载了 %zu 个键值对\n", loaded);
    return 0;
}

// 创建默认测试数据配置
concord_test_data_config_t concord_test_data_default_config(void) {
    concord_test_data_config_t config = {
        .total_keys = 10000,
        .key_min_size = 8,
        .key_max_size = 32,
        .value_min_size = 64,
        .value_max_size = 512,
        .key_pattern = NULL,
        .value_pattern = NULL,
        .use_zipf = 0,
        .zipf_s = 1.0
    };
    return config;
} 