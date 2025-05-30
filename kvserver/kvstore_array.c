/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV storage engine - kvstore_array.c
 */
#include "kv_store.h"

array_t Array;

// create
int kv_store_array_create(array_t *arr) {
	if (!arr) return -1;

	arr->array_table = kv_store_malloc(KVS_ARRAY_SIZE * sizeof(struct kvs_array_item));
	if (!arr->array_table) {
		return -1;
	}
	memset(arr->array_table, 0, KVS_ARRAY_SIZE * sizeof(struct kvs_array_item));

	arr->array_idx = 0;

	return 0;
}

// destroy
void kv_store_array_destroy(array_t *arr) {
	if (!arr) return;

	if (arr->array_table) {
		// 释放所有键值对的内存
		for (int i = 0; i < KVS_ARRAY_SIZE; i++) {
			if (arr->array_table[i].key) {
				kv_store_free(arr->array_table[i].key);
				arr->array_table[i].key = NULL;
			}
			if (arr->array_table[i].value) {
				kv_store_free(arr->array_table[i].value);
				arr->array_table[i].value = NULL;
			}
		}
		kv_store_free(arr->array_table);
		arr->array_table = NULL;
	}
	arr->array_idx = 0;
}

// set
int kvs_array_set(array_t *arr, char *key, char *value) {
	if (arr == NULL || key == NULL || value == NULL) return -1;
	if (arr->array_idx == KVS_ARRAY_SIZE) return -1;

	// 首先查找重复键
	for (int i = 0; i < arr->array_idx; i++) {
		if (arr->array_table[i].key && strcmp(arr->array_table[i].key, key) == 0) {
			// 找到重复键，更新值
			kv_store_free(arr->array_table[i].value);
			
			char *vcopy = kv_store_malloc(strlen(value) + 1);
			if (vcopy == NULL) {
				return -1;
			}
			strncpy(vcopy, value, strlen(value)+1);
			
			arr->array_table[i].value = vcopy;
			return 0;
		}
	}

	// 查找NULL键的位置（已删除的空间）
	int empty_slot = -1;
	for (int i = 0; i < arr->array_idx; i++) {
		if (arr->array_table[i].key == NULL) {
			empty_slot = i;
			break;
		}
	}

	char *kcopy = kv_store_malloc(strlen(key) + 1);
	if (kcopy == NULL) return -1;
	strncpy(kcopy, key, strlen(key)+1);
	
	char *vcopy = kv_store_malloc(strlen(value) + 1);
	if (vcopy == NULL) {
		kv_store_free(kcopy);
		return -1;
	}
	strncpy(vcopy, value, strlen(value)+1);

	// 如果找到了空位置，使用它
	if (empty_slot >= 0) {
		arr->array_table[empty_slot].key = kcopy;
		arr->array_table[empty_slot].value = vcopy;
		return 0;
	}

	// 否则添加到末尾
	arr->array_table[arr->array_idx].key = kcopy;
	arr->array_table[arr->array_idx].value = vcopy;
	arr->array_idx++;

	return 0;
}

// get
char * kvs_array_get(array_t *arr, char *key) {
	int i = 0;
	if (arr == NULL || key == NULL) return NULL;

	for (i = 0; i < arr->array_idx; i++) {
		// 检查键是否为NULL
		if (arr->array_table[i].key == NULL) {
			continue;
		}
		if (strcmp(arr->array_table[i].key, key) == 0) {
			return arr->array_table[i].value;
		}
	}

	return NULL;
}

// i > 0 : no exist
// i == 0: succeed
// i < 0 : input error
int kvs_array_delete(array_t *arr, char *key) {
	int i = 0;
	if (arr == NULL || key == NULL) return -1;

	for (i = 0; i < arr->array_idx; i++) { 
		if (arr->array_table[i].key && strcmp(arr->array_table[i].key, key) == 0) {
			// 释放内存
			kv_store_free(arr->array_table[i].value);
			kv_store_free(arr->array_table[i].key);
			
			// 将键和值置为NULL，但保持位置不变
			arr->array_table[i].key = NULL;
			arr->array_table[i].value = NULL;
			
			// 如果是最后一个元素，减少索引
			if (i == arr->array_idx - 1) {
				// 从后往前找到最后一个非NULL的键的位置
				int j = arr->array_idx - 1;
				while (j >= 0 && arr->array_table[j].key == NULL) {
					j--;
				}
				arr->array_idx = j + 1;
			}
			
			return 0;
		}
	}

	return i; 
}

// i > 0 : no exist
// i == 0: succeed
// i < 0 : input error
int kvs_array_modify(array_t *arr, char *key, char *value) {
	int i = 0;
	if (arr == NULL || key == NULL || value == NULL) return -1;

	for (i = 0; i < arr->array_idx; i++) {
		if (arr->array_table[i].key && strcmp(arr->array_table[i].key, key) == 0) {
			// 释放旧值
			kv_store_free(arr->array_table[i].value);
			
			// 创建新值的副本
			char *vcopy = kv_store_malloc(strlen(value) + 1);
			if (vcopy == NULL) return -1;
			strncpy(vcopy, value, strlen(value)+1);
			
			// 更新值
			arr->array_table[i].value = vcopy;
			return 0;
		}
	}

	return i;
}

int kvs_array_count(array_t *arr) {
	if (!arr) return -1;
	
	// 计算非NULL键的数量
	int count = 0;
	for (int i = 0; i < arr->array_idx; i++) {
		if (arr->array_table[i].key != NULL) {
			count++;
		}
	}
	
	return count;
}


