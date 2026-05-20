/*
 * miio_crypto.h — miIO 加解密接口
 *
 * 使用 OpenSSL 实现：
 *   - MD5: 密钥派生 + 签名
 *   - AES-128-CBC: 载荷加解密
 */

#ifndef MIIO_CRYPTO_H
#define MIIO_CRYPTO_H

#include "miio_protocol.h"
#include <openssl/md5.h>
#include <openssl/aes.h>

/**
 * 从 token hex 字符串派生密钥三元组
 *
 * 算法:
 *   aes_key   = MD5(token_bytes)
 *   aes_iv    = MD5(aes_key || token_bytes)
 *   sign_key  = MD5(aes_key || aes_iv || aes_key)
 *
 * @param token_hex  32字符十六进制 token
 * @param keys       输出密钥结构
 * @return 0 成功, -1 失败
 */
int miio_derive_keys(const char *token_hex, MiioKeys *keys);

/**
 * AES-128-CBC 加密（PKCS7 padding）
 *
 * @param plaintext  明文
 * @param pt_len     明文长度
 * @param keys       派生密钥
 * @param out        输出缓冲区（需至少 pt_len + 16 字节）
 * @param out_len    输出长度
 * @return 0 成功, -1 失败
 */
int miio_encrypt(const uint8_t *plaintext, size_t pt_len,
                 const MiioKeys *keys,
                 uint8_t *out, size_t *out_len);

/**
 * AES-128-CBC 解密（PKCS7 unpadding）
 *
 * @param ciphertext 密文（长度必须是 16 的倍数）
 * @param ct_len     密文长度
 * @param keys       派生密钥
 * @param out        输出缓冲区
 * @param out_len    输出明文长度
 * @return 0 成功, -1 失败
 */
int miio_decrypt(const uint8_t *ciphertext, size_t ct_len,
                 const MiioKeys *keys,
                 uint8_t *out, size_t *out_len);

/**
 * 计算 miIO 报文签名
 *
 * @param timestamp  设备时间戳（4字节小端）
 * @param nonce      16字节随机数
 * @param keys       派生密钥（使用 sign_key）
 * @param sign_out   输出 32 字节签名
 */
void miio_sign(uint32_t timestamp, const uint8_t nonce[16],
               const MiioKeys *keys, uint8_t sign_out[32]);

#endif /* MIIO_CRYPTO_H */
