/*
 * discovery.c — UDP 广播发现 miIO 设备
 *
 * 核心流程：
 *   1. 向 224.0.0.50:54321 发送 Hello 报文（明文）
 *   2. 加入组播组，收集所有响应
 *   3. 解析设备 ID 和时间戳
 */

#include "discovery.h"
#include "miio_crypto.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

/* Hello 报文模板（明文，无 token） */
static const uint8_t HELLO_PACKET[] = {
    /* Magic */       0x21, 0x31,
    /* Length */      0x00, 0x20,           /* = 32 bytes (header only) */
    /* Device ID */   0xFF, 0xFF, 0xFF, 0xFF,
    /* Timestamp */   0x00, 0x00, 0x00, 0x00,
    /* Nonce (16B) */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* Signature (32B) — 全零 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

int miio_discover(MiioDevice *results, int max_count, int timeout_s)
{
    if (!results || max_count <= 0)
        return -1;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

    struct timeval tv;
    tv.tv_sec = timeout_s;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* 加入组播组 */
    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = inet_addr(MIIO_MULTICAST);
    mreq.imr_interface.s_addr = INADDR_ANY;
    if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        /* 组播加入失败不致命，单播也能收到响应 */
        fprintf(stderr, "[WARN] 无法加入组播组 %s\n", MIIO_MULTICAST);
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(MIIO_PORT);
    dest.sin_addr.s_addr = inet_addr(MIIO_MULTICAST);

    /* 发送 Hello 广播 */
    ssize_t sent = sendto(sock, HELLO_PACKET, sizeof(HELLO_PACKET), 0,
                          (struct sockaddr *)&dest, sizeof(dest));
    if (sent < 0) {
        perror("sendto");
        close(sock);
        return -1;
    }

    printf("[INFO] 已发送 Hello 广播到 %s:%d\n", MIIO_MULTICAST, MIIO_PORT);

    /* 收集响应 */
    int count = 0;
    uint8_t buf[4096];

    while (count < max_count) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                             (struct sockaddr *)&from, &fromlen);
        if (n < 0)
            break;  /* 超时或错误 */

        if ((size_t)n < MIIO_HEADER_SIZE)
            continue;

        /* 解析头部 */
        MiioHeader hdr;
        memcpy(&hdr, buf, MIIO_HEADER_SIZE);

        /* 验证 magic number */
        if (hdr.magic != MIIO_MAGIC)
            continue;

        /* 过滤掉自己的广播包（device_id == 0xFFFFFFFF） */
        if (hdr.device_id == 0xFFFFFFFF)
            continue;

        MiioDevice *d = &results[count];
        memset(d, 0, sizeof(*d));

        strncpy(d->ip, inet_ntoa(from.sin_addr), MAX_IP_STRLEN - 1);
        d->port = MIIO_PORT;
        d->device_id = hdr.device_id;
        d->last_seen = hdr.timestamp;
        d->online = 1;
        strcpy(d->model, "unknown");
        d->token[0] = '\0';

        count++;
    }

    close(sock);
    return count;
}

int miio_handshake(const char *ip, const char *token, MiioDevice *info)
{
    if (!ip || !info)
        return -1;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return -1;

    struct timeval tv = {.tv_sec = 10, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(MIIO_PORT);
    dest.sin_addr.s_addr = inet_addr(ip);

    sendto(sock, HELLO_PACKET, sizeof(HELLO_PACKET), 0,
           (struct sockaddr *)&dest, sizeof(dest));

    uint8_t resp[4096];
    struct sockaddr_in from;
    socklen_t fromlen = sizeof(from);
    ssize_t n = recvfrom(sock, resp, sizeof(resp), 0,
                         (struct sockaddr *)&from, &fromlen);
    close(sock);

    if (n < (ssize_t)MIIO_HEADER_SIZE)
        return -1;

    MiioHeader hdr;
    memcpy(&hdr, resp, MIIO_HEADER_SIZE);

    if (hdr.magic != MIIO_MAGIC)
        return -1;

    memset(info, 0, sizeof(*info));
    strncpy(info->ip, ip, MAX_IP_STRLEN - 1);
    info->port = MIIO_PORT;
    info->device_id = hdr.device_id;
    info->last_seen = hdr.timestamp;
    info->online = 1;
    if (token)
        strncpy(info->token, token, TOKEN_HEX_LEN);

    return 0;
}
