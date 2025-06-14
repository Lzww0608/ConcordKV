/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-6-15 17:00:15
* @Description: ConcordKV storage engine - kvstore.c
 */





#include "kv_store.h"


#define KV_STORE_MAX_TOKENS 128

const char *commands[] = {
	"SET", "GET", "DEL", "MOD", "COUNT",
	"RSET", "RGET", "RDEL", "RMOD", "RCOUNT",
	"HSET", "HGET", "HDEL", "HMOD", "HCOUNT",
};

enum {
	KVS_CMD_START = 0,
	KVS_CMD_SET = KVS_CMD_START,
	KVS_CMD_GET,
	KVS_CMD_DEL,
	KVS_CMD_MOD,
	KVS_CMD_COUNT,
	
	KVS_CMD_RSET,
	KVS_CMD_RGET,
	KVS_CMD_RDEL,
	KVS_CMD_RMOD,
	KVS_CMD_RCOUNT,

	KVS_CMD_HSET,
	KVS_CMD_HGET,
	KVS_CMD_HDEL,
	KVS_CMD_HMOD,
	KVS_CMD_HCOUNT,
	
	KVS_CMD_SIZE,
};

// kv_store_malloc和kv_store_free函数现在在kv_memory.c中实现



#if ENABLE_HASH_KVENGINE

int kv_store_hash_set(char *key, char *value) {
	return kvs_hash_set(&Hash, key, value);
}
char *kv_store_hash_get(char *key) {
	return kvs_hash_get(&Hash, key);
}
int kv_store_hash_delete(char *key) {
	return kvs_hash_delete(&Hash, key);
}
int kv_store_hash_modify(char *key, char *value) {
	return kvs_hash_modify(&Hash, key, value);
}
int kv_store_hash_count(void) {
	return kvs_hash_count(&Hash);
}


#endif

#if ENABLE_RBTREE_KVENGINE 


int kv_store_rbtree_set(char *key, char *value) {
	return kvs_rbtree_set(&Tree, key, value);
}

char* kv_store_rbtree_get(char *key) {
	return kvs_rbtree_get(&Tree, key);
}
int kv_store_rbtree_delete(char *key) {

	return kvs_rbtree_delete(&Tree, key);

}
int kv_store_rbtree_modify(char *key, char *value) {
	return kvs_rbtree_modify(&Tree, key, value);
}

int kv_store_rbtree_count(void) {
	return kvs_rbtree_count(&Tree);
}

#endif

#if ENABLE_ARRAY_KVENGINE

int kv_store_array_set(char *key, char *value) {
	return kvs_array_set(&Array, key, value);

}
char *kv_store_array_get(char *key) {

	return kvs_array_get(&Array, key);

}
int kv_store_array_delete(char *key) {
	return kvs_array_delete(&Array, key);
}
int kv_store_array_modify(char *key, char *value) {
	return kvs_array_modify(&Array, key, value);
}
int kv_store_array_count(void) {
	return kvs_array_count(&Array);
}


#endif




int kv_store_split_token(char *msg, char **tokens) {

	if (msg == NULL || tokens == NULL) return -1;

	int idx = 0;

	char *token = strtok(msg, " ");

	while (token != NULL) {
		tokens[idx ++] = token;
		token = strtok(NULL, " ");
	}
	return idx;
}

// crud

int kv_store_parser_protocol(struct conn_item *item, char **tokens, int count) {

	if (item == NULL || tokens[0] == NULL || count == 0) return -1;

	int cmd = KVS_CMD_START;

	
	for (cmd = KVS_CMD_START; cmd < KVS_CMD_SIZE; cmd ++) {
		//printf("cmd: %s, %s, %ld, %ld\n",commands[cmd], tokens[0], strlen(commands[cmd]), strlen(tokens[0]));
		if (strcmp(commands[cmd], tokens[0]) == 0) {
			break;
		}
	}

	char *msg = item->wbuffer;
	char *key = tokens[1];
	char *value = tokens[2];
	memset(msg, 0, BUFFER_LENGTH);

	switch (cmd) {
		// array
		case KVS_CMD_SET: {
			int res = kv_store_array_set(key, value);
			if (!res) {
				snprintf(msg, BUFFER_LENGTH, "SUCCESS");
			} else {
				snprintf(msg, BUFFER_LENGTH, "FAILED");
			}
			//LOG("set: %d\n", res);
			
			break;
		}
		case KVS_CMD_GET: {
			char *val = kv_store_array_get(key);
			if (val) {
				snprintf(msg, BUFFER_LENGTH, "%s", val);
			} else {
				snprintf(msg, BUFFER_LENGTH, "NO EXIST");
			}
			
			//printf("get: %s\n", val);
			
			break;
		}
		case KVS_CMD_DEL: {
			//printf("del\n");

			int res = kv_store_array_delete(key);
			if (res < 0) {  // server
				snprintf(msg, BUFFER_LENGTH, "%s", "ERROR");
			} else if (res == 0) {
				snprintf(msg, BUFFER_LENGTH, "%s", "SUCCESS");
			} else {
				snprintf(msg, BUFFER_LENGTH, "NO EXIST");
			}
			
			break;
		}
		case KVS_CMD_MOD: {
			//printf("mod\n");

			int res = kv_store_array_modify(key, value);
			if (res < 0) {  // server
				snprintf(msg, BUFFER_LENGTH, "%s", "ERROR");
			} else if (res == 0) {
				snprintf(msg, BUFFER_LENGTH, "%s", "SUCCESS");
			} else {
				snprintf(msg, BUFFER_LENGTH, "NO EXIST");
			}
			
			break;
		}

		case KVS_CMD_COUNT: {
			int count = kv_store_array_count();
			if (count < 0) {  // server
				snprintf(msg, BUFFER_LENGTH, "%s", "ERROR");
			} else {
				snprintf(msg, BUFFER_LENGTH, "%d", count);
			}
			break;
		}
		
		// rbtree
		case KVS_CMD_RSET: {

			int res = kv_store_rbtree_set(key, value);
			if (!res) {
				snprintf(msg, BUFFER_LENGTH, "SUCCESS");
			} else {
				snprintf(msg, BUFFER_LENGTH, "FAILED");
			}
			break;
		}
		case KVS_CMD_RGET: {

			char *val = kv_store_rbtree_get(key);
			if (val) {
				snprintf(msg, BUFFER_LENGTH, "%s", val);
			} else {
				snprintf(msg, BUFFER_LENGTH, "NO EXIST");
			}
			
			break;
		}
		case KVS_CMD_RDEL: {

			int res = kv_store_rbtree_delete(key);
			if (res < 0) {  // server
				snprintf(msg, BUFFER_LENGTH, "%s", "ERROR");
			} else if (res == 0) {
				snprintf(msg, BUFFER_LENGTH, "%s", "SUCCESS");
			} else {
				snprintf(msg, BUFFER_LENGTH, "NO EXIST");
			}
			
			break;
		}
		case KVS_CMD_RMOD: {

			int res = kv_store_rbtree_modify(key, value);
			if (res < 0) {  // server
				snprintf(msg, BUFFER_LENGTH, "%s", "ERROR");
			} else if (res == 0) {
				snprintf(msg, BUFFER_LENGTH, "%s", "SUCCESS");
			} else {
				snprintf(msg, BUFFER_LENGTH, "NO EXIST");
			}
			
			break;
		}

		case KVS_CMD_RCOUNT: {
			int count = kv_store_rbtree_count();
			if (count < 0) {  // server
				snprintf(msg, BUFFER_LENGTH, "%s", "ERROR");
			} else {
				snprintf(msg, BUFFER_LENGTH, "%d", count);
			}
			break;
		}

		case KVS_CMD_HSET: {

			int res = kv_store_hash_set(key, value);
			if (!res) {
				snprintf(msg, BUFFER_LENGTH, "SUCCESS");
			} else {
				snprintf(msg, BUFFER_LENGTH, "FAILED");
			}
			break;
		}
		// hash
		case KVS_CMD_HGET: {

			char *val = kv_store_hash_get(key);
			if (val) {
				snprintf(msg, BUFFER_LENGTH, "%s", val);
			} else {
				snprintf(msg, BUFFER_LENGTH, "NO EXIST");
			}
			
			break;
		}
		case KVS_CMD_HDEL: {

			int res = kv_store_hash_delete(key);
			if (res < 0) {  // server
				snprintf(msg, BUFFER_LENGTH, "%s", "ERROR");
			} else if (res == 0) {
				snprintf(msg, BUFFER_LENGTH, "%s", "SUCCESS");
			} else {
				snprintf(msg, BUFFER_LENGTH, "NO EXIST");
			}
			
			break;
		}
		case KVS_CMD_HMOD: {

			int res = kv_store_hash_modify(key, value);
			if (res < 0) {  // server
				snprintf(msg, BUFFER_LENGTH, "%s", "ERROR");
			} else if (res == 0) {
				snprintf(msg, BUFFER_LENGTH, "%s", "SUCCESS");
			} else {
				snprintf(msg, BUFFER_LENGTH, "NO EXIST");
			}
			
			break;
		}

		case KVS_CMD_HCOUNT: {
			int count = kv_store_hash_count();
			if (count < 0) {  // server
				snprintf(msg, BUFFER_LENGTH, "%s", "ERROR");
			} else {
				snprintf(msg, BUFFER_LENGTH, "%d", count);
			}
			break;
		}
		
		default: {
			printf("cmd: %s\n", commands[cmd]);
			assert(0);
		}
		

	}

}


int kv_store_request(struct conn_item *item) {

	//printf("recv: %s\n", item->rbuffer);

	char *msg = item->rbuffer;
	char *tokens[KV_STORE_MAX_TOKENS];

	int count = kv_store_split_token(msg, tokens);
#if 0
	int idx = 0;
	for (idx = 0;idx < count;idx ++) {
		printf("idx: %s\n", tokens[idx]);
	}
#endif

	kv_store_parser_protocol(item, tokens, count);

	return 0;
}







int init_kvengine(void) {


#if ENABLE_ARRAY_KVENGINE
	kv_store_array_create(&Array);
#endif

#if ENABLE_RBTREE_KVENGINE
	kv_store_rbtree_create(&Tree);
#endif

#if ENABLE_HASH_KVENGINE
	kv_store_hash_create(&Hash);
#endif

}


int exit_kvengine(void) {

#if ENABLE_ARRAY_KVENGINE
	kv_store_array_destroy(&Array);
#endif

#if ENABLE_RBTREE_KVENGINE
	kv_store_rbtree_destroy(&Tree);
#endif

#if ENABLE_HASH_KVENGINE
	kv_store_hash_destroy(&Hash);
#endif

}

int init_ctx(void) {

#if ENABLE_MEM_POOL
	mp_init(&m, 4096);
#endif

}

int main() {


	init_kvengine();
	
#if (ENABLE_NETWORK_SELECT == NETWORK_EPOLL)
	epoll_entry();
#elif (ENABLE_NETWORK_SELECT == NETWORK_NTYCO)
	ntyco_entry();
#elif (ENABLE_NETWORK_SELECT == NETWORK_IOURING)
	
#endif

	exit_kvengine();

}





