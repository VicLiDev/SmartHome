/*
 * ha.h — Home Assistant REST API
 *
 * 通过 libcurl HTTPS 从 HA 获取设备列表，按房间分组。
 */

#ifndef Mijia_HA_H
#define Mijia_HA_H

/* HA 设备条目 */
typedef struct {
    char name[128];
    char room[64];
    char type[64];
} HaDevice;

/* HA 获取的设备数组 */
#define HA_MAX_DEVICES 256

/**
 * 从 Home Assistant REST API 获取所有设备
 *
 * @param ha_url    HA 地址（如 "http://192.168.1.100:8123"）
 * @param token     HA long-lived access token
 * @param devices   输出设备数组
 * @param max_count 数组容量
 * @return 实际设备数（>=0），错误返回负数
 */
int ha_get_all_devices(const char *ha_url, const char *token,
                       HaDevice *devices, int max_count);

/**
 * 打印 HA 设备列表（按房间分组）
 *
 * @param devices   设备数组
 * @param count     设备数量
 */
void ha_print_devices(const HaDevice *devices, int count);

#endif /* Mijia_HA_H */
