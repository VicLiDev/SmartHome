/**
 * 11_c_demo — 小米官方 IoT 开放平台客户端
 *
 * 演示与小米 IoT 开放平台（iot.mi.com）的对接流程。
 * 功能：OAuth2 授权、HMAC 签名、设备管理、属性读写。
 * 用法: ./mi_iot_client <子命令> [参数]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include "cJSON.h"

/* ========== 配置 ========== */
#define API_BASE       "https://api.io.mi.com/app"
#define TOKEN_URL      API_BASE "/oauth2/access_token"
#define CONFIG_FILE    ".mi_iot_config.json"

#define MAX_RESPONSE   (256 * 1024)
#define MAX_PATH       512

/* ========== 应用配置（持久化） ========== */
typedef struct {
    char app_key[128];
    char app_secret[256];
    char access_token[512];
    char device_did[64];     /* 设备 DID */
    int  siid;               /* 服务 ID */
    int  piid;               /* 属性 ID */
} AppConfig;

static AppConfig g_config = {0};

/* ========== libcurl 回调 ========== */
typedef struct {
    char *data;
    size_t size;
} MemoryBuffer;

static size_t write_cb(void *contents, size_t sz, size_t n, void *userp)
{
    size_t total = sz * n;
    MemoryBuffer *buf = (MemoryBuffer *)userp;
    if (buf->size + total + 1 > MAX_RESPONSE) return 0;
    buf->data = realloc(buf->data, buf->size + total + 1);
    if (!buf->data) return 0;
    memcpy(buf->data + buf->size, contents, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

static void buf_init(MemoryBuffer *b) { b->data = calloc(1, 1); b->size = 0; }
static void buf_free(MemoryBuffer *b) { free(b->data); b->data = NULL; b->size = 0; }

/* ========== HMAC-SHA256 签名 ========== */

/**
 * 计算字符串的 HMAC-SHA256，输出十六进制字符串
 * @param key    密钥
 * @param msg    待签名消息
 * @param out    输出缓冲区（至少 65 字节）
 */
static void hmac_sha256_hex(const char *key, const char *msg, char *out)
{
    unsigned char hmac_result[EVP_MAX_MD_SIZE];
    unsigned int hmac_len = 0;

    HMAC(EVP_sha256(), key, strlen(key),
         (unsigned char *)msg, strlen(msg),
         hmac_result, &hmac_len);

    for (unsigned int i = 0; i < hmac_len; i++) {
        sprintf(out + i * 2, "%02x", hmac_result[i]);
    }
    out[hmac_len * 2] = '\0';
}

/**
 * 生成 nonce（随机字符串，用于防重放）
 */
static void generate_nonce(char *out, int len)
{
    static const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    srand((unsigned int)time(NULL) ^ (unsigned int)clock());
    for (int i = 0; i < len - 1; i++) {
        out[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    out[len - 1] = '\0';
}

/**
 * 生成签名字符串并计算 HMAC
 * 签名规则: 对请求参数按 key 排序，拼接为 key1=val1&key2=val2，
 * 然后用 app_secret 做 HMAC-SHA256 签名。
 * @param params  JSON 对象（请求参数）
 * @param out     签名结果输出（十六进制）
 */
static void compute_signature(const cJSON *params, char *out)
{
    /* 收集所有 key */
    int count = cJSON_GetArraySize((cJSON *)params);
    char **keys = malloc(count * sizeof(char *));
    int idx = 0;

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, (cJSON *)params) {
        keys[idx++] = item->string;
    }

    /* 按 key 字母排序（简单冒泡排序） */
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (strcmp(keys[i], keys[j]) > 0) {
                char *tmp = keys[i];
                keys[i] = keys[j];
                keys[j] = tmp;
            }
        }
    }

    /* 拼接参数字符串 */
    char *param_str = calloc(4096, 1);
    for (int i = 0; i < count; i++) {
        if (i > 0) strcat(param_str, "&");
        strcat(param_str, keys[i]);
        strcat(param_str, "=");
        cJSON *val = cJSON_GetObjectItem((cJSON *)params, keys[i]);
        if (cJSON_IsString(val)) {
            strcat(param_str, val->valuestring);
        } else {
            char num[32];
            snprintf(num, sizeof(num), "%d", val->valueint);
            strcat(param_str, num);
        }
    }
    free(keys);

    printf("  签名原文: %s\n", param_str);

    /* HMAC-SHA256 签名 */
    hmac_sha256_hex(g_config.app_secret, param_str, out);
    free(param_str);
}

/* ========== HTTP 请求辅助 ========== */

/** 发送 POST JSON 请求 */
static int http_post_json(const char *url, const char *body, MemoryBuffer *out)
{
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    buf_init(out);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);  /* 演示用，生产环境应开启 */

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "  [错误] 请求失败: %s\n", curl_easy_strerror(res));
        buf_free(out);
        return -1;
    }
    if (http_code != 200) {
        fprintf(stderr, "  [错误] HTTP %ld\n", http_code);
        buf_free(out);
        return -1;
    }
    return 0;
}

/* ========== 子命令实现 ========== */

/** 配置 AppKey/AppSecret/设备信息 */
static int cmd_config(int argc, char *argv[])
{
    printf("=== 配置小米 IoT 开放平台参数 ===\n");

    printf("  当前配置:\n");
    printf("    AppKey:    %s\n",
           strlen(g_config.app_key) > 0 ? g_config.app_key : "(未设置)");
    printf("    AppSecret: %s\n",
           strlen(g_config.app_secret) > 0 ? "***" : "(未设置)");
    printf("    Token:     %s\n",
           strlen(g_config.access_token) > 0 ? "***" : "(未设置)");
    printf("    设备DID:   %s\n",
           strlen(g_config.device_did) > 0 ? g_config.device_did : "(未设置)");

    /* 交互式配置 */
    printf("\n  请输入 AppKey: ");
    if (fgets(g_config.app_key, sizeof(g_config.app_key), stdin))
        g_config.app_key[strcspn(g_config.app_key, "\n")] = '\0';

    printf("  请输入 AppSecret: ");
    if (fgets(g_config.app_secret, sizeof(g_config.app_secret), stdin))
        g_config.app_secret[strcspn(g_config.app_secret, "\n")] = '\0';

    printf("  请输入设备 DID（可选）: ");
    if (fgets(g_config.device_did, sizeof(g_config.device_did), stdin))
        g_config.device_did[strcspn(g_config.device_did, "\n")] = '\0';

    /* 保存到配置文件 */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "app_key", g_config.app_key);
    cJSON_AddStringToObject(root, "app_secret", g_config.app_secret);
    cJSON_AddStringToObject(root, "access_token", g_config.access_token);
    cJSON_AddStringToObject(root, "device_did", g_config.device_did);
    cJSON_AddNumberToObject(root, "siid", g_config.siid);
    cJSON_AddNumberToObject(root, "piid", g_config.piid);

    char *json = cJSON_Print(root);
    FILE *fp = fopen(CONFIG_FILE, "w");
    if (fp) {
        fputs(json, fp);
        fclose(fp);
        printf("\n  ✓ 配置已保存到 %s\n", CONFIG_FILE);
    } else {
        fprintf(stderr, "\n  [警告] 无法保存配置文件\n");
    }
    free(json);
    cJSON_Delete(root);
    return 0;
}

/** 加载配置文件 */
static int load_config(void)
{
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) return -1;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buf = malloc(sz + 1);
    fread(buf, 1, sz, fp);
    buf[sz] = '\0';
    fclose(fp);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return -1;

    cJSON *item;
    item = cJSON_GetObjectItem(root, "app_key");
    if (item && cJSON_IsString(item))
        strncpy(g_config.app_key, item->valuestring, sizeof(g_config.app_key) - 1);
    item = cJSON_GetObjectItem(root, "app_secret");
    if (item && cJSON_IsString(item))
        strncpy(g_config.app_secret, item->valuestring, sizeof(g_config.app_secret) - 1);
    item = cJSON_GetObjectItem(root, "access_token");
    if (item && cJSON_IsString(item))
        strncpy(g_config.access_token, item->valuestring, sizeof(g_config.access_token) - 1);
    item = cJSON_GetObjectItem(root, "device_did");
    if (item && cJSON_IsString(item))
        strncpy(g_config.device_did, item->valuestring, sizeof(g_config.device_did) - 1);
    item = cJSON_GetObjectItem(root, "siid");
    if (item && cJSON_IsNumber(item))
        g_config.siid = item->valueint;
    item = cJSON_GetObjectItem(root, "piid");
    if (item && cJSON_IsNumber(item))
        g_config.piid = item->valueint;

    cJSON_Delete(root);
    return 0;
}

/** 获取 access_token（OAuth2 授权） */
static int cmd_token(void)
{
    printf("=== 获取 access_token ===\n");

    if (strlen(g_config.app_key) == 0 || strlen(g_config.app_secret) == 0) {
        fprintf(stderr, "  [错误] 请先执行 config 配置 AppKey 和 AppSecret\n");
        return 1;
    }

    /* 构建请求参数 */
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "client_id", g_config.app_key);
    cJSON_AddStringToObject(params, "client_secret", g_config.app_secret);
    cJSON_AddStringToObject(params, "grant_type", "client_credentials");

    /* 计算签名 */
    char sign[65] = {0};
    compute_signature(params, sign);
    cJSON_AddStringToObject(params, "sign", sign);

    /* 生成 nonce 和时间戳 */
    char nonce[33];
    generate_nonce(nonce, sizeof(nonce));
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "%ld", (long)time(NULL));
    cJSON_AddStringToObject(params, "nonce", nonce);
    cJSON_AddStringToObject(params, "ts", timestamp);

    /* 构造 POST body（URL 编码格式） */
    char *post_body = calloc(4096, 1);
    int first = 1;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, params) {
        if (!first) strcat(post_body, "&");
        char encoded[512];
        snprintf(encoded, sizeof(encoded), "%s=%s", item->string,
                 cJSON_IsString(item) ? item->valuestring : "");
        strcat(post_body, encoded);
        first = 0;
    }
    cJSON_Delete(params);

    printf("  请求地址: %s\n", TOKEN_URL);
    printf("  签名: %s\n", sign);

    MemoryBuffer resp;
    int ret = http_post_json(TOKEN_URL, post_body, &resp);
    free(post_body);

    if (ret == 0) {
        printf("  响应: %s\n", resp.data);

        /* 解析并保存 token */
        cJSON *root = cJSON_Parse(resp.data);
        if (root) {
            cJSON *token = cJSON_GetObjectItem(root, "access_token");
            if (token && cJSON_IsString(token)) {
                strncpy(g_config.access_token, token->valuestring,
                        sizeof(g_config.access_token) - 1);
                printf("  ✓ access_token 已获取并保存\n");

                /* 更新配置文件 */
                load_config();  /* 先加载确保其他字段不丢失 */
                FILE *fp = fopen(CONFIG_FILE, "r");
                /* 简单地追加 token 到配置文件 */
                cJSON *cfg = cJSON_CreateObject();
                cJSON_AddStringToObject(cfg, "app_key", g_config.app_key);
                cJSON_AddStringToObject(cfg, "app_secret", g_config.app_secret);
                cJSON_AddStringToObject(cfg, "access_token", g_config.access_token);
                cJSON_AddStringToObject(cfg, "device_did", g_config.device_did);
                if (fp) { fclose(fp); }
                char *json = cJSON_Print(cfg);
                fp = fopen(CONFIG_FILE, "w");
                if (fp) { fputs(json, fp); fclose(fp); }
                free(json);
                cJSON_Delete(cfg);
            }
            cJSON_Delete(root);
        }
        buf_free(&resp);
    }
    return ret;
}

/** 查询绑定设备列表 */
static int cmd_devices(void)
{
    printf("=== 查询绑定设备列表 ===\n");

    if (strlen(g_config.access_token) == 0) {
        fprintf(stderr, "  [错误] 请先执行 token 获取 access_token\n");
        return 1;
    }

    /* 构建请求参数 */
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "access_token", g_config.access_token);
    cJSON_AddStringToObject(params, "client_id", g_config.app_key);

    char nonce[33], timestamp[32], sign[65] = {0};
    generate_nonce(nonce, sizeof(nonce));
    snprintf(timestamp, sizeof(timestamp), "%ld", (long)time(NULL));
    cJSON_AddStringToObject(params, "nonce", nonce);
    cJSON_AddStringToObject(params, "ts", timestamp);

    compute_signature(params, sign);
    cJSON_AddStringToObject(params, "sign", sign);

    /* 发送请求 */
    char url[MAX_PATH];
    snprintf(url, sizeof(url), "%s/device/list", API_BASE);

    char *body = calloc(4096, 1);
    int first = 1;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, params) {
        if (!first) strcat(body, "&");
        char part[1024];
        snprintf(part, sizeof(part), "%s=%s", item->string,
                 cJSON_IsString(item) ? item->valuestring : "");
        strcat(body, part);
        first = 0;
    }
    cJSON_Delete(params);

    MemoryBuffer resp;
    int ret = http_post_json(url, body, &resp);
    free(body);

    if (ret == 0) {
        printf("  响应: %s\n", resp.data);
        buf_free(&resp);
    }
    return ret;
}

/** 读写设备属性 */
static int cmd_props(int argc, char *argv[])
{
    printf("=== 设备属性读写 ===\n");

    /* 查找 --did, --siid, --piid 参数 */
    const char *did = NULL;
    int siid = -1, piid = -1;
    const char *value = NULL;  /* 设置属性时的值 */

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--did") == 0 && i + 1 < argc)
            did = argv[++i];
        else if (strcmp(argv[i], "--siid") == 0 && i + 1 < argc)
            siid = atoi(argv[++i]);
        else if (strcmp(argv[i], "--piid") == 0 && i + 1 < argc)
            piid = atoi(argv[++i]);
        else if (strcmp(argv[i], "--value") == 0 && i + 1 < argc)
            value = argv[++i];
    }

    /* 如果命令行没给，用配置文件中的值 */
    if (!did && strlen(g_config.device_did) > 0) did = g_config.device_did;
    if (siid < 0 && g_config.siid > 0) siid = g_config.siid;
    if (piid < 0 && g_config.piid > 0) piid = g_config.piid;

    if (!did || siid < 0 || piid < 0) {
        fprintf(stderr, "  [错误] 请指定 --did, --siid, --piid\n");
        fprintf(stderr, "  用法: props --did <设备DID> --siid <服务ID>"
                " --piid <属性ID> [--value <值>]\n");
        return 1;
    }

    if (strlen(g_config.access_token) == 0) {
        fprintf(stderr, "  [错误] 请先执行 token 获取 access_token\n");
        return 1;
    }

    printf("  设备 DID: %s\n", did);
    printf("  服务 ID:  %d\n", siid);
    printf("  属性 ID:  %d\n", piid);
    printf("  操作:     %s\n", value ? "设置属性" : "读取属性");

    /* 构建请求参数 */
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "access_token", g_config.access_token);
    cJSON_AddStringToObject(params, "client_id", g_config.app_key);

    char nonce[33], timestamp[32], sign[65] = {0};
    generate_nonce(nonce, sizeof(nonce));
    snprintf(timestamp, sizeof(timestamp), "%ld", (long)time(NULL));
    cJSON_AddStringToObject(params, "nonce", nonce);
    cJSON_AddStringToObject(params, "ts", timestamp);

    /* 构建设备属性参数 */
    cJSON *prop_item = cJSON_CreateObject();
    cJSON_AddStringToObject(prop_item, "did", did);
    cJSON_AddNumberToObject(prop_item, "siid", siid);
    cJSON_AddNumberToObject(prop_item, "piid", piid);
    if (value) cJSON_AddStringToObject(prop_item, "value", value);

    char *prop_str = cJSON_PrintUnformatted(prop_item);
    cJSON_AddStringToObject(params, "params", prop_str);
    cJSON_Delete(prop_item);
    free(prop_str);

    compute_signature(params, sign);
    cJSON_AddStringToObject(params, "sign", sign);

    /* 选择 API 端点 */
    const char *endpoint = value ? "/device/props/set" : "/device/props/get";
    char url[MAX_PATH];
    snprintf(url, sizeof(url), "%s%s", API_BASE, endpoint);

    /* 构造 body */
    char *body = calloc(8192, 1);
    int first = 1;
    cJSON *item = NULL;
    cJSON_ArrayForEach(item, params) {
        if (!first) strcat(body, "&");
        char part[2048];
        const char *v = cJSON_IsString(item) ? item->valuestring : "";
        snprintf(part, sizeof(part), "%s=%s", item->string, v);
        strcat(body, part);
        first = 0;
    }
    cJSON_Delete(params);

    printf("  请求地址: %s\n", url);
    printf("  签名: %s\n", sign);

    MemoryBuffer resp;
    int ret = http_post_json(url, body, &resp);
    free(body);

    if (ret == 0) {
        printf("  响应: %s\n", resp.data);
        buf_free(&resp);
    }
    return ret;
}

/** 打印注册指引 */
static int cmd_register(void)
{
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║      小米 IoT 开放平台 — 注册指引            ║\n");
    printf("╠══════════════════════════════════════════════╣\n");
    printf("║                                              ║\n");
    printf("║  1. 访问开放平台:                            ║\n");
    printf("║     https://iot.mi.com/new                    ║\n");
    printf("║                                              ║\n");
    printf("║  2. 注册开发者账号并登录                      ║\n");
    printf("║                                              ║\n");
    printf("║  3. 创建应用，获取:                           ║\n");
    printf("║     - AppKey（应用标识）                      ║\n");
    printf("║     - AppSecret（应用密钥，请妥善保管）        ║\n");
    printf("║                                              ║\n");
    printf("║  4. 在开发者中心绑定设备                      ║\n");
    printf("║                                              ║\n");
    printf("║  5. 使用本工具配置:                           ║\n");
    printf("║     ./mi_iot_client config                    ║\n");
    printf("║                                              ║\n");
    printf("║  6. 获取 access_token:                        ║\n");
    printf("║     ./mi_iot_client token                     ║\n");
    printf("║                                              ║\n");
    printf("║  7. 查询设备或操作属性:                       ║\n");
    printf("║     ./mi_iot_client devices                  ║\n");
    printf("║     ./mi_iot_client props --did xxx ...      ║\n");
    printf("║                                              ║\n");
    printf("║  API 文档: https://iot.mi.com/new/doc        ║\n");
    printf("╚══════════════════════════════════════════════╝\n");
    return 0;
}

/* ========== 帮助信息 ========== */
static void print_usage(const char *prog)
{
    printf("小米 IoT 开放平台客户端\n");
    printf("\n用法: %s <子命令> [参数]\n\n", prog);
    printf("子命令:\n");
    printf("  config                      配置 AppKey/AppSecret/设备信息\n");
    printf("  token                       获取 access_token\n");
    printf("  devices                     查询绑定设备列表\n");
    printf("  props --did D --siid N --piid N [--value V]\n");
    printf("                              读取或设置设备属性\n");
    printf("  register                    打印注册指引\n");
}

/* ========== 主函数 ========== */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    /* 尝试加载已保存的配置 */
    load_config();

    /* 初始化 libcurl */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    int ret = 0;
    if (strcmp(cmd, "config") == 0) {
        ret = cmd_config(argc, argv);
    } else if (strcmp(cmd, "token") == 0) {
        ret = cmd_token();
    } else if (strcmp(cmd, "devices") == 0) {
        ret = cmd_devices();
    } else if (strcmp(cmd, "props") == 0) {
        ret = cmd_props(argc, argv);
    } else if (strcmp(cmd, "register") == 0) {
        ret = cmd_register();
    } else {
        fprintf(stderr, "未知子命令: %s\n\n", cmd);
        print_usage(argv[0]);
        ret = 1;
    }

    curl_global_cleanup();
    return ret;
}
