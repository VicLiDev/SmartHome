/*
 * network.h — ARP 解析 + ping sweep + IP range
 *
 * 提供 ARP 表查询、IP 范围解析、ping 扫描、miIO unicast 探测等功能。
 */

#ifndef Mijia_NETWORK_H
#define Mijia_NETWORK_H

#include <stdint.h>

/* IP 字符串最大长度 */
#define MAX_IP_STRLEN       16
/* ARP 表最大条目数 */
#define ARP_MAX_ENTRIES    512
/* IP 范围最大 IP 数 */
#define IP_RANGE_MAX       4096

/* ARP 条目 */
typedef struct {
    char ip[MAX_IP_STRLEN];
    char mac[24];
} ArpEntry;

/**
 * 解析 IP 范围字符串，输出 IP 数组
 *
 * 支持格式:
 *   192.168.1.0/24        — CIDR
 *   192.168.1.1-254       — 起止范围
 *   192.168.1.5           — 单个 IP
 *   192.168.1.0/24,10.0.0.1-10   — 逗号分隔多段
 *
 * @param spec       范围字符串
 * @param ips        输出 IP 字符串数组（需 >= IP_RANGE_MAX）
 * @param max_count  数组容量
 * @return 实际解析的 IP 数（>=0），错误返回负数
 */
int parse_ip_ranges(const char *spec, char ips[][MAX_IP_STRLEN], int max_count);

/**
 * 读取系统 ARP 表（/proc/net/arp）
 *
 * @param entries   输出 ARP 条目数组
 * @param max_count 数组容量
 * @return 实际条目数（>=0），错误返回负数
 */
int arp_scan(ArpEntry *entries, int max_count);

/**
 * 用 fping 快速扫描 IP 列表，返回存活 IP 列表
 * 降级: 无 fping 时使用逐个 ping
 *
 * @param ips        IP 字符串数组
 * @param ip_count   IP 数量
 * @param alive_out  输出存活 IP 数组（需 >= ip_count）
 * @param max_alive  存活数组容量
 * @return 实际存活数（>=0），错误返回负数
 */
int ping_sweep(const char ips[][MAX_IP_STRLEN], int ip_count,
               char alive_out[][MAX_IP_STRLEN], int max_alive);

/**
 * 查询单个 IP 的 MAC 地址
 *
 * @param ip    目标 IP
 * @param mac   输出 MAC 缓冲区（需 >= 24 字节）
 * @return 0 找到, -1 未找到
 */
int arp_lookup(const char *ip, char *mac);

#endif /* Mijia_NETWORK_H */
