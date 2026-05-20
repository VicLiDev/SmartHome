/**
 * output.c — 表格打印、JSON/CSV 导出
 *
 * 对齐 Python 版本: output.py
 * 提供 print_device_table(), export_json(), export_csv()
 */

#include "common.h"

/* 前置声明 */
static const char *color_green_fmt(const char *fmt, int val);

/**
 * print_device_table — 彩色表格打印设备列表
 * 对齐 Python output.py: print_device_table()
 */
void print_device_table(const device_list_t *devices, bool show_mac) {
    (void)show_mac;
    if (!devices || devices->count == 0) {
        printf("  %s\n", color_yellow("未发现任何设备"));
        return;
    }

    /* 检查是否有 room 字段 */
    bool has_rooms = false;
    for (int i = 0; i < devices->count; i++) {
        if (devices->items[i].room[0]) { has_rooms = true; break; }
    }

    /* 表头 */
    {
        /* 表头用 bold 整行上色（对齐 Python 版），所以各字段用栈缓冲区 pad */
        char h1[16], h2[32], h3[32], h4[16], h5[32], h6[64], h7[32], h8[32];
        pad_to(h1, sizeof(h1), "#", 3);
        pad_to(h2, sizeof(h2), "IP", 16);
        pad_to(h3, sizeof(h3), "协议", 10);
        pad_to(h5, sizeof(h5), "类型", 14);
        pad_to(h6, sizeof(h6), "名称", 32);
        pad_to(h7, sizeof(h7), "厂商", 14);
        pad_to(h8, sizeof(h8), "MAC", 18);
        if (has_rooms) {
            pad_to(h4, sizeof(h4), "区域", 6);
            char line[1024];
            snprintf(line, sizeof(line), "  %s  %s  %s  %s  %s  %s  %s  %s",
                h1, h2, h3, h4, h5, h6, h7, h8);
            printf("%s\n", color_bold(line));
        } else {
            char line[1024];
            snprintf(line, sizeof(line), "  %s  %s  %s  %s  %s  %s  %s",
                h1, h2, h3, h5, h6, h7, h8);
            printf("%s\n", color_bold(line));
        }
    }
    /* 分隔线 — 匹配表头显示宽度 (─ = E2 94 80, 3字节/2显示宽度 → 用 ─ 个数 = sep_width/2 不对，每个─占1终端列... 不对，U+2500 BOX DRAWINGS LIGHT HORIZONTAL 在 CJK 终端中占 1 列) */
    {
        int sep_width = has_rooms ? (2 + 3 + 2 + 16 + 2 + 10 + 2 + 6 + 2 + 14 + 2 + 32 + 2 + 14 + 2 + 18)
                                : (2 + 3 + 2 + 16 + 2 + 10 + 2 + 14 + 2 + 32 + 2 + 14 + 2 + 18);
        /* 用 ASCII 减号代替 box drawing，简单可靠 */
        char sep[256];
        memset(sep, '-', (size_t)sep_width < sizeof(sep) - 1 ? (size_t)sep_width : sizeof(sep) - 1);
        sep[sep_width < (int)sizeof(sep) - 1 ? sep_width : (int)sizeof(sep) - 1] = '\0';
        printf("  %s\n", color_dim(sep));
    }

    for (int i = 0; i < devices->count; i++) {
        const device_t *d = &devices->items[i];
        const char *mac = d->mac;
        const char *vendor = d->vendor;
        const char *name = d->name;
        const char *dtype = d->type;
        const char *protocol = d->protocol;

        /* 协议颜色（code="0" 时 color_wrap 不包裹转义，不影响对齐） */
        const char *tc = "0";
        if (strcmp(protocol, "miIO") == 0 || strcmp(protocol, "ARP") == 0)
            tc = "33";
        else if (strcmp(protocol, "mDNS") == 0)
            tc = "36";
        else if (strcmp(protocol, "HomeKit") == 0)
            tc = "35";
        else if (strcmp(protocol, "miIO + mDNS") == 0)
            tc = "34";

        /* HomeKit 设备显示名处理 */
        const char *display_name = name;
        const char *display_type = dtype;
        if (strcmp(protocol, "HomeKit") == 0 && strncmp(name, "HAP: ", 5) == 0) {
            display_name = name + 4;
        } else if (!*display_name) {
            display_name = vendor;
        }

        /* 截断过长名称 */
        char name_short[MAX_DEV_NAME + 4];
        if (display_width(display_name) > 32) {
            int cw = 0;
            int ci = 0;
            const unsigned char *p = (const unsigned char *)display_name;
            while (*p && cw < 30) {
                if (*p < 0x80) { cw += 1; p++; ci++; }
                else if ((*p & 0xE0) == 0xC0) { cw += 1; p += 2; ci += 2; }
                else if ((*p & 0xF0) == 0xE0) { cw += 2; p += 3; ci += 3; }
                else if ((*p & 0xF8) == 0xF0) { cw += 2; p += 4; ci += 4; }
                else { cw += 1; p++; ci++; }
            }
            strncpy(name_short, display_name, ci);
            name_short[ci] = '\0';
            strcat(name_short, "..");
        } else {
            strncpy(name_short, display_name, MAX_DEV_NAME - 1);
            name_short[MAX_DEV_NAME - 1] = '\0';
        }

        char type_short[66];
        if (display_width(display_type) > 14) {
            int cw = 0, ci = 0;
            const unsigned char *p = (const unsigned char *)display_type;
            while (*p && cw < 12) {
                if (*p < 0x80) { cw += 1; p++; ci++; }
                else if ((*p & 0xE0) == 0xC0) { cw += 1; p += 2; ci += 2; }
                else if ((*p & 0xF0) == 0xE0) { cw += 2; p += 3; ci += 3; }
                else if ((*p & 0xF8) == 0xF0) { cw += 2; p += 4; ci += 4; }
                else { cw += 1; p++; ci++; }
            }
            strncpy(type_short, display_type, ci);
            type_short[ci] = '\0';
            strcat(type_short, "..");
        } else {
            strncpy(type_short, display_type, 65);
            type_short[65] = '\0';
        }

        char num_str[12];
        snprintf(num_str, sizeof(num_str), "%d", i + 1);

        /* 使用栈缓冲区 pad 各字段，拼接整行后一次输出（对齐 Python 版 f-string 方式） */
        char buf_num[16], buf_ip[32], buf_proto[64], buf_room[32];
        char buf_type[64], buf_name[128], buf_vendor[64], buf_mac[128];

        pad_to(buf_num, sizeof(buf_num), num_str, 3);
        pad_to(buf_ip, sizeof(buf_ip), d->ip, 16);
        color_pad(buf_proto, sizeof(buf_proto), tc, protocol, 10);
        color_pad(buf_type, sizeof(buf_type), tc, type_short, 14);
        color_pad(buf_name, sizeof(buf_name), tc, name_short, 32);
        const char *vtc = *vendor ? "32" : "0";
        color_pad(buf_vendor, sizeof(buf_vendor), vtc, vendor, 14);
        /* MAC: dim 色 + pad */
        char mac_pad[32];
        pad_to(mac_pad, sizeof(mac_pad), mac, 18);
        color_wrap(buf_mac, sizeof(buf_mac), "2", mac_pad);

        if (has_rooms) {
            color_pad(buf_room, sizeof(buf_room), "32", d->room, 6);
            char line[1024];
            snprintf(line, sizeof(line), "  %s  %s  %s  %s  %s  %s  %s  %s",
                buf_num, buf_ip, buf_proto, buf_room, buf_type, buf_name, buf_vendor, buf_mac);
            printf("%s\n", line);
        } else {
            char line[1024];
            snprintf(line, sizeof(line), "  %s  %s  %s  %s  %s  %s  %s",
                buf_num, buf_ip, buf_proto, buf_type, buf_name, buf_vendor, buf_mac);
            printf("%s\n", line);
        }
    }

    /* 协议统计 */
    printf("\n");
    typedef struct { char proto[32]; int count; } proto_stat_t;
    proto_stat_t stats[32];
    int stat_count = 0;

    for (int i = 0; i < devices->count; i++) {
        const char *p = devices->items[i].protocol;
        bool found = false;
        for (int j = 0; j < stat_count; j++) {
            if (strcmp(stats[j].proto, p) == 0) { stats[j].count++; found = true; break; }
        }
        if (!found && stat_count < 32) {
            strncpy(stats[stat_count].proto, p, 31);
            stats[stat_count].count = 1;
            stat_count++;
        }
    }

    /* 按数量排序（简单冒泡，数量少） */
    for (int i = 0; i < stat_count - 1; i++) {
        for (int j = i + 1; j < stat_count; j++) {
            if (stats[j].count > stats[i].count) {
                proto_stat_t tmp = stats[i]; stats[i] = stats[j]; stats[j] = tmp;
            }
        }
    }

    printf("  %s", color_dim("  按协议: "));
    for (int i = 0; i < stat_count; i++) {
        if (i > 0) printf(", ");
        printf("%s: %d", stats[i].proto, stats[i].count);
    }
    printf("\n");

    /* Token 汇总 */
    int token_count = 0;
    for (int i = 0; i < devices->count; i++) {
        if (devices->items[i].token[0]) token_count++;
    }
    if (token_count > 0) {
        printf("\n  %s\n", color_green_fmt("[*] %d 台设备返回了 Token（可能未绑定）:", token_count));
        for (int i = 0; i < devices->count; i++) {
            if (devices->items[i].token[0])
                printf("      %s  %s  Token: %s\n",
                    devices->items[i].ip,
                    devices->items[i].model,
                    color_yellow(devices->items[i].token));
        }
    }
}

/**
 * color_green_fmt — 内部辅助
 */
static char _green_fmt_buf[MAX_STR * 2];
static const char *color_green_fmt(const char *fmt, int val) {
    snprintf(_green_fmt_buf, sizeof(_green_fmt_buf), fmt, val);
    return color_green(_green_fmt_buf);
}

/**
 * export_json — 导出 JSON 格式
 * 对齐 Python output.py: export_json()
 *
 * 注: 手动 JSON 序列化（避免依赖 cJSON）
 */
static void print_json_str(FILE *fp, const char *str) {
    fputc('"', fp);
    if (str) {
        for (; *str; str++) {
            switch (*str) {
                case '"':  fputs("\\\"", fp); break;
                case '\\': fputs("\\\\", fp); break;
                case '\n': fputs("\\n", fp); break;
                case '\t': fputs("\\t", fp); break;
                case '\r': fputs("\\r", fp); break;
                default:
                    if ((unsigned char)*str >= 0x20)
                        fputc(*str, fp);
                    else
                        fprintf(fp, "\\u%04x", (unsigned char)*str);
            }
        }
    }
    fputc('"', fp);
}

void export_json(const device_list_t *devices, const char *output) {
    FILE *fp;
    if (output) {
        fp = fopen(output, "w");
        if (!fp) {
            printf("  %s: %s\n", color_red("无法打开文件"), output);
            return;
        }
    } else {
        fp = stdout;
    }

    fprintf(fp, "[\n");
    for (int i = 0; i < devices->count; i++) {
        const device_t *d = &devices->items[i];
        fprintf(fp, "  {\n");
        fprintf(fp, "    \"ip\": "); print_json_str(fp, d->ip); fprintf(fp, ",\n");
        fprintf(fp, "    \"port\": %d,\n", d->port);
        fprintf(fp, "    \"device_id\": %u,\n", d->device_id);
        fprintf(fp, "    \"model\": "); print_json_str(fp, d->model); fprintf(fp, ",\n");
        fprintf(fp, "    \"name\": "); print_json_str(fp, d->name); fprintf(fp, ",\n");
        fprintf(fp, "    \"type\": "); print_json_str(fp, d->type); fprintf(fp, ",\n");
        fprintf(fp, "    \"token\": "); print_json_str(fp, d->token); fprintf(fp, ",\n");
        fprintf(fp, "    \"timestamp\": %u,\n", d->timestamp);
        fprintf(fp, "    \"last_seen\": "); print_json_str(fp, d->last_seen); fprintf(fp, ",\n");
        fprintf(fp, "    \"protocol\": "); print_json_str(fp, d->protocol); fprintf(fp, ",\n");
        fprintf(fp, "    \"mac\": "); print_json_str(fp, d->mac); fprintf(fp, ",\n");
        fprintf(fp, "    \"vendor\": "); print_json_str(fp, d->vendor); fprintf(fp, "\n");
        fprintf(fp, "  }%s\n", i < devices->count - 1 ? "," : "");
    }
    fprintf(fp, "]\n");

    if (output) {
        fclose(fp);
        printf("  已导出到 %s\n", output);
    }
}

/**
 * export_csv — 导出 CSV 格式
 * 对齐 Python output.py: export_csv()
 */
void export_csv(const device_list_t *devices, const char *output) {
    FILE *fp;
    if (output) {
        fp = fopen(output, "w");
        if (!fp) {
            printf("  %s: %s\n", color_red("无法打开文件"), output);
            return;
        }
    } else {
        fp = stdout;
    }

    /* 表头 */
    fprintf(fp, "ip,port,device_id,model,name,type,token,timestamp,last_seen\n");

    for (int i = 0; i < devices->count; i++) {
        const device_t *d = &devices->items[i];
        fprintf(fp, "%s,%d,%u,", d->ip, d->port, d->device_id);
        /* CSV 字段引号 */
        print_json_str(fp, d->model); fprintf(fp, ",");
        print_json_str(fp, d->name); fprintf(fp, ",");
        print_json_str(fp, d->type); fprintf(fp, ",");
        print_json_str(fp, d->token); fprintf(fp, ",");
        fprintf(fp, "%u,", d->timestamp);
        print_json_str(fp, d->last_seen);
        fprintf(fp, "\n");
    }

    if (output) {
        fclose(fp);
        printf("  已导出到 %s\n", output);
    }
}
