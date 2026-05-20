/**
 * mdns.c — mDNS/HomeKit 发现 + MAC OUI 查询 + 三重发现
 *
 * 对齐 Python 版本: mdns.py
 * 使用 avahi-browse 实现 mDNS 发现（替代 Python zeroconf）
 * 提供 lookup_mac_vendor(), discover_mdns(), discover_all()
 */

#include "common.h"
#include <sys/wait.h>
#include <sys/select.h>

/* ═══════════════════════════════════════════════════════════ */
/* MAC OUI 厂商映射 (对齐 Python mdns.py MAC_OUI)              */
/* ═══════════════════════════════════════════════════════════ */

typedef struct {
    const char *prefix;
    const char *vendor;
    const char *type;
} oui_entry_t;

static const oui_entry_t MAC_OUI[] = {
    /* 小米 WiFi 设备 */
    {"b8:88:80", "小米", "网关/路由"},
    {"7c:c2:94", "小米", "IoT 子设备"},
    {"54:ef:44", "小米", "IoT 子设备"},
    {"80:ae:54", "小米", "IoT 子设备"},
    {"cc:da:20", "小米", "IoT 子设备"},
    {"90:fb:5d", "小米", "涂鸦平台"},
    {"78:11:dc", "小米", "IoT 子设备"},
    {"a4:cf:12", "小米", "IoT 子设备"},
    {"0c:1a:94", "小米", "IoT 子设备"},
    {"28:6c:07", "小米", "IoT 子设备"},
    {"f0:b4:29", "小米", "IoT 子设备"},
    {"2c:f4:32", "小米", "IoT 子设备"},
    {"64:b4:73", "小米", "IoT 子设备"},
    {"50:ec:49", "小米", "IoT 子设备"},
    {"24:f7:42", "小米", "IoT 子设备"},
    {"18:09:6a", "小米", "IoT 子设备"},
    {"48:3b:38", "小米", "IoT 子设备"},
    {"e4:be:ed", "小米", "IoT 子设备"},
    {"d4:a0:1c", "小米", "IoT 子设备"},
    {"04:cf:8c", "小米", "IoT 子设备"},
    {"64:09:80", "小米", "IoT 子设备"},
    {"98:f5:a9", "小米", "IoT 子设备"},
    {"34:ce:c8", "小米", "IoT 子设备"},
    {"ac:84:c6", "小米", "IoT 子设备"},
    {"f8:e4:e3", "小米", "IoT 子设备"},
    {"b4:7c:9c", "小米", "IoT 子设备"},
    {"28:e3:4f", "小米", "IoT 子设备"},
    {"5c:87:9c", "小米", "IoT 子设备"},
    {"a0:20:a6", "小米", "IoT 子设备"},
    {"78:67:37", "小米", "IoT 子设备"},
    {"e8:87:11", "小米", "IoT 子设备"},
    {"dc:a6:32", "小米", "ESP32 设备"},
    {"00:12:41", "小米", "蓝牙设备"},
    /* Aqara */
    {"00:15:8d", "Aqara", "Zigbee 子设备"},
    /* Yeelight */
    {"3c:05:2d", "Yeelight", "灯具"},
    {"7c:49:eb", "Yeelight", "灯具"},
    /* Apple */
    {"ac:8c:46", "Apple", "iPhone/iPad"},
    {"4e:83:09", "Apple", "Apple 设备"},
    {"ea:67:4f", "Apple", "Apple 设备"},
    {"ec:30:8e", "Apple", "Apple 设备"},
    {"f8:e4:3b", "Apple", "Apple 设备"},
    {"a4:b1:97", "Apple", "Apple 设备"},
    {"64:a2:f9", "Apple", "Apple 设备"},
    {"68:a8:6d", "Apple", "Apple 设备"},
    {"3c:22:fb", "Apple", "Apple 设备"},
    {"5c:f9:38", "Apple", "Apple 设备"},
    {"88:1f:a1", "Apple", "Apple 设备"},
    {"7c:04:d0", "Apple", "Apple 设备"},
    {"dc:2b:61", "Apple", "Apple 设备"},
    {"fc:65:de", "Apple", "Apple 设备"},
    {"08:f6:9f", "Apple", "Apple 设备"},
    /* 华为 */
    {"08:ee:ab", "华为", "IoT 设备"},
    {"b8:27:eb", "华为", "IoT 设备"},
    {"5c:cf:7f", "华为", "IoT 设备"},
    {"4c:11:bf", "华为", "IoT 设备"},
    {"cc:96:a0", "华为", "IoT 设备"},
    {"e4:b0:68", "华为", "IoT 设备"},
    /* TP-Link */
    {"a8:42:a1", "TP-Link", "路由器/插座"},
    {"b0:4e:26", "TP-Link", "路由器/插座"},
    {"5c:63:bf", "TP-Link", "路由器/插座"},
    {"60:e3:27", "TP-Link", "路由器/插座"},
    {"20:4e:7f", "TP-Link", "路由器/插座"},
    {"ec:17:2f", "TP-Link", "路由器/插座"},
    /* Espressif */
    {"24:0a:c4", "Espressif", "IoT 设备"},
    {"30:ae:a4", "Espressif", "IoT 设备"},
    {"bc:dd:c2", "Espressif", "IoT 设备"},
    /* Philips Hue */
    {"00:17:88", "Philips Hue", "灯具"},
    {"ec:b5:fa", "Philips Hue", "灯具"},
    /* Sonoff */
    {"00:1a:22", "Sonoff", "IoT 设备"},
    {"dc:4f:22", "Sonoff", "IoT 设备"},
    {"18:b4:30", "Sonoff", "IoT 设备"},
    /* Home Assistant */
    {"7c:b0:c2", "Home Assistant", "服务器"},
    {"6c:1f:f7", "Home Assistant", "服务器"},
};

#define OUI_COUNT (sizeof(MAC_OUI) / sizeof(MAC_OUI[0]))

/**
 * lookup_mac_vendor — 根据 MAC 前缀查询厂商和类型
 * 对齐 Python mdns.py: lookup_mac_vendor()
 */
void lookup_mac_vendor(const char *mac, char *vendor_out, char *type_out) {
    vendor_out[0] = '\0';
    type_out[0] = '\0';

    if (!mac || !*mac || strcmp(mac, "??") == 0) return;

    /* 标准化 MAC 地址为小写，取前 8 字符 (XX:XX:XX) */
    char prefix[16];
    int j = 0;
    for (int i = 0; mac[i] && j < 8; i++) {
        char c = mac[i];
        if (c == '-') c = ':';
        prefix[j++] = (c >= 'A' && c <= 'F') ? c + 32 : c;
    }
    prefix[j] = '\0';

    for (size_t i = 0; i < OUI_COUNT; i++) {
        if (strcmp(prefix, MAC_OUI[i].prefix) == 0) {
            strcpy(vendor_out, MAC_OUI[i].vendor);
            strcpy(type_out, MAC_OUI[i].type);
            return;
        }
    }
}

/* ═══════════════════════════════════════════════════════════ */
/* mDNS 发现 (使用 avahi-browse)                                 */
/* ═══════════════════════════════════════════════════════════ */

/* avahi-browse 输出解析辅助 */
typedef struct {
    char interface[32];
    char proto[8];
    char name[256];
    char type[128];
    char domain[64];
    char hostname[256];
    char address[MAX_STR];
    int  port;
    char txt[1024];
    bool is_resolved;
} avahi_record_t;

static int parse_avahi_line(const char *line, avahi_record_t *rec) {
    /* avahi-browse -t -r -p 输出格式:
     * = eth0 IPv4 xiaomi-gateway-hub1-4069 _miot-central._tcp local
     *   hostname = [xiaomi-gateway-hub1-4069.local]
     *   address = [192.168.6.119]
     *   port = [8883]
     *   txt data = [...]
     */
    memset(rec, 0, sizeof(*rec));
    rec->port = 0;
    rec->is_resolved = false;

    if (!line || !*line) return -1;

    /* 跳过注释 */
    if (line[0] == '#') return -1;

    /* 解析记录头行: = <iface> <proto> <name> <type> <domain> */
    if (line[0] == '=' || line[0] == '+') {
        char flag = line[0];
        char *p = (char *)line + 2; /* skip "= " */

        /* interface */
        rec->interface[0] = '\0';
        while (*p && *p != ' ' && *p != '\t') {
            strncat(rec->interface, p, 1);
            p++;
        }
        while (*p == ' ' || *p == '\t') p++;

        /* proto (IPv4/IPv6) */
        rec->proto[0] = '\0';
        while (*p && *p != ' ' && *p != '\t') {
            strncat(rec->proto, p, 1);
            p++;
        }
        while (*p == ' ' || *p == '\t') p++;

        /* name (可能含空格，到 type 为止) — 找到最后两个 token */
        /* 先收集所有剩余 token */
        char tokens[16][256];
        int ntokens = 0;
        while (*p && ntokens < 16) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            tokens[ntokens][0] = '\0';
            while (*p && *p != ' ' && *p != '\t') {
                strncat(tokens[ntokens], p, 1);
                p++;
            }
            ntokens++;
        }

        /* 最后三个 token: name..., type, domain */
        if (ntokens >= 3) {
            /* domain */
            strncpy(rec->domain, tokens[ntokens - 1], 63);
            /* type */
            strncpy(rec->type, tokens[ntokens - 2], MAX_MDNS_TYPE - 1);
            /* name = 剩余 token 拼接 */
            rec->name[0] = '\0';
            for (int i = 0; i < ntokens - 2; i++) {
                if (i > 0) strcat(rec->name, " ");
                strcat(rec->name, tokens[i]);
            }
        }

        if (flag == '=')
            rec->is_resolved = true;

        return 0;
    }

    /* 解析属性行 */
    if (line[0] == ' ' && line[1] == ' ') {
        char *p = (char *)line + 2;
        /* 跳过所有前导空格 */
        while (*p == ' ') p++;

        if (strncmp(p, "hostname", 8) == 0) {
            char *val = strstr(p, "= [");
            if (val) {
                val += 3;
                char *end = strchr(val, ']');
                if (end) {
                    int len = (int)(end - val);
                    if (len > 0) {
                        memcpy(rec->hostname, val, len);
                        rec->hostname[len] = '\0';
                        /* 去掉 .local */
                        char *dot = strstr(rec->hostname, ".local");
                        if (dot) *dot = '\0';
                    }
                }
            }
            return 1; /* 属性行 */
        }

        if (strncmp(p, "address", 7) == 0) {
            char *val = strstr(p, "= [");
            if (val) {
                val += 3;
                char *end = strchr(val, ']');
                if (end) {
                    int len = (int)(end - val);
                    if (len > 0 && len < MAX_STR) {
                        memcpy(rec->address, val, len);
                        rec->address[len] = '\0';
                    }
                }
            }
            return 1;
        }

        if (strncmp(p, "port", 4) == 0) {
            char *val = strstr(p, "= [");
            if (val) {
                val += 3;
                rec->port = atoi(val);
            }
            return 1;
        }

        if (strncmp(p, "txt", 3) == 0) {
            char *val = strstr(p, "= [");
            if (val) {
                val += 3;
                char *end = strrchr(val, ']');
                if (end) {
                    int len = (int)(end - val);
                    if (len > 0 && len < (int)sizeof(rec->txt)) {
                        memcpy(rec->txt, val, len);
                        rec->txt[len] = '\0';
                    }
                }
            }
            return 1;
        }
    }

    return -1;
}

/**
 * discover_mdns — 通过 avahi-browse 进行 mDNS 发现
 * 对齐 Python mdns.py: discover_mdns()
 *
 * 优化: 6 个服务类型用 fork 并行，总耗时 ≈ max(各服务) 而非 sum(各服务)
 */
void discover_mdns(int timeout, device_list_t *devices) {
    /* 检查 avahi-browse 是否可用 */
    if (system("which avahi-browse >/dev/null 2>&1") != 0) {
        printf("  %s\n", color_yellow("  mDNS 发现需要 avahi-browse (apt install avahi-utils)"));
        return;
    }

    /* 服务类型列表 (对齐 Python MDNS_SERVICE_TYPES) */
    const char *service_types[] = {
        "_miio._udp",
        "_miio._tcp",
        "_miot-central._tcp",
        "_yeelight._tcp",
        "_meshcop._udp",
        "_hap._tcp",
        NULL
    };

    /* 计算服务数量 */
    int svc_count = 0;
    while (service_types[svc_count]) svc_count++;

    /* 为每个服务创建临时文件路径 */
    char tmpfiles[svc_count][MAX_STR];
    pid_t pids[svc_count];
    for (int i = 0; i < svc_count; i++) {
        snprintf(tmpfiles[i], MAX_STR, "/tmp/mijia_mdns_%d_%d.txt", getpid(), i);
        pids[i] = 0;
    }

    /* fork 并行启动所有 avahi-browse 子进程 */
    for (int i = 0; i < svc_count; i++) {
        pids[i] = fork();
        if (pids[i] == 0) {
            /* 子进程: 运行 avahi-browse，结果写到临时文件，带超时 */
            FILE *out = fopen(tmpfiles[i], "w");
            if (!out) _exit(1);

            /* 用 pipe + fork + exec 实现带超时的 popen */
            int pipefd[2];
            if (pipe(pipefd) == 0) {
                pid_t child = fork();
                if (child == 0) {
                    /* 孙子进程: 执行 avahi-browse */
                    close(pipefd[0]);
                    dup2(pipefd[1], STDOUT_FILENO);
                    close(pipefd[1]);
                    /* 重定向 stderr 到 /dev/null，抑制 "Failed to resolve" 警告 */
                    FILE *devnull = fopen("/dev/null", "w");
                    if (devnull) { dup2(fileno(devnull), STDERR_FILENO); fclose(devnull); }
                    execlp("avahi-browse", "avahi-browse", "-r", service_types[i],
                           (char *)NULL);
                    _exit(127);
                }
                close(pipefd[1]);

                /* 子进程: 用 select 读管道，带累计超时 */
                fd_set rfds;
                struct timeval tv;
                char buf[2048];
                struct timespec t_start;
                clock_gettime(CLOCK_MONOTONIC, &t_start);
                double deadline = timeout + 1.0; /* 累计超时 */

                while (1) {
                    struct timespec t_now;
                    clock_gettime(CLOCK_MONOTONIC, &t_now);
                    double remaining = deadline - (t_now.tv_sec - t_start.tv_sec)
                                       - (t_now.tv_nsec - t_start.tv_nsec) / 1e9;
                    if (remaining <= 0) break;

                    FD_ZERO(&rfds);
                    FD_SET(pipefd[0], &rfds);
                    tv.tv_sec = (long)remaining;
                    tv.tv_usec = (long)((remaining - (long)remaining) * 1e6);
                    int sel = select(pipefd[0] + 1, &rfds, NULL, NULL, &tv);
                    if (sel <= 0) break; /* 超时或错误 */

                    ssize_t n = read(pipefd[0], buf, sizeof(buf) - 1);
                    if (n <= 0) break;
                    buf[n] = '\0';
                    fputs(buf, out);
                }

                close(pipefd[0]);
                kill(child, SIGKILL);
                waitpid(child, NULL, 0);
            } else {
                /* pipe 失败，回退到 popen */
                char cmd[MAX_STR * 2];
                snprintf(cmd, sizeof(cmd),
                         "timeout %d avahi-browse -r %s 2>/dev/null",
                         timeout + 1, service_types[i]);
                FILE *fp = popen(cmd, "r");
                if (fp) {
                    char buf[2048];
                    while (fgets(buf, sizeof(buf), fp))
                        fputs(buf, out);
                    pclose(fp);
                }
            }
            fclose(out);
            _exit(0);
        } else if (pids[i] < 0) {
            pids[i] = 0; /* fork 失败，跳过 */
        }
    }

    /* 父进程: 等待所有子进程完成（带总超时） */
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int total_timeout = timeout + 3; /* 总超时 */

    for (int i = 0; i < svc_count; i++) {
        if (pids[i] <= 0) continue;

        while (1) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = (now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1e9;
            if (elapsed >= total_timeout) {
                kill(pids[i], SIGKILL);
                break;
            }
            int status;
            pid_t ret = waitpid(pids[i], &status, WNOHANG);
            if (ret == pids[i]) break; /* 子进程结束 */
            usleep(50000); /* 50ms 轮询 */
        }
    }

    /* 解析所有临时文件 */
    char **seen_ips = NULL;
    int seen_ip_count = 0, seen_ip_cap = 0;
    char **seen_names = NULL;
    int seen_name_count = 0, seen_name_cap = 0;

    for (int si = 0; si < svc_count; si++) {
        const char *svc = service_types[si];
        bool is_hap = (strcmp(svc, "_hap._tcp") == 0);

        FILE *fp = fopen(tmpfiles[si], "r");
        if (!fp) continue;

        char line[2048];
        avahi_record_t current_rec;

        char acc_hostname[256] = {0};
        char acc_address[MAX_STR] = {0};
        int  acc_port = 0;
        char acc_txt[2048] = {0};
        bool has_record = false;

        while (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\n")] = '\0';

            int r = parse_avahi_line(line, &current_rec);

            if (r == 0) {
                /* 新记录头 — 先保存上一条 */
                if (has_record && *acc_address) {
                    /* 过滤 IPv6 地址 */
                    if (strchr(acc_address, ':') != NULL) {
                        memset(acc_hostname, 0, sizeof(acc_hostname));
                        memset(acc_address, 0, sizeof(acc_address));
                        acc_port = 0;
                        memset(acc_txt, 0, sizeof(acc_txt));
                        has_record = true;
                        goto next_record2;
                    }
                    /* 去重 */
                    if (is_hap) {
                        bool dup = false;
                        for (int j = 0; j < seen_name_count; j++) {
                            if (strcmp(seen_names[j], current_rec.name) == 0) { dup = true; break; }
                        }
                        if (dup) { has_record = false; continue; }
                        if (seen_name_count >= seen_name_cap) {
                            seen_name_cap = seen_name_cap == 0 ? 32 : seen_name_cap * 2;
                            seen_names = realloc(seen_names, seen_name_cap * sizeof(char*));
                        }
                        seen_names[seen_name_count++] = strdup(current_rec.name);
                    } else {
                        bool dup = false;
                        for (int j = 0; j < seen_ip_count; j++) {
                            if (strcmp(seen_ips[j], acc_address) == 0) { dup = true; break; }
                        }
                        if (dup) { has_record = false; continue; }
                        if (seen_ip_count >= seen_ip_cap) {
                            seen_ip_cap = seen_ip_cap == 0 ? 32 : seen_ip_cap * 2;
                            seen_ips = realloc(seen_ips, seen_ip_cap * sizeof(char*));
                        }
                        seen_ips[seen_ip_count++] = strdup(acc_address);
                    }

                    /* 提取 model */
                    char model[MAX_MODEL];
                    strncpy(model, current_rec.name, MAX_MODEL - 1);
                    model[MAX_MODEL - 1] = '\0';

                    char *dot_und = strstr(model, "._");
                    if (dot_und) *dot_und = '\0';

                    int mlen = strlen(model);
                    if (mlen > 2 && model[mlen-1] >= '0' && model[mlen-1] <= '9') {
                        int k = mlen - 1;
                        while (k > 0 && model[k] >= '0' && model[k] <= '9') k--;
                        if (k > 0 && model[k] == '-') model[k] = '\0';
                    }

                    char dev_name[MAX_DEV_NAME], dev_type[64];
                    device_db_lookup(model, dev_name, dev_type);

                    device_t *d = device_list_add(devices);
                    strncpy(d->ip, acc_address, MAX_STR - 1);
                    d->port = acc_port;
                    strncpy(d->mdns_name, current_rec.name, MAX_MDNS_NAME - 1);
                    snprintf(d->mdns_type, MAX_MDNS_TYPE, "%s.local.", svc);
                    strncpy(d->mdns_server, acc_hostname, MAX_MDNS_SERVER - 1);
                    strncpy(d->mdns_props, acc_txt, 1023);

                    if (is_hap) {
                        /* HomeKit 设备 */
                        char hap_name[MAX_DEV_NAME] = {0};
                        int hap_ci = 1;
                        char hap_id[64] = {0};

                        char *p = acc_txt;
                        while (*p) {
                            if (strncmp(p, "\"md=", 4) == 0) {
                                p += 4;
                                const char *end = strchr(p, '"');
                                if (end) {
                                    int vl = (int)(end - p);
                                    if (vl > 0) { memcpy(hap_name, p, vl); hap_name[vl] = '\0'; }
                                }
                            } else if (strncmp(p, "\"ci=", 4) == 0) {
                                p += 4;
                                hap_ci = atoi(p);
                            } else if (strncmp(p, "\"id=", 4) == 0) {
                                p += 4;
                                const char *end = strchr(p, '"');
                                if (end) {
                                    int vl = (int)(end - p);
                                    if (vl > 0) { memcpy(hap_id, p, vl); hap_id[vl] = '\0'; }
                                }
                            }
                            p++;
                        }

                        const char *hap_cat = "Other";
                        switch (hap_ci) {
                            case 1: hap_cat = "Accessory"; break;
                            case 2: hap_cat = "Bridge"; break;
                            case 3: hap_cat = "Fan"; break;
                            case 5: hap_cat = "Lightbulb"; break;
                            case 6: hap_cat = "Door Lock"; break;
                            case 7: hap_cat = "Outlet"; break;
                            case 8: hap_cat = "Switch"; break;
                            case 9: hap_cat = "Thermostat"; break;
                            case 10: hap_cat = "Sensor"; break;
                            case 17: hap_cat = "Video Doorbell"; break;
                            case 22: hap_cat = "Camera"; break;
                            case 24: hap_cat = "Air Purifier"; break;
                            case 28: hap_cat = "Heater"; break;
                            case 32: hap_cat = "Window Covering"; break;
                            case 45: hap_cat = "Valve"; break;
                            case 48: hap_cat = "Humidifier"; break;
                            default: {
                                static char buf[32];
                                snprintf(buf, sizeof(buf), "Category-%d", hap_ci);
                                hap_cat = buf;
                            }
                        }

                        if (hap_name[0] == '\0') {
                            has_record = false;
                            continue;
                        }

                        snprintf(d->name, MAX_DEV_NAME, "HAP: %s (%s)", hap_name, hap_cat);
                        strncpy(d->model, hap_id, MAX_MODEL - 1);
                        strncpy(d->type, hap_cat, 63);
                        strncpy(d->protocol, "HomeKit", 31);
                    } else {
                        if (strcmp(dev_name, "未知设备") != 0)
                            strncpy(d->name, dev_name, MAX_DEV_NAME - 1);
                        else
                            strncpy(d->name, acc_hostname, MAX_DEV_NAME - 1);
                        strncpy(d->model, model, MAX_MODEL - 1);
                        strncpy(d->type, dev_type, 63);
                        strncpy(d->protocol, "mDNS", 31);
                    }

                    time_t now_t = time(NULL);
                    struct tm *tm_info = localtime(&now_t);
                    strftime(d->last_seen, 32, "%Y-%m-%d %H:%M:%S", tm_info);
                }

                /* 开始新记录 */
                memset(acc_hostname, 0, sizeof(acc_hostname));
                memset(acc_address, 0, sizeof(acc_address));
                acc_port = 0;
                memset(acc_txt, 0, sizeof(acc_txt));
                has_record = true;
                next_record2: ;
            } else if (r == 1) {
                /* 属性行，累计到当前记录 */
                if (current_rec.hostname[0]) strncpy(acc_hostname, current_rec.hostname, 255);
                if (current_rec.address[0]) strncpy(acc_address, current_rec.address, MAX_STR - 1);
                if (current_rec.port > 0) acc_port = current_rec.port;
                if (current_rec.txt[0]) {
                    if (acc_txt[0]) strncat(acc_txt, " ", sizeof(acc_txt) - strlen(acc_txt) - 1);
                    strncat(acc_txt, current_rec.txt, sizeof(acc_txt) - strlen(acc_txt) - 1);
                }
            }
        }

        /* 保存最后一条记录 */
        if (has_record && *acc_address && !strchr(acc_address, ':')) {
            char model[MAX_MODEL];
            strncpy(model, current_rec.name, MAX_MODEL - 1);
            model[MAX_MODEL - 1] = '\0';
            char *dot_und = strstr(model, "._");
            if (dot_und) *dot_und = '\0';

            char dev_name[MAX_DEV_NAME], dev_type[64];
            device_db_lookup(model, dev_name, dev_type);

            device_t *d = device_list_add(devices);
            strncpy(d->ip, acc_address, MAX_STR - 1);
            d->port = acc_port;
            if (strcmp(dev_name, "未知设备") != 0)
                strncpy(d->name, dev_name, MAX_DEV_NAME - 1);
            else
                strncpy(d->name, acc_hostname, MAX_DEV_NAME - 1);
            strncpy(d->model, model, MAX_MODEL - 1);
            strncpy(d->type, dev_type, 63);
            strncpy(d->protocol, "mDNS", 31);
            snprintf(d->mdns_type, MAX_MDNS_TYPE, "%s.local.", svc);
            strncpy(d->mdns_name, current_rec.name, MAX_MDNS_NAME - 1);
            strncpy(d->mdns_server, acc_hostname, MAX_MDNS_SERVER - 1);
            strncpy(d->mdns_props, acc_txt, 1023);
            time_t now_t = time(NULL);
            struct tm *tm_info = localtime(&now_t);
            strftime(d->last_seen, 32, "%Y-%m-%d %H:%M:%S", tm_info);
        }

        fclose(fp);
    }

    /* 清理临时文件 */
    for (int i = 0; i < svc_count; i++)
        unlink(tmpfiles[i]);

    /* 释放去重表 */
    for (int i = 0; i < seen_ip_count; i++) free(seen_ips[i]);
    free(seen_ips);
    for (int i = 0; i < seen_name_count; i++) free(seen_names[i]);
    free(seen_names);
}

/* ═══════════════════════════════════════════════════════════ */
/* ARP map 辅助（供 mdns 内部使用）                              */
/* ═══════════════════════════════════════════════════════════ */

typedef struct { char ip[MAX_STR]; char mac[24]; } arp_entry_t;

static int build_arp_map(arp_entry_t **out) {
    int count = 0, cap = 0;
    *out = NULL;

    FILE *fp = fopen("/proc/net/arp", "r");
    if (!fp) return 0;

    char line[512];
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return 0; }

    while (fgets(line, sizeof(line), fp)) {
        char ip[64], mac[64], flags[16];
        if (sscanf(line, "%s %*s %s %s", ip, flags, mac) < 3) continue;
        if (strcmp(mac, "00:00:00:00:00:00") == 0) continue;
        if (strcmp(flags, "0x2") != 0 && strcmp(flags, "0x6") != 0) continue;

        if (count >= cap) {
            cap = cap == 0 ? 64 : cap * 2;
            *out = realloc(*out, cap * sizeof(arp_entry_t));
        }
        strncpy((*out)[count].ip, ip, MAX_STR - 1);
        strncpy((*out)[count].mac, mac, 23);
        count++;
    }

    fclose(fp);
    return count;
}

static const char *find_mac_in_map(arp_entry_t *map, int count, const char *ip) {
    for (int i = 0; i < count; i++) {
        if (strcmp(map[i].ip, ip) == 0) return map[i].mac;
    }
    return NULL;
}

/**
 * discover_all — 三重发现 (ARP + miIO + mDNS)
 *
 * 流程:
 *   1. ARP 扫描获取在线设备
 *   2. fork 子进程: 通过 pipe 传递 mDNS 结果 (二进制 device_t)
 *   3. 父进程同时执行: miIO 广播 → unicast 探测
 *   4. 从管道读取 mDNS 结果
 *   5. 合并去重 + ARP 补充
 */
void discover_all(int timeout, device_list_t *devices) {
    /* 阶段0: ARP 扫描 (瞬时，读系统缓存) */
    arp_entry_t *arp_map = NULL;
    int arp_count = build_arp_map(&arp_map);
    if (arp_count > 0)
        printf("  %s\n", color_dim_fmt("  [ARP] 系统缓存中发现 %d 台在线设备", arp_count));

    /* 创建管道，用于 mDNS 子进程传回设备数据 */
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("pipe");
        /* 退化: 串行执行 unicast + mDNS */
        device_list_t all_devs;
        device_list_init(&all_devs);
        if (arp_count > 0) {
            char **alive_ips = malloc(arp_count * sizeof(char*));
            for (int i = 0; i < arp_count; i++) alive_ips[i] = arp_map[i].ip;
            discover_devices_from_alive(alive_ips, arp_count, timeout, &all_devs);
            free(alive_ips);
        }
        discover_mdns(5, &all_devs);
        for (int i = 0; i < all_devs.count; i++) {
            device_t *nd = device_list_add(devices);
            *nd = all_devs.items[i];
        }
        device_list_free(&all_devs);
        free(arp_map);
        return;
    }

    /* fork 子进程: mDNS 发现 */
    fflush(stdout);
    fflush(stderr);
    pid_t pid_mdns = fork();
    if (pid_mdns == 0) {
        /* === 子进程 === */
        close(pipefd[0]);

        device_list_t mdns_devs;
        device_list_init(&mdns_devs);
        discover_mdns(5, &mdns_devs);  /* mDNS 固定 5 秒超时 */

        int count = mdns_devs.count;
        {
            ssize_t _wr __attribute__((unused)) = write(pipefd[1], &count, sizeof(int));
        }
        for (int i = 0; i < count; i++) {
            ssize_t _wr __attribute__((unused)) = write(pipefd[1], &mdns_devs.items[i], sizeof(device_t));
        }

        device_list_free(&mdns_devs);
        close(pipefd[1]);
        _exit(0);
    }

    /* === 父进程 === */
    close(pipefd[1]);

    /* 父进程: 对 ARP 存活 IP 并行 unicast 探测 */
    device_list_t miio_devices;
    device_list_init(&miio_devices);
    if (arp_count > 0) {
        char **alive_ips = malloc(arp_count * sizeof(char*));
        for (int i = 0; i < arp_count; i++)
            alive_ips[i] = arp_map[i].ip;
        discover_devices_from_alive(alive_ips, arp_count, timeout, &miio_devices);
        free(alive_ips);
    }
    printf("  %s\n", color_dim_fmt("  [miIO] 发现 %d 台设备", miio_devices.count));

    /* 从管道读取 mDNS 结果 (子进程写完关闭管道后 read 返回 EOF) */
    device_list_t mdns_devices;
    device_list_init(&mdns_devices);
    {
        int mdns_count = 0;
        ssize_t n = read(pipefd[0], &mdns_count, sizeof(int));
        if (n == sizeof(int) && mdns_count > 0) {
            for (int i = 0; i < mdns_count; i++) {
                device_t d;
                memset(&d, 0, sizeof(d));
                ssize_t dn = read(pipefd[0], &d, sizeof(device_t));
                if (dn == sizeof(device_t)) {
                    device_t *nd = device_list_add(&mdns_devices);
                    *nd = d;
                } else break;
            }
        }
    }
    close(pipefd[0]);

    /* 等待 mDNS 子进程退出 */
    int mdns_status;
    waitpid(pid_mdns, &mdns_status, 0);
    printf("  %s\n", color_dim_fmt("  [mDNS] 发现 %d 台设备", mdns_devices.count));

    /* 合并去重: miIO + mDNS */
    char **seen_keys = NULL;
    int seen_count = 0, seen_cap = 0;

    /* 添加 miIO 设备 */
    for (int i = 0; i < miio_devices.count; i++) {
        device_t *d = &miio_devices.items[i];
        char key[MAX_STR + MAX_MODEL + 2];
        snprintf(key, sizeof(key), "%s:%s", d->ip, d->model);

        bool dup = false;
        for (int j = 0; j < seen_count; j++) {
            if (strcmp(seen_keys[j], key) == 0) { dup = true; break; }
        }
        if (dup) continue;

        if (seen_count >= seen_cap) {
            seen_cap = seen_cap == 0 ? 64 : seen_cap * 2;
            seen_keys = realloc(seen_keys, seen_cap * sizeof(char*));
        }
        seen_keys[seen_count++] = strdup(key);

        const char *mac = find_mac_in_map(arp_map, arp_count, d->ip);
        if (mac && !d->mac[0]) {
            strncpy(d->mac, mac, 23);
            char vendor[64] = {0}, vtype[64] = {0};
            lookup_mac_vendor(mac, vendor, vtype);
            strncpy(d->vendor, vendor, 63);
            if (!*d->type || strcmp(d->type, "未知") == 0)
                strncpy(d->type, vtype, 63);
        }

        device_t *nd = device_list_add(devices);
        *nd = *d;
    }

    /* 添加 mDNS 设备 */
    for (int i = 0; i < mdns_devices.count; i++) {
        device_t *d = &mdns_devices.items[i];
        char key[MAX_STR + MAX_MODEL + 8];
        if (strcmp(d->protocol, "HomeKit") == 0)
            snprintf(key, sizeof(key), "hap:%s", d->model);
        else
            snprintf(key, sizeof(key), "%s:%s", d->ip, d->model);

        bool dup = false;
        for (int j = 0; j < seen_count; j++) {
            if (strcmp(seen_keys[j], key) == 0) { dup = true; break; }
        }
        if (dup) continue;

        if (seen_count >= seen_cap) {
            seen_cap = seen_cap == 0 ? 64 : seen_cap * 2;
            seen_keys = realloc(seen_keys, seen_cap * sizeof(char*));
        }
        seen_keys[seen_count++] = strdup(key);

        const char *mac = find_mac_in_map(arp_map, arp_count, d->ip);
        if (mac) {
            if (!d->mac[0]) strncpy(d->mac, mac, 23);
            char vendor[64] = {0}, vtype[64] = {0};
            lookup_mac_vendor(mac, vendor, vtype);
            if (!d->vendor[0]) strncpy(d->vendor, vendor, 63);
            if (!*d->type || strcmp(d->type, "未知") == 0)
                strncpy(d->type, vtype, 63);
        }

        device_t *nd = device_list_add(devices);
        *nd = *d;
    }

    /* ARP 补充发现 */
    int discovered_count = devices->count;
    for (int i = 0; i < arp_count; i++) {
        const char *ip = arp_map[i].ip;
        const char *mac = arp_map[i].mac;

        bool found = false;
        for (int j = 0; j < discovered_count; j++) {
            if (strcmp(devices->items[j].ip, ip) == 0) { found = true; break; }
        }
        if (found) continue;

        char vendor[64] = {0}, dtype[64] = {0};
        lookup_mac_vendor(mac, vendor, dtype);
        if (!*vendor) continue;

        device_t *d = device_list_add(devices);
        strncpy(d->ip, ip, MAX_STR - 1);
        strncpy(d->mac, mac, 23);
        strncpy(d->vendor, vendor, 63);
        strncpy(d->type, dtype, 63);
        strncpy(d->name, vendor, MAX_DEV_NAME - 1);
        strncpy(d->protocol, "ARP", 31);
    }

    if (devices->count > discovered_count)
        printf("  %s\n", color_dim_fmt("  [ARP] 补充发现 %d 台设备", devices->count - discovered_count));

    printf("  %s\n", color_dim_fmt("  去重后共 %d 台设备", devices->count));

    /* 清理 */
    device_list_free(&miio_devices);
    device_list_free(&mdns_devices);
    for (int i = 0; i < seen_count; i++) free(seen_keys[i]);
    free(seen_keys);
    free(arp_map);
}

/* color_dim_fmt 已在 common.h 中定义为 static inline */
