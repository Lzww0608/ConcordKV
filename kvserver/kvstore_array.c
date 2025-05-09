
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

	if (!arr) return ;

	if (!arr->array_table)
		kv_store_free(arr->array_table);

}


// set
int kvs_array_set(array_t *arr, char *key, char *value) {

	if (arr == NULL || key == NULL || value == NULL) return -1;
	if (arr->array_idx == KVS_ARRAY_SIZE) return -1;

	char *kcopy = kv_store_malloc(strlen(key) + 1);
	if (kcopy == NULL) return -1;
	strncpy(kcopy, key, strlen(key)+1);
	
	// defensive programming
	char *vcopy = kv_store_malloc(strlen(value) + 1);
	if (vcopy == NULL) {
		kv_store_free(kcopy);
		return -1;
	}
	strncpy(vcopy, value, strlen(value)+1);

	// find a duplicate key and replace its value
	int i = 0;
	for (i = 0;i < arr->array_idx;i ++) {
		if (arr->array_table[i].key == NULL) {

			arr->array_table[i].key = kcopy;
			arr->array_table[i].value = vcopy;
			arr->array_idx ++;

			return 0;
		}
	}

	// set a new key-value pair
	if (i < KVS_ARRAY_SIZE && i == arr->array_idx) {
		arr->array_table[arr->array_idx].key = kcopy;
		arr->array_table[arr->array_idx].value = vcopy;
		arr->array_idx ++;
	}

	return 0;
}


// get
char * kvs_array_get(array_t *arr, char *key) {

	int i = 0;
	if (arr == NULL) return NULL;

	for (i = 0;i < arr->array_idx;i ++) {
		// If a NULL key is encountered, return NULL (defensive check)
		if (arr->array_table[i].key == NULL) {
			return NULL;
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

	for (i = 0;i < arr->array_idx;i ++) { 

		if (strcmp(arr->array_table[i].key, key) == 0) {
			
			kv_store_free(arr->array_table[i].value);
			arr->array_table[i].value = NULL;

			kv_store_free(arr->array_table[i].key);
			arr->array_table[i].key = NULL;

			arr->array_idx --;

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

	for (i = 0;i < arr->array_idx;i ++) {

		if (strcmp(arr->array_table[i].key, key) == 0) {

			kv_store_free(arr->array_table[i].value);
			arr->array_table[i].value = NULL;

			char *vcopy = kv_store_malloc(strlen(value) + 1);
			strncpy(vcopy, value, strlen(value)+1);

			arr->array_table[i].value = vcopy;

			return 0;
		}
		
	}

	return i;
}


int kvs_array_count(array_t *arr) {
	if (!arr) return -1;
	
	return arr->array_idx;
}


