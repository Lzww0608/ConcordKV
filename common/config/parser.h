#ifndef __CONCORD_CONFIG_PARSER_H__
#define __CONCORD_CONFIG_PARSER_H__

#include "config.h"

// 支持的配置文件格式
typedef enum {
    CONFIG_FORMAT_JSON = 0,  // JSON格式
    CONFIG_FORMAT_INI,       // INI格式
    CONFIG_FORMAT_YAML,      // YAML格式
    CONFIG_FORMAT_TOML,      // TOML格式
    CONFIG_FORMAT_AUTO       // 根据文件扩展名自动识别
} config_format_t;

// 配置解析选项
typedef struct {
    config_format_t format;  // 格式类型
    int case_sensitive;      // 键名是否区分大小写
    int allow_comments;      // 是否允许注释
    int allow_includes;      // 是否允许包含其他文件
} config_parser_options_t;

// 创建默认解析选项
config_parser_options_t concord_config_parser_default_options();

// 从文件加载配置（使用指定格式解析器）
concord_config_t *concord_config_load_with_format(const char *filename, config_format_t format);

// 保存配置到文件（使用指定格式）
int concord_config_save_with_format(concord_config_t *cfg, const char *filename, config_format_t format);

// 解析JSON字符串
concord_config_t *concord_config_parse_json(const char *json_str);

// 将配置转换为JSON字符串
char *concord_config_to_json(concord_config_t *cfg, int pretty);

// 解析INI字符串
concord_config_t *concord_config_parse_ini(const char *ini_str);

// 将配置转换为INI字符串
char *concord_config_to_ini(concord_config_t *cfg);

// 解析YAML字符串
concord_config_t *concord_config_parse_yaml(const char *yaml_str);

// 将配置转换为YAML字符串
char *concord_config_to_yaml(concord_config_t *cfg);

// 解析TOML字符串
concord_config_t *concord_config_parse_toml(const char *toml_str);

// 将配置转换为TOML字符串
char *concord_config_to_toml(concord_config_t *cfg);

// 根据文件扩展名猜测配置格式
config_format_t concord_config_guess_format(const char *filename);

// 注册自定义解析器
typedef concord_config_t *(*config_parser_func)(const char *str);
typedef char *(*config_serializer_func)(concord_config_t *cfg);

int concord_config_register_parser(config_format_t format, config_parser_func parser, config_serializer_func serializer);

#endif /* __CONCORD_CONFIG_PARSER_H__ */ 