/**
 * 08_c_demo - Home Assistant REST API 客户端
 *
 * 通过 HTTP REST API 与 Home Assistant 交互。
 * 使用 libcurl 发送 HTTP 请求，支持 JSON 响应解析。
 *
 * 支持子命令:
 *   states              获取所有实体状态
 *   get <ENTITY_ID>     获取指定实体状态
 *   call <DOMAIN> <SERVICE> <ENTITY> [--data JSON]
 *                       调用服务 (如 light.turn_on)
 *   history <ENTITY>    获取实体历史数据
 *   config              获取 HA 配置信息
 *
 * 参数:
 *   -t, --token <TOKEN>   长寿命访问令牌 (必须)
 *   -H, --host <URL>      HA 地址 (默认: http://localhost:8123)
 *
 * 编译: make
 * 运行: ./ha_client -t "eyJ..." states
 *       ./ha_client -t "eyJ..." get light.living_room
 *       ./ha_client -t "eyJ..." call light turn_on light.living_room --data '{"brightness":255}'
 *       ./ha_client -t "eyJ..." -H http://192.168.1.100:8123 states
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <curl/curl.h>

/* ========== 配置常量 ========== */

#define HA_DEFAULT_URL  "http://localhost:8123"
#define API_BASE        "/api"
#define MAX_URL_LEN     512
#define MAX_TOKEN_LEN   1024
#define MAX_RESP_SIZE   (1024 * 1024)  /* 1MB 响应缓冲 */

/* ========== 全局状态 ========== */

static char g_token[MAX_TOKEN_LEN] = {0};   /* HA 访问令牌 */
static char g_base_url[MAX_URL_LEN] = {0}; /* HA 基础 URL */

/* ========== CURL 回调 ========== */

/**
 * libcurl 写回调: 将响应数据追加到缓冲区
 */
typedef struct {
    char *data;
    size_t size;
} curl_response_t;

static size_t curl_write_cb(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t total_size = size * nmemb;
    curl_response_t *resp = (curl_response_t *)userp;

    if (resp->size + total_size + 1 > MAX_RESP_SIZE) {
        fprintf(stderr, "[!] 响应过大，已截断\n");
        return total_size;
    }

    memcpy(resp->data + resp->size, contents, total_size);
    resp->size += total_size;
    resp->data[resp->size] = '\0';
    return total_size;
}

/* ========== HTTP 请求函数 ========== */

/**
 * 初始化 CURL 全局状态
 */
static bool curl_global_init_once(void)
{
    static bool initialized = false;
    if (!initialized) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
            fprintf(stderr, "[!] CURL 初始化失败\n");
            return false;
        }
        initialized = true;
    }
    return true;
}

/**
 * 执行 HTTP GET 请求
 * path: API 路径 (如 "/api/states")
 * 返回: 响应字符串 (需调用者 free)，失败返回 NULL
 */
static char *ha_get(const char *path)
{
    if (!curl_global_init_once()) return NULL;

    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), "%s%s", g_base_url, path);

    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    curl_response_t resp = {0};
    resp.data = calloc(1, MAX_RESP_SIZE);
    if (!resp.data) { curl_easy_cleanup(curl); return NULL; }

    /* 设置请求头 */
    struct curl_slist *headers = NULL;
    char auth_header[MAX_TOKEN_LEN + 32];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", g_token);
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "[!] 请求失败: %s\n", curl_easy_strerror(res));
        free(resp.data);
        return NULL;
    }

    if (http_code != 200) {
        fprintf(stderr, "[!] HTTP 错误: %ld\n", http_code);
        if (resp.size > 0) fprintf(stderr, "[!] 响应: %s\n", resp.data);
        free(resp.data);
        return NULL;
    }

    return resp.data;
}

/**
 * 执行 HTTP POST 请求
 * path: API 路径
 * json_data: POST 的 JSON 数据 (可为 NULL)
 * 返回: 响应字符串 (需调用者 free)，失败返回 NULL
 */
static char *ha_post(const char *path, const char *json_data)
{
    if (!curl_global_init_once()) return NULL;

    char url[MAX_URL_LEN];
    snprintf(url, sizeof(url), "%s%s", g_base_url, path);

    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    curl_response_t resp = {0};
    resp.data = calloc(1, MAX_RESP_SIZE);
    if (!resp.data) { curl_easy_cleanup(curl); return NULL; }

    /* 设置请求头 */
    struct curl_slist *headers = NULL;
    char auth_header[MAX_TOKEN_LEN + 32];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", g_token);
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data ? json_data : "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "[!] 请求失败: %s\n", curl_easy_strerror(res));
        free(resp.data);
        return NULL;
    }

    if (http_code != 200 && http_code != 201) {
        fprintf(stderr, "[!] HTTP 错误: %ld\n", http_code);
        if (resp.size > 0) fprintf(stderr, "[!] 响应: %s\n", resp.data);
        free(resp.data);
        return NULL;
    }

    return resp.data;
}

/* ========== 简单 JSON 格式化输出 ========== */

/**
 * 简单打印响应 JSON (直接输出，不做深度格式化)
 * 对于 states 列表，提取 entity_id 和 state 做摘要
 */
static void print_states_summary(const char *json)
{
    /* 查找所有 "entity_id" 和 "state" 对 */
    printf("══════════════════════════════════════════════════════════\n");

    const char *ptr = json;
    int count = 0;
    while (count < 200) {
        const char *eid = strstr(ptr, "\"entity_id\"");
        if (!eid) break;

        /* 提取 entity_id 值 */
        const char *eid_colon = strchr(eid + 10, ':');
        if (!eid_colon) break;
        const char *eid_start = strchr(eid_colon, '"');
        if (!eid_start) break;
        eid_start++;
        const char *eid_end = strchr(eid_start, '"');
        if (!eid_end) break;

        /* 查找紧跟的 "state" 字段 */
        const char *state = strstr(eid_end, "\"state\"");
        const char *state_val = "unknown";
        char state_buf[128] = "unknown";

        if (state && state - eid_end < 500) {
            const char *s_colon = strchr(state + 6, ':');
            if (s_colon) {
                const char *s_start = strchr(s_colon, '"');
                if (s_start) {
                    s_start++;
                    const char *s_end = strchr(s_start, '"');
                    if (s_end && (s_end - s_start) < 120) {
                        int slen = (int)(s_end - s_start);
                        strncpy(state_buf, s_start, slen);
                        state_buf[slen] = '\0';
                        state_val = state_buf;
                    }
                }
            }
        }

        printf("  %-45s → %s\n", eid_start, state_val);
        count++;
        ptr = eid_end + 1;
    }

    printf("══════════════════════════════════════════════════════════\n");
    printf("[*] 共 %d 个实体\n", count);
}

/* ========== 子命令实现 ========== */

/**
 * states - 获取所有实体状态
 */
static int cmd_states(void)
{
    printf("[*] 获取所有实体状态...\n");
    char *resp = ha_get(API_BASE "/states");
    if (!resp) return -1;

    print_states_summary(resp);
    free(resp);
    return 0;
}

/**
 * get <ENTITY_ID> - 获取指定实体状态
 */
static int cmd_get(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "[!] 用法: %s get <ENTITY_ID>\n", argv[0]);
        return 1;
    }

    char path[MAX_URL_LEN];
    snprintf(path, sizeof(path), API_BASE "/states/%s", argv[2]);

    printf("[*] 获取实体 [%s] 状态...\n", argv[2]);
    char *resp = ha_get(path);
    if (!resp) return -1;

    printf("[*] 实体状态:\n");
    printf("══════════════════════════════════════════════════════════\n");
    printf("%s\n", resp);
    printf("══════════════════════════════════════════════════════════\n");
    free(resp);
    return 0;
}

/**
 * call <DOMAIN> <SERVICE> <ENTITY> [--data JSON]
 * 调用 HA 服务
 */
static int cmd_call(int argc, char *argv[])
{
    if (argc < 5) {
        fprintf(stderr, "[!] 用法: %s call <DOMAIN> <SERVICE> <ENTITY> [--data JSON]\n", argv[0]);
        fprintf(stderr, "[!] 示例: %s call light turn_on light.lamp --json '{\"brightness\":255}'\n", argv[0]);
        return 1;
    }

    const char *domain = argv[2];
    const char *service = argv[3];
    const char *entity = argv[4];
    const char *data = "{}";

    /* 解析可选的 --data 参数 */
    for (int i = 5; i < argc - 1; i++) {
        if (strcmp(argv[i], "--data") == 0) {
            data = argv[i + 1];
            break;
        }
        if (strcmp(argv[i], "--json") == 0) {
            data = argv[i + 1];
            break;
        }
    }

    char path[MAX_URL_LEN];
    snprintf(path, sizeof(path), API_BASE "/services/%s/%s", domain, service);

    /* 构建请求 JSON: {"entity_id": "xxx", ...data} */
    char payload[2048];
    snprintf(payload, sizeof(payload), "{\"entity_id\":\"%s\"", entity);

    /* 如果 data 不只是 "{}"，合并额外字段 */
    if (strlen(data) > 2) {
        /* 简单拼接: 去掉 data 的首尾花括号 */
        const char *inner = data;
        while (*inner == ' ' || *inner == '{') inner++;
        char inner_copy[1024];
        strncpy(inner_copy, inner, sizeof(inner_copy) - 1);
        inner_copy[sizeof(inner_copy) - 1] = '\0';
        size_t ilen = strlen(inner_copy);
        while (ilen > 0 && (inner_copy[ilen - 1] == ' ' || inner_copy[ilen - 1] == '}'))
            inner_copy[--ilen] = '\0';

        if (ilen > 0) {
            strncat(payload, ", ", sizeof(payload) - strlen(payload) - 1);
            strncat(payload, inner_copy, sizeof(payload) - strlen(payload) - 1);
        }
    }
    strncat(payload, "}", sizeof(payload) - strlen(payload) - 1);

    printf("[*] 调用服务: %s.%s\n", domain, service);
    printf("[*] 目标实体: %s\n", entity);
    printf("[*] 请求数据: %s\n", payload);

    char *resp = ha_post(path, payload);
    if (resp) {
        printf("[*] 响应: %s\n", resp);
        free(resp);
    } else {
        fprintf(stderr, "[!] 服务调用失败\n");
        return -1;
    }
    return 0;
}

/**
 * history <ENTITY> - 获取实体历史数据
 */
static int cmd_history(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "[!] 用法: %s history <ENTITY_ID>\n", argv[0]);
        return 1;
    }

    char path[MAX_URL_LEN];
    /* HA history API: /api/history/period/<start_time>?filter_entity_id=<entity> */
    /* 简化: 使用最近 1 天的数据 */
    snprintf(path, sizeof(path),
             API_BASE "/history/period/1970-01-01T00:00:00?filter_entity_id=%s",
             argv[2]);

    printf("[*] 获取实体 [%s] 历史数据...\n", argv[2]);
    char *resp = ha_get(path);
    if (!resp) return -1;

    /* 历史数据可能很长，只打印前 2000 字符 */
    size_t len = strlen(resp);
    printf("[*] 历史数据 (共 %zu 字节):\n", len);
    printf("══════════════════════════════════════════════════════════\n");
    if (len > 2000) {
        fwrite(resp, 1, 2000, stdout);
        printf("\n... (已截断，共 %zu 字节)\n", len);
    } else {
        printf("%s\n", resp);
    }
    printf("══════════════════════════════════════════════════════════\n");
    free(resp);
    return 0;
}

/**
 * config - 获取 Home Assistant 配置信息
 */
static int cmd_config(void)
{
    printf("[*] 获取 Home Assistant 配置...\n");
    char *resp = ha_get(API_BASE "/config");
    if (!resp) return -1;

    printf("[*] HA 配置:\n");
    printf("══════════════════════════════════════════════════════════\n");

    /* 提取关键配置字段 */
    const char *fields[] = {
        "location_name", "latitude", "longitude",
        "elevation", "unit_system", "time_zone",
        "version", "configuration_url", NULL
    };

    for (int i = 0; fields[i]; i++) {
        char search[64];
        snprintf(search, sizeof(search), "\"%s\"", fields[i]);
        const char *pos = strstr(resp, search);
        if (pos) {
            const char *colon = strchr(pos + strlen(search), ':');
            if (colon) {
                /* 跳过冒号和空白 */
                const char *val = colon + 1;
                while (*val == ' ' || *val == '\t') val++;

                char val_buf[256] = {0};
                if (*val == '"') {
                    /* 字符串值 */
                    val++;
                    const char *end = strchr(val, '"');
                    if (end && (end - val) < 250) {
                        int vlen = (int)(end - val);
                        strncpy(val_buf, val, vlen);
                    }
                } else {
                    /* 数值或其他 */
                    const char *end = val;
                    while (*end && *end != ',' && *end != '\n' && *end != '}')
                        end++;
                    int vlen = (int)(end - val);
                    if (vlen > 250) vlen = 250;
                    strncpy(val_buf, val, vlen);
                }
                printf("  %-22s %s\n", fields[i], val_buf);
            }
        }
    }

    printf("══════════════════════════════════════════════════════════\n");
    free(resp);
    return 0;
}

/* ========== 参数解析 ========== */

/**
 * 解析命令行参数，提取 token 和 host
 */
static int parse_args(int argc, char *argv[])
{
    /* 设置默认值 */
    strncpy(g_base_url, HA_DEFAULT_URL, sizeof(g_base_url) - 1);

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--token") == 0) && i + 1 < argc) {
            strncpy(g_token, argv[++i], MAX_TOKEN_LEN - 1);
        }
        else if ((strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "--host") == 0) && i + 1 < argc) {
            strncpy(g_base_url, argv[++i], MAX_URL_LEN - 1);
        }
    }

    /* 去掉 base_url 末尾的斜杠 */
    size_t blen = strlen(g_base_url);
    while (blen > 0 && g_base_url[blen - 1] == '/')
        g_base_url[--blen] = '\0';

    if (g_token[0] == '\0') {
        fprintf(stderr, "[!] 错误: 未提供访问令牌\n");
        fprintf(stderr, "[!] 请使用 -t <TOKEN> 或设置 --token 参数\n");
        fprintf(stderr, "[!] 在 HA 中创建令牌: 个人资料 → 长寿命访问令牌\n");
        return -1;
    }

    return 0;
}

/* ========== 主函数 ========== */

static void print_usage(const char *prog)
{
    printf("用法: %s -t <TOKEN> [-H <URL>] <命令> [参数]\n\n", prog);
    printf("参数:\n");
    printf("  -t, --token <TOKEN>   Home Assistant 长寿命访问令牌 (必须)\n");
    printf("  -H, --host <URL>      HA 地址 (默认: %s)\n\n", HA_DEFAULT_URL);
    printf("命令:\n");
    printf("  states              获取所有实体状态\n");
    printf("  get <ENTITY_ID>     获取指定实体状态\n");
    printf("  call <DOMAIN> <SERVICE> <ENTITY> [--data JSON]\n");
    printf("                      调用服务\n");
    printf("  history <ENTITY>    获取实体历史数据\n");
    printf("  config              获取 HA 配置信息\n");
    printf("\n示例:\n");
    printf("  %s -t 'eyJ...' states\n", prog);
    printf("  %s -t 'eyJ...' get light.living_room\n", prog);
    printf("  %s -t 'eyJ...' call light turn_on light.lamp --data '{\"brightness\":255}'\n", prog);
    printf("  %s -t 'eyJ...' -H http://192.168.1.100:8123 config\n", prog);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    /* 解析全局参数 (token, host) */
    if (parse_args(argc, argv) != 0) return 1;

    /* 查找子命令 (第一个非 - 开头的参数) */
    const char *cmd = NULL;
    int cmd_idx = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            cmd = argv[i];
            cmd_idx = i;
            break;
        }
    }

    if (!cmd) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(cmd, "states") == 0)
        return cmd_states();
    else if (strcmp(cmd, "get") == 0)
        return cmd_get(argc - cmd_idx, argv + cmd_idx);
    else if (strcmp(cmd, "call") == 0)
        return cmd_call(argc - cmd_idx, argv + cmd_idx);
    else if (strcmp(cmd, "history") == 0)
        return cmd_history(argc - cmd_idx, argv + cmd_idx);
    else if (strcmp(cmd, "config") == 0)
        return cmd_config();
    else {
        fprintf(stderr, "[!] 未知命令: %s\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
