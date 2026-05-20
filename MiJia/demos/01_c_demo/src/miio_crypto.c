/*
 * miio_crypto.c — 基于 OpenSSL 的 miIO 加解密实现
 *
 * 核心算法（与 python-miio 一致）：
 *   aes_key   = MD5(token_bytes)
 *   aes_iv    = MD5(aes_key || token_bytes)
 *   sign_key  = MD5(aes_key || aes_iv || aes_key)
 */

#include "miio_crypto.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int miio_derive_keys(const char *token_hex, MiioKeys *keys)
{
    if (!token_hex || strlen(token_hex) != TOKEN_HEX_LEN || !keys)
        return -1;

    /* 将 hex token 转为 bytes */
    unsigned char token_bytes[16];
    for (int i = 0; i < 16; i++) {
        unsigned int val;
        if (sscanf(token_hex + i * 2, "%02x", &val) != 1)
            return -1;
        token_bytes[i] = (unsigned char)val;
    }

    /* aes_key = MD5(token) */
    MD5((const unsigned char *)token_bytes, 16, keys->aes_key);

    /* aes_iv = MD5(aes_key + token) */
    {
        unsigned char buf[32];
        memcpy(buf, keys->aes_key, 16);
        memcpy(buf + 16, token_bytes, 16);
        MD5(buf, 32, keys->aes_iv);
    }

    /* sign_key = MD5(aes_key + iv + aes_key) */
    {
        unsigned char buf[48];
        memcpy(buf, keys->aes_key, 16);
        memcpy(buf + 16, keys->aes_iv, 16);
        memcpy(buf + 32, keys->aes_key, 16);
        MD5(buf, 48, keys->sign_key);
    }

#ifdef DEBUG
    printf("[DEBUG] derive_keys: key=%02x%02x... iv=%02x%02x... sign=%02x%02x...\n",
           keys->aes_key[0], keys->aes_key[1],
           keys->aes_iv[0], keys->aes_iv[1],
           keys->sign_key[0], keys->sign_key[1]);
#endif

    return 0;
}

int miio_encrypt(const uint8_t *pt, size_t pt_len,
                 const MiioKeys *keys,
                 uint8_t *out, size_t *out_len)
{
    if (!pt || pt_len == 0 || !keys || !out || !out_len)
        return -1;

    AES_KEY aes_enc;
    AES_set_encrypt_key(keys->aes_key, 128, &aes_enc);

    /* PKCS7 padding */
    size_t padded_len = ((pt_len / 16) + 1) * 16;
    uint8_t *padded = (uint8_t *)calloc(1, padded_len);
    if (!padded) return -1;

    memcpy(padded, pt, pt_len);
    padded[padded_len - 1] = (uint8_t)(16 - (pt_len % 16));

    /* 加密（OpenSSL 会修改 IV，所以用副本） */
    uint8_t iv_copy[16];
    memcpy(iv_copy, keys->aes_iv, 16);

    AES_cbc_encrypt(padded, out, padded_len, &aes_enc, iv_copy, AES_ENCRYPT);
    *out_len = padded_len;

    free(padded);
    return 0;
}

int miio_decrypt(const uint8_t *ct, size_t ct_len,
                 const MiioKeys *keys,
                 uint8_t *out, size_t *out_len)
{
    if (!ct || ct_len == 0 || ct_len % 16 != 0 || !keys || !out)
        return -1;

    AES_KEY aes_dec;
    AES_set_decrypt_key(keys->aes_key, 128, &aes_dec);

    uint8_t iv_copy[16];
    memcpy(iv_copy, keys->aes_iv, 16);

    AES_cbc_encrypt(ct, out, ct_len, &aes_dec, iv_copy, AES_DECRYPT);

    /* 去除 PKCS7 padding */
    if (ct_len > 0) {
        uint8_t pad_val = out[ct_len - 1];
        if (pad_val > 0 && pad_val <= 16) {
            *out_len = ct_len - pad_val;
        } else {
            *out_len = ct_len;
        }
    } else {
        *out_len = 0;
    }

    return 0;
}

void miio_sign(uint32_t ts, const uint8_t nonce[16],
               const MiioKeys *keys, uint8_t sign_out[32])
{
    unsigned char buf[20];  /* 4(ts) + 16(nonce) */
    memcpy(buf, &ts, 4);     /* 小端序 */
    memcpy(buf + 4, nonce, 16);
    MD5(buf, 20, sign_out);
}
