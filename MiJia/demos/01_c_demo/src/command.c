/*
 * command.c — JSON-RPC 命令构建与发送
 *
 * 核心流程：
 *   1. Hello 握手获取设备时间戳
 *   2. 构建 JSON-RPC 载荷 → AES 加密
 *   3. 计算签名 → 组装完整报文
 *   4. UDP 发送 → 接收响应 → 解密
 */

#include "command.h"
#include "miio_crypto.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

/* ═══ 内部辅助函数 ═══ */

/**
 * 构建明文 Hello 报文
 */
static void build_hello_packet(uint8_t *buf, size_t *len)
{
    size_t off = 0;

    /* Magic (big-endian) */
    buf[off++] = 0x21; buf[off++] = 0x31;

    /* Length = 32 (header only) */
    buf[off++] = 0x00; buf[off++] = 0x20;

    /* Device ID = broadcast */
    buf[off++] = 0xFF; buf[off++] = 0xFF;
    buf[off++] = 0xFF; buf[off++] = 0xFF;

    /* Timestamp = 0 */
    buf[off++] = 0x00; buf[off++] = 0x00;
    buf[off++] = 0x00; buf[off++] = 0x00;

    /* Nonce (16 bytes, all 0xFF) */
    memset(buf + off, 0xFF, NONCE_SIZE);
    off += NONCE_SIZE;

    /* Signature (32 bytes, all 0x00) */
    memset(buf + off, 0x00, SIGNATURE_SIZE);
    off += SIGNATURE_SIZE;

    *len = off;
}

/**
 * 与设备握手，返回设备时间戳
 */
static int do_handshake(const char *ip, uint16_t port,
                        uint32_t *out_ts)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    struct timeval tv = {.tv_sec = 10, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t hello[MIIO_HEADER_SIZE];
    size_t hlen;
    build_hello_packet(hello, &hlen);

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    dest.sin_addr.s_addr = inet_addr(ip);

    sendto(sock, hello, hlen, 0, (struct sockaddr *)&dest, sizeof(dest));

    uint8_t resp[4096];
    ssize_t n = recvfrom(sock, resp, sizeof(resp), 0, NULL, NULL);
    close(sock);

    if (n < (ssize_t)MIIO_HEADER_SIZE)
        return -1;

    /* 提取时间戳 */
    memcpy(out_ts, resp + 8, 4);  /* offset 8, little-endian */
    return 0;
}

/* ═══ 公开接口 ═══ */

int miio_send_command(const char *ip, uint16_t port,
                      const char *token,
                      const char *method, const char *params,
                      int req_id,
                      MiioResponse *response,
                      int timeout)
{
    if (!ip || !token || !method || !response)
        return -1;

    /* Step 1: 握手获取设备时间戳 */
    uint32_t device_ts = 0;
    if (do_handshake(ip, port, &device_ts) != 0) {
        snprintf(response->error_msg, sizeof(response->error_msg),
                 "握手超时");
        response->error_code = -1;
        return -1;
    }

    /* Step 2: 派生密钥 */
    MiioKeys keys;
    if (miio_derive_keys(token, &keys) != 0) {
        snprintf(response->error_msg, sizeof(response->error_msg),
                 "Token 格式错误");
        response->error_code = -2;
        return -1;
    }

    /* Step 3: 构建 JSON-RPC 载荷 */
    char payload_json[2048];
    int plen = snprintf(payload_json, sizeof(payload_json),
                         "{\"id\":%d,\"method\":\"%s\",\"params\":%s}",
                         req_id, method, params ? params : "[]");

    /* Step 4: AES 加密 */
    uint8_t encrypted[MAX_PAYLOAD_SIZE + 16];
    size_t enc_len = 0;
    if (miio_encrypt((const uint8_t *)payload_json, plen,
                     &keys, encrypted, &enc_len) != 0) {
        snprintf(response->error_msg, sizeof(response->error_msg),
                 "加密失败");
        response->error_code = -3;
        return -1;
    }

    /* Step 5: 构建完整报文 */
    uint8_t packet[MIIO_HEADER_SIZE + MAX_PAYLOAD_SIZE + 16];
    size_t pkt_off = 0;

    /* Magic */
    packet[pkt_off++] = 0x21; packet[pkt_off++] = 0x31;

    /* Length (placeholder, fill later) */
    size_t len_offset = pkt_off;
    pkt_off += 2;

    /* Device ID */
    packet[pkt_off++] = 0xFF; packet[pkt_off++] = 0xFF;
    packet[pkt_off++] = 0xFF; packet[pkt_off++] = 0xFF;

    /* Timestamp */
    memcpy(packet + pkt_off, &device_ts, 4);
    pkt_off += 4;

    /* Nonce (随机) */
    uint8_t nonce[NONCE_SIZE];
    for (int i = 0; i < NONCE_SIZE; i++)
        nonce[i] = (uint8_t)(i * 17 + 42);
    memcpy(packet + pkt_off, nonce, NONCE_SIZE);
    pkt_off += NONCE_SIZE;

    /* Signature */
    uint8_t signature[SIGNATURE_SIZE];
    miio_sign(device_ts, nonce, &keys, signature);
    memcpy(packet + pkt_off, signature, SIGNATURE_SIZE);
    pkt_off += SIGNATURE_SIZE;

    /* Encrypted payload */
    memcpy(packet + pkt_off, encrypted, enc_len);
    pkt_off += enc_len;

    /* 填充长度字段（big-endian） */
    uint16_t total_len = (uint16_t)pkt_off;
    packet[len_offset]     = (total_len >> 8) & 0xFF;
    packet[len_offset + 1] = total_len & 0xFF;

    /* Step 6: 发送并接收响应 */
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        response->error_code = -4;
        strcpy(response->error_msg, "socket 创建失败");
        return -1;
    }

    struct timeval tv = {.tv_sec = timeout, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    dest.sin_addr.s_addr = inet_addr(ip);

    sendto(sock, packet, pkt_off, 0, (struct sockaddr *)&dest, sizeof(dest));

    uint8_t resp_buf[4096];
    ssize_t n = recvfrom(sock, resp_buf, sizeof(resp_buf), 0, NULL, NULL);
    close(sock);

    if (n < (ssize_t)MIIO_HEADER_SIZE) {
        snprintf(response->error_msg, sizeof(response->error_msg),
                 "响应太短 (%zd 字节)", n);
        response->error_code = -5;
        return -1;
    }

    /* Step 7: 解密响应 */
    size_t resp_enc_len = (size_t)n - MIIO_HEADER_SIZE;
    if (resp_enc_len == 0) {
        response->result_json = strdup("{\"raw_header_only\":true}");
        response->error_code = 0;
        return 0;
    }

    uint8_t decrypted[MAX_PAYLOAD_SIZE];
    size_t dec_len = 0;
    if (miio_decrypt(resp_buf + MIIO_HEADER_SIZE, resp_enc_len,
                     &keys, decrypted, &dec_len) != 0) {
        snprintf(response->error_msg, sizeof(response->error_msg),
                 "解密失败");
        response->error_code = -6;
        return -1;
    }
    decrypted[dec_len] = '\0';

    /* 解析 JSON 响应 */
    response->result_json = strdup((char *)decrypted);
    response->id = req_id;
    response->error_code = 0;

#ifdef DEBUG
    printf("[DEBUG] 响应: %s\n", decrypted);
#endif

    return 0;
}

int miio_get_info(const char *ip, const char *token,
                  char *json_out, size_t len)
{
    MiioResponse resp;
    memset(&resp, 0, sizeof(resp));
    int ret = miio_send_command(ip, MIIO_PORT, token,
                                "miIO.info", "[]", 1, &resp, 10);
    if (ret == 0 && resp.result_json) {
        strncpy(json_out, resp.result_json, len - 1);
        json_out[len - 1] = '\0';
        free(resp.result_json);
    } else {
        snprintf(json_out, len, "{\"error\":\"%s\"}", resp.error_msg);
    }
    return ret;
}

int miio_get_prop(const char *ip, const char *token,
                  const char **props, int count,
                  char *json_out, size_t len)
{
    /* 构建 props 数组 JSON */
    char params[1024] = "[";
    for (int i = 0; i < count && props[i]; i++) {
        if (i > 0) strcat(params, ",");
        strcat(params, "\"");
        strcat(params, props[i]);
        strcat(params, "\"");
    }
    strcat(params, "]");

    MiioResponse resp;
    memset(&resp, 0, sizeof(resp));
    int ret = miio_send_command(ip, MIIO_PORT, token,
                                "get_prop", params, 1, &resp, 10);
    if (ret == 0 && resp.result_json) {
        strncpy(json_out, resp.result_json, len - 1);
        json_out[len - 1] = '\0';
        free(resp.result_json);
    } else {
        snprintf(json_out, len, "{\"error\":\"%s\"}", resp.error_msg);
    }
    return ret;
}

int miio_set_power(const char *ip, const char *token,
                   const char *state)
{
    char params[64];
    snprintf(params, sizeof(params), "[\"%s\"]", state ? state : "on");

    MiioResponse resp;
    memset(&resp, 0, sizeof(resp));
    int ret = miio_send_command(ip, MIIO_PORT, token,
                                "set_power", params, 1, &resp, 10);
    if (resp.result_json) free(resp.result_json);
    return ret;
}
