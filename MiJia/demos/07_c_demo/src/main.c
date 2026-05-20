/**
 * 07_c_demo - Zigbee2MQTT 桥接工具
 *
 * 通过 MQTT 与 Zigbee2MQTT 服务通信，控制 Zigbee 子设备。
 * 使用 popen 调用 mosquitto_pub / mosquitto_sub 命令行工具。
 *
 * 支持子命令:
 *   devices                        - 获取桥接设备列表
 *   get <FRIENDLY_NAME>            - 获取设备状态
 *   set <FRIENDLY_NAME> --json J   - 设置设备状态
 *   remove <FRIENDLY_NAME>         - 移除设备
 *   permit-join [true|false]       - 开启/关闭配对
 *   health                         - 检查 Z2M 服务健康状态
 *
 * 环境变量:
 *   Z2M_HOST       MQTT 服务器地址 (默认: localhost)
 *   Z2M_PORT       MQTT 端口 (默认: 1883)
 *   Z2M_PREFIX     主题前缀 (默认: zigbee2mqtt)
 *
 * 编译: make
 * 运行: ./z2m_bridge devices
 *       ./z2m_bridge set my_lamp --json '{"state":"ON","brightness":255}'
 *       Z2M_HOST=192.168.1.100 ./z2m_bridge devices
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* ========== 配置 ========== */

#define Z2M_DEFAULT_HOST    "localhost"
#define Z2M_DEFAULT_PORT    "1883"
#define Z2M_DEFAULT_PREFIX  "zigbee2mqtt"

/* 命令缓冲区大小 */
#define CMD_BUF_SIZE 1024
/* 读取响应缓冲区大小 */
#define RESP_BUF_SIZE 8192

/* ========== 工具函数 ========== */

/**
 * 获取配置值: 优先环境变量，其次默认值
 */
static const char *get_config(const char *env_name, const char *default_val)
{
    const char *val = getenv(env_name);
    return (val && val[0]) ? val : default_val;
}

/**
 * 获取 MQTT 连接参数字符串: "-h HOST -p PORT"
 */
static void get_mqtt_args(char *buf, int buf_size)
{
    snprintf(buf, buf_size, "-h %s -p %s",
             get_config("Z2M_HOST", Z2M_DEFAULT_HOST),
             get_config("Z2M_PORT", Z2M_DEFAULT_PORT));
}

/**
 * 获取主题前缀
 */
static const char *get_prefix(void)
{
    return get_config("Z2M_PREFIX", Z2M_DEFAULT_PREFIX);
}

/**
 * 执行 mosquitto_pub 命令并读取响应
 * topic: MQTT 主题
 * payload: 发送的消息 (可为 NULL，仅订阅)
 * timeout: 等待响应的超时秒数 (用于 mosquitto_sub)
 *
 * 返回: 读取到的响应字符串 (需调用者 free)，失败返回 NULL
 */
static char *mqtt_request(const char *topic, const char *payload, int timeout)
{
    char mqtt_args[128];
    get_mqtt_args(mqtt_args, sizeof(mqtt_args));

    char cmd[CMD_BUF_SIZE];
    char resp_file[64];

    /* 使用临时文件捕获响应 */
    snprintf(resp_file, sizeof(resp_file), "/tmp/z2m_resp_%d", getpid());

    if (payload) {
        /* 发布消息并等待响应 (使用 mosquitto_sub 短暂订阅) */
        snprintf(cmd, sizeof(cmd),
                 "timeout %d mosquitto_sub %s -t '%s' -C 1 -W %d > %s 2>/dev/null & "
                 "sleep 0.5 && "
                 "mosquitto_pub %s -t '%s' -m '%s' 2>/dev/null && "
                 "wait",
                 timeout + 2, mqtt_args, topic, timeout, resp_file,
                 mqtt_args, topic, payload);
    } else {
        /* 仅订阅 (用于一次性读取) */
        snprintf(cmd, sizeof(cmd),
                 "timeout %d mosquitto_sub %s -t '%s' -C 1 -W %d > %s 2>/dev/null",
                 timeout, mqtt_args, topic, timeout, resp_file);
    }

    int ret = system(cmd);
    (void)ret;

    /* 读取响应文件 */
    FILE *fp = fopen(resp_file, "r");
    if (!fp) return NULL;

    char *response = calloc(1, RESP_BUF_SIZE);
    if (!response) { fclose(fp); unlink(resp_file); return NULL; }

    size_t total = 0;
    size_t n;
    while ((n = fread(response + total, 1, RESP_BUF_SIZE - total - 1, fp)) > 0) {
        total += n;
        if (total >= RESP_BUF_SIZE - 1) break;
    }
    fclose(fp);
    unlink(resp_file);

    /* 去除尾部空白 */
    while (total > 0 && (response[total - 1] == '\n' || response[total - 1] == '\r'))
        response[--total] = '\0';

    if (total == 0) {
        free(response);
        return NULL;
    }
    return response;
}

/**
 * 仅发布消息 (无需等待响应)
 */
static int mqtt_publish(const char *topic, const char *payload)
{
    char mqtt_args[128];
    get_mqtt_args(mqtt_args, sizeof(mqtt_args));

    char cmd[CMD_BUF_SIZE];
    snprintf(cmd, sizeof(cmd), "mosquitto_pub %s -t '%s' -m '%s' 2>/dev/null",
             mqtt_args, topic, payload);
    return system(cmd);
}

/**
 * 美化打印 JSON (简单缩进，不依赖 cJSON)
 * 对于长 JSON 字符串直接输出，短字符串也直接输出
 */
static void print_json(const char *json)
{
    /* 简单输出，不做格式化 */
    if (strlen(json) > 200) {
        /* 长输出分段打印 */
        printf("%s\n", json);
    } else {
        printf("  %s\n", json);
    }
}

/* ========== 子命令实现 ========== */

/**
 * devices - 获取桥接设备列表
 * 通过 zigbee2mqtt/bridge/devices 主题获取
 */
static int cmd_devices(void)
{
    const char *prefix = get_prefix();
    char topic[256];
    snprintf(topic, sizeof(topic), "%s/bridge/devices", prefix);

    printf("[*] 正在获取设备列表...\n");

    char *resp = mqtt_request(topic, NULL, 5);
    if (!resp) {
        fprintf(stderr, "[!] 获取设备列表失败，请检查:\n");
        fprintf(stderr, "    1. Zigbee2MQTT 是否运行\n");
        fprintf(stderr, "    2. mosquitto 是否安装 (apt install mosquitto-clients)\n");
        fprintf(stderr, "    3. MQTT 连接配置是否正确\n");
        return -1;
    }

    printf("[*] 设备列表:\n");
    printf("══════════════════════════════════════════════════════════\n");

    /* 简单提取设备信息并打印 */
    const char *ptr = resp;
    const char *friendly = NULL;

    /* 查找每个 "friendly_name" 字段 */
    while ((friendly = strstr(ptr, "\"friendly_name\"")) != NULL) {
        /* 查找对应的值 */
        const char *colon = strchr(friendly, ':');
        if (!colon) break;
        const char *start = strchr(colon, '"');
        if (!start) break;
        start++;
        const char *end = strchr(start, '"');
        if (!end) break;

        /* 查找关联的 ieeeAddr */
        const char *after = friendly;
        const char *ieee = strstr(after, "\"ieeeAddr\"");
        char ieee_str[32] = "N/A";

        if (ieee && ieee < end + 200) {
            const char *ic = strchr(ieee, ':');
            if (ic) {
                const char *is = strchr(ic, '"');
                if (is) {
                    is++;
                    const char *ie = strchr(is, '"');
                    if (ie && (ie - is) < 30) {
                        int ilen = (int)(ie - is);
                        strncpy(ieee_str, is, ilen);
                        ieee_str[ilen] = '\0';
                    }
                }
            }
        }

        printf("  设备: %-30s  IEEE: %s\n", start, ieee_str);
        ptr = end + 1;
    }

    printf("══════════════════════════════════════════════════════════\n");
    free(resp);
    return 0;
}

/**
 * get <FRIENDLY_NAME> - 获取设备状态
 */
static int cmd_get(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "[!] 用法: %s get <FRIENDLY_NAME>\n", argv[0]);
        return 1;
    }

    const char *prefix = get_prefix();
    char topic[256];
    snprintf(topic, sizeof(topic), "%s/%s/get", prefix, argv[2]);

    /* 发送空 JSON 触发状态上报 */
    printf("[*] 获取设备 [%s] 状态...\n", argv[2]);
    int ret = mqtt_publish(topic, "");
    (void)ret;

    /* 同时订阅设备状态主题 */
    char state_topic[256];
    snprintf(state_topic, sizeof(state_topic), "%s/%s", prefix, argv[2]);

    char *resp = mqtt_request(state_topic, NULL, 3);
    if (resp) {
        printf("[*] 状态: %s\n", resp);
        free(resp);
    } else {
        printf("[*] 已发送获取请求，请查看 %s 主题消息\n", state_topic);
    }
    return 0;
}

/**
 * set <FRIENDLY_NAME> --json '{"state":"ON"}' - 设置设备状态
 */
static int cmd_set(int argc, char *argv[])
{
    if (argc < 5 || strcmp(argv[3], "--json") != 0) {
        fprintf(stderr, "[!] 用法: %s set <FRIENDLY_NAME> --json <JSON>\n", argv[0]);
        return 1;
    }

    const char *prefix = get_prefix();
    char topic[256];
    snprintf(topic, sizeof(topic), "%s/%s/set", prefix, argv[2]);

    printf("[*] 设置设备 [%s]: %s\n", argv[2], argv[4]);
    int ret = mqtt_publish(topic, argv[4]);
    if (ret == 0) {
        printf("[*] 命令已发送\n");
    } else {
        fprintf(stderr, "[!] 发送失败\n");
    }
    return (ret == 0) ? 0 : -1;
}

/**
 * remove <FRIENDLY_NAME> - 从网络中移除设备
 */
static int cmd_remove(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "[!] 用法: %s remove <FRIENDLY_NAME>\n", argv[0]);
        return 1;
    }

    const char *prefix = get_prefix();
    char topic[256];
    snprintf(topic, sizeof(topic), "%s/bridge/config/remove", prefix);

    /* 移除需要 JSON 格式 */
    char payload[512];
    snprintf(payload, sizeof(payload), "{\"friendly_name\":\"%s\"}", argv[2]);

    printf("[*] 移除设备 [%s]...\n", argv[2]);
    int ret = mqtt_publish(topic, payload);

    char state_topic[256];
    snprintf(state_topic, sizeof(state_topic), "%s/bridge/response/remove", prefix);
    char *resp = mqtt_request(state_topic, NULL, 5);

    if (resp) {
        printf("[*] 响应: %s\n", resp);
        free(resp);
    } else {
        printf("[*] 移除请求已发送\n");
    }
    return (ret == 0) ? 0 : -1;
}

/**
 * permit-join [true|false] - 开启/关闭配对
 */
static int cmd_permit_join(int argc, char *argv[])
{
    const char *state = (argc >= 3) ? argv[2] : "true";

    if (strcmp(state, "true") != 0 && strcmp(state, "false") != 0) {
        fprintf(stderr, "[!] 用法: %s permit-join [true|false]\n", argv[0]);
        return 1;
    }

    const char *prefix = get_prefix();
    char topic[256];
    snprintf(topic, sizeof(topic), "%s/bridge/config/permit_join", prefix);

    printf("[*] 设置允许配对: %s\n", state);
    int ret = mqtt_publish(topic, state);

    if (strcmp(state, "true") == 0) {
        printf("[*] 配对模式已开启 (默认 60 秒)\n");
        printf("[*] 请将 Zigbee 设备设置为配对模式\n");
    } else {
        printf("[*] 配对模式已关闭\n");
    }
    return (ret == 0) ? 0 : -1;
}

/**
 * health - 检查 Zigbee2MQTT 服务健康状态
 */
static int cmd_health(void)
{
    const char *prefix = get_prefix();
    char topic[256];
    snprintf(topic, sizeof(topic), "%s/bridge/info", prefix);

    printf("[*] 检查 Zigbee2MQTT 健康状态...\n");
    printf("  MQTT 服务器: %s:%s\n",
           get_config("Z2M_HOST", Z2M_DEFAULT_HOST),
           get_config("Z2M_PORT", Z2M_DEFAULT_PORT));
    printf("  主题前缀:   %s\n", prefix);

    char *resp = mqtt_request(topic, NULL, 3);
    if (resp) {
        printf("[*] Zigbee2MQTT 状态: 在线\n");
        printf("[*] 信息: %s\n", resp);
        free(resp);
    } else {
        printf("[!] Zigbee2MQTT 状态: 离线或不可达\n");
        return -1;
    }

    /* 检查 mosquitto-clients 是否安装 */
    if (system("which mosquitto_pub > /dev/null 2>&1") != 0) {
        printf("[!] 警告: 未找到 mosquitto_pub/mosquitto_sub\n");
        printf("[!] 请安装: sudo apt install mosquitto-clients\n");
    }
    return 0;
}

/* ========== 主函数 ========== */

static void print_usage(const char *prog)
{
    printf("用法: %s <命令> [参数]\n\n", prog);
    printf("命令:\n");
    printf("  devices                        获取设备列表\n");
    printf("  get <FRIENDLY_NAME>            获取设备状态\n");
    printf("  set <NAME> --json <JSON>       设置设备状态\n");
    printf("  remove <FRIENDLY_NAME>         移除设备\n");
    printf("  permit-join [true|false]       开启/关闭配对\n");
    printf("  health                         检查服务健康\n");
    printf("\n环境变量:\n");
    printf("  Z2M_HOST=%-20s MQTT 地址\n", Z2M_DEFAULT_HOST);
    printf("  Z2M_PORT=%-20s MQTT 端口\n", Z2M_DEFAULT_PORT);
    printf("  Z2M_PREFIX=%-18s 主题前缀\n", Z2M_DEFAULT_PREFIX);
    printf("\n示例:\n");
    printf("  %s devices\n", prog);
    printf("  %s set my_lamp --json '{\"state\":\"ON\"}'\n", prog);
    printf("  %s permit-join true\n", prog);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "devices") == 0)
        return cmd_devices();
    else if (strcmp(cmd, "get") == 0)
        return cmd_get(argc, argv);
    else if (strcmp(cmd, "set") == 0)
        return cmd_set(argc, argv);
    else if (strcmp(cmd, "remove") == 0)
        return cmd_remove(argc, argv);
    else if (strcmp(cmd, "permit-join") == 0)
        return cmd_permit_join(argc, argv);
    else if (strcmp(cmd, "health") == 0)
        return cmd_health();
    else {
        fprintf(stderr, "[!] 未知命令: %s\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
