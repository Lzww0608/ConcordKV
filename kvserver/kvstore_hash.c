
// Zipper

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "kv_store.h"


#define MAX_KEY_LEN	128
#define MAX_VALUE_LEN	512


#define MAX_TABLE_SIZE	102400

#define ENABLE_POINTER_KEY	1


typedef struct hash_node_s {
#if ENABLE_POINTER_KEY
	char *key;
	char *value;
#else
	char key[MAX_KEY_LEN];
	char value[MAX_VALUE_LEN];
#endif	
	struct hash_node_s *next;
	
} hash_node_t;


typedef struct hashtable_s {

	hash_node_t **nodes; // Pointer to an array of pointers to hash_node_t

	int max_slots;		// Maximum number of slots in the hash table
	int count;			// Current number of elements in the hash table

} hashtable_t;


hashtable_t Hash;


// Hash function to calculate the hash index based on the key
static int _hash(char *key, int size) {

	if (!key) return -1;

	int sum = 0;
	int i = 0;

	while (key[i] != 0) {
		sum += key[i];
		i ++;
	}

	return sum % size;

}

hash_node_t *_create_node(char *key, char *value) {

	hash_node_t *node = (hash_node_t*)kv_store_malloc(sizeof(hash_node_t));
	if (!node) return NULL;

#if ENABLE_POINTER_KEY

	node->key = kv_store_malloc(strlen(key) + 1);
	if (!node->key) {
		kv_store_free(node);
		return NULL;
	}
	strcpy(node->key, key);

	node->value = kv_store_malloc(strlen(value) + 1);
	if (!node->value) {
		kv_store_free(node->key);
		kv_store_free(node);
		return NULL;
	}
	strcpy(node->value, value);

#else

	strncpy(node->key, key, MAX_KEY_LEN);
	strncpy(node->value, value, MAX_VALUE_LEN);
	
#endif

	node->next = NULL;

	return node;
}


//
int init_hashtable(hashtable_t *hash) {

	if (!hash) return -1;

	hash->nodes = (hash_node_t**)kv_store_malloc(sizeof(hash_node_t*) * MAX_TABLE_SIZE);
	if (!hash->nodes) return -1;

	hash->max_slots = MAX_TABLE_SIZE;
	hash->count = 0; 

	return 0;
}

// 
void dest_hashtable(hashtable_t *hash) {

	if (!hash) return;

	int i = 0;
	for (i = 0;i < hash->max_slots;i ++) {
		hash_node_t *node = hash->nodes[i];

		while (node != NULL) { // error

			hash_node_t *tmp = node;
			node = node->next;
			hash->nodes[i] = node;
			
			kv_store_free(tmp);
			
		}
	}

	kv_store_free(hash->nodes);
	
}




int put_kv_hashtable(hashtable_t *hash, char *key, char *value) {

	if (!hash || !key || !value) return -1;

	int idx = _hash(key, MAX_TABLE_SIZE);

	hash_node_t *node = hash->nodes[idx];

	while (node != NULL) {
		// Key already exists, return 1 to indicate duplicate
		if (strcmp(node->key, key) == 0) { 
			return 1;
		}
		node = node->next;
	}


	hash_node_t *new_node = _create_node(key, value);
	new_node->next = hash->nodes[idx];
	hash->nodes[idx] = new_node;
	
	hash->count ++;

	return 0;
}


char * get_kv_hashtable(hashtable_t *hash, char *key) {

	if (!hash || !key) return NULL;

	int idx = _hash(key, MAX_TABLE_SIZE);

	hash_node_t *node = hash->nodes[idx];

	while (node != NULL) {

		if (strcmp(node->key, key) == 0) {
			return node->value;
		}

		node = node->next;
	}


	return NULL;

}


int count_kv_hashtable(hashtable_t *hash) {
	return hash->count;
}


// -2: input error
// -1: not exist
//  0: succeed
int delete_kv_hashtable(hashtable_t *hash, char *key) {
	if (!hash || !key) return -2;

	int idx = _hash(key, MAX_TABLE_SIZE);

	hash_node_t *head = hash->nodes[idx];
	if (head == NULL) return -1; 
	// head node
	if (strcmp(head->key, key) == 0) {
		hash_node_t *tmp = head->next;
		hash->nodes[idx] = tmp;

#if ENABLE_POINTER_KEY
		if (head->key) {
			kv_store_free(head->key);
		}
		if (head->value) {
			kv_store_free(head->value);
		}
		kv_store_free(head);
#else
		free(head);
#endif
		hash->count --;

		return 0;
	}

	hash_node_t *cur = head;
	while (cur->next != NULL) {
		if (strcmp(cur->next->key, key) == 0) break; // search node
		
		cur = cur->next;
	}

	if (cur->next == NULL) {
		
		return -1;
	}

	hash_node_t *tmp = cur->next;
	cur->next = tmp->next;
#if ENABLE_POINTER_KEY
	if (tmp->key) {
		kv_store_free(tmp->key);
	}
	if (tmp->value) {
		kv_store_free(tmp->value);
	}
	kv_store_free(tmp);
#else
	free(tmp);
#endif
	hash->count --;

	return 0;
}

// 1: exist
// 0: not exist
int exist_kv_hashtable(hashtable_t *hash, char *key) {

	char *value = get_kv_hashtable(hash, key);
	if (value) return 1;
	else return 0;
	
}




// 5 + 2

int kv_store_hash_create(hashtable_t *hash) {

	return init_hashtable(hash);
	
}


void kv_store_hash_destroy(hashtable_t *hash) {

	return dest_hashtable(hash);

}


int kvs_hash_set(hashtable_t *hash, char *key, char *value) {

	return put_kv_hashtable(hash, key, value);

}


char *kvs_hash_get(hashtable_t *hash, char *key) {

	return get_kv_hashtable(hash, key);

}

int kvs_hash_delete(hashtable_t *hash, char *key) {

	return delete_kv_hashtable(hash, key);

}


int kvs_hash_modify(hashtable_t *hash, char *key, char *value) {

	if (!hash || !key || !value) return -1;

	int idx = _hash(key, MAX_TABLE_SIZE);

	hash_node_t *node = hash->nodes[idx];

	while (node != NULL) {

		if (strcmp(node->key, key) == 0) {
			kv_store_free(node->value);

			node->value = kv_store_malloc(strlen(value) + 1);
			if (node->value) {
				strcpy(node->value, value);
				return 0;
			} else 
				assert(0);
		}

		node = node->next;
	}


	return -1;

}

int kvs_hash_count(hashtable_t *hash) {
	return hash->count;
}




