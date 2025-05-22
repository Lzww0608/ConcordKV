/**
 * @file metrics.c
 * @brief ConcordKV统一指标收集与报告框架实现
 */

#include "metrics.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>
#include <assert.h>

// 最小指标哈希表大小
#define METRICS_MIN_CAPACITY 16

// 指标存储值
typedef union {
    int64_t counter_value;        // 计数器值
    double gauge_value;           // 仪表盘值
    struct {                      // 直方图值
        double *buckets;          // 桶数组
        double *values;           // 最近的观测值（环形缓冲区）
        int bucket_count;         // 桶数量
        int values_capacity;      // 观测值容量
        int values_count;         // 观测值计数
        int values_index;         // 下一个观测值索引
        double min;               // 最小边界
        double max;               // 最大边界
        double bucket_width;      // 桶宽度
    } histogram;
    struct {                      // 计量表值
        int64_t count;            // 总计数
        double rate1m;            // 1分钟速率
        double rate5m;            // 5分钟速率
        double rate15m;           // 15分钟速率
        double rate_mean;         // 平均速率
        uint64_t start_time;      // 开始时间(毫秒)
        uint64_t last_update;     // 最后更新时间(毫秒)
    } meter;
} metric_value_t;

// 指标结构
struct concord_metric_t {
    char *name;                   // 指标名称
    char *description;            // 指标描述
    concord_metric_type_t type;   // 指标类型
    pthread_mutex_t mutex;        // 互斥锁
    metric_value_t value;         // 指标值
    struct concord_metric_t *next; // 哈希冲突链表
};

// 指标仓库结构
struct concord_metrics_repo_t {
    concord_metric_t **buckets;   // 哈希桶
    int capacity;                 // 容量
    int size;                     // 大小
    pthread_mutex_t mutex;        // 互斥锁
    int server_running;           // 服务器是否运行
    int server_port;              // 服务器端口
};

// 计算字符串的哈希值
static uint32_t hash_string(const char *str) {
    uint32_t hash = 5381;
    int c;

    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }

    return hash;
}

// 获取当前时间戳(毫秒)
static uint64_t get_current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 创建指标仓库
concord_metrics_repo_t *concord_metrics_repo_create(int capacity) {
    if (capacity < METRICS_MIN_CAPACITY) {
        capacity = METRICS_MIN_CAPACITY;
    }

    concord_metrics_repo_t *repo = (concord_metrics_repo_t *)calloc(1, sizeof(concord_metrics_repo_t));
    if (!repo) {
        return NULL;
    }

    repo->buckets = (concord_metric_t **)calloc(capacity, sizeof(concord_metric_t *));
    if (!repo->buckets) {
        free(repo);
        return NULL;
    }

    repo->capacity = capacity;
    repo->size = 0;
    repo->server_running = 0;
    repo->server_port = 0;

    if (pthread_mutex_init(&repo->mutex, NULL) != 0) {
        free(repo->buckets);
        free(repo);
        return NULL;
    }

    return repo;
}

// 销毁指标
static void metric_destroy(concord_metric_t *metric) {
    if (!metric) {
        return;
    }

    if (metric->type == CONCORD_METRIC_HISTOGRAM) {
        free(metric->value.histogram.buckets);
        free(metric->value.histogram.values);
    }

    free(metric->name);
    free(metric->description);
    pthread_mutex_destroy(&metric->mutex);
    free(metric);
}

// 销毁指标仓库
void concord_metrics_repo_destroy(concord_metrics_repo_t *repo) {
    if (!repo) {
        return;
    }

    // 停止服务器
    if (repo->server_running) {
        concord_metrics_stop_server(repo);
    }

    // 销毁所有指标
    for (int i = 0; i < repo->capacity; i++) {
        concord_metric_t *metric = repo->buckets[i];
        while (metric) {
            concord_metric_t *next = metric->next;
            metric_destroy(metric);
            metric = next;
        }
    }

    free(repo->buckets);
    pthread_mutex_destroy(&repo->mutex);
    free(repo);
}

// 添加指标到仓库
static int add_metric_to_repo(concord_metrics_repo_t *repo, concord_metric_t *metric) {
    if (!repo || !metric) {
        return -1;
    }

    pthread_mutex_lock(&repo->mutex);

    // 计算哈希值
    uint32_t hash = hash_string(metric->name);
    int index = hash % repo->capacity;

    // 检查是否已存在
    concord_metric_t *current = repo->buckets[index];
    while (current) {
        if (strcmp(current->name, metric->name) == 0) {
            pthread_mutex_unlock(&repo->mutex);
            return -1; // 已存在
        }
        current = current->next;
    }

    // 添加到链表头部
    metric->next = repo->buckets[index];
    repo->buckets[index] = metric;
    repo->size++;

    pthread_mutex_unlock(&repo->mutex);
    return 0;
}

// 创建计数器
concord_metric_t *concord_metrics_create_counter(
    concord_metrics_repo_t *repo,
    const char *name,
    const char *description,
    int64_t initial_value
) {
    if (!repo || !name) {
        return NULL;
    }

    concord_metric_t *metric = (concord_metric_t *)calloc(1, sizeof(concord_metric_t));
    if (!metric) {
        return NULL;
    }

    metric->name = strdup(name);
    metric->description = description ? strdup(description) : NULL;
    metric->type = CONCORD_METRIC_COUNTER;
    metric->value.counter_value = initial_value;

    if (!metric->name || (description && !metric->description)) {
        metric_destroy(metric);
        return NULL;
    }

    if (pthread_mutex_init(&metric->mutex, NULL) != 0) {
        metric_destroy(metric);
        return NULL;
    }

    if (add_metric_to_repo(repo, metric) != 0) {
        metric_destroy(metric);
        return NULL;
    }

    return metric;
}

// 创建仪表盘
concord_metric_t *concord_metrics_create_gauge(
    concord_metrics_repo_t *repo,
    const char *name,
    const char *description,
    double initial_value
) {
    if (!repo || !name) {
        return NULL;
    }

    concord_metric_t *metric = (concord_metric_t *)calloc(1, sizeof(concord_metric_t));
    if (!metric) {
        return NULL;
    }

    metric->name = strdup(name);
    metric->description = description ? strdup(description) : NULL;
    metric->type = CONCORD_METRIC_GAUGE;
    metric->value.gauge_value = initial_value;

    if (!metric->name || (description && !metric->description)) {
        metric_destroy(metric);
        return NULL;
    }

    if (pthread_mutex_init(&metric->mutex, NULL) != 0) {
        metric_destroy(metric);
        return NULL;
    }

    if (add_metric_to_repo(repo, metric) != 0) {
        metric_destroy(metric);
        return NULL;
    }

    return metric;
}

// 创建直方图
concord_metric_t *concord_metrics_create_histogram(
    concord_metrics_repo_t *repo,
    const char *name,
    const char *description,
    concord_histogram_config_t *config
) {
    if (!repo || !name || !config || config->bucket_count <= 0 || config->min >= config->max) {
        return NULL;
    }

    concord_metric_t *metric = (concord_metric_t *)calloc(1, sizeof(concord_metric_t));
    if (!metric) {
        return NULL;
    }

    metric->name = strdup(name);
    metric->description = description ? strdup(description) : NULL;
    metric->type = CONCORD_METRIC_HISTOGRAM;

    // 初始化直方图
    metric->value.histogram.bucket_count = config->bucket_count;
    metric->value.histogram.min = config->min;
    metric->value.histogram.max = config->max;
    metric->value.histogram.bucket_width = (config->max - config->min) / config->bucket_count;
    
    // 分配桶
    metric->value.histogram.buckets = (double *)calloc(config->bucket_count, sizeof(double));
    if (!metric->value.histogram.buckets) {
        metric_destroy(metric);
        return NULL;
    }

    // 分配观测值缓冲区（存储最近1000个观测值，用于计算百分位数）
    metric->value.histogram.values_capacity = 1000;
    metric->value.histogram.values = (double *)calloc(
        metric->value.histogram.values_capacity, sizeof(double)
    );
    if (!metric->value.histogram.values) {
        metric_destroy(metric);
        return NULL;
    }

    metric->value.histogram.values_count = 0;
    metric->value.histogram.values_index = 0;

    if (!metric->name || (description && !metric->description)) {
        metric_destroy(metric);
        return NULL;
    }

    if (pthread_mutex_init(&metric->mutex, NULL) != 0) {
        metric_destroy(metric);
        return NULL;
    }

    if (add_metric_to_repo(repo, metric) != 0) {
        metric_destroy(metric);
        return NULL;
    }

    return metric;
}

// 创建计量表
concord_metric_t *concord_metrics_create_meter(
    concord_metrics_repo_t *repo,
    const char *name,
    const char *description
) {
    if (!repo || !name) {
        return NULL;
    }

    concord_metric_t *metric = (concord_metric_t *)calloc(1, sizeof(concord_metric_t));
    if (!metric) {
        return NULL;
    }

    metric->name = strdup(name);
    metric->description = description ? strdup(description) : NULL;
    metric->type = CONCORD_METRIC_METER;
    
    // 初始化计量表
    metric->value.meter.count = 0;
    metric->value.meter.rate1m = 0.0;
    metric->value.meter.rate5m = 0.0;
    metric->value.meter.rate15m = 0.0;
    metric->value.meter.rate_mean = 0.0;
    metric->value.meter.start_time = get_current_time_ms();
    metric->value.meter.last_update = metric->value.meter.start_time;

    if (!metric->name || (description && !metric->description)) {
        metric_destroy(metric);
        return NULL;
    }

    if (pthread_mutex_init(&metric->mutex, NULL) != 0) {
        metric_destroy(metric);
        return NULL;
    }

    if (add_metric_to_repo(repo, metric) != 0) {
        metric_destroy(metric);
        return NULL;
    }

    return metric;
}

// 增加计数器值
int concord_metrics_counter_inc(concord_metric_t *metric, int64_t value) {
    if (!metric || metric->type != CONCORD_METRIC_COUNTER || value < 0) {
        return -1;
    }

    pthread_mutex_lock(&metric->mutex);
    metric->value.counter_value += value;
    pthread_mutex_unlock(&metric->mutex);

    return 0;
}

// 设置仪表盘值
int concord_metrics_gauge_set(concord_metric_t *metric, double value) {
    if (!metric || metric->type != CONCORD_METRIC_GAUGE) {
        return -1;
    }

    pthread_mutex_lock(&metric->mutex);
    metric->value.gauge_value = value;
    pthread_mutex_unlock(&metric->mutex);

    return 0;
}

// 增加仪表盘值
int concord_metrics_gauge_inc(concord_metric_t *metric, double value) {
    if (!metric || metric->type != CONCORD_METRIC_GAUGE) {
        return -1;
    }

    pthread_mutex_lock(&metric->mutex);
    metric->value.gauge_value += value;
    pthread_mutex_unlock(&metric->mutex);

    return 0;
}

// 减少仪表盘值
int concord_metrics_gauge_dec(concord_metric_t *metric, double value) {
    if (!metric || metric->type != CONCORD_METRIC_GAUGE) {
        return -1;
    }

    pthread_mutex_lock(&metric->mutex);
    metric->value.gauge_value -= value;
    pthread_mutex_unlock(&metric->mutex);

    return 0;
}

// 记录直方图观测值
int concord_metrics_histogram_observe(concord_metric_t *metric, double value) {
    if (!metric || metric->type != CONCORD_METRIC_HISTOGRAM) {
        return -1;
    }

    pthread_mutex_lock(&metric->mutex);

    // 更新桶计数
    if (value >= metric->value.histogram.min && value <= metric->value.histogram.max) {
        int bucket = (int)((value - metric->value.histogram.min) / metric->value.histogram.bucket_width);
        if (bucket >= metric->value.histogram.bucket_count) {
            bucket = metric->value.histogram.bucket_count - 1;
        }
        metric->value.histogram.buckets[bucket]++;
    } else if (value < metric->value.histogram.min) {
        // 小于最小值，放入第一个桶
        metric->value.histogram.buckets[0]++;
    } else {
        // 大于最大值，放入最后一个桶
        metric->value.histogram.buckets[metric->value.histogram.bucket_count - 1]++;
    }

    // 添加到观测值数组
    metric->value.histogram.values[metric->value.histogram.values_index] = value;
    metric->value.histogram.values_index = 
        (metric->value.histogram.values_index + 1) % metric->value.histogram.values_capacity;
    
    if (metric->value.histogram.values_count < metric->value.histogram.values_capacity) {
        metric->value.histogram.values_count++;
    }

    pthread_mutex_unlock(&metric->mutex);

    return 0;
}

// 指数移动平均权重常量
#define M1_ALPHA  0.08  // 1分钟权重系数: 1 - exp(-5/60)
#define M5_ALPHA  0.016 // 5分钟权重系数: 1 - exp(-5/300)
#define M15_ALPHA 0.005 // 15分钟权重系数: 1 - exp(-5/900)

// 更新指数移动平均
static double update_ema(double current, double new_value, double alpha) {
    return current + alpha * (new_value - current);
}

// 记录计量表事件
int concord_metrics_meter_mark(concord_metric_t *metric, int64_t count) {
    if (!metric || metric->type != CONCORD_METRIC_METER || count < 0) {
        return -1;
    }

    pthread_mutex_lock(&metric->mutex);

    uint64_t now = get_current_time_ms();
    uint64_t interval = now - metric->value.meter.last_update;
    
    // 总计数
    metric->value.meter.count += count;

    // 仅当间隔大于5秒时才更新速率
    if (interval >= 5000) {
        double seconds = interval / 1000.0;
        double instant_rate = count / seconds;

        // 更新各时间窗口的速率
        metric->value.meter.rate1m = update_ema(metric->value.meter.rate1m, instant_rate, M1_ALPHA);
        metric->value.meter.rate5m = update_ema(metric->value.meter.rate5m, instant_rate, M5_ALPHA);
        metric->value.meter.rate15m = update_ema(metric->value.meter.rate15m, instant_rate, M15_ALPHA);

        // 更新平均速率
        uint64_t total_seconds = (now - metric->value.meter.start_time) / 1000;
        if (total_seconds > 0) {
            metric->value.meter.rate_mean = (double)metric->value.meter.count / total_seconds;
        }

        metric->value.meter.last_update = now;
    }

    pthread_mutex_unlock(&metric->mutex);

    return 0;
}

// 获取指标的类型
concord_metric_type_t concord_metrics_get_type(concord_metric_t *metric) {
    if (!metric) {
        return (concord_metric_type_t)-1;
    }
    return metric->type;
}

// 获取指标的名称
const char *concord_metrics_get_name(concord_metric_t *metric) {
    if (!metric) {
        return NULL;
    }
    return metric->name;
}

// 获取指标的描述
const char *concord_metrics_get_description(concord_metric_t *metric) {
    if (!metric) {
        return NULL;
    }
    return metric->description;
}

// 获取计数器的当前值
int64_t concord_metrics_counter_get(concord_metric_t *metric) {
    if (!metric || metric->type != CONCORD_METRIC_COUNTER) {
        return -1;
    }

    int64_t value;
    pthread_mutex_lock(&metric->mutex);
    value = metric->value.counter_value;
    pthread_mutex_unlock(&metric->mutex);

    return value;
}

// 获取仪表盘的当前值
double concord_metrics_gauge_get(concord_metric_t *metric) {
    if (!metric || metric->type != CONCORD_METRIC_GAUGE) {
        return NAN;
    }

    double value;
    pthread_mutex_lock(&metric->mutex);
    value = metric->value.gauge_value;
    pthread_mutex_unlock(&metric->mutex);

    return value;
}

// 获取直方图的观测值计数
int64_t concord_metrics_histogram_count(concord_metric_t *metric) {
    if (!metric || metric->type != CONCORD_METRIC_HISTOGRAM) {
        return -1;
    }

    int64_t count = 0;
    pthread_mutex_lock(&metric->mutex);
    for (int i = 0; i < metric->value.histogram.bucket_count; i++) {
        count += (int64_t)metric->value.histogram.buckets[i];
    }
    pthread_mutex_unlock(&metric->mutex);

    return count;
}

// 比较两个double值，用于排序
static int compare_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

// 获取直方图的某个百分位数值
double concord_metrics_histogram_percentile(concord_metric_t *metric, double percentile) {
    if (!metric || metric->type != CONCORD_METRIC_HISTOGRAM || 
        percentile < 0.0 || percentile > 1.0) {
        return NAN;
    }

    pthread_mutex_lock(&metric->mutex);

    if (metric->value.histogram.values_count == 0) {
        pthread_mutex_unlock(&metric->mutex);
        return 0.0;
    }

    // 创建一个观测值的副本
    double *values = (double *)malloc(metric->value.histogram.values_count * sizeof(double));
    if (!values) {
        pthread_mutex_unlock(&metric->mutex);
        return NAN;
    }

    memcpy(values, metric->value.histogram.values, 
           metric->value.histogram.values_count * sizeof(double));
    int count = metric->value.histogram.values_count;

    pthread_mutex_unlock(&metric->mutex);

    // 排序观测值
    qsort(values, count, sizeof(double), compare_double);

    // 计算百分位数索引
    int index = (int)(percentile * (count - 1));
    double result = values[index];

    free(values);
    return result;
}

// 获取直方图的平均值
double concord_metrics_histogram_mean(concord_metric_t *metric) {
    if (!metric || metric->type != CONCORD_METRIC_HISTOGRAM) {
        return NAN;
    }

    pthread_mutex_lock(&metric->mutex);

    if (metric->value.histogram.values_count == 0) {
        pthread_mutex_unlock(&metric->mutex);
        return 0.0;
    }

    double sum = 0.0;
    for (int i = 0; i < metric->value.histogram.values_count; i++) {
        sum += metric->value.histogram.values[i];
    }

    double mean = sum / metric->value.histogram.values_count;
    pthread_mutex_unlock(&metric->mutex);

    return mean;
}

// 获取计量表的1分钟事件速率
double concord_metrics_meter_rate1(concord_metric_t *metric) {
    if (!metric || metric->type != CONCORD_METRIC_METER) {
        return NAN;
    }

    double rate;
    pthread_mutex_lock(&metric->mutex);
    rate = metric->value.meter.rate1m;
    pthread_mutex_unlock(&metric->mutex);

    return rate;
}

// 获取计量表的5分钟事件速率
double concord_metrics_meter_rate5(concord_metric_t *metric) {
    if (!metric || metric->type != CONCORD_METRIC_METER) {
        return NAN;
    }

    double rate;
    pthread_mutex_lock(&metric->mutex);
    rate = metric->value.meter.rate5m;
    pthread_mutex_unlock(&metric->mutex);

    return rate;
}

// 获取计量表的15分钟事件速率
double concord_metrics_meter_rate15(concord_metric_t *metric) {
    if (!metric || metric->type != CONCORD_METRIC_METER) {
        return NAN;
    }

    double rate;
    pthread_mutex_lock(&metric->mutex);
    rate = metric->value.meter.rate15m;
    pthread_mutex_unlock(&metric->mutex);

    return rate;
}

// 获取计量表的平均事件速率
double concord_metrics_meter_rate_mean(concord_metric_t *metric) {
    if (!metric || metric->type != CONCORD_METRIC_METER) {
        return NAN;
    }

    double rate;
    pthread_mutex_lock(&metric->mutex);
    rate = metric->value.meter.rate_mean;
    pthread_mutex_unlock(&metric->mutex);

    return rate;
}

// 根据名称查找指标
concord_metric_t *concord_metrics_find(concord_metrics_repo_t *repo, const char *name) {
    if (!repo || !name) {
        return NULL;
    }

    uint32_t hash = hash_string(name);
    int index = hash % repo->capacity;

    pthread_mutex_lock(&repo->mutex);

    concord_metric_t *metric = repo->buckets[index];
    while (metric) {
        if (strcmp(metric->name, name) == 0) {
            pthread_mutex_unlock(&repo->mutex);
            return metric;
        }
        metric = metric->next;
    }

    pthread_mutex_unlock(&repo->mutex);
    return NULL;
}

// 获取仓库中的指标数量
int concord_metrics_count(concord_metrics_repo_t *repo) {
    if (!repo) {
        return -1;
    }

    int count;
    pthread_mutex_lock(&repo->mutex);
    count = repo->size;
    pthread_mutex_unlock(&repo->mutex);

    return count;
}

// 枚举仓库中的所有指标
int concord_metrics_foreach(
    concord_metrics_repo_t *repo,
    void (*callback)(concord_metric_t *metric, void *user_data),
    void *user_data
) {
    if (!repo || !callback) {
        return -1;
    }

    pthread_mutex_lock(&repo->mutex);

    for (int i = 0; i < repo->capacity; i++) {
        concord_metric_t *metric = repo->buckets[i];
        while (metric) {
            callback(metric, user_data);
            metric = metric->next;
        }
    }

    pthread_mutex_unlock(&repo->mutex);
    return 0;
} 