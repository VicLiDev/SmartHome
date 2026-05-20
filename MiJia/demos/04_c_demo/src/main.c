/**
 * 04_c_demo — 小米云端 API（micloud）
 *
 * 通过 libcurl 发送 HTTPS 请求与小米云端交互：
 *   login  — 用用户名/密码登录，获取 serviceToken
 *   devices — 获取账号下所有设备列表
 *   token  — 根据 DID 查询设备 token
 *
 * 编译: make
 * 用法: ./micloud <子命令> [参数...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "cJSON.h"

/* ========== 全局状态 ========== */

/* 登录后保存的 serviceToken */
static char g_service_token[1024] = {0};
/* 登录后保存的 userId */
static char g_user_id[128] = {0};

/* ========== curl 回调 ========== */

/**
 * curl 写回调：将响应数据追加到动态缓冲区
 */
static size_t write_cb(void *data, size_t size, size_t nmemb, void *userp)
{
    size_t total = size * nmemb;
    char **buf = (char **)userp;

    /* 计算已有长度 */
    size_t old_len = *buf ? strlen(*buf) : 0;
    char *tmp = realloc(*buf, old_len + total + 1);
    if (!tmp) return 0;
    *buf = tmp;
    memcpy(*buf + old_len, data, total);
    (*buf)[old_len + total] = '\0';

    return total;
}

/**
 * curl header 回调：捕获 Set-Cookie 中的 serviceToken
 */
static size_t header_cb(void *data, size_t size, size_t nmemb, void *userp)
{
    size_t total = size * nmemb;
    char *line = (char *)data;

    /* 查找 serviceToken= */
    char *p = strstr(line, "serviceToken=");
    if (p) {
        p += strlen("serviceToken=");
        /* 提取 token 值（到分号或行尾） */
        char *end = strchr(p, ';');
        if (!end) end = p + strlen(p) - 1; /* 去掉 \r\n */
        size_t len = (size_t)(end - p);
        if (len > 0 && len < sizeof(g_service_token)) {
            memcpy(g_service_token, p, len);
            g_service_token[len] = '\0';
        }
    }

    /* 查找 userId */
    p = strstr(line, "userId=");
    if (p) {
        p += strlen("userId=");
        char *end = strchr(p, ';');
        if (!end) end = p + strlen(p) - 1;
        size_t len = (size_t)(end - p);
        if (len > 0 && len < sizeof(g_user_id)) {
            memcpy(g_user_id, p, len);
            g_user_id[len] = '\0';
        }
    }

    return total;
}

/* ========== HTTP 请求封装 ========== */

/**
 * 执行 HTTPS GET 请求
 */
static int https_get(const char *url, const char *cookie,
                     char **resp_body, long *resp_code)
{
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    *resp_body = NULL;

    /* 设置 User-Agent（模拟 Android 小米 App） */
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers,
        "User-Agent: Android-7.1.1-1.0.0-ONEPLUS A3010-136-QNSUReleaseKeys "
        "APP/xiaomi.smarthome APPV/62830");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp_body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, NULL);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    if (cookie && *cookie) {
        /* 将 serviceToken 附加到 Cookie 中 */
        char full_cookie[2048];
        snprintf(full_cookie, sizeof(full_cookie),
            "userId=%s; serviceToken=%s", g_user_id, g_service_token);
        curl_easy_setopt(curl, CURLOPT_COOKIE, full_cookie);
    }

    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, resp_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (int)res;
}

/**
 * 执行 HTTPS POST 请求（application/x-www-form-urlencoded）
 */
static int https_post(const char *url, const char *post_fields,
                      char **resp_body, long *resp_code)
{
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    *resp_body = NULL;

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers,
        "User-Agent: Android-7.1.1-1.0.0-ONEPLUS A3010-136-QNSUReleaseKeys "
        "APP/xiaomi.smarthome APPV/62830");
    headers = curl_slist_append(headers,
        "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_fields);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, resp_body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, NULL);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, resp_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (int)res;
}

/* ========== 子命令实现 ========== */

/**
 * login — 登录小米账号，获取 serviceToken
 *
 * 流程：
 *  1. GET 登录页获取 sign 等参数（简化版：直接 POST）
 *  2. POST 用户名密码到 /pass/serviceLogin
 *  3. 从响应头中提取 serviceToken
 */
static int cmd_login(const char *user, const char *pass)
{
    if (!user || !pass) {
        fprintf(stderr, "用法: micloud login --user <用户名> --pass <密码>\n");
        return 1;
    }

    printf("=== 登录小米云服务 ===\n");
    printf("用户: %s\n", user);

    /* 构造 POST 数据 */
    char post_data[2048];
    snprintf(post_data, sizeof(post_data),
        "sid=xiaomiio&"
        "_json=true&"
        "user=%s&"
        "hash=%s",  /* 简化：实际需要 MD5(password) + nonce */
        user, pass);

    /* 登录 URL */
    const char *login_url =
        "https://account.xiaomi.com/pass/serviceLogin?sid=xiaomiio&_json=true";

    char *body = NULL;
    long code = 0;

    printf("正在登录...\n");
    int ret = https_post(login_url, post_data, &body, &code);
    if (ret != 0) {
        fprintf(stderr, "[错误] 登录请求失败: curl error %d\n", ret);
        free(body);
        return 1;
    }

    /* 检查 HTTP 状态码 */
    if (code != 200) {
        fprintf(stderr, "[错误] 登录失败 HTTP %ld\n", code);
        if (body) {
            fprintf(stderr, "响应: %s\n", body);
            free(body);
        }
        return 1;
    }

    /* 解析响应（小米 API 可能返回 JSON 或 location 重定向） */
    if (body) {
        /* 检查是否有错误 */
        cJSON *root = cJSON_Parse(body);
        if (root) {
            cJSON *err = cJSON_GetObjectItem(root, "error");
            if (err && cJSON_IsString(err) && strlen(err->valuestring) > 0) {
                fprintf(stderr, "[错误] 登录失败: %s\n", err->valuestring);
                cJSON_Delete(root);
                free(body);
                return 1;
            }
            /* 可能包含 notificationUrl 等 */
            cJSON *loc = cJSON_GetObjectItem(root, "location");
            if (loc && cJSON_IsString(loc)) {
                /* 跟随 location 获取最终 token */
                printf("重定向: %s\n", loc->valuestring);
            }
            cJSON_Delete(root);
        }
        free(body);
    }

    /* serviceToken 已在 header_cb 中提取 */
    if (g_service_token[0] == '\0') {
        fprintf(stderr, "[错误] 未获取到 serviceToken，请检查用户名密码\n");
        fprintf(stderr, "注意: 本 demo 为简化实现，实际需要处理 2FA / 验证码\n");
        return 1;
    }

    printf("登录成功!\n");
    printf("userId: %s\n", g_user_id);
    printf("serviceToken: %s\n", g_service_token);
    return 0;
}

/**
 * devices — 获取设备列表
 *
 * GET https://api.io.mi.com/app/home/device_list
 * Cookie: userId=...; serviceToken=...
 */
static int cmd_devices(void)
{
    if (g_service_token[0] == '\0') {
        fprintf(stderr, "[错误] 请先执行 login 登录\n");
        return 1;
    }

    printf("=== 获取设备列表 ===\n");

    const char *api_url =
        "https://api.io.mi.com/app/home/device_list?master=0&requestId=app_ios_1";

    char *body = NULL;
    long code = 0;

    int ret = https_get(api_url, "cookie", &body, &code);
    if (ret != 0) {
        fprintf(stderr, "[错误] 请求失败: curl error %d\n", ret);
        free(body);
        return 1;
    }

    if (code != 200) {
        fprintf(stderr, "[错误] HTTP %ld\n", code);
        if (body) { fprintf(stderr, "响应: %s\n", body); free(body); }
        return 1;
    }

    /* 解析设备列表 JSON */
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        fprintf(stderr, "[错误] JSON 解析失败\n");
        fprintf(stderr, "原始响应: %s\n", body);
        free(body);
        return 1;
    }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    cJSON *list = NULL;
    if (result && cJSON_IsObject(result)) {
        list = cJSON_GetObjectItem(result, "list");
    }
    if (!list || !cJSON_IsArray(list)) {
        /* 尝试直接在 result 中找 device_list */
        list = cJSON_GetObjectItem(root, "result");
        if (list && !cJSON_IsArray(list)) list = NULL;
    }

    if (list) {
        int count = cJSON_GetArraySize(list);
        printf("共找到 %d 个设备:\n\n", count);
        printf("%-4s %-20s %-16s %-30s %-10s %s\n",
               "#", "名称", "IP", "模型", "在线", "DID");
        printf("---- -------------------- ---------------- "
               "------------------------------ ---------- ----\n");

        cJSON *item = NULL;
        int idx = 0;
        cJSON_ArrayForEach(item, list) {
            idx++;
            cJSON *name  = cJSON_GetObjectItem(item, "name");
            cJSON *localip = cJSON_GetObjectItem(item, "localip");
            cJSON *model = cJSON_GetObjectItem(item, "model");
            cJSON *isonline = cJSON_GetObjectItem(item, "isonline");
            cJSON *did   = cJSON_GetObjectItem(item, "did");
            cJSON *token = cJSON_GetObjectItem(item, "token");

            printf("%-4d %-20s %-16s %-30s %-10s %s",
                idx,
                name && name->valuestring ? name->valuestring : "-",
                localip && localip->valuestring ? localip->valuestring : "-",
                model && model->valuestring ? model->valuestring : "-",
                isonline ? (isonline->valueint ? "在线" : "离线") : "-",
                did && did->valuestring ? did->valuestring : "-");

            if (token && token->valuestring && strlen(token->valuestring) > 0) {
                /* 显示 token 前8位 + ... */
                printf(" (token: %.8s...)", token->valuestring);
            }
            printf("\n");
        }
    } else {
        /* 可能是其他格式的响应 */
        printf("原始响应:\n");
        char *pretty = cJSON_Print(root);
        if (pretty) { printf("%s\n", pretty); free(pretty); }
    }

    cJSON_Delete(root);
    free(body);
    return 0;
}

/**
 * token — 根据 DID 查找设备 token
 *
 * 先获取设备列表，再从中匹配指定 DID 的 token。
 */
static int cmd_token(const char *did)
{
    if (!did) {
        fprintf(stderr, "用法: micloud token --did <设备DID>\n");
        return 1;
    }

    if (g_service_token[0] == '\0') {
        fprintf(stderr, "[错误] 请先执行 login 登录\n");
        return 1;
    }

    printf("=== 查询设备 Token ===\n");
    printf("DID: %s\n", did);

    const char *api_url =
        "https://api.io.mi.com/app/home/device_list?master=0&requestId=app_ios_1";

    char *body = NULL;
    long code = 0;
    int ret = https_get(api_url, "cookie", &body, &code);
    if (ret != 0 || code != 200) {
        fprintf(stderr, "[错误] 获取设备列表失败 (HTTP %ld)\n", code);
        free(body);
        return 1;
    }

    cJSON *root = cJSON_Parse(body);
    if (!root) { free(body); return 1; }

    cJSON *result = cJSON_GetObjectItem(root, "result");
    cJSON *list = result ? cJSON_GetObjectItem(result, "list") : NULL;
    int found = 0;

    if (list && cJSON_IsArray(list)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, list) {
            cJSON *item_did = cJSON_GetObjectItem(item, "did");
            if (item_did && item_did->valuestring &&
                strcmp(item_did->valuestring, did) == 0)
            {
                cJSON *name  = cJSON_GetObjectItem(item, "name");
                cJSON *model = cJSON_GetObjectItem(item, "model");
                cJSON *ip    = cJSON_GetObjectItem(item, "localip");
                cJSON *token = cJSON_GetObjectItem(item, "token");

                printf("找到设备!\n");
                printf("  名称: %s\n",
                    name && name->valuestring ? name->valuestring : "-");
                printf("  模型: %s\n",
                    model && model->valuestring ? model->valuestring : "-");
                printf("  IP:   %s\n",
                    ip && ip->valuestring ? ip->valuestring : "-");
                printf("  Token: %s\n",
                    token && token->valuestring ? token->valuestring : "(未获取到)");
                found = 1;
                break;
            }
        }
    }

    if (!found) {
        printf("未找到 DID=%s 的设备\n", did);
    }

    cJSON_Delete(root);
    free(body);
    return found ? 0 : 1;
}

/* ========== 参数解析 ========== */

/**
 * 从 argv 中查找 --key value 参数
 */
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
           "  login   --user <用户名> --pass <密码>   登录小米云服务\n"
           "  devices                                 获取设备列表\n"
           "  token   --did <设备DID>                 查询设备 Token\n"
           "\n"
           "流程示例:\n"
           "  %s login --user user@example.com --pass mypass\n"
           "  %s devices\n"
           "  %s token --did 123456789\n",
           prog, prog, prog, prog);
}

/* ========== 入口 ========== */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    /* 初始化 libcurl（全局一次） */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    const char *cmd = argv[1];
    int ret = 0;

    if (strcmp(cmd, "login") == 0) {
        const char *user = get_opt(argc, argv, "--user");
        const char *pass = get_opt(argc, argv, "--pass");
        ret = cmd_login(user, pass);
    }
    else if (strcmp(cmd, "devices") == 0) {
        ret = cmd_devices();
    }
    else if (strcmp(cmd, "token") == 0) {
        const char *did = get_opt(argc, argv, "--did");
        ret = cmd_token(did);
    }
    else {
        fprintf(stderr, "[错误] 未知子命令: %s\n\n", cmd);
        usage(argv[0]);
        ret = 1;
    }

    curl_global_cleanup();
    return ret;
}
