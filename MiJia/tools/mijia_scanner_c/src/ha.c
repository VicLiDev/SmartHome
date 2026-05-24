/**
 * ha.c — Home Assistant API
 *
 * 对齐 Python 版本: ha.py
 * 从 Home Assistant REST API 获取设备列表，按房间分组。
 * 依赖: libcurl (apt install libcurl4-openssl-dev)
 *
 * 如果 libcurl 不可用，提供空 stub
 */

#include "common.h"

#ifdef HAVE_LIBCURL
#include <curl/curl.h>
#endif

/* ═══════════════════════════════════════════════════════════ */
/* HTTP 请求辅助                                                */
/* ═══════════════════════════════════════════════════════════ */

#ifdef HAVE_LIBCURL

typedef struct {
    char *data;
    size_t size;
} curl_buf_t;

static size_t curl_write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    curl_buf_t *buf = (curl_buf_t *)userp;
    buf->data = realloc(buf->data, buf->size + realsize + 1);
    if (!buf->data) return 0;
    memcpy(buf->data + buf->size, contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = '\0';
    return realsize;
}

static char *http_request(const char *url, const char *bearer_token,
                          const char *method, const char *post_data) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;

    curl_buf_t buf = { .data = malloc(1), .size = 0 };
    buf.data[0] = '\0';

    struct curl_slist *headers = NULL;
    char auth_header[MAX_STR + 32];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", bearer_token);
    headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");  /* 局域网直连，不走代理 */
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    if (post_data)
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        printf("  %s\n", color_dim_fmt("  [HA] 连接失败: %s", curl_easy_strerror(res)));
        free(buf.data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return NULL;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code < 200 || http_code >= 300) {
        printf("  %s\n", color_dim_fmt("  [HA] HTTP %ld", http_code));
        free(buf.data);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        return NULL;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return buf.data;
}

#endif /* HAVE_LIBCURL */

/* ═══════════════════════════════════════════════════════════ */
/* 简易 JSON 解析（不依赖第三方库）                               */
/* ═══════════════════════════════════════════════════════════ */

#ifdef HAVE_LIBCURL

/* 跳过空白 */
static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* 从 JSON 对象中提取字符串值: {"key": "value"} */
static bool json_get_string(const char *json, const char *key, char *out, int out_size) {
    char search[MAX_STR];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *p = strstr(json, search);
    if (!p) return false;
    p += strlen(search);
    p = skip_ws(p);
    if (*p != ':') return false;
    p++;
    p = skip_ws(p);
    if (*p != '"') return false;
    p++;

    int i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        if (*p == '\\') {
            p++;
            if (!*p) break;
            switch (*p) {
                case 'n': out[i++] = '\n'; break;
                case 't': out[i++] = '\t'; break;
                case 'r': out[i++] = '\r'; break;
                case '\\': out[i++] = '\\'; break;
                case '"': out[i++] = '"'; break;
                default: out[i++] = *p; break;
            }
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    return true;
}

/* 查找 JSON 数组中的所有 "entity_id" */
/* 返回状态数组（每个元素是 entity_id 开始的指针） */
#define MAX_STATES 4096
typedef struct {
    const char *ptr;
    int len;
} json_ptr_t;

static int json_find_states(const char *json, json_ptr_t *states) {
    int count = 0;

    /* HA /api/states 直接返回 JSON 数组: [{...}, {...}] */
    const char *arr = json;
    /* 跳过前导空白 */
    while (*arr == ' ' || *arr == '\t' || *arr == '\n' || *arr == '\r') arr++;

    /* 如果是 {"message": ...} 格式的错误响应，直接返回 */
    if (*arr == '{') {
        const char *msg = strstr(arr, "\"message\"");
        if (msg) {
            char err[256] = {0};
            if (json_get_string(arr, "message", err, sizeof(err)))
                printf("  %s\n", color_dim_fmt("  [HA] 错误: %s", err));
        }
        return 0;
    }

    /* 找到数组起始 [ */
    arr = strchr(arr, '[');
    if (!arr) return 0;
    arr++;

    /* 逐个查找 { ... } */
    int depth = 1;
    while (*arr && count < MAX_STATES) {
        if (*arr == '{') {
            if (depth == 1) {
                /* 找到对象开始，记录位置 */
                states[count].ptr = arr;
                /* 找到对象结束 */
                const char *obj = arr + 1;
                int obj_depth = 1;
                while (*obj && obj_depth > 0) {
                    if (*obj == '{') obj_depth++;
                    else if (*obj == '}') obj_depth--;
                    else if (*obj == '"') {
                        obj++;
                        while (*obj && *obj != '"') {
                            if (*obj == '\\') obj++;
                            obj++;
                        }
                    }
                    obj++;
                }
                states[count].len = (int)(obj - arr);
                count++;
                arr = obj;
            } else {
                depth++;
                arr++;
            }
        } else if (*arr == '}') {
            depth--;
            arr++;
        } else if (*arr == '[') {
            depth++;
            arr++;
        } else if (*arr == ']') {
            if (depth == 1) break;
            depth--;
            arr++;
        } else if (*arr == '"') {
            arr++;
            while (*arr && *arr != '"') {
                if (*arr == '\\') arr++;
                arr++;
            }
            if (*arr) arr++;
        } else {
            arr++;
        }
    }

    return count;
}

/* ═══════════════════════════════════════════════════════════ */
/* HA 房间关键词和设备类型提取 (对齐 Python ha.py)                */
/* ═══════════════════════════════════════════════════════════ */

static const char *ROOM_KW[] = {
    "休闲阳台", "生活阳台", "入户玄关",
    "客厅", "书房", "厨房", "主卧", "次卧", "卧室",
    "玄关", "阳台", "入户", "卫生间", "走廊",
    NULL
};

static const char *KEEP_DOMAINS[] = {
    "climate", "light", "switch", "camera", "cover", "sensor", "binary_sensor",
    "button", "number", "event", "text", "fan", "lock", "select", "update",
    NULL
};

static bool is_keep_domain(const char *entity_id) {
    for (int i = 0; KEEP_DOMAINS[i]; i++) {
        int dlen = strlen(KEEP_DOMAINS[i]);
        if (strncmp(entity_id, KEEP_DOMAINS[i], dlen) == 0 && entity_id[dlen] == '.')
            return true;
    }
    return false;
}

static const char *find_room(const char *name) {
    for (int i = 0; ROOM_KW[i]; i++) {
        if (strstr(name, ROOM_KW[i]))
            return ROOM_KW[i];
    }
    return NULL;
}

static const char *JUNK_WORDS[] = {
    "开关状态切换", "功能异常", "电机反向", "全部开关指示灯状态",
    "防闪烁模式", "故障", "浸没状态", "电池电量", "设备被重置", NULL
};

static void extract_device_name(const char *name, const char *room, char *out) {
    /* 去掉房间前缀 */
    const char *p = name;
    if (room) {
        const char *rp = strstr(p, room);
        if (rp) {
            p = rp + strlen(room);
            while (*p == ' ' || *p == '\xe3' || *p == '\x80' || *p == '\x80') p++;
        }
    }

    /* 复制到临时缓冲区 */
    char buf[MAX_DEV_NAME];
    strncpy(buf, p, MAX_DEV_NAME - 1);
    buf[MAX_DEV_NAME - 1] = '\0';

    /* 去掉尾部空格 */
    int len = strlen(buf);
    while (len > 0 && (buf[len-1] == ' ')) buf[--len] = '\0';

    /* 去掉垃圾词 */
    for (int i = 0; JUNK_WORDS[i]; i++) {
        char *junk = strstr(buf, JUNK_WORDS[i]);
        if (junk) {
            int jlen = strlen(JUNK_WORDS[i]);
            memmove(junk, junk + jlen, strlen(junk + jlen) + 1);
        }
    }

    /* 再次去掉尾部空格 */
    len = strlen(buf);
    while (len > 0 && buf[len-1] == ' ') buf[--len] = '\0';

    strncpy(out, buf, MAX_DEV_NAME - 1);
    out[MAX_DEV_NAME - 1] = '\0';
}

static const char *HIGH_PRIORITY_DTYPE[] = {
    "智能音箱", "智能开关", "黑板插座", "智能家庭面板", "水浸卫士", "移动检测", NULL
};
static const char *NORMAL_DTYPE[] = {
    "灯带", "窗帘", "布帘", "纱帘", "水阀", "水浸",
    "灯", "空调", "音箱", "电视", "插座", "开关", "马桶",
    "电脑", "监控", "面板", "传感器", NULL
};

static bool extract_device_type(const char *dname, char *dtype_out) {
    dtype_out[0] = '\0';

    for (int i = 0; HIGH_PRIORITY_DTYPE[i]; i++) {
        if (strstr(dname, HIGH_PRIORITY_DTYPE[i])) {
            strcpy(dtype_out, HIGH_PRIORITY_DTYPE[i]);
            return true;
        }
    }
    for (int i = 0; NORMAL_DTYPE[i]; i++) {
        if (strstr(dname, NORMAL_DTYPE[i])) {
            strcpy(dtype_out, NORMAL_DTYPE[i]);
            return true;
        }
    }
    return false;
}

#endif /* HAVE_LIBCURL */

/* color_dim_fmt 已在 common.h 声明 */

/**
 * ha_get_all_devices — 从 HA REST API 获取所有设备
 * 对齐 Python ha.py: ha_get_all_devices()
 */
void ha_get_all_devices(const char *ha_url, const char *token, device_list_t *devices) {
    if (!token || !*token) return;

#ifndef HAVE_LIBCURL
    printf("  %s\n", color_dim("  [HA] 需要安装 libcurl4-openssl-dev 后重新编译"));
    printf("  %s\n", color_dim("       sudo apt install libcurl4-openssl-dev && make clean && make"));
    (void)ha_url;
    (void)devices;
    return;
#else

    /* 构造 URL */
    char url[MAX_STR + 32];
    snprintf(url, sizeof(url), "%s/api/states", ha_url);

    char *response = http_request(url, token, "GET", NULL);
    if (!response) return;

    /* 解析 states 数组 */
    json_ptr_t states[MAX_STATES];
    int state_count = json_find_states(response, states);

    /* 去重表: room|type */
    char **seen_keys = NULL;
    int seen_count = 0, seen_cap = 0;
    int room_count = 0;
    char rooms_found[64][32];

    for (int i = 0; i < state_count; i++) {
        /* 提取 entity_id */
        char entity_id[256] = {0};
        if (!json_get_string(states[i].ptr, "entity_id", entity_id, sizeof(entity_id)))
            continue;

        /* 检查 domain */
        if (!is_keep_domain(entity_id)) continue;

        /* 提取 friendly_name */
        char friendly_name[MAX_DEV_NAME] = {0};
        if (!json_get_string(states[i].ptr, "friendly_name", friendly_name, sizeof(friendly_name)))
            continue;

        /* 提取 room */
        const char *room = find_room(friendly_name);
        if (!room) continue;

        /* 提取设备名 */
        char dname[MAX_DEV_NAME] = {0};
        extract_device_name(friendly_name, room, dname);

        /* 跳过含 * 的条目 */
        if (strchr(dname, '*')) continue;

        /* 提取设备类型 */
        char dtype[64] = {0};
        if (!extract_device_type(dname, dtype)) continue;
        if (!*dtype) continue;

        /* 去重 */
        char dedup_key[128];
        snprintf(dedup_key, sizeof(dedup_key), "%s|%s", room, dtype);
        bool dup = false;
        for (int j = 0; j < seen_count; j++) {
            if (strcmp(seen_keys[j], dedup_key) == 0) { dup = true; break; }
        }
        if (dup) continue;

        if (seen_count >= seen_cap) {
            seen_cap = seen_cap == 0 ? 64 : seen_cap * 2;
            seen_keys = realloc(seen_keys, seen_cap * sizeof(char*));
        }
        seen_keys[seen_count++] = strdup(dedup_key);

        /* 记录 room */
        bool room_new = true;
        for (int j = 0; j < room_count; j++) {
            if (strcmp(rooms_found[j], room) == 0) { room_new = false; break; }
        }
        if (room_new && room_count < 64)
            strncpy(rooms_found[room_count++], room, 31);

        /* 添加设备 */
        device_t *d = device_list_add(devices);
        strncpy(d->name, dname, MAX_DEV_NAME - 1);
        strncpy(d->room, room, 31);
        strncpy(d->type, dtype, 63);
    }

    printf("  %s\n", color_dim_fmt("  [HA] 获取到 %d 个设备（%d 个房间）", devices->count, room_count));

    free(response);
    for (int i = 0; i < seen_count; i++) free(seen_keys[i]);
    free(seen_keys);
#endif /* HAVE_LIBCURL */
}

/* ═══════════════════════════════════════════════════════════ */
/* HA 实体状态查询 & 服务调用                                     */
/* ═══════════════════════════════════════════════════════════ */

/**
 * ha_get_entity_state — 查询单个实体的 state 字段
 * @return 0 成功, -1 失败
 */
int ha_get_entity_state(const char *ha_url, const char *token,
                        const char *entity_id, char *state_out, int state_sz) {
    if (!token || !*token) {
        printf("  %s\n", color_dim("  [HA] 未配置 HA_TOKEN"));
        return -1;
    }
    state_out[0] = '\0';

#ifndef HAVE_LIBCURL
    printf("  %s\n", color_dim("  [HA] 需要安装 libcurl4-openssl-dev 后重新编译"));
    return -1;
#else
    char url[MAX_STR + 64];
    snprintf(url, sizeof(url), "%s/api/states/%s", ha_url, entity_id);

    char *resp = http_request(url, token, "GET", NULL);
    if (!resp) return -1;

    /* 提取 "state": "xxx" */
    if (!json_get_string(resp, "state", state_out, state_sz)) {
        /* 可能是错误响应 */
        char msg[256] = {0};
        if (json_get_string(resp, "message", msg, sizeof(msg))) {
            char buf[MAX_STR * 2];
            snprintf(buf, sizeof(buf), "  [HA] 错误: %s", msg);
            printf("  %s\n", color_red(buf));
        }
        free(resp);
        return -1;
    }

    free(resp);
    return 0;
#endif
}

/**
 * ha_call_service — 调用 HA 服务
 * @return 0 成功, -1 失败
 */
int ha_call_service(const char *ha_url, const char *token,
                    const char *domain, const char *service,
                    const char *entity_id) {
    if (!token || !*token) {
        printf("  %s\n", color_dim("  [HA] 未配置 HA_TOKEN"));
        return -1;
    }

#ifndef HAVE_LIBCURL
    printf("  %s\n", color_dim("  [HA] 需要安装 libcurl4-openssl-dev 后重新编译"));
    return -1;
#else
    char url[MAX_STR + 64];
    snprintf(url, sizeof(url), "%s/api/services/%s/%s", ha_url, domain, service);

    char post[MAX_STR + 128];
    snprintf(post, sizeof(post), "{\"entity_id\":\"%s\"}", entity_id);

    char *resp = http_request(url, token, "POST", post);
    if (!resp) return -1;

    free(resp);
    return 0;
#endif
}
