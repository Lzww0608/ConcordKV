/**
 * @file timer.h
 * @brief 高精度定时器接口，使用epoll + 最小堆实现
 */

#ifndef __CONCORD_TIMER_H__
#define __CONCORD_TIMER_H__

#include <stdint.h>
#include <time.h>
#include <sys/epoll.h>

/**
 * @brief 定时器回调函数类型
 * @param arg 用户提供的参数
 * @param fired_time 实际触发时间
 */
typedef void (*concord_timer_callback_t)(void *arg, uint64_t fired_time);

/**
 * @brief 定时器类型
 */
typedef enum {
    CONCORD_TIMER_ONCE = 0,      /**< 一次性定时器 */
    CONCORD_TIMER_PERIODIC       /**< 周期性定时器 */
} concord_timer_type_t;

/**
 * @brief 定时器ID类型
 */
typedef uint64_t concord_timer_id_t;

/**
 * @brief 定时器管理器结构体
 */
typedef struct concord_timer_manager_t concord_timer_manager_t;

/**
 * @brief 创建定时器管理器
 * 
 * @return 定时器管理器指针，失败返回NULL
 */
concord_timer_manager_t *concord_timer_manager_create(void);

/**
 * @brief 销毁定时器管理器
 * 
 * @param manager 定时器管理器
 */
void concord_timer_manager_destroy(concord_timer_manager_t *manager);

/**
 * @brief 添加定时器
 * 
 * @param manager 定时器管理器
 * @param type 定时器类型
 * @param timeout_ms 超时时间(毫秒)
 * @param callback 回调函数
 * @param arg 回调函数参数
 * @return 定时器ID，失败返回0
 */
concord_timer_id_t concord_timer_add(
    concord_timer_manager_t *manager,
    concord_timer_type_t type,
    uint64_t timeout_ms,
    concord_timer_callback_t callback,
    void *arg
);

/**
 * @brief 取消定时器
 * 
 * @param manager 定时器管理器
 * @param timer_id 定时器ID
 * @return 成功返回0，失败返回-1
 */
int concord_timer_cancel(concord_timer_manager_t *manager, concord_timer_id_t timer_id);

/**
 * @brief 修改定时器超时时间
 * 
 * @param manager 定时器管理器
 * @param timer_id 定时器ID
 * @param new_timeout_ms 新的超时时间(毫秒)
 * @return 成功返回0，失败返回-1
 */
int concord_timer_update(
    concord_timer_manager_t *manager,
    concord_timer_id_t timer_id, 
    uint64_t new_timeout_ms
);

/**
 * @brief 定时器管理器事件循环，处理所有到期的定时器
 * 
 * @param manager 定时器管理器
 * @param timeout_ms 等待超时时间，-1表示一直等待，0表示不等待
 * @return 处理的定时器数量
 */
int concord_timer_process(concord_timer_manager_t *manager, int timeout_ms);

/**
 * @brief 获取当前高精度时间戳(纳秒)
 * 
 * @return 纳秒级时间戳
 */
uint64_t concord_time_now_ns(void);

/**
 * @brief 获取当前高精度时间戳(微秒)
 * 
 * @return 微秒级时间戳
 */
uint64_t concord_time_now_us(void);

/**
 * @brief 获取当前高精度时间戳(毫秒)
 * 
 * @return 毫秒级时间戳
 */
uint64_t concord_time_now_ms(void);

/**
 * @brief 休眠指定时间(毫秒)
 * 
 * @param ms 毫秒数
 */
void concord_time_sleep_ms(uint64_t ms);

/**
 * @brief 休眠指定时间(微秒)
 * 
 * @param us 微秒数
 */
void concord_time_sleep_us(uint64_t us);

/**
 * @brief 格式化时间戳为字符串
 * 
 * @param timestamp_ms 毫秒级时间戳
 * @param format 格式字符串，参考strftime
 * @param buf 输出缓冲区
 * @param size 缓冲区大小
 * @return 成功返回buf，失败返回NULL
 */
char *concord_time_format(uint64_t timestamp_ms, const char *format, char *buf, size_t size);

/**
 * @brief 解析时间字符串为时间戳
 * 
 * @param time_str 时间字符串
 * @param format 格式字符串，参考strptime
 * @return 毫秒级时间戳，失败返回0
 */
uint64_t concord_time_parse(const char *time_str, const char *format);

/**
 * @brief 计算两个时间点之间的差值(毫秒)
 * 
 * @param start 开始时间(毫秒)
 * @param end 结束时间(毫秒)
 * @return 时间差(毫秒)
 */
uint64_t concord_time_diff_ms(uint64_t start, uint64_t end);

#endif /* __CONCORD_TIMER_H__ */ 