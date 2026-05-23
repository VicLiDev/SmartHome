/**
 * ha_switch.c — 通过 Home Assistant REST API 控制开关
 *
 * 编译: gcc -std=c11 -Wall -Wextra -O2 -Wno-stringop-truncation -o ha_switch ha_switch.c -lcurl
 *
 * 用法: ./ha_switch <on|off|toggle|status> [entity_id]
 * 默认实体: switch.cuco_cn_945611612_v3_on_p_2_1 (书房智能插座)
 *
 * 读取同目录 config.ini 中的 HA_URL 和 HA_TOKEN
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>

#define MAX_CFG 2048
#define MAX_URL 512
#define MAX_TOKEN 1024
#define MAX_BUF 4096

static char g_url[MAX_URL] = "";
static char g_token[MAX_TOKEN] = "";

typedef struct {
    char *data;
    size_t size;
} curl_buf_t;

static size_t write_cb(void *ptr, size_t sz, size_t nmemb, void *userdata) {
    curl_buf_t *buf = (curl_buf_t *)userdata;
    size_t total = sz * nmemb;
    buf->data = realloc(buf->data, buf->size + total + 1);
    if (!buf->data) return 0;
    memcpy(buf->data + buf->size, ptr, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

static void load_config(void) {
    char path[512];
    snprintf(path, sizeof(path), "%s/config.ini", getenv("HOME") ?: ".");
    const char *try_paths[] = {
        path,
        "./config.ini",
        "../config.ini",
        "./mijia_scanner_c/config.ini",
        NULL
    };
    for (int i = 0; try_paths[i]; i++) {
        if (access(try_paths[i], R_OK) == 0) {
            strncpy(path, try_paths[i], sizeof(path) - 1);
            break;
        }
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        fprintf(stderr, "错误: 找不到 config.ini (需要 HA_URL 和 HA_TOKEN)\n");
        exit(1);
    }
    char line[MAX_CFG];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "HA_URL=", 7) == 0)
            strncpy(g_url, line + 7, MAX_URL - 1);
        else if (strncmp(line, "HA_TOKEN=", 9) == 0)
            strncpy(g_token, line + 9, MAX_TOKEN - 1);
    }
    fclose(fp);
    /* 去 tail \n */
    g_url[strcspn(g_url, "\r\n")] = '\0';
    g_token[strcspn(g_token, "\r\n")] = '\0';

    if (!*g_url || !*g_token) {
        fprintf(stderr, "错误: config.ini 中缺少 HA_URL 或 HA_TOKEN\n");
        exit(1);
    }
}

static CURL *make_req(const char *method, const char *api_path, const char *post_data) {
    char full_url[MAX_URL];
    snprintf(full_url, sizeof(full_url), "%s%s", g_url, api_path);

    CURL *curl = curl_easy_init();
    if (!curl) { fprintf(stderr, "curl_easy_init 失败\n"); exit(1); }

    struct curl_slist *headers = NULL;
    char auth[64 + MAX_TOKEN];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", g_token);
    headers = curl_slist_append(headers, auth);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, full_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");

    if (post_data)
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);

    return curl;
}

static int ha_request(const char *method, const char *api_path, const char *post_data, char *resp, size_t resp_sz) {
    CURL *curl = make_req(method, api_path, post_data);
    curl_buf_t buf = { .data = malloc(1), .size = 0 };
    buf.data[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "请求失败: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        free(buf.data);
        return -1;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (resp && resp_sz > 0)
        snprintf(resp, resp_sz, "%s", buf.data);

    curl_easy_cleanup(curl);
    free(buf.data);

    return (int)http_code;
}

/* 从 HA 状态 JSON 中提取 state 字段值 */
static const char *json_get_state(const char *json) {
    /* 简单解析: 找 "state": "xxx" */
    const char *p = strstr(json, "\"state\"");
    if (!p) return NULL;
    p = strchr(p + 7, ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return NULL;
    p++;
    static char state[128];
    int i = 0;
    while (*p && *p != '"' && i < (int)sizeof(state) - 1)
        state[i++] = *p++;
    state[i] = '\0';
    return state;
}

/* 从 HA 状态 JSON 中提取 friendly_name */
static const char *json_get_name(const char *json) {
    const char *p = strstr(json, "\"friendly_name\"");
    if (!p) return NULL;
    p = strchr(p + 15, ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return NULL;
    p++;
    static char name[256];
    int i = 0;
    while (*p && *p != '"' && i < (int)sizeof(name) - 1)
        name[i++] = *p++;
    name[i] = '\0';
    return name;
}

static void usage(const char *prog) {
    printf("用法: %s <on|off|toggle|status> [entity_id]\n", prog);
    printf("\n命令:\n");
    printf("  on      打开开关\n");
    printf("  off     关闭开关\n");
    printf("  toggle  切换开关状态\n");
    printf("  status  查询当前状态\n");
    printf("\n默认实体: switch.cuco_cn_945611612_v3_on_p_2_1 (书房智能插座)\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage(argv[0]);
        return 0;
    }

    const char *action = argv[1];
    const char *entity = (argc >= 3) ? argv[2]
        : "switch.cuco_cn_945611612_v3_on_p_2_1";

    if (strcmp(action, "on") && strcmp(action, "off") &&
        strcmp(action, "toggle") && strcmp(action, "status")) {
        fprintf(stderr, "未知命令: %s (支持 on/off/toggle/status)\n", action);
        return 1;
    }

    load_config();
    curl_global_init(CURL_GLOBAL_DEFAULT);

    char api_path[256];
    char resp[MAX_BUF] = "";

    /* 构造状态查询路径 */
    snprintf(api_path, sizeof(api_path), "/api/states/%s", entity);

    if (strcmp(action, "status") == 0) {
        int code = ha_request("GET", api_path, NULL, resp, sizeof(resp));
        if (code != 200) {
            fprintf(stderr, "查询失败 (HTTP %d)\n", code);
            return 1;
        }
        const char *state = json_get_state(resp);
        const char *name = json_get_name(resp);
        printf("实体: %s\n", entity);
        if (name) printf("名称: %s\n", name);
        printf("状态: %s\n", state ? state : "未知");
    } else {
        /* 先查当前状态 (toggle 需要) */
        const char *cur = "";
        if (strcmp(action, "toggle") == 0) {
            int code = ha_request("GET", api_path, NULL, resp, sizeof(resp));
            if (code != 200) {
                fprintf(stderr, "查询当前状态失败 (HTTP %d)\n", code);
                return 1;
            }
            cur = json_get_state(resp);
            if (!cur) { fprintf(stderr, "解析状态失败\n"); return 1; }
        }

        const char *target;
        if (strcmp(action, "toggle") == 0)
            target = (strcmp(cur, "on") == 0) ? "off" : "on";
        else
            target = action;

        /* POST /api/services/switch/turn_on 或 turn_off */
        char svc_path[128];
        snprintf(svc_path, sizeof(svc_path), "/api/services/switch/turn_%s", target);

        char post_data[512];
        snprintf(post_data, sizeof(post_data), "{\"entity_id\":\"%s\"}", entity);

        int code = ha_request("POST", svc_path, post_data, resp, sizeof(resp));
        if (code != 200) {
            fprintf(stderr, "操作失败 (HTTP %d)\n", code);
            return 1;
        }

        printf("%s → %s\n", entity, target);
    }

    curl_global_cleanup();
    return 0;
}
