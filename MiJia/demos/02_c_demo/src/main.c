/*
 * 02_c_demo — miOT 本地协议 Demo
 *
 * miOT 在 miIO 传输层（UDP 54321 + AES-128-CBC）之上，使用 SIID/PIID
 * 属性模型构建 JSON-RPC 请求。本程序演示 get_properties / set_properties。
 *
 * 用法:
 *   ./miot_local_demo list-models
 *   ./miot_local_demo discover
 *   ./miot_local_demo get  <IP> <TOKEN> --siid N --piid N
 *   ./miot_local_demo set  <IP> <TOKEN> --siid N --piid N --value V
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include "miio_protocol.h"
#include "miio_crypto.h"
#include "discovery.h"
#include "command.h"
#include "cJSON.h"

/* ═══════════════════════════════════════════════════════════
 * 硬编码设备 SIID/PIID 参考表
 * ═══════════════════════════════════════════════════════════ */

typedef struct {
    const char *model;      /* 型号标识 */
    const char *name;       /* 中文名 */
    int  siid_on;           /* 电源服务 SIID（-1 表示无） */
    int  piid_on;           /* 开关属性 PIID */
    int  piid_mode;         /* 模式属性 PIID（-1 表示无） */
    int  piid_val1;         /* 常用数值属性 PIID（如亮度/风速，-1 表示无） */
} DeviceModel;

static const DeviceModel g_models[] = {
    { "zhimi.fan.v3",             "智米直流变频落地扇 3",         2,  1,  3,  4 },
    { "zhimi.fan.sa1",            "智米落地扇 SA1",               2,  1,  3,  4 },
    { "zhimi.airpurifier.m6",     "米家空气净化器 Pro H",         2,  1,  5,  4 },
    { "zhimi.airpurifier.ma4",    "米家空气净化器 4",             2,  1,  5,  4 },
    { "zhimi.humidifier.cb1",     "米家加湿器",                   2,  1,  5,  3 },
    { "yeelink.light.bslamp2",    "米台灯 2",                     2,  1,  3,  2 },
    { "yeelink.light.color4",     "Yeelight 彩光灯泡",           2,  1,  3,  2 },
    { "yeelink.light.strip2",     "Yeelight 彩光灯带",           2,  1,  3,  2 },
    { "cuco.plug.v3",             "米家智能插座 3",               2,  1, -1, -1 },
    { "cuco.plug.m1",             "米家智能插排",                 2,  1, -1, -1 },
    { "lumi.sensor_ht.v1",        "米家温湿度传感器",            -1, -1, -1, -1 },
    { "lumi.sensor_motion.v2",    "米家人体传感器",              -1, -1, -1, -1 },
    { "lumi.sensor_magnet.v2",    "米家门磁传感器",              -1, -1, -1, -1 },
    { "zhimi.heater.mc2",         "米家踢脚线电暖器",            2,  1,  2,  3 },
    { "chuangmi.plug.v3",         "米家智能插座",                2,  1, -1, -1 },
};
#define MODEL_COUNT (int)(sizeof(g_models) / sizeof(g_models[0]))

/* ═══════════════════════════════════════════════════════════
 * 辅助函数
 * ═══════════════════════════════════════════════════════════ */

/** 打印用法 */
static void usage(const char *prog)
{
    printf("miOT 本地协议 Demo\n");
    printf("用法:\n");
    printf("  %s list-models\n", prog);
    printf("  %s discover\n", prog);
    printf("  %s get  <IP> <TOKEN> --siid N --piid N\n", prog);
    printf("  %s set  <IP> <TOKEN> --siid N --piid N --value V\n", prog);
}

/** 打印硬编码设备模型表 */
static void cmd_list_models(void)
{
    printf("═ %-30s %-28s %-6s %-6s %-8s %-8s\n",
           "型号", "名称", "SIID", "PIID", "PIID_mode", "PIID_val");
    printf("─ %.0s\n", "─────────────────────────────────────────────────────────────────");
    for (int i = 0; i < MODEL_COUNT; i++) {
        const DeviceModel *d = &g_models[i];
        printf("  %-30s %-28s ", d->model, d->name);
        if (d->siid_on < 0)
            printf("%-6s %-6s %-8s %-8s", "-", "-", "-", "-");
        else
            printf("%-6d %-6d %-8d %-8d",
                   d->siid_on, d->piid_on, d->piid_mode, d->piid_val1);
        printf("\n");
    }
    printf("共 %d 个设备型号\n", MODEL_COUNT);
}

/** 发现局域网内 miIO 设备 */
static void cmd_discover(void)
{
    MiioDevice devices[MAX_DEVICE_COUNT];
    int count = miio_discover(devices, MAX_DEVICE_COUNT, 5);
    if (count < 0) {
        fprintf(stderr, "[错误] 发现设备失败\n");
        return;
    }
    if (count == 0) {
        printf("未发现设备\n");
        return;
    }
    printf("发现 %d 台设备:\n", count);
    printf("  %-16s %-6s %-12s %-36s %-20s\n",
           "IP", "端口", "DeviceID", "Model", "Token");
    for (int i = 0; i < count; i++) {
        MiioDevice *d = &devices[i];
        printf("  %-16s %-6u %-12u %-36s %-20s\n",
               d->ip, d->port, d->device_id, d->model, d->token);
    }
}

/**
 * 构建 miOT get_properties JSON 字符串
 *
 * 示例: {"method":"get_properties","params":[{"did":"auto","siid":2,"piid":1}]}
 */
static char *build_get_props(int siid, int piid)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "method", "get_properties");

    cJSON *params = cJSON_CreateArray();
    cJSON *item   = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "did",  "auto");
    cJSON_AddNumberToObject(item, "siid", siid);
    cJSON_AddNumberToObject(item, "piid", piid);
    cJSON_AddItemToArray(params, item);
    cJSON_AddItemToObject(root, "params", params);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

/**
 * 构建 miOT set_properties JSON 字符串
 *
 * 示例: {"method":"set_properties","params":[{"did":"auto","siid":2,"piid":1,"value":true}]}
 */
static char *build_set_props(int siid, int piid, const char *value_str)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "method", "set_properties");

    cJSON *params = cJSON_CreateArray();
    cJSON *item   = cJSON_CreateObject();
    cJSON_AddStringToObject(item, "did",  "auto");
    cJSON_AddNumberToObject(item, "siid", siid);
    cJSON_AddNumberToObject(item, "piid", piid);

    /* 尝试解析为整数或布尔值 */
    long lval = 0;
    int is_bool = 0, bval = 0;
    if (strcmp(value_str, "true") == 0)      { is_bool = 1; bval = 1; }
    else if (strcmp(value_str, "false") == 0) { is_bool = 1; bval = 0; }
    else if (strcmp(value_str, "on") == 0)    { is_bool = 1; bval = 1; }
    else if (strcmp(value_str, "off") == 0)   { is_bool = 1; bval = 0; }
    else { lval = strtol(value_str, NULL, 0); }

    if (is_bool)
        cJSON_AddBoolToObject(item, "value", bval);
    else
        cJSON_AddNumberToObject(item, "value", (double)lval);

    cJSON_AddItemToArray(params, item);
    cJSON_AddItemToObject(root, "params", params);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}

/**
 * 发送 miOT 命令并打印结果
 *
 * 构建 JSON-RPC，通过 miIO 传输层发送（miio_send_command 内部
 * 完成加密/解密），然后解析返回的 properties 数组。
 */
static int send_miot_command(const char *ip, const char *token,
                             const char *method, const char *params)
{
    MiioResponse resp;
    memset(&resp, 0, sizeof(resp));

    printf("[发送] %s %s\n", method, params);

    int ret = miio_send_command(ip, MIIO_PORT, token,
                                method, params,
                                1, &resp, 5);
    if (ret != 0) {
        fprintf(stderr, "[错误] 发送命令失败\n");
        return -1;
    }

    /* 检查远端是否返回错误 */
    if (resp.error_code != 0) {
        fprintf(stderr, "[错误] 设备返回错误 code=%d: %s\n",
                resp.error_code, resp.error_msg);
        if (resp.result_json) free(resp.result_json);
        return -1;
    }

    /* 解析 result_json（JSON 字符串） */
    if (!resp.result_json || resp.result_json[0] == '\0') {
        printf("[响应] 空\n");
        return 0;
    }

    cJSON *root = cJSON_Parse(resp.result_json);
    if (!root) {
        fprintf(stderr, "[错误] 无法解析响应 JSON: %s\n", resp.result_json);
        free(resp.result_json);
        return -1;
    }

    printf("[响应] %s\n", cJSON_Print(root));

    /* 尝试解析为 properties 数组并友好打印 */
    cJSON *props = NULL;
    if (cJSON_IsArray(root)) {
        props = root;
    } else {
        props = cJSON_GetObjectItem(root, "result");
        if (props && cJSON_IsArray(props)) { /* 已指向数组 */ }
        else { props = NULL; }
    }

    if (props && cJSON_IsArray(props)) {
        printf("── 属性详情 ──\n");
        int n = cJSON_GetArraySize(props);
        for (int i = 0; i < n; i++) {
            cJSON *elem = cJSON_GetArrayItem(props, i);
            int   siid = cJSON_GetObjectItem(elem, "siid")->valueint;
            int   piid = cJSON_GetObjectItem(elem, "piid")->valueint;
            int   code = cJSON_GetObjectItem(elem, "code")->valueint;
            if (code != 0) {
                printf("  SIID=%d PIID=%d  code=%d (失败)\n", siid, piid, code);
                continue;
            }
            cJSON *val = cJSON_GetObjectItem(elem, "value");
            char *vs   = cJSON_PrintUnformatted(val);
            printf("  SIID=%d PIID=%d  value=%s\n", siid, piid, vs);
            free(vs);
        }
    }

    cJSON_Delete(root);
    free(resp.result_json);
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 * main — 子命令路由
 * ═══════════════════════════════════════════════════════════ */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    /* ---- list-models ---- */
    if (strcmp(cmd, "list-models") == 0) {
        cmd_list_models();
        return 0;
    }

    /* ---- discover ---- */
    if (strcmp(cmd, "discover") == 0) {
        cmd_discover();
        return 0;
    }

    /* ---- get / set ---- */
    int siid = -1, piid = -1;
    const char *value_str = NULL;

    if (strcmp(cmd, "get") == 0 || strcmp(cmd, "set") == 0) {
        if (argc < 4) {
            fprintf(stderr, "[错误] 用法: %s %s <IP> <TOKEN> --siid N --piid N "
                    "[--value V]\n", argv[0], cmd);
            return 1;
        }
        const char *ip    = argv[2];
        const char *token = argv[3];

        /* 用 getopt_long 解析可选参数 */
        static struct option long_opts[] = {
            {"siid",  required_argument, 0, 's'},
            {"piid",  required_argument, 0, 'p'},
            {"value", required_argument, 0, 'v'},
            {"help",  no_argument,       0, 'h'},
            {0, 0, 0, 0}
        };
        optind = 4;  /* 跳过 argv[0..3] */
        int opt;
        while ((opt = getopt_long(argc, argv, "", long_opts, NULL)) != -1) {
            switch (opt) {
            case 's': siid = atoi(optarg); break;
            case 'p': piid = atoi(optarg); break;
            case 'v': value_str = optarg;  break;
            case 'h': usage(argv[0]); return 0;
            default:  usage(argv[0]); return 1;
            }
        }

        if (siid < 0 || piid < 0) {
            fprintf(stderr, "[错误] 必须指定 --siid 和 --piid\n");
            return 1;
        }

        if (strcmp(cmd, "set") == 0 && value_str == NULL) {
            fprintf(stderr, "[错误] set 命令必须指定 --value\n");
            return 1;
        }

        char *json;
        if (strcmp(cmd, "get") == 0) {
            json = build_get_props(siid, piid);
        } else {
            json = build_set_props(siid, piid, value_str);
        }
        if (!json) {
            fprintf(stderr, "[错误] 构建 JSON 失败\n");
            return 1;
        }

        int ret = send_miot_command(ip, token, cmd, json);
        free(json);
        return ret;
    }

    /* 未知命令 */
    fprintf(stderr, "[错误] 未知命令: %s\n", cmd);
    usage(argv[0]);
    return 1;
}
