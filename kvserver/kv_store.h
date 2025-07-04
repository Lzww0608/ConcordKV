/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-25 19:44:03
* @Description: ConcordKV storage engine - kv_store.h
 */
#ifndef __KV_STORE_H__
#define __KV_STORE_H__

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>


#define BUFFER_LENGTH		512


//#define ENABLE_LOG	1

#ifdef ENABLE_LOG

#define LOG(_fmt, ...)  fprintf(stdout, "[%s:%d]: %s"_fmt, __FILE__, __LINE__, __VAR_ARGS__)


#else

#define LOG(_fmt, ...)


#endif


typedef int (*RCALLBACK)(int fd);


struct conn_item {
	int fd;
	
	char rbuffer[BUFFER_LENGTH];
	int rlen;
	char wbuffer[BUFFER_LENGTH];
	int wlen;

	union {
		RCALLBACK accept_callback;
		RCALLBACK recv_callback;
	} recv_t;
	RCALLBACK send_callback;
};

int epoll_entry(void);
int ntyco_entry(void);


int kv_store_request(struct conn_item *item);

void *kv_store_malloc(size_t size);
void kv_store_free(void *ptr);



#define NETWORK_EPOLL		0
#define NETWORK_NTYCO		1
#define NETWORK_IOURING		2

#define ENABLE_NETWORK_SELECT	NETWORK_EPOLL


#define ENABLE_ARRAY_KVENGINE	1
#define ENABLE_RBTREE_KVENGINE		1
#define ENABLE_SKIPTABLE_KVENGINE	1
#define ENABLE_HASH_KVENGINE	1

#define ENABLE_MEM_POOL			0


#if ENABLE_MEM_POOL

int mp_init(mempool_t *m, int size);
void mp_dest(mempool_t *m);

void *mp_alloc(mempool_t *m);
void mp_free(mempool_t *m, void *ptr);

extern mempool_t m;

#endif


#if ENABLE_HASH_KVENGINE

typedef struct bucket_s bucket_t;
typedef struct hash_node_s hash_node_t;

typedef struct hashtable_s {
    bucket_t *buckets;
    int max_slots;
    int count;
} hashtable_t;

extern hashtable_t Hash;

int kv_store_hash_create(hashtable_t *hash);
void kv_store_hash_destroy(hashtable_t *hash);
int kvs_hash_set(hashtable_t *hash, char *key, char *value);
char *kvs_hash_get(hashtable_t *hash, char *key);
int kvs_hash_delete(hashtable_t *hash, char *key);
int kvs_hash_modify(hashtable_t *hash, char *key, char *value);
int kvs_hash_count(hashtable_t *hash);

#endif



#if ENABLE_ARRAY_KVENGINE

struct kvs_array_item {
	char *key;
	char *value;
};

#define KVS_ARRAY_SIZE		1024

typedef struct array_s {
	struct kvs_array_item *array_table;
	int array_idx;
} array_t;

extern array_t Array;


int kv_store_array_create(array_t *arr);
void kv_store_array_destroy(array_t *arr); 

int kvs_array_set(array_t *arr, char *key, char *value);
char *kvs_array_get(array_t *arr, char *key);
int kvs_array_delete(array_t *arr, char *key);
int kvs_array_modify(array_t *arr, char *key, char *value);
int kvs_array_count(array_t *arr);
size_t kvs_array_memory_usage(array_t *arr);


#endif


#if ENABLE_RBTREE_KVENGINE

typedef struct _rbtree rbtree_t;
typedef struct _rbtree_node rbtree_node_t;

extern rbtree_t Tree;

int kv_store_rbtree_create(rbtree_t *tree);
void kv_store_rbtree_destroy(rbtree_t *tree);
int kvs_rbtree_set(rbtree_t *tree, char *key, char *value);
char* kvs_rbtree_get(rbtree_t *tree, char *key);
int kvs_rbtree_delete(rbtree_t *tree, char *key);
int kvs_rbtree_modify(rbtree_t *tree, char *key, char *value);
int kvs_rbtree_count(rbtree_t *tree);

// 红黑树内部函数，允许哈希表复用
rbtree_node_t *rbtree_search(rbtree_t *tree, char *key);
void rbtree_insert_node(rbtree_t *tree, rbtree_node_t *node);
rbtree_node_t *rbtree_delete_node(rbtree_t *tree, rbtree_node_t *node);
rbtree_node_t *rbtree_create_node(char *key, char *value);

#endif



#endif


