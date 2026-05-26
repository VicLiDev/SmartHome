/**
 * ha_ac.c — 通过 Home Assistant REST API 控制书房空调
 *
 * 编译: gcc -std=c11 -Wall -Wextra -O2 -Wno-stringop-truncation -o ha_ac ha_ac.c -lcurl
 *
 * 用法: ./ha_ac <命令> [参数] [entity_id]
 *
 * 命令:
 *   status              查询当前状态（默认）
 *   on                  开机
 *   off                 关机
 *   toggle              开关切换
 *   temp <温度>         设置温度 (16~32)
 *   mode <模式>         设置模式 (cool/dry/fan_only/heat/off)
 *   fan <风速>          设置风速 (一档~五档/自动)
 *
 * 默认实体: climate.daikin_cn_x_10357_2544043221_ipbox_601803328177_indoor_10427c64_k5
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
#define MAX_BUF 65536
#define MAX_CLIMATES 32

static char g_url[MAX_URL] = "";
static char g_token[MAX_TOKEN] = "";

#define DEFAULT_ENTITY "climate.daikin_cn_x_10357_2544043221_ipbox_601803328177_indoor_10427c64_k5"

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
    g_url[strcspn(g_url, "\r\n")] = '\0';
    g_token[strcspn(g_token, "\r\n")] = '\0';

    if (!*g_url || !*g_token) {
        fprintf(stderr, "错误: config.ini 中缺少 HA_URL 或 HA_TOKEN\n");
        exit(1);
    }
}

static int ha_request(const char *method, const char *api_path, const char *post_data, char *resp, size_t resp_sz) {
    char full_url[MAX_URL];
    snprintf(full_url, sizeof(full_url), "%s%s", g_url, api_path);

    CURL *curl = curl_easy_init();
    if (!curl) { fprintf(stderr, "curl_easy_init 失败\n"); return -1; }

    struct curl_slist *headers = NULL;
    char auth[64 + MAX_TOKEN];
    snprintf(auth, sizeof(auth), "Authorization: Bearer %s", g_token);
    headers = curl_slist_append(headers, auth);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_buf_t buf = { .data = malloc(1), .size = 0 };
    buf.data[0] = '\0';

    curl_easy_setopt(curl, CURLOPT_URL, full_url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");

    if (post_data)
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "请求失败: %s\n", curl_easy_strerror(res));
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        free(buf.data);
        return -1;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (resp && resp_sz > 0)
        snprintf(resp, resp_sz, "%s", buf.data);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    free(buf.data);

    return (int)http_code;
}

/* 从 JSON 中提取字符串字段值 (简单解析) */
static const char *json_get_str(const char *json, const char *key) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return NULL;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return NULL;
    p++;
    static char val[256];
    int i = 0;
    while (*p && *p != '"' && i < (int)sizeof(val) - 1)
        val[i++] = *p++;
    val[i] = '\0';
    return val;
}

/* 从 JSON 中提取数字字段值 */
static double json_get_num(const char *json, const char *key) {
    const char *v = json_get_str(json, key);
    if (!v) {
        /* 数字可能不带引号, 尝试直接解析 */
        char pattern[128];
        snprintf(pattern, sizeof(pattern), "\"%s\":", key);
        const char *p = strstr(json, pattern);
        if (!p) return -1;
        p += strlen(pattern);
        while (*p == ' ' || *p == '\t') p++;
        return atof(p);
    }
    return atof(v);
}

/* 模式别名: 中文 → 英文 */
static const char *resolve_mode(const char *arg) {
    if (strcmp(arg, "制冷") == 0) return "cool";
    if (strcmp(arg, "制热") == 0) return "heat";
    if (strcmp(arg, "除湿") == 0) return "dry";
    if (strcmp(arg, "送风") == 0) return "fan_only";
    if (strcmp(arg, "关") == 0)   return "off";
    return arg;
}

static int is_valid_mode(const char *mode) {
    return strcmp(mode, "cool") == 0 || strcmp(mode, "dry") == 0 ||
           strcmp(mode, "fan_only") == 0 || strcmp(mode, "heat") == 0 ||
           strcmp(mode, "off") == 0;
}

typedef struct {
    char entity_id[256];
    char friendly_name[256];
    char state[64];
} climate_entry_t;

/* 从 HA 全量状态 JSON 中提取所有 climate 实体 */
static int parse_climates(const char *json, climate_entry_t *list, int max) {
    int count = 0;
    const char *p = json;
    while (count < max) {
        /* 找 "entity_id":"climate. 或 "entity_id": "climate. */
        p = strstr(p, "\"entity_id\"");
        if (!p) break;
        p += 12; /* skip "entity_id" */
        while (*p == ' ' || *p == '\t' || *p == ':') p++;
        if (*p != '"') break;
        p++;
        if (strncmp(p, "climate.", 8) != 0) {
            /* 不是 climate 实体, 继续搜索 */
            p++;
            continue;
        }

        climate_entry_t *e = &list[count];
        /* entity_id 包含 "climate." 前缀 (p 现在指向 'c') */
        int i = 0;
        while (*p && *p != '"' && i < (int)sizeof(e->entity_id) - 1)
            e->entity_id[i++] = *p++;
        e->entity_id[i] = '\0';

        /* 向后找对应的 "state": 和 "friendly_name":
           它们在同一个 {...} 对象中 */
        const char *obj_end = strchr(p, '}');
        if (!obj_end) break;

        /* 找 state */
        const char *s = p;
        while (s < obj_end) {
            const char *k = strstr(s, "\"state\":");
            if (!k || k > obj_end) break;
            k += 8;
            while (*k == ' ' || *k == '\t') k++;
            if (*k == '"') {
                k++;
                int j = 0;
                while (*k && *k != '"' && j < (int)sizeof(e->state) - 1)
                    e->state[j++] = *k++;
                e->state[j] = '\0';
            }
            break;
        }

        /* 找 friendly_name */
        s = p;
        while (s < obj_end) {
            const char *k = strstr(s, "\"friendly_name\":");
            if (!k || k > obj_end) break;
            k += 16;
            while (*k == ' ' || *k == '\t') k++;
            if (*k == '"') {
                k++;
                int j = 0;
                while (*k && *k != '"' && j < (int)sizeof(e->friendly_name) - 1)
                    e->friendly_name[j++] = *k++;
                e->friendly_name[j] = '\0';
            }
            break;
        }

        count++;
        p = obj_end + 1;
    }
    return count;
}

static void print_climate_list(const climate_entry_t *list, int count) {
    if (count == 0) {
        printf("未找到 climate 实体\n");
        return;
    }
    for (int i = 0; i < count; i++) {
        printf("  [%d] %s\n", i + 1, list[i].friendly_name);
        printf("      %s  %s\n", list[i].entity_id, strcmp(list[i].state, "off") == 0 ? "off" : "on");
    }
}

static const char *select_entity(const climate_entry_t *list, int count) {
    print_climate_list(list, count);
    printf("\n选择实体编号: ");
    fflush(stdout);

    char buf[16];
    if (!fgets(buf, sizeof(buf), stdin)) {
        fprintf(stderr, "无效选择\n");
        exit(1);
    }
    int choice = atoi(buf);
    if (choice < 1 || choice > count) {
        fprintf(stderr, "无效选择\n");
        exit(1);
    }
    return list[choice - 1].entity_id;
}

static void usage(const char *prog) {
    printf("用法: %s <命令> [参数] [entity_id]\n", prog);
    printf("\n命令:\n");
    printf("  status              查询当前状态（默认）\n");
    printf("  on                  开机\n");
    printf("  off                 关机\n");
    printf("  toggle              开关切换\n");
    printf("  temp <温度>         设置温度 (16~32)\n");
    printf("  mode <模式>         设置模式 (cool/dry/fan_only/heat/off)\n");
    printf("                     中文别名: 制冷/制热/除湿/送风/关\n");
    printf("  fan <风速>          设置风速 (1~5/auto 或 一档~五档/自动)\n");
    printf("  list                列出所有空调实体\n");
    printf("\nentity_id 留空使用默认实体，传 - 交互式选择，或传数字序号\n");
    printf("\n默认实体: %s\n", DEFAULT_ENTITY);
}

int main(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage(argv[0]);
        return 0;
    }

    const char *cmd = argv[1];
    const char *arg = NULL;
    const char *entity = DEFAULT_ENTITY;

    /* 解析参数 */
    if (strcmp(cmd, "temp") == 0 || strcmp(cmd, "mode") == 0 || strcmp(cmd, "fan") == 0) {
        if (argc < 3) {
            fprintf(stderr, "错误: %s 命令需要一个参数\n", cmd);
            return 1;
        }
        arg = argv[2];
        if (argc >= 4) entity = argv[3];
    } else if (strcmp(cmd, "list") == 0) {
        entity = NULL;
    } else {
        if (argc >= 3) entity = argv[2];
    }

    if (strcmp(cmd, "status") && strcmp(cmd, "on") && strcmp(cmd, "off") &&
        strcmp(cmd, "toggle") && strcmp(cmd, "temp") && strcmp(cmd, "mode") &&
        strcmp(cmd, "fan") && strcmp(cmd, "list")) {
        fprintf(stderr, "未知命令: %s\n", cmd);
        return 1;
    }

    load_config();
    curl_global_init(CURL_GLOBAL_DEFAULT);

    /* list 命令 */
    if (strcmp(cmd, "list") == 0) {
        size_t buf_sz = 524288;
        char *resp = calloc(1, buf_sz);
        if (!resp) { fprintf(stderr, "内存分配失败\n"); return 1; }
        int code = ha_request("GET", "/api/states", NULL, resp, buf_sz);
        if (code != 200) { fprintf(stderr, "查询失败 (HTTP %d)\n", code); free(resp); return 1; }
        climate_entry_t climates[MAX_CLIMATES];
        int count = parse_climates(resp, climates, MAX_CLIMATES);
        print_climate_list(climates, count);
        free(resp);
        curl_global_cleanup();
        return 0;
    }

    /* 交互式选择 或 数字序号选择 */
    if (strcmp(entity, "-") == 0 || (strlen(entity) <= 2 && entity[0] >= '1' && entity[0] <= '9')) {
        size_t buf_sz = 524288;
        char *resp = calloc(1, buf_sz);
        if (!resp) { fprintf(stderr, "内存分配失败\n"); return 1; }
        int code = ha_request("GET", "/api/states", NULL, resp, buf_sz);
        if (code != 200) { fprintf(stderr, "查询失败 (HTTP %d)\n", code); free(resp); return 1; }
        static climate_entry_t climates[MAX_CLIMATES];
        int count = parse_climates(resp, climates, MAX_CLIMATES);
        free(resp);
        if (count == 0) { fprintf(stderr, "未找到 climate 实体\n"); return 1; }

        if (strcmp(entity, "-") == 0) {
            entity = select_entity(climates, count);
        } else {
            int idx = atoi(entity);
            if (idx < 1 || idx > count) {
                fprintf(stderr, "无效序号: %d (范围 1~%d)\n", idx, count);
                return 1;
            }
            entity = climates[idx - 1].entity_id;
        }
    }

    char resp[MAX_BUF] = "";
    char api_path[512];
    char post_data[1024];
    int code;

    /* 获取 friendly_name (status/toggle 复用) */
    snprintf(api_path, sizeof(api_path), "/api/states/%s", entity);
    code = ha_request("GET", api_path, NULL, resp, sizeof(resp));
    if (code != 200) { fprintf(stderr, "查询失败 (HTTP %d)\n", code); return 1; }
    const char *name_raw = json_get_str(resp, "friendly_name");
    char name_buf[256];
    strncpy(name_buf, name_raw ? name_raw : entity, sizeof(name_buf) - 1);
    name_buf[sizeof(name_buf) - 1] = '\0';
    const char *name = name_buf;

    if (strcmp(cmd, "status") == 0) {
        /* json_get_str 使用 static 缓冲区，必须立即拷贝 */
        char state_s[128] = "", hvac_s[128] = "", fan_s[128] = "", action_s[128] = "";
        const char *s;
        s = json_get_str(resp, "state");      if (s) strncpy(state_s, s, sizeof(state_s)-1);
        s = json_get_str(resp, "hvac_mode"); if (s) strncpy(hvac_s, s, sizeof(hvac_s)-1);
        s = json_get_str(resp, "fan_mode");  if (s) strncpy(fan_s, s, sizeof(fan_s)-1);
        s = json_get_str(resp, "hvac_action"); if (s) strncpy(action_s, s, sizeof(action_s)-1);
        double temp = json_get_num(resp, "temperature");
        const char *cur_temp_s = json_get_str(resp, "current_temperature");

        printf("名称: %s\n", name);
        printf("状态: %s\n", state_s[0] ? state_s : "未知");
        printf("温度: %.0f°C\n", temp);
        printf("模式: %s\n", hvac_s[0] ? hvac_s : "?");
        printf("风速: %s\n", fan_s[0] ? fan_s : "?");
        if (action_s[0]) printf("动作: %s\n", action_s);
        if (cur_temp_s) printf("室温: %s°C\n", cur_temp_s);
    }
    else if (strcmp(cmd, "on") == 0) {
        snprintf(post_data, sizeof(post_data), "{\"entity_id\":\"%s\"}", entity);
        code = ha_request("POST", "/api/services/climate/turn_on", post_data, resp, sizeof(resp));
        if (code != 200) { fprintf(stderr, "操作失败 (HTTP %d)\n", code); return 1; }
        printf("%s → 开机\n", name);
    }
    else if (strcmp(cmd, "off") == 0) {
        snprintf(post_data, sizeof(post_data), "{\"entity_id\":\"%s\"}", entity);
        code = ha_request("POST", "/api/services/climate/turn_off", post_data, resp, sizeof(resp));
        if (code != 200) { fprintf(stderr, "操作失败 (HTTP %d)\n", code); return 1; }
        printf("%s → 关机\n", name);
    }
    else if (strcmp(cmd, "toggle") == 0) {
        const char *cur = json_get_str(resp, "state");
        if (!cur) { fprintf(stderr, "解析状态失败\n"); return 1; }

        const char *svc;
        const char *label;
        if (strcmp(cur, "off") == 0) {
            svc = "/api/services/climate/turn_on";
            label = "开机";
        } else {
            svc = "/api/services/climate/turn_off";
            label = "关机";
        }
        snprintf(post_data, sizeof(post_data), "{\"entity_id\":\"%s\"}", entity);
        code = ha_request("POST", svc, post_data, resp, sizeof(resp));
        if (code != 200) { fprintf(stderr, "操作失败 (HTTP %d)\n", code); return 1; }
        printf("%s → %s\n", name, label);
    }
    else if (strcmp(cmd, "temp") == 0) {
        int temp = atoi(arg);
        if (temp < 16 || temp > 32) {
            fprintf(stderr, "错误: 温度范围为 16~32°C\n");
            return 1;
        }
        snprintf(post_data, sizeof(post_data), "{\"entity_id\":\"%s\",\"temperature\":%d}", entity, temp);
        code = ha_request("POST", "/api/services/climate/set_temperature", post_data, resp, sizeof(resp));
        if (code != 200) { fprintf(stderr, "操作失败 (HTTP %d)\n", code); return 1; }
        printf("%s → %d°C\n", name, temp);
    }
    else if (strcmp(cmd, "mode") == 0) {
        const char *mode = resolve_mode(arg);
        if (!is_valid_mode(mode)) {
            fprintf(stderr, "错误: 不支持的模式 '%s'\n", arg);
            return 1;
        }
        if (strcmp(mode, "off") == 0) {
            snprintf(post_data, sizeof(post_data), "{\"entity_id\":\"%s\"}", entity);
            code = ha_request("POST", "/api/services/climate/turn_off", post_data, resp, sizeof(resp));
        } else {
            snprintf(post_data, sizeof(post_data), "{\"entity_id\":\"%s\",\"hvac_mode\":\"%s\"}", entity, mode);
            code = ha_request("POST", "/api/services/climate/set_hvac_mode", post_data, resp, sizeof(resp));
        }
        if (code != 200) { fprintf(stderr, "操作失败 (HTTP %d)\n", code); return 1; }
        printf("%s → %s\n", name, mode);
    }
    else if (strcmp(cmd, "fan") == 0) {
        /* 数字别名: 1~5 → 一档~五档, auto → 自动 */
        const char *fan = arg;
        if (strlen(arg) == 1 && arg[0] >= '1' && arg[0] <= '5') {
            static const char *fan_names[] = {"", "一档", "二档", "三档", "四档", "五档"};
            fan = fan_names[arg[0] - '0'];
        } else if (strcmp(arg, "auto") == 0) {
            fan = "自动";
        }
        snprintf(post_data, sizeof(post_data), "{\"entity_id\":\"%s\",\"fan_mode\":\"%s\"}", entity, fan);
        code = ha_request("POST", "/api/services/climate/set_fan_mode", post_data, resp, sizeof(resp));
        if (code != 200) { fprintf(stderr, "操作失败 (HTTP %d)\n", code); return 1; }
        printf("%s → 风速 %s\n", name, fan);
    }

    curl_global_cleanup();
    return 0;
}
