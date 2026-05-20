/*
 * mdns.h — mDNS 发现 + MAC OUI 查询
 *
 * 通过 avahi-browse 命令行工具实现 mDNS/DNS-SD 发现（零编译依赖）。
 * 同时提供 MAC 地址 OUI 前缀厂商查询。
 */

#ifndef Mijia_MDNS_H
#define Mijia_MDNS_H

#include "protocol.h"

/* mDNS 发现的最大设备数 */
#define MDNS_MAX_DEVICES  128

/* ═══ OUI 条目 ═══ */
typedef struct {
    const char *prefix;   /* MAC 前缀 "b8:88:80" */
    const char *vendor;   /* 厂商名 */
    const char *dtype;    /* 设备类型 */
} MacOuiEntry;

/**
 * 根据 MAC 前缀查询厂商和设备类型
 *
 * @param mac       MAC 地址字符串
 * @param vendor    输出厂商缓冲区（需 >= 64 字节）
 * @param dtype     输出类型缓冲区（需 >= 64 字节）
 */
void lookup_mac_vendor(const char *mac, char *vendor, char *dtype);

/**
 * 通过 avahi-browse 发现 mDNS 设备
 *
 * @param results    设备数组输出
 * @param max_count  数组容量
 * @param timeout_s  监听超时秒数
 * @return 实际发现的设备数（>=0），错误返回负数
 */
int mdns_discover(ScanDevice *results, int max_count, int timeout_s);

#endif /* Mijia_MDNS_H */
