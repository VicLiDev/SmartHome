/**
 * protocol.c — miIO 协议核心函数
 *
 * 对齐 Python 版本: protocol.py
 * 提供 build_hello_packet(), discover_devices()
 * miIO 广播发现（UDP 54321）
 *
 * 注: AES 加解密需要 OpenSSL，深度扫描功能暂未实现（可选）
 */

#include "common.h"
#include <sys/ioctl.h>

/**
 * build_hello_packet — 构建 miIO Hello 广播报文（32字节明文）
 * 对齐 Python protocol.py: build_hello_packet()
 */
void build_hello_packet(uint8_t *pkt) {
    memset(pkt, 0, 32);
    /* Magic: 0x2131 (big-endian) */
    pkt[0] = 0x21; pkt[1] = 0x31;
    /* Length: 32 (big-endian) */
    pkt[2] = 0x00; pkt[3] = 0x20;
    /* Device ID: 0xFFFFFFFF (big-endian) */
    pkt[4] = 0xFF; pkt[5] = 0xFF; pkt[6] = 0xFF; pkt[7] = 0xFF;
    /* Timestamp: 0 */
    /* Offset 12-27: Nonce (all 0xFF) */
    memset(pkt + 12, 0xFF, 16);
    /* Offset 28-31: Token (all 0x00) — already zero */
}

/**
 * 解析 miIO Hello 响应中的 model 字段
 * 对齐 Python 的 JSON/key=value 解析逻辑
 */
static void parse_model_from_response(const uint8_t *data, int len, char *model_out) {
    model_out[0] = '\0';
    strcpy(model_out, "unknown");

    if (len <= 32) return;

    /* 尝试 UTF-8 解码附加数据 */
    const uint8_t *extra = data + 32;
    int extra_len = len - 32;

    /* 提取可打印字符串（以 \0 分隔的多个片段） */
    char buf[1024];
    int buf_len = 0;
    for (int i = 0; i < extra_len && buf_len < (int)sizeof(buf) - 1; i++) {
        char c = (char)extra[i];
        if (c >= 0x20 && c < 0x7F) {
            buf[buf_len++] = c;
        } else {
            buf[buf_len++] = '\0';
        }
    }
    buf[buf_len] = '\0';

    /* 逐段查找 model */
    const char *p = buf;
    while (p < buf + buf_len) {
        if (!*p) { p++; continue; }

        /* 跳过不含 model 的段 */
        if (!strstr(p, "model") && !strstr(p, "MODEL")) {
            p += strlen(p) + 1;
            continue;
        }

        /* 尝试 JSON: {"model":"xxx",...} 或 {"model":"xxx"} */
        const char *json_start = strchr(p, '{');
        if (json_start) {
            /* 找到 "model":" 或 "model": " */
            const char *key = strstr(json_start, "\"model\"");
            if (key) {
                const char *colon = strchr(key + 7, ':');
                if (colon) {
                    const char *val = strchr(colon + 1, '"');
                    if (val) {
                        val++;
                        const char *end = strchr(val, '"');
                        if (end) {
                            int vlen = (int)(end - val);
                            if (vlen > 0 && vlen < MAX_MODEL) {
                                memcpy(model_out, val, vlen);
                                model_out[vlen] = '\0';
                                return;
                            }
                        }
                    }
                }
            }
        }

        /* 尝试 key=value: model=xxx */
        const char *kv = strstr(p, "model=");
        if (kv) {
            kv += 6;
            const char *end = kv;
            while (*end && *end != '&' && *end != ' ' && *end != '\0') end++;
            int vlen = (int)(end - kv);
            if (vlen > 0 && vlen < MAX_MODEL) {
                memcpy(model_out, kv, vlen);
                model_out[vlen] = '\0';
                return;
            }
        }

        p += strlen(p) + 1;
    }
}

/**
 * discover_devices — 快速扫描：发送 Hello 广播，收集所有 miIO 设备响应
 * 对齐 Python protocol.py: discover_devices()
 */
void discover_devices(int timeout, device_list_t *devices) {
    int sock;
    struct sockaddr_in addr;
    uint8_t hello[32];
    uint8_t buf[4096];
    fd_set readfds;
    struct timeval tv;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        printf("  %s\n", color_red("socket 创建失败"));
        return;
    }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* 尝试加入组播组 */
    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = inet_addr(MIIO_MULTICAST);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    /* 绑定端口（可能被占用，忽略错误） */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(MIIO_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    /* 发送 Hello 广播 */
    build_hello_packet(hello);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(MIIO_PORT);
    addr.sin_addr.s_addr = inet_addr(MIIO_MULTICAST);
    sendto(sock, hello, 32, 0, (struct sockaddr *)&addr, sizeof(addr));

    /* 收集响应 */
    uint32_t *seen_ids = NULL;
    int seen_count = 0, seen_cap = 0;

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    while (1) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1e9;
        if (elapsed >= timeout) break;

        double remaining = timeout - elapsed;
        tv.tv_sec = (long)remaining;
        tv.tv_usec = (long)((remaining - tv.tv_sec) * 1e6);
        if (tv.tv_sec == 0 && tv.tv_usec < 10000)
            tv.tv_usec = 10000; /* 最少 10ms */

        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        int ret = select(sock + 1, &readfds, NULL, NULL, &tv);
        if (ret <= 0) continue;

        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
        if (n < 32) continue;

        /* 解析头部 */
        uint16_t magic = (buf[0] << 8) | buf[1];
        if (magic != MIIO_MAGIC) continue;

        uint32_t device_id = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
                             ((uint32_t)buf[6] << 8)  | (uint32_t)buf[7];
        if (device_id == 0xFFFFFFFF) continue;

        /* 去重 */
        bool dup = false;
        for (int i = 0; i < seen_count; i++) {
            if (seen_ids[i] == device_id) { dup = true; break; }
        }
        if (dup) continue;

        /* 添加到已见列表 */
        if (seen_count >= seen_cap) {
            seen_cap = seen_cap == 0 ? 32 : seen_cap * 2;
            seen_ids = realloc(seen_ids, seen_cap * sizeof(uint32_t));
        }
        seen_ids[seen_count++] = device_id;

        uint32_t ts = ((uint32_t)buf[8] << 24) | ((uint32_t)buf[9] << 16) |
                      ((uint32_t)buf[10] << 8) | (uint32_t)buf[11];

        /* 提取 token (offset 28, 最多 16 bytes) */
        char token_hex[TOKEN_HEX_LEN + 1] = {0};
        int token_bytes_len = (n >= 44) ? 16 : (n >= 28 ? n - 28 : 0);
        if (token_bytes_len > 0) {
            bool has_nonzero = false;
            for (int i = 0; i < token_bytes_len; i++) {
                if (buf[28 + i] != 0) { has_nonzero = true; break; }
            }
            if (has_nonzero) {
                for (int i = 0; i < token_bytes_len; i++)
                    sprintf(token_hex + i * 2, "%02x", buf[28 + i]);
            }
        }

        /* 解析 model */
        char model[MAX_MODEL] = {0};
        parse_model_from_response(buf, n, model);

        char name[MAX_DEV_NAME] = {0};
        char dtype[64] = {0};
        device_db_lookup(model, name, dtype);

        /* 构建设备条目 */
        device_t *d = device_list_add(devices);
        strncpy(d->ip, inet_ntoa(from.sin_addr), MAX_STR - 1);
        d->port = MIIO_PORT;
        d->device_id = device_id;
        strncpy(d->model, model, MAX_MODEL - 1);
        strncpy(d->name, name, MAX_DEV_NAME - 1);
        strncpy(d->type, dtype, 63);
        strncpy(d->token, token_hex, TOKEN_HEX_LEN);
        d->timestamp = ts;
        time_t now_t = time(NULL);
        struct tm *tm_info = localtime(&now_t);
        strftime(d->last_seen, 32, "%Y-%m-%d %H:%M:%S", tm_info);
        strncpy(d->protocol, "miIO", 31);

        /* MAC 从 ARP 查 */
        const char *mac = arp_lookup(d->ip);
        if (mac) {
            strncpy(d->mac, mac, 23);
            char vendor[64] = {0}, vtype[64] = {0};
            lookup_mac_vendor(mac, vendor, vtype);
            strncpy(d->vendor, vendor, 63);
            if (!*d->type || strcmp(d->type, "未知") == 0)
                strncpy(d->type, vtype, 63);
        }
    }

    free(seen_ids);
    close(sock);
}
