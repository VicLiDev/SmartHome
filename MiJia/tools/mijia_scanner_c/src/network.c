/**
 * network.c — ARP、ping、范围扫描
 *
 * 对齐 Python 版本: network.py
 * 提供 parse_ip_ranges(), arp_scan(), ping_sweep(), probe_one_ip(),
 *       discover_devices_from_alive(), arp_lookup()
 */

#include "common.h"
#include <sys/wait.h>

/**
 * parse_ip_ranges — 解析 IP 范围字符串
 * 对齐 Python network.py: parse_ip_ranges()
 * 支持格式: CIDR / 起止范围 / 单 IP / 逗号分隔
 *
 * Returns: 解析出的 IP 数量（写入 ips_out）
 */
int parse_ip_ranges(const char *spec, char **ips_out, int max_ips) {
    int count = 0;
    char buf[MAX_STR * 4];
    strncpy(buf, spec, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *saveptr = NULL;
    char *part = strtok_r(buf, ",", &saveptr);

    while (part && count < max_ips) {
        /* 去空格 */
        while (*part == ' ') part++;
        if (!*part) { part = strtok_r(NULL, ",", &saveptr); continue; }

        if (strchr(part, '/')) {
            /* CIDR: 192.168.1.0/24 */
            char network_str[MAX_STR];
            strncpy(network_str, part, MAX_STR - 1);
            network_str[MAX_STR - 1] = '\0';

            char *slash = strchr(network_str, '/');
            int prefix_len = atoi(slash + 1);
            *slash = '\0';

            struct in_addr addr;
            inet_pton(AF_INET, network_str, &addr);
            uint32_t net = ntohl(addr.s_addr);

            uint32_t mask = prefix_len == 0 ? 0 : (~0u << (32 - prefix_len));
            uint32_t start_ip = (net & mask) + 1; /* skip network */
            uint32_t end_ip = (net | ~mask) - 1;   /* skip broadcast */

            for (uint32_t ip = start_ip; ip <= end_ip && count < max_ips; ip++) {
                struct in_addr a;
                a.s_addr = htonl(ip);
                ips_out[count] = malloc(MAX_STR);
                strncpy(ips_out[count], inet_ntoa(a), MAX_STR - 1);
                ips_out[count][MAX_STR - 1] = '\0';
                count++;
            }
        } else if (strchr(part, '-')) {
            /* 范围: 192.168.1.1-254 或 192.168.1.1-192.168.1.254 */
            char range[MAX_STR];
            strncpy(range, part, MAX_STR - 1);
            range[MAX_STR - 1] = '\0';

            char *dash = strchr(range, '-');
            *dash = '\0';
            char *end_str = dash + 1;

            uint32_t start_ip, end_ip;
            struct in_addr a;

            if (strchr(end_str, '.')) {
                /* 全格式: 192.168.1.1-192.168.1.254 */
                inet_pton(AF_INET, range, &a);
                start_ip = ntohl(a.s_addr);
                inet_pton(AF_INET, end_str, &a);
                end_ip = ntohl(a.s_addr);
            } else {
                /* 简格式: 192.168.1.1-254 */
                char *last_dot = strrchr(range, '.');
                *last_dot = '\0';
                int start_octet = atoi(last_dot + 1);
                int end_octet = atoi(end_str);

                char base[MAX_STR];
                strncpy(base, range, MAX_STR - 1);
                strcat(base, ".");

                for (int i = start_octet; i <= end_octet && count < max_ips; i++) {
                    ips_out[count] = malloc(MAX_STR);
                    snprintf(ips_out[count], MAX_STR, "%s%d", base, i);
                    count++;
                }
                part = strtok_r(NULL, ",", &saveptr);
                continue;
            }

            for (uint32_t ip = start_ip; ip <= end_ip && count < max_ips; ip++) {
                a.s_addr = htonl(ip);
                ips_out[count] = malloc(MAX_STR);
                strncpy(ips_out[count], inet_ntoa(a), MAX_STR - 1);
                ips_out[count][MAX_STR - 1] = '\0';
                count++;
            }
        } else {
            /* 单个 IP */
            ips_out[count] = malloc(MAX_STR);
            strncpy(ips_out[count], part, MAX_STR - 1);
            ips_out[count][MAX_STR - 1] = '\0';
            count++;
        }

        part = strtok_r(NULL, ",", &saveptr);
    }

    return count;
}

/**
 * arp_scan — 读取系统 ARP 表
 * 对齐 Python network.py: arp_scan()
 * 读取 /proc/net/arp，返回所有已完成 ARP 条目
 */
void arp_scan(device_list_t *arp_devices) {
    FILE *fp = fopen("/proc/net/arp", "r");
    if (!fp) return;

    char line[512];
    /* 跳过表头 */
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return; }

    while (fgets(line, sizeof(line), fp)) {
        char ip[64], hw_type[16], flags[16], mac[64], mask[16], dev[32];
        int n = sscanf(line, "%s %s %s %s %s %s", ip, hw_type, flags, mac, mask, dev);
        if (n < 6) continue;

        /* 只保留已解析的以太网条目 (flags 0x2 = completed) */
        if (strcmp(mac, "00:00:00:00:00:00") == 0) continue;
        if (strcmp(flags, "0x2") != 0 && strcmp(flags, "0x6") != 0) continue;

        char vendor[64] = {0}, dtype[64] = {0};
        lookup_mac_vendor(mac, vendor, dtype);

        if (*vendor) {
            device_t *d = device_list_add(arp_devices);
            strncpy(d->ip, ip, MAX_STR - 1);
            strncpy(d->mac, mac, 23);
            strncpy(d->vendor, vendor, 63);
            strncpy(d->type, dtype, 63);
            strncpy(d->name, vendor, MAX_DEV_NAME - 1);
            strncpy(d->protocol, "ARP", 31);
            d->port = 0;
        }
    }

    fclose(fp);
}

/**
 * ping_sweep — 用 fping 快速扫描存活 IP
 * 对齐 Python network.py: _ping_sweep()
 * 优先使用 fping（并行，速度快），降级到系统 ping
 */
void ping_sweep(char **ips, int count, char **alive_out, int *alive_count) {
    *alive_count = 0;

    if (count == 0) return;

    /* 检查 fping 是否可用 */
    if (system("which fping >/dev/null 2>&1") == 0) {
        /* 构建 fping 命令 */
        int cmd_len = 64 + count * (MAX_STR + 4);
        char *cmd = malloc(cmd_len);
        snprintf(cmd, cmd_len, "fping -a -q -t500");
        for (int i = 0; i < count && (int)strlen(cmd) < cmd_len - MAX_STR - 4; i++)
            sprintf(cmd + strlen(cmd), " %s", ips[i]);

        FILE *fp = popen(cmd, "r");
        free(cmd);
        if (fp) {
            char line[MAX_STR];
            while (fgets(line, sizeof(line), fp)) {
                /* 去掉换行 */
                line[strcspn(line, "\n")] = '\0';
                if (*line && *alive_count < count) {
                    alive_out[*alive_count] = strdup(line);
                    (*alive_count)++;
                }
            }
            pclose(fp);
        }
        return;
    }

    /* 降级: 逐个 ping（很慢） */
    printf("  %s\n", color_dim("(未安装 fping，使用逐个 ping，速度较慢，建议: apt install fping)"));

    for (int i = 0; i < count; i++) {
        char cmd[MAX_STR + 32];
        snprintf(cmd, sizeof(cmd), "ping -c 1 -W 2 %s >/dev/null 2>&1", ips[i]);
        if (system(cmd) == 0 && *alive_count < count) {
            alive_out[*alive_count] = strdup(ips[i]);
            (*alive_count)++;
        }
    }
}

/**
 * probe_one_ip — 探测单个 IP（线程安全）
 * 对齐 Python network.py: _probe_one_ip()
 */
typedef struct {
    const char *ip;
    const uint8_t *hello;
    int timeout;
    device_t result;
    bool found;
} probe_arg_t;

static void *probe_one_ip_thread(void *arg) {
    probe_arg_t *pa = (probe_arg_t *)arg;
    pa->found = false;
    memset(&pa->result, 0, sizeof(pa->result));

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return NULL;

    struct timeval tv;
    tv.tv_sec = pa->timeout;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(MIIO_PORT);
    inet_pton(AF_INET, pa->ip, &addr.sin_addr);

    sendto(sock, pa->hello, 32, 0, (struct sockaddr *)&addr, sizeof(addr));

    uint8_t buf[4096];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
    close(sock);

    if (n < 32) return NULL;

    uint16_t magic = (buf[0] << 8) | buf[1];
    if (magic != MIIO_MAGIC) return NULL;

    uint32_t device_id = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
                         ((uint32_t)buf[6] << 8)  | (uint32_t)buf[7];
    if (device_id == 0xFFFFFFFF) return NULL;

    uint32_t ts = ((uint32_t)buf[8] << 24) | ((uint32_t)buf[9] << 16) |
                  ((uint32_t)buf[10] << 8) | (uint32_t)buf[11];

    /* Token */
    char token_hex[TOKEN_HEX_LEN + 1] = {0};
    int token_bytes_len = (n >= 44) ? 16 : (n >= 28 ? n - 28 : 0);
    if (token_bytes_len > 0) {
        bool has_nz = false;
        for (int i = 0; i < token_bytes_len; i++)
            if (buf[28 + i]) { has_nz = true; break; }
        if (has_nz)
            for (int i = 0; i < token_bytes_len; i++)
                sprintf(token_hex + i * 2, "%02x", buf[28 + i]);
    }

    /* Model 解析 */
    char model[MAX_MODEL] = "unknown";
    if (n > 32) {
        /* 简化: 查找 model= 或 JSON "model" */
        const uint8_t *extra = buf + 32;
        int elen = n - 32;
        /* 尝试 key=value */
        for (int i = 0; i < elen - 6; i++) {
            if (memcmp(extra + i, "model=", 6) == 0) {
                int j = 0;
                for (int k = i + 6; k < elen && j < MAX_MODEL - 1; k++) {
                    char c = (char)extra[k];
                    if (c == '&' || c == ' ' || c == '\0' || c == '\n') break;
                    model[j++] = c;
                }
                model[j] = '\0';
                break;
            }
        }
    }

    char name[MAX_DEV_NAME], dtype[64];
    device_db_lookup(model, name, dtype);

    strncpy(pa->result.ip, pa->ip, MAX_STR - 1);
    pa->result.port = MIIO_PORT;
    pa->result.device_id = device_id;
    strncpy(pa->result.model, model, MAX_MODEL - 1);
    strncpy(pa->result.name, name, MAX_DEV_NAME - 1);
    strncpy(pa->result.type, dtype, 63);
    strncpy(pa->result.token, token_hex, TOKEN_HEX_LEN);
    pa->result.timestamp = ts;
    time_t now_t = time(NULL);
    struct tm *tm_info = localtime(&now_t);
    strftime(pa->result.last_seen, 32, "%Y-%m-%d %H:%M:%S", tm_info);
    strncpy(pa->result.protocol, "miIO", 31);

    pa->found = true;
    return NULL;
}

/**
 * discover_devices_from_alive — 对存活 IP 并行 miIO Hello 探测
 * 对齐 Python network.py: discover_devices_from_alive()
 */
void discover_devices_from_alive(char **alive_ips, int count, int timeout, device_list_t *devices) {
    if (count == 0) return;

    uint8_t hello[32];
    build_hello_packet(hello);

    printf("  %s\n", color_dim("并行探测 IP (线程池)..."));

    /* 创建线程数组 */
    int max_threads = count < 64 ? count : 64;
    probe_arg_t *args = calloc(count, sizeof(probe_arg_t));
    pthread_t *threads = calloc(count, sizeof(pthread_t));

    /* 启动线程（批量，最多 64 并发） */
    int started = 0;
    while (started < count) {
        int batch = count - started;
        if (batch > max_threads) batch = max_threads;

        for (int i = 0; i < batch; i++) {
            int idx = started + i;
            args[idx].ip = alive_ips[idx];
            args[idx].hello = hello;
            args[idx].timeout = timeout;
            pthread_create(&threads[idx], NULL, probe_one_ip_thread, &args[idx]);
        }

        /* 等待本批次完成 */
        for (int i = 0; i < batch; i++) {
            int idx = started + i;
            pthread_join(threads[idx], NULL);
        }

        started += batch;
    }

    /* 收集结果（去重 device_id） */
    uint32_t *seen_ids = NULL;
    int seen_count = 0, seen_cap = 0;

    for (int i = 0; i < count; i++) {
        if (!args[i].found) continue;

        uint32_t did = args[i].result.device_id;
        bool dup = false;
        for (int j = 0; j < seen_count; j++) {
            if (seen_ids[j] == did) { dup = true; break; }
        }
        if (dup) continue;

        if (seen_count >= seen_cap) {
            seen_cap = seen_cap == 0 ? 32 : seen_cap * 2;
            seen_ids = realloc(seen_ids, seen_cap * sizeof(uint32_t));
        }
        seen_ids[seen_count++] = did;

        /* 查 ARP 补充 MAC */
        const char *mac = arp_lookup(args[i].result.ip);
        if (mac) {
            strncpy(args[i].result.mac, mac, 23);
            char vendor[64] = {0}, vtype[64] = {0};
            lookup_mac_vendor(mac, vendor, vtype);
            strncpy(args[i].result.vendor, vendor, 63);
            if (!*args[i].result.type || strcmp(args[i].result.type, "未知") == 0)
                strncpy(args[i].result.type, vtype, 63);
        }

        device_t *d = device_list_add(devices);
        *d = args[i].result;
    }

    free(seen_ids);
    free(args);
    free(threads);
}

/**
 * arp_lookup — 查询单个 IP 的 MAC 地址
 * 对齐 Python network.py: arp_lookup()
 */
const char *arp_lookup(const char *ip) {
    static char mac_buf[24];
    mac_buf[0] = '\0';

    FILE *fp = fopen("/proc/net/arp", "r");
    if (!fp) return NULL;

    char line[512];
    /* 跳过表头 */
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return NULL; }

    while (fgets(line, sizeof(line), fp)) {
        char fip[64], fmac[64];
        int n = sscanf(line, "%s %*s %*s %s", fip, fmac);
        if (n < 2) continue;
        if (strcmp(fip, ip) == 0 && strcmp(fmac, "00:00:00:00:00:00") != 0) {
            strncpy(mac_buf, fmac, 23);
            mac_buf[23] = '\0';
            fclose(fp);
            return mac_buf;
        }
    }

    fclose(fp);
    return NULL;
}
