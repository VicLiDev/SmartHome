/*
 * discovery.h — UDP 广播发现 miIO 设备
 */

#ifndef DISCOVERY_H
#define DISCOVERY_H

#include "miio_protocol.h"

/**
 * 发送 Hello 广播，收集在线设备
 *
 * @param results    设备数组输出
 * @param max_count  数组容量
 * @param timeout_s  超时秒数
 * @return 实际发现的设备数（>=0），错误返回负数
 */
int miio_discover(MiioDevice *results, int max_count, int timeout_s);

/**
 * 单台设备握手 + 获取基本信息
 *
 * @param ip    目标 IP
 * @param token 设备 token（可为空，仅做 hello 握手）
 * @param info  输出设备信息
 * @return 0 成功, -1 失败
 */
int miio_handshake(const char *ip, const char *token, MiioDevice *info);

#endif /* DISCOVERY_H */
