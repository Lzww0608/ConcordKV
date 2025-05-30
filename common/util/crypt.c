/*
* @Author: Lzww0608
* @Date: 2025-5-30 09:56:35
* @LastEditors: Lzww0608
* @LastEditTime: 2025-5-30 09:56:35
* @Description: ConcordKV utility functions - crypt.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>  // 添加这个头文件用于getpid()
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/md5.h>
#include <openssl/aes.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#include "crypt.h"

// OpenSSL版本兼容性
#if OPENSSL_VERSION_NUMBER < 0x10100000L
// 为旧版本OpenSSL添加缺失的函数
#define EVP_CIPHER_key_length(cipher) ((cipher)->key_len)
#define EVP_CIPHER_iv_length(cipher) ((cipher)->iv_len)
#define EVP_MD_size(md) ((md)->md_size)

// 兼容性函数实现
static EVP_CIPHER_CTX *EVP_CIPHER_CTX_new(void) {
    EVP_CIPHER_CTX *ctx = malloc(sizeof(EVP_CIPHER_CTX));
    if (ctx) {
        EVP_CIPHER_CTX_init(ctx);
    }
    return ctx;
}

static void EVP_CIPHER_CTX_free(EVP_CIPHER_CTX *ctx) {
    if (ctx) {
        EVP_CIPHER_CTX_cleanup(ctx);
        free(ctx);
    }
}

static EVP_MD_CTX *EVP_MD_CTX_new(void) {
    EVP_MD_CTX *ctx = malloc(sizeof(EVP_MD_CTX));
    if (ctx) {
        EVP_MD_CTX_init(ctx);
    }
    return ctx;
}

static void EVP_MD_CTX_free(EVP_MD_CTX *ctx) {
    if (ctx) {
        EVP_MD_CTX_cleanup(ctx);
        free(ctx);
    }
}

static HMAC_CTX *HMAC_CTX_new(void) {
    HMAC_CTX *ctx = malloc(sizeof(HMAC_CTX));
    if (ctx) {
        HMAC_CTX_init(ctx);
    }
    return ctx;
}

static void HMAC_CTX_free(HMAC_CTX *ctx) {
    if (ctx) {
        HMAC_CTX_cleanup(ctx);
        free(ctx);
    }
}
#endif

// 全局初始化标志
static int g_crypt_initialized = 0;

// 打印OpenSSL错误
static void print_openssl_error(const char *msg) {
    fprintf(stderr, "%s: ", msg);
    ERR_print_errors_fp(stderr);
}

// 初始化加密库
int concord_crypt_init(void) {
    if (g_crypt_initialized) {
        return 0;  // 已经初始化
    }
    
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    // 旧版OpenSSL需要手动初始化
    OpenSSL_add_all_algorithms();
    ERR_load_crypto_strings();
    RAND_seed(&g_crypt_initialized, sizeof(g_crypt_initialized));
#endif

    // 随机数初始化
    if (!RAND_status()) {
        unsigned char buf[128];
        time_t now = time(NULL);
        unsigned long pid = (unsigned long)getpid();
        
        RAND_add(&now, sizeof(now), 1.0);
        RAND_add(&pid, sizeof(pid), 1.0);
        
        memset(buf, 0, sizeof(buf));
        if (RAND_bytes(buf, sizeof(buf)) <= 0) {
            print_openssl_error("RAND_bytes failed");
            return -1;
        }
        
        RAND_add(buf, sizeof(buf), sizeof(buf));
    }
    
    g_crypt_initialized = 1;
    return 0;
}

// 清理加密库
void concord_crypt_cleanup(void) {
    if (!g_crypt_initialized) {
        return;
    }
    
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    // 旧版OpenSSL需要手动清理
    EVP_cleanup();
    ERR_free_strings();
#endif
    
    g_crypt_initialized = 0;
}

// 生成随机数据
int concord_crypt_random(uint8_t *buf, size_t len) {
    if (!g_crypt_initialized) {
        if (concord_crypt_init() != 0) {
            return -1;
        }
    }
    
    if (RAND_bytes(buf, len) <= 0) {
        print_openssl_error("RAND_bytes failed");
        return -1;
    }
    
    return 0;
}

// 获取加密算法的EVP密码
static const EVP_CIPHER *get_cipher(concord_crypt_algorithm_t algorithm) {
    switch (algorithm) {
        case CONCORD_CRYPT_AES_128_CBC:
            return EVP_aes_128_cbc();
        case CONCORD_CRYPT_AES_192_CBC:
            return EVP_aes_192_cbc();
        case CONCORD_CRYPT_AES_256_CBC:
            return EVP_aes_256_cbc();
        case CONCORD_CRYPT_AES_128_CTR:
            return EVP_aes_128_ctr();
        case CONCORD_CRYPT_AES_192_CTR:
            return EVP_aes_192_ctr();
        case CONCORD_CRYPT_AES_256_CTR:
            return EVP_aes_256_ctr();
        case CONCORD_CRYPT_AES_128_GCM:
            return EVP_aes_128_gcm();
        case CONCORD_CRYPT_AES_192_GCM:
            return EVP_aes_192_gcm();
        case CONCORD_CRYPT_AES_256_GCM:
            return EVP_aes_256_gcm();
        case CONCORD_CRYPT_CHACHA20:
            return EVP_chacha20();
        case CONCORD_CRYPT_CHACHA20_POLY1305:
            return EVP_chacha20_poly1305();
        default:
            return NULL;
    }
}

// 获取哈希算法的EVP摘要
static const EVP_MD *get_digest(concord_crypt_hash_algorithm_t algorithm) {
    switch (algorithm) {
        case CONCORD_CRYPT_HASH_MD5:
            return EVP_md5();
        case CONCORD_CRYPT_HASH_SHA1:
            return EVP_sha1();
        case CONCORD_CRYPT_HASH_SHA224:
            return EVP_sha224();
        case CONCORD_CRYPT_HASH_SHA256:
            return EVP_sha256();
        case CONCORD_CRYPT_HASH_SHA384:
            return EVP_sha384();
        case CONCORD_CRYPT_HASH_SHA512:
            return EVP_sha512();
        case CONCORD_CRYPT_HASH_BLAKE2B:
            return EVP_blake2b512();
        case CONCORD_CRYPT_HASH_BLAKE2S:
            return EVP_blake2s256();
        default:
            return NULL;
    }
}

// 修改 crypt_context_t 结构添加 key 字段
typedef struct {
    concord_crypt_algorithm_t algorithm;  // 算法类型
    EVP_CIPHER_CTX *ctx;                 // 算法特定上下文
    uint8_t key[32];                     // 存储密钥（最大支持256位）
} crypt_context_internal_t;

// 修改 crypt_hash_context_t 结构
typedef struct {
    concord_crypt_hash_algorithm_t algorithm;  // 算法类型
    EVP_MD_CTX *ctx;                          // 算法特定上下文
} crypt_hash_context_internal_t;

// 修改 crypt_hmac_context_t 结构
typedef struct {
    concord_crypt_hmac_algorithm_t algorithm;  // 算法类型
    HMAC_CTX *ctx;                            // 算法特定上下文
} crypt_hmac_context_internal_t;

// 创建加密上下文
concord_crypt_context_t *concord_crypt_create(concord_crypt_algorithm_t algorithm, const uint8_t *key, size_t key_len) {
    if (!g_crypt_initialized) {
        if (concord_crypt_init() != 0) {
            return NULL;
        }
    }
    
    const EVP_CIPHER *cipher = get_cipher(algorithm);
    if (!cipher) {
        fprintf(stderr, "Unknown cipher algorithm: %d\n", algorithm);
        return NULL;
    }
    
    // 检查密钥长度
    if (key_len != (size_t)EVP_CIPHER_key_length(cipher)) {
        fprintf(stderr, "Invalid key length: %zu (expected %d)\n", key_len, EVP_CIPHER_key_length(cipher));
        return NULL;
    }
    
    crypt_context_internal_t *internal_ctx = malloc(sizeof(crypt_context_internal_t));
    if (!internal_ctx) {
        return NULL;
    }
    
    internal_ctx->algorithm = algorithm;
    internal_ctx->ctx = EVP_CIPHER_CTX_new();
    if (!internal_ctx->ctx) {
        print_openssl_error("EVP_CIPHER_CTX_new failed");
        free(internal_ctx);
        return NULL;
    }
    
    // 保存密钥
    if (key_len <= sizeof(internal_ctx->key)) {
        memcpy(internal_ctx->key, key, key_len);
    }
    
    return (concord_crypt_context_t *)internal_ctx;
}

// 销毁加密上下文
void concord_crypt_destroy(concord_crypt_context_t *ctx) {
    if (!ctx) {
        return;
    }
    
    crypt_context_internal_t *internal_ctx = (crypt_context_internal_t *)ctx;
    
    if (internal_ctx->ctx) {
        EVP_CIPHER_CTX_free(internal_ctx->ctx);
    }
    
    // 安全清除密钥
    memset(internal_ctx->key, 0, sizeof(internal_ctx->key));
    
    free(internal_ctx);
}

// 加密数据
int concord_crypt_encrypt(concord_crypt_context_t *ctx, const uint8_t *in, size_t in_len, uint8_t *out, size_t *out_len, const uint8_t *iv, size_t iv_len) {
    if (!ctx || !in || !out || !out_len || !iv) {
        return -1;
    }
    
    crypt_context_internal_t *internal_ctx = (crypt_context_internal_t *)ctx;
    if (!internal_ctx->ctx) {
        return -1;
    }
    
    const EVP_CIPHER *cipher = get_cipher(internal_ctx->algorithm);
    if (!cipher) {
        fprintf(stderr, "Unknown cipher algorithm: %d\n", internal_ctx->algorithm);
        return -1;
    }
    
    // 检查IV长度
    if (iv_len != (size_t)EVP_CIPHER_iv_length(cipher)) {
        fprintf(stderr, "Invalid IV length: %zu (expected %d)\n", iv_len, EVP_CIPHER_iv_length(cipher));
        return -1;
    }
    
    // 初始化加密
    if (!EVP_EncryptInit_ex(internal_ctx->ctx, cipher, NULL, NULL, NULL)) {
        print_openssl_error("EVP_EncryptInit_ex failed");
        return -1;
    }
    
    // 设置密钥和IV
    if (!EVP_EncryptInit_ex(internal_ctx->ctx, NULL, NULL, internal_ctx->key, iv)) {
        print_openssl_error("EVP_EncryptInit_ex failed");
        return -1;
    }
    
    int out_len_int = 0;
    int final_len = 0;
    
    // 加密数据
    if (!EVP_EncryptUpdate(internal_ctx->ctx, out, &out_len_int, in, in_len)) {
        print_openssl_error("EVP_EncryptUpdate failed");
        return -1;
    }
    
    // 完成加密
    if (!EVP_EncryptFinal_ex(internal_ctx->ctx, out + out_len_int, &final_len)) {
        print_openssl_error("EVP_EncryptFinal_ex failed");
        return -1;
    }
    
    *out_len = out_len_int + final_len;
    return 0;
}

// 解密数据
int concord_crypt_decrypt(concord_crypt_context_t *ctx, const uint8_t *in, size_t in_len, uint8_t *out, size_t *out_len, const uint8_t *iv, size_t iv_len) {
    if (!ctx || !in || !out || !out_len || !iv) {
        return -1;
    }
    
    crypt_context_internal_t *internal_ctx = (crypt_context_internal_t *)ctx;
    if (!internal_ctx->ctx) {
        return -1;
    }
    
    const EVP_CIPHER *cipher = get_cipher(internal_ctx->algorithm);
    if (!cipher) {
        fprintf(stderr, "Unknown cipher algorithm: %d\n", internal_ctx->algorithm);
        return -1;
    }
    
    // 检查IV长度
    if (iv_len != (size_t)EVP_CIPHER_iv_length(cipher)) {
        fprintf(stderr, "Invalid IV length: %zu (expected %d)\n", iv_len, EVP_CIPHER_iv_length(cipher));
        return -1;
    }
    
    // 初始化解密
    if (!EVP_DecryptInit_ex(internal_ctx->ctx, cipher, NULL, NULL, NULL)) {
        print_openssl_error("EVP_DecryptInit_ex failed");
        return -1;
    }
    
    // 设置密钥和IV
    if (!EVP_DecryptInit_ex(internal_ctx->ctx, NULL, NULL, internal_ctx->key, iv)) {
        print_openssl_error("EVP_DecryptInit_ex failed");
        return -1;
    }
    
    int out_len_int = 0;
    int final_len = 0;
    
    // 解密数据
    if (!EVP_DecryptUpdate(internal_ctx->ctx, out, &out_len_int, in, in_len)) {
        print_openssl_error("EVP_DecryptUpdate failed");
        return -1;
    }
    
    // 完成解密
    if (!EVP_DecryptFinal_ex(internal_ctx->ctx, out + out_len_int, &final_len)) {
        print_openssl_error("EVP_DecryptFinal_ex failed");
        return -1;
    }
    
    *out_len = out_len_int + final_len;
    return 0;
}

// 创建哈希上下文
concord_crypt_hash_context_t *concord_crypt_hash_create(concord_crypt_hash_algorithm_t algorithm) {
    if (!g_crypt_initialized) {
        if (concord_crypt_init() != 0) {
            return NULL;
        }
    }
    
    const EVP_MD *md = get_digest(algorithm);
    if (!md) {
        fprintf(stderr, "Unknown hash algorithm: %d\n", algorithm);
        return NULL;
    }
    
    concord_crypt_hash_context_t *ctx = malloc(sizeof(concord_crypt_hash_context_t));
    if (!ctx) {
        return NULL;
    }
    
    ctx->algorithm = algorithm;
    ctx->ctx = EVP_MD_CTX_new();
    if (!ctx->ctx) {
        print_openssl_error("EVP_MD_CTX_new failed");
        free(ctx);
        return NULL;
    }
    
    if (!EVP_DigestInit_ex(ctx->ctx, md, NULL)) {
        print_openssl_error("EVP_DigestInit_ex failed");
        EVP_MD_CTX_free(ctx->ctx);
        free(ctx);
        return NULL;
    }
    
    return ctx;
}

// 销毁哈希上下文
void concord_crypt_hash_destroy(concord_crypt_hash_context_t *ctx) {
    if (!ctx) {
        return;
    }
    
    if (ctx->ctx) {
        EVP_MD_CTX_free(ctx->ctx);
    }
    
    free(ctx);
}

// 更新哈希数据
int concord_crypt_hash_update(concord_crypt_hash_context_t *ctx, const uint8_t *data, size_t len) {
    if (!ctx || !ctx->ctx || !data) {
        return -1;
    }
    
    if (!EVP_DigestUpdate(ctx->ctx, data, len)) {
        print_openssl_error("EVP_DigestUpdate failed");
        return -1;
    }
    
    return 0;
}

// 完成哈希计算
int concord_crypt_hash_final(concord_crypt_hash_context_t *ctx, uint8_t *digest, size_t *digest_len) {
    if (!ctx || !ctx->ctx || !digest || !digest_len) {
        return -1;
    }
    
    const EVP_MD *md = get_digest(ctx->algorithm);
    if (!md) {
        fprintf(stderr, "Unknown hash algorithm: %d\n", ctx->algorithm);
        return -1;
    }
    
    unsigned int md_len = EVP_MD_size(md);
    if (*digest_len < md_len) {
        fprintf(stderr, "Buffer too small for digest: %zu (required %u)\n", *digest_len, md_len);
        return -1;
    }
    
    if (!EVP_DigestFinal_ex(ctx->ctx, digest, &md_len)) {
        print_openssl_error("EVP_DigestFinal_ex failed");
        return -1;
    }
    
    *digest_len = md_len;
    return 0;
}

// 一步式哈希计算
int concord_crypt_hash_simple(concord_crypt_hash_algorithm_t algorithm, const uint8_t *data, size_t len, uint8_t *digest, size_t *digest_len) {
    concord_crypt_hash_context_t *ctx = concord_crypt_hash_create(algorithm);
    if (!ctx) {
        return -1;
    }
    
    int ret = concord_crypt_hash_update(ctx, data, len);
    if (ret != 0) {
        concord_crypt_hash_destroy(ctx);
        return ret;
    }
    
    ret = concord_crypt_hash_final(ctx, digest, digest_len);
    concord_crypt_hash_destroy(ctx);
    
    return ret;
}

// 将哈希值转换为十六进制字符串
char *concord_crypt_hash_to_hex(concord_crypt_hash_algorithm_t algorithm, const uint8_t *data, size_t len) {
    if (!g_crypt_initialized) {
        if (concord_crypt_init() != 0) {
            return NULL;
        }
    }
    
    const EVP_MD *md = get_digest(algorithm);
    if (!md) {
        fprintf(stderr, "Unknown hash algorithm: %d\n", algorithm);
        return NULL;
    }
    
    unsigned int md_len = EVP_MD_size(md);
    uint8_t digest[EVP_MAX_MD_SIZE];
    size_t digest_len = sizeof(digest);
    
    if (concord_crypt_hash_simple(algorithm, data, len, digest, &digest_len) != 0) {
        return NULL;
    }
    
    char *hex = malloc(digest_len * 2 + 1);
    if (!hex) {
        return NULL;
    }
    
    for (size_t i = 0; i < digest_len; i++) {
        sprintf(hex + i * 2, "%02x", digest[i]);
    }
    
    return hex;
}

// 创建HMAC上下文
concord_crypt_hmac_context_t *concord_crypt_hmac_create(concord_crypt_hmac_algorithm_t algorithm, const uint8_t *key, size_t key_len) {
    if (!g_crypt_initialized) {
        if (concord_crypt_init() != 0) {
            return NULL;
        }
    }
    
    const EVP_MD *md;
    switch (algorithm) {
        case CONCORD_CRYPT_HMAC_MD5:
            md = EVP_md5();
            break;
        case CONCORD_CRYPT_HMAC_SHA1:
            md = EVP_sha1();
            break;
        case CONCORD_CRYPT_HMAC_SHA224:
            md = EVP_sha224();
            break;
        case CONCORD_CRYPT_HMAC_SHA256:
            md = EVP_sha256();
            break;
        case CONCORD_CRYPT_HMAC_SHA384:
            md = EVP_sha384();
            break;
        case CONCORD_CRYPT_HMAC_SHA512:
            md = EVP_sha512();
            break;
        default:
            fprintf(stderr, "Unknown HMAC algorithm: %d\n", algorithm);
            return NULL;
    }
    
    concord_crypt_hmac_context_t *ctx = malloc(sizeof(concord_crypt_hmac_context_t));
    if (!ctx) {
        return NULL;
    }
    
    ctx->algorithm = algorithm;
    ctx->ctx = HMAC_CTX_new();
    if (!ctx->ctx) {
        print_openssl_error("HMAC_CTX_new failed");
        free(ctx);
        return NULL;
    }
    
    if (!HMAC_Init_ex(ctx->ctx, key, key_len, md, NULL)) {
        print_openssl_error("HMAC_Init_ex failed");
        HMAC_CTX_free(ctx->ctx);
        free(ctx);
        return NULL;
    }
    
    return ctx;
}

// 销毁HMAC上下文
void concord_crypt_hmac_destroy(concord_crypt_hmac_context_t *ctx) {
    if (!ctx) {
        return;
    }
    
    if (ctx->ctx) {
        HMAC_CTX_free(ctx->ctx);
    }
    
    free(ctx);
}

// 更新HMAC数据
int concord_crypt_hmac_update(concord_crypt_hmac_context_t *ctx, const uint8_t *data, size_t len) {
    if (!ctx || !ctx->ctx || !data) {
        return -1;
    }
    
    if (!HMAC_Update(ctx->ctx, data, len)) {
        print_openssl_error("HMAC_Update failed");
        return -1;
    }
    
    return 0;
}

// 完成HMAC计算
int concord_crypt_hmac_final(concord_crypt_hmac_context_t *ctx, uint8_t *digest, size_t *digest_len) {
    if (!ctx || !ctx->ctx || !digest || !digest_len) {
        return -1;
    }
    
    unsigned int md_len = *digest_len;
    if (!HMAC_Final(ctx->ctx, digest, &md_len)) {
        print_openssl_error("HMAC_Final failed");
        return -1;
    }
    
    *digest_len = md_len;
    return 0;
}

// 一步式HMAC计算
int concord_crypt_hmac_simple(concord_crypt_hmac_algorithm_t algorithm, const uint8_t *key, size_t key_len, const uint8_t *data, size_t len, uint8_t *digest, size_t *digest_len) {
    concord_crypt_hmac_context_t *ctx = concord_crypt_hmac_create(algorithm, key, key_len);
    if (!ctx) {
        return -1;
    }
    
    int ret = concord_crypt_hmac_update(ctx, data, len);
    if (ret != 0) {
        concord_crypt_hmac_destroy(ctx);
        return ret;
    }
    
    ret = concord_crypt_hmac_final(ctx, digest, digest_len);
    concord_crypt_hmac_destroy(ctx);
    
    return ret;
}

// 将HMAC值转换为十六进制字符串
char *concord_crypt_hmac_to_hex(concord_crypt_hmac_algorithm_t algorithm, const uint8_t *key, size_t key_len, const uint8_t *data, size_t len) {
    if (!g_crypt_initialized) {
        if (concord_crypt_init() != 0) {
            return NULL;
        }
    }
    
    const EVP_MD *md;
    unsigned int md_len;
    
    switch (algorithm) {
        case CONCORD_CRYPT_HMAC_MD5:
            md = EVP_md5();
            md_len = MD5_DIGEST_LENGTH;
            break;
        case CONCORD_CRYPT_HMAC_SHA1:
            md = EVP_sha1();
            md_len = SHA_DIGEST_LENGTH;
            break;
        case CONCORD_CRYPT_HMAC_SHA224:
            md = EVP_sha224();
            md_len = SHA224_DIGEST_LENGTH;
            break;
        case CONCORD_CRYPT_HMAC_SHA256:
            md = EVP_sha256();
            md_len = SHA256_DIGEST_LENGTH;
            break;
        case CONCORD_CRYPT_HMAC_SHA384:
            md = EVP_sha384();
            md_len = SHA384_DIGEST_LENGTH;
            break;
        case CONCORD_CRYPT_HMAC_SHA512:
            md = EVP_sha512();
            md_len = SHA512_DIGEST_LENGTH;
            break;
        default:
            fprintf(stderr, "Unknown HMAC algorithm: %d\n", algorithm);
            return NULL;
    }
    
    uint8_t digest[EVP_MAX_MD_SIZE];
    size_t digest_len = md_len;
    
    if (concord_crypt_hmac_simple(algorithm, key, key_len, data, len, digest, &digest_len) != 0) {
        return NULL;
    }
    
    char *hex = malloc(digest_len * 2 + 1);
    if (!hex) {
        return NULL;
    }
    
    for (size_t i = 0; i < digest_len; i++) {
        sprintf(hex + i * 2, "%02x", digest[i]);
    }
    
    return hex;
}

// Base64编码
char *concord_crypt_base64_encode(const uint8_t *data, size_t len) {
    if (!g_crypt_initialized) {
        if (concord_crypt_init() != 0) {
            return NULL;
        }
    }
    
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bmem = BIO_new(BIO_s_mem());
    
    if (!b64 || !bmem) {
        if (b64) BIO_free(b64);
        if (bmem) BIO_free(bmem);
        return NULL;
    }
    
    BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    
    if (BIO_write(b64, data, len) <= 0 || BIO_flush(b64) <= 0) {
        BIO_free_all(b64);
        return NULL;
    }
    
    BUF_MEM *bptr;
    BIO_get_mem_ptr(b64, &bptr);
    
    char *result = malloc(bptr->length + 1);
    if (!result) {
        BIO_free_all(b64);
        return NULL;
    }
    
    memcpy(result, bptr->data, bptr->length);
    result[bptr->length] = '\0';
    
    BIO_free_all(b64);
    return result;
}

// Base64解码
int concord_crypt_base64_decode(const char *str, uint8_t *data, size_t *len) {
    if (!g_crypt_initialized) {
        if (concord_crypt_init() != 0) {
            return -1;
        }
    }
    
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bmem = BIO_new_mem_buf(str, -1);
    
    if (!b64 || !bmem) {
        if (b64) BIO_free(b64);
        if (bmem) BIO_free(bmem);
        return -1;
    }
    
    BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    
    int decode_len = BIO_read(b64, data, *len);
    if (decode_len <= 0) {
        BIO_free_all(b64);
        return -1;
    }
    
    *len = decode_len;
    BIO_free_all(b64);
    return 0;
}

// PBKDF2密钥派生
int concord_crypt_pbkdf2(const char *password, size_t password_len, const uint8_t *salt, size_t salt_len, uint32_t iterations, concord_crypt_hash_algorithm_t algorithm, uint8_t *key, size_t key_len) {
    if (!g_crypt_initialized) {
        if (concord_crypt_init() != 0) {
            return -1;
        }
    }
    
    const EVP_MD *md = get_digest(algorithm);
    if (!md) {
        fprintf(stderr, "Unknown hash algorithm: %d\n", algorithm);
        return -1;
    }
    
    if (PKCS5_PBKDF2_HMAC(password, password_len, salt, salt_len, iterations, md, key_len, key) != 1) {
        print_openssl_error("PKCS5_PBKDF2_HMAC failed");
        return -1;
    }
    
    return 0;
}

// 生成密码哈希（带盐）
char *concord_crypt_password_hash(const char *password, concord_crypt_hash_algorithm_t algorithm) {
    if (!g_crypt_initialized) {
        if (concord_crypt_init() != 0) {
            return NULL;
        }
    }
    
    // 生成盐
    uint8_t salt[16];
    if (concord_crypt_random(salt, sizeof(salt)) != 0) {
        return NULL;
    }
    
    // 获取摘要大小
    const EVP_MD *md = get_digest(algorithm);
    if (!md) {
        fprintf(stderr, "Unknown hash algorithm: %d\n", algorithm);
        return NULL;
    }
    
    unsigned int md_len = EVP_MD_size(md);
    uint8_t hash[EVP_MAX_MD_SIZE];
    
    // 派生密钥
    if (concord_crypt_pbkdf2(password, strlen(password), salt, sizeof(salt), 10000, algorithm, hash, md_len) != 0) {
        return NULL;
    }
    
    // 编码（算法+盐+哈希）
    size_t b64_len = (sizeof(salt) + md_len) * 4 / 3 + 5; // 四舍五入并加上额外空间
    char *b64 = malloc(b64_len);
    if (!b64) {
        return NULL;
    }
    
    uint8_t buffer[256];
    size_t buffer_len = 0;
    
    // 添加算法标识
    buffer[buffer_len++] = algorithm;
    
    // 添加盐
    memcpy(buffer + buffer_len, salt, sizeof(salt));
    buffer_len += sizeof(salt);
    
    // 添加哈希
    memcpy(buffer + buffer_len, hash, md_len);
    buffer_len += md_len;
    
    // Base64编码
    char *encoded = concord_crypt_base64_encode(buffer, buffer_len);
    if (!encoded) {
        free(b64);
        return NULL;
    }
    
    // 构造最终字符串
    snprintf(b64, b64_len, "$%d$%s", algorithm, encoded);
    free(encoded);
    
    return b64;
}

// 验证密码
int concord_crypt_password_verify(const char *password, const char *hash_str) {
    if (!g_crypt_initialized) {
        if (concord_crypt_init() != 0) {
            return -1;
        }
    }
    
    // 解析哈希字符串
    if (hash_str[0] != '$') {
        return -1;
    }
    
    // 获取算法
    int algorithm = atoi(hash_str + 1);
    if (algorithm < 0 || algorithm >= 8) { // 8是哈希算法的数量
        return -1;
    }
    
    // 找到第二个$
    const char *b64_start = strchr(hash_str + 1, '$');
    if (!b64_start) {
        return -1;
    }
    b64_start++; // 跳过$
    
    // 解码Base64
    size_t b64_len = strlen(b64_start);
    size_t decoded_max = b64_len * 3 / 4 + 3; // 预估最大解码大小
    uint8_t *decoded = malloc(decoded_max);
    if (!decoded) {
        return -1;
    }
    
    size_t decoded_len = decoded_max;
    if (concord_crypt_base64_decode(b64_start, decoded, &decoded_len) != 0) {
        free(decoded);
        return -1;
    }
    
    // 验证解码的数据
    if (decoded_len < 1 + 16) { // 至少需要1字节算法 + 16字节盐
        free(decoded);
        return -1;
    }
    
    // 提取算法和盐
    uint8_t hash_algorithm = decoded[0];
    uint8_t *salt = decoded + 1;
    
    // 获取摘要大小
    const EVP_MD *md = get_digest(hash_algorithm);
    if (!md) {
        free(decoded);
        return -1;
    }
    
    unsigned int md_len = EVP_MD_size(md);
    if (decoded_len != 1 + 16 + md_len) { // 算法 + 盐 + 哈希
        free(decoded);
        return -1;
    }
    
    // 提取存储的哈希
    uint8_t *stored_hash = decoded + 1 + 16;
    
    // 计算密码哈希
    uint8_t computed_hash[EVP_MAX_MD_SIZE];
    if (concord_crypt_pbkdf2(password, strlen(password), salt, 16, 10000, hash_algorithm, computed_hash, md_len) != 0) {
        free(decoded);
        return -1;
    }
    
    // 比较哈希
    int result = (memcmp(stored_hash, computed_hash, md_len) == 0) ? 1 : 0;
    free(decoded);
    
    return result;
} 