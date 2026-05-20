/*
 * command.h — JSON-RPC 命令构建与发送
 */

#ifndef COMMAND_H
#define COMMAND_H

#include "miio_protocol.h"
#include "miio_crypto.h"

/**
 * 发送 miIO 命令到指定设备
 *
 * @param ip       设备 IP
 * @param port     设备端口（通常 54321）
 * @param token    设备 token
 * @param method   RPC 方法名
 * @param params   JSON 格式的参数字符串
 * @param req_id   请求 ID（用于匹配响应）
 * @param response 输出响应（需调用者 free result_json）
 * @param timeout  超时秒数
 * @return 0 成功, -1 失败
 */
int miio_send_command(const char *ip, uint16_t port,
                      const char *token,
                      const char *method, const char *params,
                      int req_id,
                      MiioResponse *response,
                      int timeout);

/** 快捷方法：获取设备信息 */
int miio_get_info(const char *ip, const char *token,
                  char *json_out, size_t len);

/** 快捷方法：获取属性 */
int miio_get_prop(const char *ip, const char *token,
                  const char **props, int count,
                  char *json_out, size_t len);

/** 快捷方法：开关电源 */
int miio_set_power(const char *ip, const char *token,
                   const char *state);  /* "on" / "off" */

#endif /* COMMAND_H */
