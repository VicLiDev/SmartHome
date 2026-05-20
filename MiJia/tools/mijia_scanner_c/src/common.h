/**
 * common.h — 公共头文件
 *
 * mijia_scanner_c: C 语言米家设备网络探测器
 * 对齐 Python 版本: mijia_scanner/mijia_scanner_lib/
 *
 * 模块对应关系:
 *   color.c      <- color.py      ANSI 彩色终端输出
 *   device_db.c  <- device_db.py  设备型号数据库
 *   protocol.c   <- protocol.py   miIO 协议核心
 *   network.c    <- network.py    ARP / ping / 范围扫描
 *   mdns.c       <- mdns.py       mDNS/HomeKit 发现
 *   ha.c         <- ha.py         Home Assistant API
 *   output.c     <- output.py     表格打印 / 导出
 *   main.c       <- mijia_scanner.py  主程序入口
 */

#ifndef MIJIA_SCANNER_COMMON_H
#define MIJIA_SCANNER_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <pthread.h>

/* ═══════════════════════════════════════════════════════════ */
/* miIO 协议常量 (对齐 protocol.py)                             */
/* ═══════════════════════════════════════════════════════════ */

#define MIIO_MAGIC       0x2131
#define MIIO_PORT        54321
#define MIIO_MULTICAST   "224.0.0.50"
#define MIIO_HDR_MIN     32
#define MIIO_TOKEN_LEN   16   /* 16 bytes = 32 hex chars */
#define TOKEN_HEX_LEN    32

#define MIIO_NONCE_LEN   16
#define MIIO_SIGN_LEN    16

/* ═══════════════════════════════════════════════════════════ */
/* 设备结构体                                                  */
/* ═══════════════════════════════════════════════════════════ */

#define MAX_STR 256
#define MAX_DEV_NAME   128
#define MAX_MODEL      128
#define MAX_MDNS_TYPE  128
#define MAX_MDNS_NAME  256
#define MAX_MDNS_SERVER 256

typedef struct {
    char     ip[MAX_STR];           /* IP 地址 */
    int      port;                  /* 端口 */
    uint32_t device_id;             /* miIO Device ID */
    char     model[MAX_MODEL];      /* 设备型号 */
    char     name[MAX_DEV_NAME];    /* 设备名称 */
    char     type[64];              /* 设备类型 */
    char     token[TOKEN_HEX_LEN+1];/* Token (hex string) */
    uint32_t timestamp;             /* 时间戳 */
    char     last_seen[32];         /* 最后发现时间 */
    char     protocol[32];          /* 协议: miIO / mDNS / HomeKit / ARP / miIO+mDNS */
    char     mac[24];               /* MAC 地址 */
    char     vendor[64];            /* 厂商 */
    char     room[32];              /* HA 区域 */
    /* mDNS 扩展字段 */
    char     mdns_type[MAX_MDNS_TYPE];
    char     mdns_name[MAX_MDNS_NAME];
    char     mdns_server[MAX_MDNS_SERVER];
    char     mdns_props[1024];
} device_t;

/* ═══════════════════════════════════════════════════════════ */
/* 设备列表动态数组                                              */
/* ═══════════════════════════════════════════════════════════ */

typedef struct {
    device_t *items;
    int       count;
    int       capacity;
} device_list_t;

static inline void device_list_init(device_list_t *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static inline void device_list_free(device_list_t *list) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

/* 返回新增元素指针 */
static inline device_t *device_list_add(device_list_t *list) {
    if (list->count >= list->capacity) {
        int new_cap = list->capacity == 0 ? 16 : list->capacity * 2;
        list->items = realloc(list->items, new_cap * sizeof(device_t));
        if (!list->items) { perror("realloc"); exit(1); }
        list->capacity = new_cap;
    }
    device_t *d = &list->items[list->count++];
    memset(d, 0, sizeof(*d));
    d->port = MIIO_PORT;
    return d;
}

/* ═══════════════════════════════════════════════════════════ */
/* 全局标志                                                     */
/* ═══════════════════════════════════════════════════════════ */

extern int g_no_color;   /* --no-color 标志 */

/* ═══════════════════════════════════════════════════════════ */
/* color.h 函数声明                                              */
/* ═══════════════════════════════════════════════════════════ */

int  display_width(const char *text);
char *pad_cjk(const char *text, int width);
void pad_to(char *buf, int bufsize, const char *text, int width);
void color_init(void);
const char *color_red(const char *t);
const char *color_green(const char *t);
const char *color_yellow(const char *t);
const char *color_blue(const char *t);
const char *color_magenta(const char *t);
const char *color_cyan(const char *t);
const char *color_bold(const char *t);
const char *color_dim(const char *t);
const char *color_cpad(const char *code, const char *text, int width);
void color_wrap(char *buf, int bufsize, const char *code, const char *text);
void color_pad(char *buf, int bufsize, const char *code, const char *text, int width);

/* ═══════════════════════════════════════════════════════════ */
/* device_db.h 函数声明                                          */
/* ═══════════════════════════════════════════════════════════ */

void device_db_lookup(const char *model, char *name_out, char *type_out);
void device_db_print_all(void);
int  device_db_count(void);

/* ═══════════════════════════════════════════════════════════ */
/* protocol.h 函数声明                                           */
/* ═══════════════════════════════════════════════════════════ */

void build_hello_packet(uint8_t *pkt);
void discover_devices(int timeout, device_list_t *devices);

/* ═══════════════════════════════════════════════════════════ */
/* network.h 函数声明                                            */
/* ═══════════════════════════════════════════════════════════ */

int  parse_ip_ranges(const char *spec, char **ips_out, int max_ips);
void arp_scan(device_list_t *arp_devices);
void ping_sweep(char **ips, int count, char **alive_out, int *alive_count);
void probe_one_ip(const char *ip, const uint8_t *hello, int timeout, device_list_t *devices);
void discover_devices_from_alive(char **alive_ips, int count, int timeout, device_list_t *devices);
const char *arp_lookup(const char *ip);

/* ═══════════════════════════════════════════════════════════ */
/* mdns.h 函数声明                                               */
/* ═══════════════════════════════════════════════════════════ */

void discover_mdns(int timeout, device_list_t *devices);
void lookup_mac_vendor(const char *mac, char *vendor_out, char *type_out);
void discover_all(int timeout, device_list_t *devices);

/* ═══════════════════════════════════════════════════════════ */
/* ha.h 函数声明                                                 */
/* ═══════════════════════════════════════════════════════════ */

void ha_get_all_devices(const char *ha_url, const char *token, device_list_t *devices);

/* ═══════════════════════════════════════════════════════════ */
/* output.h 函数声明                                             */
/* ═══════════════════════════════════════════════════════════ */

void print_device_table(const device_list_t *devices, bool show_mac);
void export_json(const device_list_t *devices, const char *output);
void export_csv(const device_list_t *devices, const char *output);

/* ═══════════════════════════════════════════════════════════ */
/* 通用格式化着色辅助（variadic，放在所有声明之后）               */
/* ═══════════════════════════════════════════════════════════ */

#include <stdarg.h>
static inline const char *color_dim_fmt(const char *fmt, ...) {
    static char _buf[MAX_STR * 2];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(_buf, sizeof(_buf), fmt, ap);
    va_end(ap);
    return color_dim(_buf);
}

#endif /* MIJIA_SCANNER_COMMON_H */
