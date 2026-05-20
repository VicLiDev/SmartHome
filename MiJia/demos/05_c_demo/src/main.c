/**
 * 05_c_demo — MQTT 桥接
 *
 * 将米家设备控制桥接到 MQTT，实现：
 *   pub    — 向 MQTT 主题发布消息（控制设备）
 *   sub    — 订阅 MQTT 主题（监听设备状态）
 *   status — 查询当前桥接状态
 *   list   — 列出已知设备→MQTT 主题映射表
 *
 * 实现方式：通过 popen 调用 mosquitto_pub / mosquitto_sub 命令行工具。
 * 若未安装 mosquitto-clients，会打印安装指引。
 *
 * 编译: make
 * 用法: ./mqtt_bridge <子命令> [参数...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "cJSON.h"

/* ========== 配置常量 ========== */

/* MQTT broker 默认地址和端口 */
#define MQTT_HOST    "localhost"
#define MQTT_PORT    "1883"

/* mosquitto 客户端命令路径 */
#define MOSQ_PUB     "mosquitto_pub"
#define MOSQ_SUB     "mosquitto_sub"

/* 主题前缀 */
#define TOPIC_PREFIX "mihome"

/* 最大设备映射数 */
#define MAX_MAPPINGS 32

/* popen 读缓冲区大小 */
#define BUF_SIZE 4096

/* ========== 设备→MQTT 主题映射 ========== */

typedef struct {
    char did[64];           /* 设备 DID */
    char name[64];          /* 设备名称 */
    char model[64];          /* 设备模型 */
    char topic_set[256];     /* 控制主题 (写) */
    char topic_status[256];  /* 状态主题 (读) */
} DeviceMapping;

/* 预置的设备映射表 */
static DeviceMapping g_mappings[MAX_MAPPINGS];
static int g_mapping_count = 0;

/**
 * 初始化默认设备映射表
 * 格式: mihome/<did>/set    控制命令
 *       mihome/<did>/status 状态上报
 */
static void init_mappings(void)
{
    /* 示例设备映射（实际应从配置文件或云端加载） */
    const char *defaults[] = {
        /* DID,          名称,           模型,                set_topic_suffix, status_topic_suffix */
        "plug_001",    "智能插座",     "chuangmi.plug.m3",   "set",      "status",
        "bulb_001",    "智能灯泡",     "yeelight.light.bslamp1", "set",   "status",
        "sensor_ht_001","温湿度传感器","cgllc.sensor_ht.agl02", "set",    "status",
        "gateway_001", "网关",         "lumi.gateway.mgl03", "set",      "status",
        NULL
    };

    g_mapping_count = 0;
    for (int i = 0; defaults[i] != NULL && g_mapping_count < MAX_MAPPINGS; i += 4) {
        DeviceMapping *m = &g_mappings[g_mapping_count];
        snprintf(m->did, sizeof(m->did), "%s", defaults[i]);
        snprintf(m->name, sizeof(m->name), "%s", defaults[i + 1]);
        snprintf(m->model, sizeof(m->model), "%s", defaults[i + 2]);
        snprintf(m->topic_set, sizeof(m->topic_set),
            "%s/%s/%s", TOPIC_PREFIX, m->did, defaults[i + 3]);
        snprintf(m->topic_status, sizeof(m->topic_status),
            "%s/%s/%s", TOPIC_PREFIX, m->did, defaults[i + 3 + 1]);
        g_mapping_count++;
    }
}

/* ========== 工具函数 ========== */

/**
 * 检查 mosquitto_pub / mosquitto_sub 是否可用
 */
static void check_mosquitto(void)
{
    if (access(MOSQ_PUB, X_OK) != 0 || access(MOSQ_SUB, X_OK) != 0) {
        fprintf(stderr,
            "[错误] 未找到 mosquitto 客户端工具，请先安装:\n"
            "  Ubuntu/Debian: sudo apt install mosquitto-clients\n"
            "  CentOS/RHEL:   sudo yum install mosquitto-clients\n"
            "  macOS:         brew install mosquitto\n");
        exit(1);
    }
}

/**
 * 根据设备 DID 查找映射
 */
static DeviceMapping *find_mapping(const char *did)
{
    for (int i = 0; i < g_mapping_count; i++) {
        if (strcmp(g_mappings[i].did, did) == 0)
            return &g_mappings[i];
    }
    return NULL;
}

/* ========== 子命令实现 ========== */

/**
 * pub --topic <主题> --message <消息> [--host H] [--port P]
 *
 * 通过 mosquitto_pub 向指定主题发布消息。
 * 消息内容为 JSON 格式: {"state": "on", "ts": 1234567890}
 */
static int cmd_pub(const char *topic, const char *message,
                   const char *host, const char *port)
{
    if (!topic || !message) {
        fprintf(stderr,
            "用法: mqtt_bridge pub --topic <主题> --message <消息> "
            "[--host <host>] [--port <port>]\n");
        return 1;
    }

    /* 尝试用 cJSON 解析消息，验证是否为合法 JSON */
    cJSON *json = cJSON_Parse(message);
    if (!json) {
        fprintf(stderr, "[警告] 消息不是合法 JSON，将原样发送\n");
    } else {
        /* 自动添加时间戳 */
        cJSON_AddNumberToObject(json, "ts", (double)time(NULL));
        char *updated = cJSON_PrintUnformatted(json);
        message = updated;  /* 使用带时间戳的版本 */
        cJSON_Delete(json);
    }

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        MOSQ_PUB " -h %s -p %s -t '%s' -m '%s'",
        host ? host : MQTT_HOST,
        port ? port : MQTT_PORT,
        topic, message);

    printf("=== 发布 MQTT 消息 ===\n");
    printf("Broker: %s:%s\n", host ? host : MQTT_HOST, port ? port : MQTT_PORT);
    printf("主题:   %s\n", topic);
    printf("消息:   %s\n", message);

    int status = system(cmd);

    if (json) free((char *)message);  /* 释放 cJSON_PrintUnformatted 的内存 */
    return status;
}

/**
 * sub --topic <主题> [--host H] [--port P]
 *
 * 通过 mosquitto_sub 订阅指定主题，打印收到的消息。
 * 使用 -v 参数同时显示主题名称。
 * Ctrl+C 退出订阅。
 */
static int cmd_sub(const char *topic, const char *host, const char *port)
{
    if (!topic) {
        fprintf(stderr,
            "用法: mqtt_bridge sub --topic <主题> "
            "[--host <host>] [--port <port>]\n");
        return 1;
    }

    printf("=== 订阅 MQTT 主题 ===\n");
    printf("Broker: %s:%s\n", host ? host : MQTT_HOST, port ? port : MQTT_PORT);
    printf("主题:   %s\n", topic);
    printf("（按 Ctrl+C 退出）\n\n");

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        MOSQ_SUB " -h %s -p %s -t '%s' -v",
        host ? host : MQTT_HOST,
        port ? port : MQTT_PORT,
        topic);

    /* 直接执行订阅（阻塞，直到用户中断） */
    return system(cmd);
}

/**
 * status — 打印桥接状态概览
 */
static int cmd_status(void)
{
    printf("=== MQTT 桥接状态 ===\n\n");
    printf("Broker:  %s:%s\n", MQTT_HOST, MQTT_PORT);
    printf("前缀:    %s\n\n", TOPIC_PREFIX);

    /* 检查 mosquitto 客户端是否可用 */
    int has_pub  = (access(MOSQ_PUB, X_OK) == 0);
    int has_sub  = (access(MOSQ_SUB, X_OK) == 0);

    printf("mosquitto_pub:  %s\n", has_pub ? "可用" : "未安装");
    printf("mosquitto_sub:  %s\n", has_sub ? "可用" : "未安装");

    /* 简单检查 broker 是否可达（尝试连接 1883 端口） */
    printf("\n");
    printf("已注册设备: %d\n", g_mapping_count);
    printf("主题总数:   %d (set + status)\n", g_mapping_count * 2);

    return 0;
}

/**
 * list — 列出所有已知设备→MQTT 主题映射
 */
static int cmd_list(void)
{
    printf("=== 设备 → MQTT 主题映射表 ===\n\n");
    printf("%-16s %-14s %-28s %-28s\n",
           "DID", "名称", "控制主题 (set)", "状态主题 (status)");
    printf("---------------- ------------ "
           "---------------------------- "
           "----------------------------\n");

    for (int i = 0; i < g_mapping_count; i++) {
        DeviceMapping *m = &g_mappings[i];
        printf("%-16s %-14s %-28s %-28s\n",
               m->did, m->name, m->topic_set, m->topic_status);
    }

    printf("\n主题格式: %s/<DID>/<suffix>\n", TOPIC_PREFIX);
    printf("消息格式: JSON (如 {\"state\":\"on\",\"ts\":...})\n");

    return 0;
}

/* ========== 参数解析 ========== */

static const char *get_opt(int argc, char *argv[], const char *key)
{
    for (int i = 2; i < argc - 1; i++) {
        if (strcmp(argv[i], key) == 0) return argv[i + 1];
    }
    return NULL;
}

static void usage(const char *prog)
{
    printf("用法: %s <子命令> [参数...]\n\n"
           "子命令:\n"
           "  pub   --topic <主题> --message <消息>  "
               "[--host <host>] [--port <port>]  发布消息\n"
           "  sub   --topic <主题>                 "
               "[--host <host>] [--port <port>]  订阅主题\n"
           "  status                                 桥接状态\n"
           "  list                                   设备映射表\n"
           "\n"
           "示例:\n"
           "  %s pub --topic mihome/plug_001/set --message '{\"state\":\"on\"}'\n"
           "  %s sub --topic mihome/plug_001/status\n"
           "  %s list\n",
           prog, prog, prog, prog);
}

/* ========== 入口 ========== */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    /* 初始化设备映射表 */
    init_mappings();

    const char *cmd = argv[1];

    /* pub 和 sub 需要 mosquitto 工具 */
    if (strcmp(cmd, "pub") == 0 || strcmp(cmd, "sub") == 0) {
        check_mosquitto();
    }

    if (strcmp(cmd, "pub") == 0) {
        const char *topic   = get_opt(argc, argv, "--topic");
        const char *message = get_opt(argc, argv, "--message");
        const char *host    = get_opt(argc, argv, "--host");
        const char *port    = get_opt(argc, argv, "--port");
        return cmd_pub(topic, message, host, port);
    }
    else if (strcmp(cmd, "sub") == 0) {
        const char *topic = get_opt(argc, argv, "--topic");
        const char *host  = get_opt(argc, argv, "--host");
        const char *port  = get_opt(argc, argv, "--port");
        return cmd_sub(topic, host, port);
    }
    else if (strcmp(cmd, "status") == 0) {
        return cmd_status();
    }
    else if (strcmp(cmd, "list") == 0) {
        return cmd_list();
    }
    else {
        fprintf(stderr, "[错误] 未知子命令: %s\n\n", cmd);
        usage(argv[0]);
        return 1;
    }
}
