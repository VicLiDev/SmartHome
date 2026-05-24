/*
 * ha.h — Home Assistant REST API
 *
 * 通过 libcurl 从 HA 获取设备列表、查询单实体状态、调用服务。
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
 * @param devices   输出设备列表（动态数组）
 */
void ha_get_all_devices(const char *ha_url, const char *token, void *devices);

/**
 * 查询单个实体的 state 字段
 *
 * @param ha_url      HA 地址
 * @param token       HA token
 * @param entity_id   实体 ID (如 "switch.xxx")
 * @param state_out   输出状态字符串 (如 "on"/"off")
 * @param state_sz    state_out 缓冲区大小
 * @return 0 成功, -1 失败
 */
int ha_get_entity_state(const char *ha_url, const char *token,
                        const char *entity_id, char *state_out, int state_sz);

/**
 * 调用 HA 服务 (如 switch.turn_on)
 *
 * @param ha_url      HA 地址
 * @param token       HA token
 * @param domain      服务域 (如 "switch", "light")
 * @param service     服务名 (如 "turn_on", "toggle")
 * @param entity_id   目标实体 ID
 * @return 0 成功, -1 失败
 */
int ha_call_service(const char *ha_url, const char *token,
                    const char *domain, const char *service,
                    const char *entity_id);

#endif /* Mijia_HA_H */
