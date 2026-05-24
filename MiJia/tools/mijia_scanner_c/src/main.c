/**
 * main.c — 米家设备网络探测器（主程序入口）
 *
 * 对齐 Python 版本: mijia_scanner.py
 * 子命令: scan / deep / monitor / info / models / export
 *
 * 编译: make
 * 用法: ./mijia_scanner scan [--timeout N] [--range CIDR] [--mdns-only] [--json] [--csv]
 *       ./mijia_scanner deep --token xxx [--timeout N]
 *       ./mijia_scanner monitor [--interval N]
 *       ./mijia_scanner info <ip> [--token xxx]
 *       ./mijia_scanner models
 *       ./mijia_scanner export --format json|csv [-o file]
 */

#include "common.h"
#include <getopt.h>
#include <stdarg.h>
#include <unistd.h>

/* ═══════════════════════════════════════════════════════════ */
/* 格式化着色辅助                                                */
/* ═══════════════════════════════════════════════════════════ */

static char _bold_fmt_buf[MAX_STR * 2];
static const char *color_bold_fmt(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char tmp[MAX_STR];
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (!g_no_color && isatty(STDOUT_FILENO))
        snprintf(_bold_fmt_buf, sizeof(_bold_fmt_buf), "\033[1m%s\033[0m", tmp);
    else
        snprintf(_bold_fmt_buf, sizeof(_bold_fmt_buf), "%s", tmp);
    return _bold_fmt_buf;
}

/* ═══════════════════════════════════════════════════════════ */
/* config.ini 读取（对齐 Python: 从配置文件读取 HA token）        */
/* ═══════════════════════════════════════════════════════════ */

static void read_config_ini(const char *dir, char *ha_url_out, char *ha_token_out) {
    ha_url_out[0] = '\0';
    ha_token_out[0] = '\0';

    char path[MAX_STR];
    snprintf(path, sizeof(path), "%s/config.ini", dir);

    FILE *fp = fopen(path, "r");
    if (!fp) return;

    char line[MAX_STR];
    while (fgets(line, sizeof(line), fp)) {
        /* 去换行 */
        line[strcspn(line, "\n")] = '\0';
        /* 跳过注释和空行 */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') continue;

        char *eq = strchr(p, '=');
        if (!eq) continue;

        *eq = '\0';
        char *key = p;
        char *val = eq + 1;
        while (*key == ' ') key++;
        int klen = strlen(key);
        while (klen > 0 && key[klen-1] == ' ') key[--klen] = '\0';
        while (*val == ' ') val++;

        if (strcmp(key, "HA_URL") == 0)
            strncpy(ha_url_out, val, MAX_STR - 1);
        else if (strcmp(key, "HA_TOKEN") == 0)
            strncpy(ha_token_out, val, MAX_STR - 1);
    }
    fclose(fp);
}

/* ═══════════════════════════════════════════════════════════ */
/* 获取可执行文件所在目录                                        */
/* ═══════════════════════════════════════════════════════════ */

static void get_exe_dir(char *dir_out, int max_len) {
    dir_out[0] = '\0';
    char link[MAX_STR];
    ssize_t len = readlink("/proc/self/exe", link, sizeof(link) - 1);
    if (len > 0) {
        link[len] = '\0';
        char *last_slash = strrchr(link, '/');
        if (last_slash) {
            *last_slash = '\0';
            strncpy(dir_out, link, max_len - 1);
        }
    }
}

/* ═══════════════════════════════════════════════════════════ */
/* 子命令: scan                                                 */
/* ═══════════════════════════════════════════════════════════ */

static void cmd_scan(int argc, char **argv) {
    int timeout = 5;
    int mdns_only = 0;
    int do_json = 0;
    int do_csv = 0;
    char range_spec[MAX_STR] = {0};
    char ha_url[MAX_STR] = {0};
    char ha_token[MAX_STR] = {0};

    static struct option long_opts[] = {
        {"timeout",   required_argument, 0, 't'},
        {"range",     required_argument, 0, 'r'},
        {"mdns-only", no_argument,       0, 'm'},
        {"json",      no_argument,       0, 'j'},
        {"csv",       no_argument,       0, 'c'},
        {"ha-url",    required_argument, 0, 'u'},
        {"ha-token",  required_argument, 0, 'k'},
        {0, 0, 0, 0}
    };

    optind = 0;
    int opt;
    while ((opt = getopt_long(argc, argv, "t:r:mjcu:k:", long_opts, NULL)) != -1) {
        switch (opt) {
            case 't': timeout = atoi(optarg); break;
            case 'r': strncpy(range_spec, optarg, MAX_STR - 1); break;
            case 'm': mdns_only = 1; break;
            case 'j': do_json = 1; break;
            case 'c': do_csv = 1; break;
            case 'u': strncpy(ha_url, optarg, MAX_STR - 1); break;
            case 'k': strncpy(ha_token, optarg, MAX_STR - 1); break;
        }
    }

    device_list_t devices;
    device_list_init(&devices);

    if (range_spec[0]) {
        /* 跨网段扫描 */
        printf("  %s\n", color_bold("米家设备跨网段扫描"));
        printf("  %s\n", color_dim_fmt("  范围: %s", 0));
        printf("  %s\n", color_dim("  ──────────────────────────────────────────────────────"));

        /* 解析 IP 范围 */
        char **ips = malloc(2048 * sizeof(char*));
        int ip_count = parse_ip_ranges(range_spec, ips, 2048);
        if (ip_count == 0) {
            printf("  %s\n", color_red("  未解析到有效 IP 地址"));
            free(ips);
            return;
        }

        printf("  %s\n", color_dim_fmt("  范围: %s  (%d 个 IP)", ip_count));
        printf("  %s\n", color_dim("  ──────────────────────────────────────────────────────"));

        /* fping 筛活 */
        printf("  %s\n", color_dim("  [1/3] ping 预筛存活 IP..."));
        char **alive = malloc(ip_count * sizeof(char*));
        int alive_count = 0;
        ping_sweep(ips, ip_count, alive, &alive_count);
        printf("  %s\n", color_dim_fmt("        存活 %d/%d 台", alive_count, ip_count));

        if (alive_count == 0) {
            printf("  %s\n", color_yellow("  无存活 IP"));
            for (int i = 0; i < ip_count; i++) free(ips[i]);
            free(ips);
            return;
        }

        /* miIO + mDNS 扫描 */
        printf("  %s\n", color_dim("  [2/3] miIO + mDNS 扫描中..."));

        /* miIO unicast 探测 */
        discover_devices_from_alive(alive, alive_count, timeout, &devices);
        printf("  %s\n", color_dim_fmt("  [miIO] 发现 %d 台设备", devices.count));

        /* mDNS 发现 */
        int mdns_start = devices.count;
        discover_mdns(5, &devices);
        printf("  %s\n", color_dim_fmt("  [mDNS] 发现 %d 台设备", devices.count - mdns_start));

        /* ARP 补充 */
        device_list_t arp_devs;
        device_list_init(&arp_devs);
        arp_scan(&arp_devs);
        int discovered_before = devices.count;
        for (int i = 0; i < arp_devs.count; i++) {
            bool found = false;
            for (int j = 0; j < discovered_before; j++) {
                if (strcmp(devices.items[j].ip, arp_devs.items[i].ip) == 0) { found = true; break; }
            }
            if (!found) {
                device_t *d = device_list_add(&devices);
                *d = arp_devs.items[i];
            }
        }
        device_list_free(&arp_devs);

        /* MAC/厂商补充 */
        for (int i = 0; i < devices.count; i++) {
            if (!devices.items[i].mac[0]) {
                const char *mac = arp_lookup(devices.items[i].ip);
                if (mac) {
                    strncpy(devices.items[i].mac, mac, 23);
                    char vendor[64] = {0}, vtype[64] = {0};
                    lookup_mac_vendor(mac, vendor, vtype);
                    strncpy(devices.items[i].vendor, vendor, 63);
                    if (!*devices.items[i].type || strcmp(devices.items[i].type, "未知") == 0)
                        strncpy(devices.items[i].type, vtype, 63);
                }
            }
        }

        printf("  %s\n", color_dim_fmt("        去重后共 %d 台设备\n", devices.count));

        /* 清理 */
        for (int i = 0; i < ip_count; i++) free(ips[i]);
        free(ips);
        for (int i = 0; i < alive_count; i++) free(alive[i]);
        free(alive);
    } else if (mdns_only) {
        printf("  %s\n", color_bold_fmt("米家设备 mDNS 扫描 (超时: %ds)", timeout));
        printf("  %s\n", color_dim("  ──────────────────────────────────────────────────────"));
        discover_mdns(timeout, &devices);
    } else {
        printf("  %s\n", color_bold_fmt("米家设备扫描 (超时: %ds)", timeout));
        printf("  %s\n", color_dim("  ──────────────────────────────────────────────────────"));
        discover_all(timeout, &devices);
    }

    if (do_json) { export_json(&devices, NULL); device_list_free(&devices); return; }
    if (do_csv)  { export_csv(&devices, NULL);  device_list_free(&devices); return; }

    /* HA 区域补充 */
    if (!ha_token[0]) {
        char exe_dir[MAX_STR];
        get_exe_dir(exe_dir, sizeof(exe_dir));
        if (!*exe_dir) strncpy(exe_dir, ".", MAX_STR - 1);
        char cfg_url[MAX_STR], cfg_token[MAX_STR];
        read_config_ini(exe_dir, cfg_url, cfg_token);
        if (!ha_url[0] && cfg_url[0]) strncpy(ha_url, cfg_url, MAX_STR - 1);
        if (!ha_token[0] && cfg_token[0]) strncpy(ha_token, cfg_token, MAX_STR - 1);
    }

    if (ha_token[0]) {
        if (!ha_url[0]) strncpy(ha_url, "http://192.168.6.127:8123", MAX_STR - 1);

        printf("\n");
        device_list_t ha_devs;
        device_list_init(&ha_devs);
        ha_get_all_devices(ha_url, ha_token, &ha_devs);

        if (ha_devs.count > 0) {
            printf("\n  %s\n", color_bold("Home Assistant 设备列表（含区域）"));
            printf("  %s\n", color_dim("  ─────────────────────────────────────────────────────────────"));

            /* 按房间分组 */
            const char *room_order[] = {
                "客厅", "书房", "厨房", "主卧", "次卧", "卧室", "玄关", "入户玄关", "入户",
                "休闲阳台", "生活阳台", "阳台", "卫生间", "走廊", NULL
            };

            /* 收集所有房间 */
            char rooms[64][32];
            int room_count = 0;
            for (int i = 0; i < ha_devs.count; i++) {
                bool found = false;
                for (int j = 0; j < room_count; j++) {
                    if (strcmp(rooms[j], ha_devs.items[i].room) == 0) { found = true; break; }
                }
                if (!found && room_count < 64)
                    strncpy(rooms[room_count++], ha_devs.items[i].room, 31);
            }

            /* 按预定义顺序排序 */
            for (int i = 0; i < room_count - 1; i++) {
                int pi = -1;
                for (int k = 0; room_order[k]; k++) {
                    if (strcmp(rooms[i], room_order[k]) == 0) { pi = k; break; }
                }
                int pj = -1;
                for (int k = 0; room_order[k]; k++) {
                    if (strcmp(rooms[i+1], room_order[k]) == 0) { pj = k; break; }
                }
                if (pi == -1) pi = 99;
                if (pj == -1) pj = 99;
                if (pi > pj) {
                    char tmp[32];
                    strncpy(tmp, rooms[i], 31); rooms[i][31] = '\0';
                    strncpy(rooms[i], rooms[i+1], 31); rooms[i][31] = '\0';
                    strncpy(rooms[i+1], tmp, 31); rooms[i+1][31] = '\0';
                }
            }

            for (int r = 0; r < room_count; r++) {
                int devs_in_room = 0;
                for (int i = 0; i < ha_devs.count; i++)
                    if (strcmp(ha_devs.items[i].room, rooms[r]) == 0) devs_in_room++;
                if (devs_in_room == 0) continue;

                printf("\n  %s\n", color_bold_fmt("    [%s] (%d 个设备)", rooms[r], devs_in_room));
                for (int i = 0; i < ha_devs.count; i++) {
                    if (strcmp(ha_devs.items[i].room, rooms[r]) != 0) continue;
                    printf("      %s %s\n", color_cpad("36", ha_devs.items[i].type, 8), ha_devs.items[i].name);
                }
            }
        }
        device_list_free(&ha_devs);
    }

    printf("\n");
    print_device_table(&devices, true);

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timebuf[32];
    strftime(timebuf, 32, "%Y-%m-%d %H:%M:%S", tm_info);
    printf("\n  共发现 %d 台设备  [%s]\n", devices.count, timebuf);

    device_list_free(&devices);
}

/* ═══════════════════════════════════════════════════════════ */
/* 子命令: models                                               */
/* ═══════════════════════════════════════════════════════════ */

static void cmd_models(int argc, char **argv) {
    (void)argc; (void)argv;
    device_db_print_all();
}

/* ═══════════════════════════════════════════════════════════ */
/* 子命令: info                                                 */
/* ═══════════════════════════════════════════════════════════ */

static void cmd_info(int argc, char **argv) {
    if (argc < 3) {
        printf("  用法: mijia_scanner info <ip> [--token xxx] [--timeout N]\n");
        return;
    }

    const char *ip = argv[2];
    char token[TOKEN_HEX_LEN + 1] = {0};
    int timeout = 10;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--token") == 0 && i + 1 < argc)
            strncpy(token, argv[++i], TOKEN_HEX_LEN);
        else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc)
            timeout = atoi(argv[++i]);
    }

    printf("  %s\n", color_bold_fmt("  设备信息: %s", ip));
    printf("  %s\n", color_dim("  ──────────────────────────────────────────────────────"));

    if (!token[0]) {
        printf("  %s\n", color_yellow("  未提供 token，尝试 Hello 握手..."));

        uint8_t hello[32];
        build_hello_packet(hello);

        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        struct timeval tv = { .tv_sec = timeout, .tv_usec = 0 };
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(MIIO_PORT);
        inet_pton(AF_INET, ip, &addr.sin_addr);

        sendto(sock, hello, 32, 0, (struct sockaddr *)&addr, sizeof(addr));

        uint8_t buf[4096];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
        close(sock);

        if (n >= 32) {
            uint32_t devid = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) |
                             ((uint32_t)buf[6] << 8)  | (uint32_t)buf[7];
            uint32_t ts = ((uint32_t)buf[8] << 24) | ((uint32_t)buf[9] << 16) |
                          ((uint32_t)buf[10] << 8) | (uint32_t)buf[11];

            printf("  Device ID: %u\n", devid);
            printf("  Timestamp: %u\n", ts);

            if (n >= 44) {
                bool has_nz = false;
                for (int i = 28; i < 44; i++) if (buf[i]) { has_nz = true; break; }
                if (has_nz) {
                    char tok[33];
                    for (int i = 0; i < 16; i++) sprintf(tok + i*2, "%02x", buf[28+i]);
                    tok[32] = '\0';
                    printf("  Token:     %s\n", color_yellow(tok));
                    printf("  %s\n", color_yellow("  发现明文 token，可用于深度查询"));
                }
            }
        } else if (n < 0) {
            printf("  %s\n", color_red("  Hello 握手超时"));
        }
    } else {
        /* 深度查询需要 AES 加解密 — 暂未实现，需要 OpenSSL */
        printf("  %s\n", color_dim("  Token: <hidden>..."));
        printf("  %s\n", color_red("  深度查询需要 AES 加解密支持（待实现，需要 libssl/libcrypto）"));
    }
}

/* ═══════════════════════════════════════════════════════════ */
/* 子命令: export                                               */
/* ═══════════════════════════════════════════════════════════ */

static void cmd_export(int argc, char **argv) {
    char fmt[16] = "json";
    char output[MAX_STR] = {0};
    int timeout = 5;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--format") == 0 && i + 1 < argc)
            strncpy(fmt, argv[++i], 15);
        else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i + 1 < argc)
            strncpy(output, argv[++i], MAX_STR - 1);
        else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc)
            timeout = atoi(argv[++i]);
    }

    printf("  %s\n", color_dim("  正在扫描设备..."));

    device_list_t devices;
    device_list_init(&devices);
    discover_all(timeout, &devices);

    if (devices.count == 0) {
        printf("  %s\n", color_yellow("  未发现设备，无内容可导出"));
        return;
    }

    if (strcmp(fmt, "json") == 0)
        export_json(&devices, output[0] ? output : NULL);
    else if (strcmp(fmt, "csv") == 0)
        export_csv(&devices, output[0] ? output : NULL);
    else
        printf("  %s: %s\n", color_red("  不支持的格式"), fmt);

    device_list_free(&devices);
}

/* ═══════════════════════════════════════════════════════════ */
/* 子命令: ha (Home Assistant 实体控制)                         */
/* ═══════════════════════════════════════════════════════════ */

static void cmd_ha(int argc, char **argv) {
    if (argc < 3) {
        printf("  用法: mijia_scanner ha <on|off|toggle|status|list> [entity_id] [选项]\n");
        printf("\n");
        printf("  命令:\n");
        printf("    status <entity_id>     查询实体状态\n");
        printf("    on     <entity_id>     打开实体 (switch/light/fan/cover)\n");
        printf("    off    <entity_id>     关闭实体\n");
        printf("    toggle <entity_id>     切换实体状态\n");
        printf("    list [--domain xxx]    列出 HA 设备 (按房间分组)\n");
        printf("\n");
        printf("  示例:\n");
        printf("    mijia_scanner ha status switch.study_plug\n");
        printf("    mijia_scanner ha on light.study_desk\n");
        printf("    mijia_scanner ha list\n");
        return;
    }

    const char *subcmd = argv[2];

    /* 读取配置 */
    char exe_dir[MAX_STR];
    get_exe_dir(exe_dir, sizeof(exe_dir));
    if (!*exe_dir) strncpy(exe_dir, ".", MAX_STR - 1);

    char ha_url[MAX_STR] = {0}, ha_token[MAX_STR] = {0};
    read_config_ini(exe_dir, ha_url, ha_token);

    /* 也接受命令行参数覆盖 */
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--ha-url") == 0 && i + 1 < argc)
            strncpy(ha_url, argv[++i], MAX_STR - 1);
        else if (strcmp(argv[i], "--ha-token") == 0 && i + 1 < argc)
            strncpy(ha_token, argv[++i], MAX_STR - 1);
    }

    if (!ha_url[0]) strncpy(ha_url, "http://192.168.6.127:8123", MAX_STR - 1);
    if (!ha_token[0]) {
        printf("  %s\n", color_yellow("  未配置 HA_TOKEN，请在 config.ini 中设置"));
        return;
    }

    /* list 子命令: 列出所有 HA 设备 */
    if (strcmp(subcmd, "list") == 0) {
        device_list_t devices;
        device_list_init(&devices);
        ha_get_all_devices(ha_url, ha_token, &devices);

        if (devices.count > 0) {
            printf("\n  %s\n", color_bold("Home Assistant 设备列表"));
            printf("  %s\n", color_dim("  ─────────────────────────────────────────────────────────────"));

            /* 按房间分组 */
            const char *room_order[] = {
                "客厅", "书房", "厨房", "主卧", "次卧", "卧室", "玄关", "入户玄关", "入户",
                "休闲阳台", "生活阳台", "阳台", "卫生间", "走廊", NULL
            };

            char rooms[64][32];
            int room_count = 0;
            for (int i = 0; i < devices.count; i++) {
                bool found = false;
                for (int j = 0; j < room_count; j++) {
                    if (strcmp(rooms[j], devices.items[i].room) == 0) { found = true; break; }
                }
                if (!found && room_count < 64)
                    strncpy(rooms[room_count++], devices.items[i].room, 31);
            }

            /* 冒泡排序 */
            for (int i = 0; i < room_count - 1; i++) {
                for (int j = 0; j < room_count - 1 - i; j++) {
                    int pi = -1, pj = -1;
                    for (int k = 0; room_order[k]; k++) {
                        if (strcmp(rooms[j], room_order[k]) == 0) pi = k;
                        if (strcmp(rooms[j+1], room_order[k]) == 0) pj = k;
                    }
                    if (pi == -1) pi = 99;
                    if (pj == -1) pj = 99;
                    if (pi > pj) {
                        char tmp[32];
                        strncpy(tmp, rooms[j], 31); tmp[31] = '\0';
                        strncpy(rooms[j], rooms[j+1], 31); rooms[j][31] = '\0';
                        strncpy(rooms[j+1], tmp, 31); rooms[j+1][31] = '\0';
                    }
                }
            }

            for (int r = 0; r < room_count; r++) {
                int devs_in_room = 0;
                for (int i = 0; i < devices.count; i++)
                    if (strcmp(devices.items[i].room, rooms[r]) == 0) devs_in_room++;
                if (devs_in_room == 0) continue;

                printf("\n  %s\n", color_bold_fmt("  [%s] (%d 个设备)", rooms[r], devs_in_room));
                for (int i = 0; i < devices.count; i++) {
                    if (strcmp(devices.items[i].room, rooms[r]) != 0) continue;
                    printf("    %s %s\n", color_cpad("36", devices.items[i].type, 8), devices.items[i].name);
                }
            }
        }
        device_list_free(&devices);
        return;
    }

    /* on/off/toggle/status 需要实体 ID */
    if (argc < 4) {
        printf("  %s\n", color_yellow("  请指定 entity_id"));
        return;
    }

    const char *entity = argv[3];

    if (strcmp(subcmd, "status") == 0) {
        char state[MAX_STR] = {0};
        if (ha_get_entity_state(ha_url, ha_token, entity, state, sizeof(state)) == 0) {
            printf("  实体: %s\n", entity);
            printf("  状态: %s\n", strcmp(state, "on") == 0 ? color_green("on") :
                   strcmp(state, "off") == 0 ? color_red("off") : state);
        }
    } else if (strcmp(subcmd, "on") == 0 || strcmp(subcmd, "off") == 0) {
        /* 解析 domain: switch.xxx -> switch, light.xxx -> light */
        char domain[64] = {0};
        const char *dot = strchr(entity, '.');
        if (dot) {
            int dlen = (int)(dot - entity);
            if (dlen > 0 && dlen < (int)sizeof(domain)) {
                strncpy(domain, entity, dlen);
                domain[dlen] = '\0';
            }
        }
        if (!domain[0]) strncpy(domain, "switch", sizeof(domain) - 1);

        if (ha_call_service(ha_url, ha_token, domain, subcmd, entity) == 0) {
            printf("  %s → %s\n", entity, color_green(subcmd));
        } else {
            char buf[MAX_STR * 2];
            snprintf(buf, sizeof(buf), "  操作失败: %s", entity);
            printf("  %s\n", color_red(buf));
        }
    } else if (strcmp(subcmd, "toggle") == 0) {
        char state[MAX_STR] = {0};
        if (ha_get_entity_state(ha_url, ha_token, entity, state, sizeof(state)) != 0) {
            printf("  %s\n", color_red("  无法获取当前状态"));
            return;
        }
        const char *target = (strcmp(state, "on") == 0) ? "off" : "on";

        char domain[64] = {0};
        const char *dot = strchr(entity, '.');
        if (dot) {
            int dlen = (int)(dot - entity);
            if (dlen > 0 && dlen < (int)sizeof(domain)) {
                strncpy(domain, entity, dlen);
                domain[dlen] = '\0';
            }
        }
        if (!domain[0]) strncpy(domain, "switch", sizeof(domain) - 1);

        if (ha_call_service(ha_url, ha_token, domain, target, entity) == 0) {
            printf("  %s: %s → %s\n", entity,
                   strcmp(state, "on") == 0 ? color_green("on") : color_red("off"),
                   strcmp(target, "on") == 0 ? color_green("on") : color_red("off"));
        } else {
            printf("  %s\n", color_red("  切换失败"));
        }
    } else {
        printf("  未知子命令: %s (支持 on/off/toggle/status/list)\n", subcmd);
    }
}

/* ═══════════════════════════════════════════════════════════ */
/* 子命令: monitor                                              */
/* ═══════════════════════════════════════════════════════════ */

static volatile sig_atomic_t g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

static void cmd_monitor(int argc, char **argv) {
    int interval = 10;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc)
            interval = atoi(argv[++i]);
    }

    printf("  %s\n", color_bold_fmt("米家设备监控 (间隔: %ds, Ctrl+C 退出)", interval));
    printf("  %s\n\n", color_dim("  ──────────────────────────────────────────────────────"));

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 用简单的 IP 集合跟踪上下线 */
    char prev_ips[512][MAX_STR];
    int prev_count = 0;

    while (g_running) {
        device_list_t devices;
        device_list_init(&devices);
        discover_all(3, &devices);

        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timebuf[32];
        strftime(timebuf, 32, "%Y-%m-%d %H:%M:%S", tm_info);

        /* 检测上线 */
        for (int i = 0; i < devices.count; i++) {
            const char *ip = devices.items[i].ip;
            bool was_online = false;
            for (int j = 0; j < prev_count; j++) {
                if (strcmp(prev_ips[j], ip) == 0) { was_online = true; break; }
            }
            if (!was_online) {
                printf("  %s  %s  %s  %s  %s\n",
                    color_green("[+ ONLINE]"), timebuf, ip,
                    devices.items[i].model, devices.items[i].name);
            }
        }

        /* 检测离线 */
        for (int i = 0; i < prev_count; i++) {
            bool still_online = false;
            for (int j = 0; j < devices.count; j++) {
                if (strcmp(devices.items[j].ip, prev_ips[i]) == 0) { still_online = true; break; }
            }
            if (!still_online) {
                printf("  %s  %s  %s\n", color_red("[- OFFLINE]"), timebuf, prev_ips[i]);
            }
        }

        if (prev_count > 0 && devices.count > 0) {
            /* 检查是否有变化 */
            bool changed = false;
            if (devices.count != prev_count) {
                changed = true;
            } else {
                for (int i = 0; i < devices.count; i++) {
                    bool found = false;
                    for (int j = 0; j < prev_count; j++) {
                        if (strcmp(devices.items[j].ip, devices.items[i].ip) == 0) { found = true; break; }
                    }
                    if (!found) { changed = true; break; }
                }
            }
            if (!changed) {
                printf("  %s  %s  无变化 (%d 台在线)\n",
                    color_dim("[  ...    ]"), timebuf, devices.count);
            }
        }

        /* 更新 prev */
        prev_count = devices.count < 512 ? devices.count : 512;
        for (int i = 0; i < prev_count; i++)
            strncpy(prev_ips[i], devices.items[i].ip, MAX_STR - 1);

        device_list_free(&devices);

        /* sleep interval 秒（每秒检查 g_running） */
        for (int s = 0; s < interval && g_running; s++)
            sleep(1);
    }

    printf("  %s\n", color_dim("\n  监控已停止"));
}

/* ═══════════════════════════════════════════════════════════ */
/* 帮助信息                                                     */
/* ═══════════════════════════════════════════════════════════ */

static void print_usage(const char *prog) {
    printf(
"mijia_scanner — 米家 (miIO) 设备网络探测器 (C 语言版)\n"
"\n"
"用法:\n"
"  %s <命令> [选项]\n"
"\n"
"命令:\n"
"  scan                    快速扫描（miIO 广播 + mDNS 双协议）\n"
"  scan --timeout 10       扫描超时 10 秒\n"
"  scan --mdns-only        仅 mDNS 扫描（发现新协议设备）\n"
"  scan --range 192.168.1.0/24    跨网段扫描\n"
"  scan --range 10.0.0.1-254       IP 范围扫描\n"
"  scan --json             JSON 格式输出\n"
"  scan --csv              CSV 格式输出\n"
"  ha status <entity>      查询 HA 实体状态\n"
"  ha on <entity>          打开 HA 实体 (switch/light/fan)\n"
"  ha off <entity>         关闭 HA 实体\n"
"  ha toggle <entity>      切换 HA 实体状态\n"
"  ha list                 列出 HA 设备 (按房间分组)\n"
"  models                  打印已知型号数据库\n"
"  info <ip>               查询单台设备信息\n"
"  monitor --interval 30   每 30 秒监控设备上下线\n"
"  export --format csv -o devices.csv\n"
"\n"
"协议说明:\n"
"  miIO (旧) — UDP 广播 54321，适用于老设备\n"
"  miOT (新) — mDNS _miot-central._tcp.local.，适用于新设备\n"
"  默认同时使用两种协议发现，确保最大覆盖\n"
"\n"
"依赖:\n"
"  avahi-utils    mDNS 发现 (apt install avahi-utils)\n"
"  fping          快速 ping (apt install fping)\n"
"  libcurl        HA API (apt install libcurl4-openssl-dev)\n"
"  gcc / make     编译\n"
"\n"
"全局选项:\n"
"  --no-color               禁用彩色输出\n"
"\n"
    , prog);
}

/* ═══════════════════════════════════════════════════════════ */
/* 主入口                                                       */
/* ═══════════════════════════════════════════════════════════ */

int main(int argc, char **argv) {
    /* 检查 --no-color 全局选项 */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-color") == 0) {
            g_no_color = 1;
            /* 从 argv 移除 */
            for (int j = i; j < argc - 1; j++) argv[j] = argv[j+1];
            argc--;
            i--;
        }
    }

    color_init();

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "scan") == 0)        cmd_scan(argc, argv);
    else if (strcmp(cmd, "models") == 0) cmd_models(argc, argv);
    else if (strcmp(cmd, "info") == 0)   cmd_info(argc, argv);
    else if (strcmp(cmd, "export") == 0) cmd_export(argc, argv);
    else if (strcmp(cmd, "ha") == 0)     cmd_ha(argc, argv);
    else if (strcmp(cmd, "monitor") == 0)cmd_monitor(argc, argv);
    else if (strcmp(cmd, "deep") == 0) {
        /* deep scan 暂未实现（需要 AES） */
        printf("  %s\n", color_yellow("  深度扫描需要 AES 加解密支持（待实现）"));
        printf("  %s\n", color_dim("  提示: 可用 Python 版本的 'deep' 子命令"));
        return 1;
    }
    else if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "help") == 0)
        print_usage(argv[0]);
    else {
        printf("  未知命令: %s\n", cmd);
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}
