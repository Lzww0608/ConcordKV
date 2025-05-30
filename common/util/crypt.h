/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV utility functions - crypt.h
 */
#ifndef __CONCORD_CRYPT_H__
#define __CONCORD_CRYPT_H__

#include <stdint.h>
#include <stddef.h>

// 加密算法
typedef enum {
    CONCORD_CRYPT_AES_128_CBC = 0,  // AES-128 CBC模式
    CONCORD_CRYPT_AES_192_CBC,      // AES-192 CBC模式
    CONCORD_CRYPT_AES_256_CBC,      // AES-256 CBC模式
    CONCORD_CRYPT_AES_128_CTR,      // AES-128 CTR模式
    CONCORD_CRYPT_AES_192_CTR,      // AES-192 CTR模式
    CONCORD_CRYPT_AES_256_CTR,      // AES-256 CTR模式
    CONCORD_CRYPT_AES_128_GCM,      // AES-128 GCM模式
    CONCORD_CRYPT_AES_192_GCM,      // AES-192 GCM模式
    CONCORD_CRYPT_AES_256_GCM,      // AES-256 GCM模式
    CONCORD_CRYPT_CHACHA20,         // ChaCha20算法
    CONCORD_CRYPT_CHACHA20_POLY1305 // ChaCha20-Poly1305算法
} concord_crypt_algorithm_t;

// 哈希算法
typedef enum {
    CONCORD_CRYPT_HASH_MD5 = 0,      // MD5哈希
    CONCORD_CRYPT_HASH_SHA1,         // SHA1哈希
    CONCORD_CRYPT_HASH_SHA224,       // SHA224哈希
    CONCORD_CRYPT_HASH_SHA256,       // SHA256哈希
    CONCORD_CRYPT_HASH_SHA384,       // SHA384哈希
    CONCORD_CRYPT_HASH_SHA512,       // SHA512哈希
    CONCORD_CRYPT_HASH_BLAKE2B,      // BLAKE2b哈希
    CONCORD_CRYPT_HASH_BLAKE2S       // BLAKE2s哈希
} concord_crypt_hash_algorithm_t;

// HMAC算法
typedef enum {
    CONCORD_CRYPT_HMAC_MD5 = 0,      // HMAC-MD5
    CONCORD_CRYPT_HMAC_SHA1,         // HMAC-SHA1
    CONCORD_CRYPT_HMAC_SHA224,       // HMAC-SHA224
    CONCORD_CRYPT_HMAC_SHA256,       // HMAC-SHA256
    CONCORD_CRYPT_HMAC_SHA384,       // HMAC-SHA384
    CONCORD_CRYPT_HMAC_SHA512        // HMAC-SHA512
} concord_crypt_hmac_algorithm_t;

// 加密上下文
typedef struct concord_crypt_context {
    concord_crypt_algorithm_t algorithm;  // 算法类型
    void *ctx;                           // 算法特定上下文
} concord_crypt_context_t;

// 哈希上下文
typedef struct concord_crypt_hash_context {
    concord_crypt_hash_algorithm_t algorithm;  // 算法类型
    void *ctx;                               // 算法特定上下文
} concord_crypt_hash_context_t;

// HMAC上下文
typedef struct concord_crypt_hmac_context {
    concord_crypt_hmac_algorithm_t algorithm;  // 算法类型
    void *ctx;                               // 算法特定上下文
} concord_crypt_hmac_context_t;

// 初始化加密库
int concord_crypt_init(void);

// 清理加密库
void concord_crypt_cleanup(void);

// 生成随机数据
int concord_crypt_random(uint8_t *buf, size_t len);

// 加密函数
concord_crypt_context_t *concord_crypt_create(concord_crypt_algorithm_t algorithm, const uint8_t *key, size_t key_len);
void concord_crypt_destroy(concord_crypt_context_t *ctx);
int concord_crypt_encrypt(concord_crypt_context_t *ctx, const uint8_t *in, size_t in_len, uint8_t *out, size_t *out_len, const uint8_t *iv, size_t iv_len);
int concord_crypt_decrypt(concord_crypt_context_t *ctx, const uint8_t *in, size_t in_len, uint8_t *out, size_t *out_len, const uint8_t *iv, size_t iv_len);

// 便捷加密/解密函数
int concord_crypt_encrypt_simple(concord_crypt_algorithm_t algorithm, const uint8_t *key, size_t key_len, const uint8_t *in, size_t in_len, uint8_t *out, size_t *out_len);
int concord_crypt_decrypt_simple(concord_crypt_algorithm_t algorithm, const uint8_t *key, size_t key_len, const uint8_t *in, size_t in_len, uint8_t *out, size_t *out_len);

// 哈希函数
concord_crypt_hash_context_t *concord_crypt_hash_create(concord_crypt_hash_algorithm_t algorithm);
void concord_crypt_hash_destroy(concord_crypt_hash_context_t *ctx);
int concord_crypt_hash_update(concord_crypt_hash_context_t *ctx, const uint8_t *data, size_t len);
int concord_crypt_hash_final(concord_crypt_hash_context_t *ctx, uint8_t *digest, size_t *digest_len);

// 便捷哈希函数
int concord_crypt_hash_simple(concord_crypt_hash_algorithm_t algorithm, const uint8_t *data, size_t len, uint8_t *digest, size_t *digest_len);

// 哈希为十六进制字符串
char *concord_crypt_hash_to_hex(concord_crypt_hash_algorithm_t algorithm, const uint8_t *data, size_t len);

// HMAC函数
concord_crypt_hmac_context_t *concord_crypt_hmac_create(concord_crypt_hmac_algorithm_t algorithm, const uint8_t *key, size_t key_len);
void concord_crypt_hmac_destroy(concord_crypt_hmac_context_t *ctx);
int concord_crypt_hmac_update(concord_crypt_hmac_context_t *ctx, const uint8_t *data, size_t len);
int concord_crypt_hmac_final(concord_crypt_hmac_context_t *ctx, uint8_t *digest, size_t *digest_len);

// 便捷HMAC函数
int concord_crypt_hmac_simple(concord_crypt_hmac_algorithm_t algorithm, const uint8_t *key, size_t key_len, const uint8_t *data, size_t len, uint8_t *digest, size_t *digest_len);

// HMAC为十六进制字符串
char *concord_crypt_hmac_to_hex(concord_crypt_hmac_algorithm_t algorithm, const uint8_t *key, size_t key_len, const uint8_t *data, size_t len);

// Base64编码/解码
char *concord_crypt_base64_encode(const uint8_t *data, size_t len);
int concord_crypt_base64_decode(const char *str, uint8_t *data, size_t *len);

// 常用函数
int concord_crypt_pbkdf2(const char *password, size_t password_len, const uint8_t *salt, size_t salt_len, uint32_t iterations, concord_crypt_hash_algorithm_t algorithm, uint8_t *key, size_t key_len);
char *concord_crypt_password_hash(const char *password, concord_crypt_hash_algorithm_t algorithm);
int concord_crypt_password_verify(const char *password, const char *hash);

#endif /* __CONCORD_CRYPT_H__ */ 