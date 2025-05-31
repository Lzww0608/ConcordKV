/*
 * @Author: Lzww0608  
 * @Date: 2025-5-30 23:25:00
 * @LastEditors: Lzww0608
 * @LastEditTime: 2025-5-30 23:25:02
 * @Description: ConcordKV B+Tree存储引擎适配器头文件
 */
#ifndef __BTREE_ADAPTER_H__
#define __BTREE_ADAPTER_H__

#include "kvstore_btree.h"
#include "kv_engine_interface.h"

#ifdef __cplusplus
extern "C" {
#endif

// === B+Tree存储引擎适配器接口 ===

/**
 * 设置键值对
 * @param btree B+Tree指针
 * @param key 键
 * @param value 值
 * @return 0成功，负数表示错误
 */
int kvs_btree_set(btree_t *btree, char *key, char *value);

/**
 * 获取键对应的值
 * @param btree B+Tree指针
 * @param key 键
 * @return 值字符串，未找到返回NULL
 */
char* kvs_btree_get(btree_t *btree, char *key);

/**
 * 删除键值对
 * @param btree B+Tree指针
 * @param key 键
 * @return 0成功，负数表示错误
 */
int kvs_btree_delete(btree_t *btree, char *key);

/**
 * 修改键对应的值
 * @param btree B+Tree指针
 * @param key 键
 * @param value 新值
 * @return 0成功，正数表示键不存在，负数表示错误
 */
int kvs_btree_modify(btree_t *btree, char *key, char *value);

/**
 * 获取键数量
 * @param btree B+Tree指针
 * @return 键数量
 */
int kvs_btree_count(btree_t *btree);

/**
 * 创建B+Tree存储引擎
 * @param order B+Tree阶数
 * @return 新创建的B+Tree，失败返回NULL
 */
btree_t* kv_store_btree_create(int order);

/**
 * 销毁B+Tree存储引擎
 * @param btree B+Tree指针
 */
void kv_store_btree_destroy(btree_t *btree);

#ifdef __cplusplus
}
#endif

#endif // __BTREE_ADAPTER_H__ 