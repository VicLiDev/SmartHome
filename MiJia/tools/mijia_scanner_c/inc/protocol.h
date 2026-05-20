/*
 * protocol.h — miIO 协议实现
 *
 * 提供 miIO 广播发现、深度扫描、设备信息查询等功能。
 * 复用 demos/01_c_demo 的 miio_crypto 和 command 模块。
 */

#ifndef Mijia_PROTOCOL_H
#define Mijia_PROTOCOL_H

#include "miio_protocol.h"

/* ═══ 协议常量 ═══ */
#define MIIO_HDR_MIN      32      /* Hello 响应最小头部 */
#define SCAN_MAX_DEVICES  256     /* 单次扫描最大设备数 */

/* ═══ 发现结果中的设备信息（扩展 MiioDevice） ═══ */
typedef struct {
    char     ip[MAX_IP_STRLEN];
    uint16_t port;
    uint32_t device_id;
    char     model[MAX_MODEL_LEN];
    char     name[128];           /* 中文名称 */
    char     type[64];            /* 设备类型 */
    char     token[TOKEN_HEX_LEN + 1];
    uint32_t timestamp;
    char     last_seen[32];       /* 时间字符串 */
    char     protocol[32];        /* "miIO", "mDNS", "ARP" 等 */
    char     mac[24];             /* MAC 地址 */
    char     vendor[64];          /* 厂商 */
    /* 深度扫描扩展字段 */
    char     fw_ver[64];
    char     mcu_firmware_ver[64];
    char     hw_ver[64];
    char     ssid[64];
    char     bssid[24];
    int      rssi;
} ScanDevice;

/**
 * 快速扫描：发送 Hello 广播，收集所有 miIO 设备响应
 *
 * @param results    设备数组输出
 * @param max_count  数组容量
 * @param timeout_s  超时秒数
 * @return 实际发现的设备数（>=0），错误返回负数
 */
int protocol_discover_devices(ScanDevice *results, int max_count, int timeout_s);

/**
 * 深度扫描单台设备，发送 miIO.info + get_prop 获取详细信息
 *
 * @param ip       设备 IP
 * @param token    设备 token（32字符hex）
 * @param dev      输出设备信息
 * @param timeout  超时秒数
 * @return 0 成功, -1 失败
 */
int protocol_deep_scan(const char *ip, const char *token,
                       ScanDevice *dev, int timeout);

/**
 * 对单个 IP 发送 unicast Hello 探测
 *
 * @param ip       目标 IP
 * @param dev      输出设备信息
 * @param timeout  超时秒数
 * @return 0 成功, -1 失败/超时
 */
int protocol_probe_ip(const char *ip, ScanDevice *dev, int timeout);

#endif /* Mijia_PROTOCOL_H */
