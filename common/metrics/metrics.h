/**
 * @file metrics.h
 * @brief ConcordKV统一指标收集与报告框架
 */

#ifndef __CONCORD_METRICS_H__
#define __CONCORD_METRICS_H__

#include <stdint.h>
#include <time.h>
#include <pthread.h>

// 指标类型
typedef enum {
    CONCORD_METRIC_COUNTER = 0,   // 累加计数器，只增不减
    CONCORD_METRIC_GAUGE,         // 仪表盘，可增可减
    CONCORD_METRIC_HISTOGRAM,     // 直方图，记录数值分布
    CONCORD_METRIC_METER          // 计量表，记录事件速率
} concord_metric_type_t;

// 直方图桶配置
typedef struct {
    double min;       // 最小边界
    double max;       // 最大边界
    int bucket_count; // 桶数量
} concord_histogram_config_t;

// 指标结构前向声明
typedef struct concord_metric_t concord_metric_t;

// 指标仓库前向声明
typedef struct concord_metrics_repo_t concord_metrics_repo_t;

/**
 * @brief 创建指标仓库
 * 
 * @param capacity 初始容量
 * @return 指标仓库指针
 */
concord_metrics_repo_t *concord_metrics_repo_create(int capacity);

/**
 * @brief 销毁指标仓库
 * 
 * @param repo 指标仓库
 */
void concord_metrics_repo_destroy(concord_metrics_repo_t *repo);

/**
 * @brief 创建计数器
 * 
 * @param repo 指标仓库
 * @param name 指标名称
 * @param description 指标描述
 * @param initial_value 初始值
 * @return 指标指针，失败返回NULL
 */
concord_metric_t *concord_metrics_create_counter(
    concord_metrics_repo_t *repo,
    const char *name,
    const char *description,
    int64_t initial_value
);

/**
 * @brief 创建仪表盘
 * 
 * @param repo 指标仓库
 * @param name 指标名称
 * @param description 指标描述
 * @param initial_value 初始值
 * @return 指标指针，失败返回NULL
 */
concord_metric_t *concord_metrics_create_gauge(
    concord_metrics_repo_t *repo,
    const char *name,
    const char *description,
    double initial_value
);

/**
 * @brief 创建直方图
 * 
 * @param repo 指标仓库
 * @param name 指标名称
 * @param description 指标描述
 * @param config 直方图配置
 * @return 指标指针，失败返回NULL
 */
concord_metric_t *concord_metrics_create_histogram(
    concord_metrics_repo_t *repo,
    const char *name,
    const char *description,
    concord_histogram_config_t *config
);

/**
 * @brief 创建计量表
 * 
 * @param repo 指标仓库
 * @param name 指标名称
 * @param description 指标描述
 * @return 指标指针，失败返回NULL
 */
concord_metric_t *concord_metrics_create_meter(
    concord_metrics_repo_t *repo,
    const char *name,
    const char *description
);

/**
 * @brief 增加计数器值
 * 
 * @param metric 计数器指标
 * @param value 增加的值
 * @return 成功返回0，失败返回-1
 */
int concord_metrics_counter_inc(concord_metric_t *metric, int64_t value);

/**
 * @brief 设置仪表盘值
 * 
 * @param metric 仪表盘指标
 * @param value 设置的值
 * @return 成功返回0，失败返回-1
 */
int concord_metrics_gauge_set(concord_metric_t *metric, double value);

/**
 * @brief 增加仪表盘值
 * 
 * @param metric 仪表盘指标
 * @param value 增加的值
 * @return 成功返回0，失败返回-1
 */
int concord_metrics_gauge_inc(concord_metric_t *metric, double value);

/**
 * @brief 减少仪表盘值
 * 
 * @param metric 仪表盘指标
 * @param value 减少的值
 * @return 成功返回0，失败返回-1
 */
int concord_metrics_gauge_dec(concord_metric_t *metric, double value);

/**
 * @brief 记录直方图观测值
 * 
 * @param metric 直方图指标
 * @param value 观测值
 * @return 成功返回0，失败返回-1
 */
int concord_metrics_histogram_observe(concord_metric_t *metric, double value);

/**
 * @brief 记录计量表事件
 * 
 * @param metric 计量表指标
 * @param count 事件计数
 * @return 成功返回0，失败返回-1
 */
int concord_metrics_meter_mark(concord_metric_t *metric, int64_t count);

/**
 * @brief 获取指标的类型
 * 
 * @param metric 指标
 * @return 指标类型
 */
concord_metric_type_t concord_metrics_get_type(concord_metric_t *metric);

/**
 * @brief 获取指标的名称
 * 
 * @param metric 指标
 * @return 指标名称
 */
const char *concord_metrics_get_name(concord_metric_t *metric);

/**
 * @brief 获取指标的描述
 * 
 * @param metric 指标
 * @return 指标描述
 */
const char *concord_metrics_get_description(concord_metric_t *metric);

/**
 * @brief 获取计数器的当前值
 * 
 * @param metric 计数器指标
 * @return 计数器值，失败返回-1
 */
int64_t concord_metrics_counter_get(concord_metric_t *metric);

/**
 * @brief 获取仪表盘的当前值
 * 
 * @param metric 仪表盘指标
 * @return 仪表盘值，失败返回NAN
 */
double concord_metrics_gauge_get(concord_metric_t *metric);

/**
 * @brief 获取直方图的观测值计数
 * 
 * @param metric 直方图指标
 * @return 观测值计数，失败返回-1
 */
int64_t concord_metrics_histogram_count(concord_metric_t *metric);

/**
 * @brief 获取直方图的某个百分位数值
 * 
 * @param metric 直方图指标
 * @param percentile 百分位数(0.0-1.0)
 * @return 百分位数值，失败返回NAN
 */
double concord_metrics_histogram_percentile(concord_metric_t *metric, double percentile);

/**
 * @brief 获取直方图的平均值
 * 
 * @param metric 直方图指标
 * @return 平均值，失败返回NAN
 */
double concord_metrics_histogram_mean(concord_metric_t *metric);

/**
 * @brief 获取计量表的1分钟事件速率
 * 
 * @param metric 计量表指标
 * @return 1分钟事件速率，失败返回NAN
 */
double concord_metrics_meter_rate1(concord_metric_t *metric);

/**
 * @brief 获取计量表的5分钟事件速率
 * 
 * @param metric 计量表指标
 * @return 5分钟事件速率，失败返回NAN
 */
double concord_metrics_meter_rate5(concord_metric_t *metric);

/**
 * @brief 获取计量表的15分钟事件速率
 * 
 * @param metric 计量表指标
 * @return 15分钟事件速率，失败返回NAN
 */
double concord_metrics_meter_rate15(concord_metric_t *metric);

/**
 * @brief 获取计量表的平均事件速率
 * 
 * @param metric 计量表指标
 * @return 平均事件速率，失败返回NAN
 */
double concord_metrics_meter_rate_mean(concord_metric_t *metric);

/**
 * @brief 根据名称查找指标
 * 
 * @param repo 指标仓库
 * @param name 指标名称
 * @return 指标指针，未找到返回NULL
 */
concord_metric_t *concord_metrics_find(concord_metrics_repo_t *repo, const char *name);

/**
 * @brief 获取仓库中的指标数量
 * 
 * @param repo 指标仓库
 * @return 指标数量
 */
int concord_metrics_count(concord_metrics_repo_t *repo);

/**
 * @brief 枚举仓库中的所有指标
 * 
 * @param repo 指标仓库
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return 成功返回0，失败返回-1
 */
int concord_metrics_foreach(
    concord_metrics_repo_t *repo,
    void (*callback)(concord_metric_t *metric, void *user_data),
    void *user_data
);

// 输出格式类型
typedef enum {
    CONCORD_METRICS_FORMAT_TEXT = 0,  // 纯文本格式
    CONCORD_METRICS_FORMAT_JSON,      // JSON格式
    CONCORD_METRICS_FORMAT_PROMETHEUS // Prometheus格式
} concord_metrics_format_t;

/**
 * @brief 将指标导出为指定格式的字符串
 * 
 * @param repo 指标仓库
 * @param format 输出格式
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小
 * @return 成功返回写入的字节数，失败返回-1
 */
int concord_metrics_dump(
    concord_metrics_repo_t *repo,
    concord_metrics_format_t format,
    char *buffer,
    size_t size
);

/**
 * @brief 启动HTTP指标服务器
 * 
 * @param repo 指标仓库
 * @param host 主机地址
 * @param port 端口
 * @param path 指标路径
 * @return 成功返回0，失败返回-1
 */
int concord_metrics_start_server(
    concord_metrics_repo_t *repo,
    const char *host,
    int port,
    const char *path
);

/**
 * @brief 停止HTTP指标服务器
 * 
 * @param repo 指标仓库
 * @return 成功返回0，失败返回-1
 */
int concord_metrics_stop_server(concord_metrics_repo_t *repo);

#endif /* __CONCORD_METRICS_H__ */ 