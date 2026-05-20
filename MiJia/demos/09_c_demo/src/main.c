/**
 * 09_c_demo — Node-RED 流程管理客户端
 *
 * 通过 HTTP REST API 与 Node-RED 交互，管理自动化流程。
 * 依赖: libcurl, cJSON
 * 用法: ./nodered_client <子命令> [参数]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "cJSON.h"

/* ========== 配置 ========== */
#define NODE_RED_URL  "http://localhost:1880"
#define MAX_RESPONSE  (256 * 1024)   /* 最大响应 256KB */

/* 可通过环境变量 NODE_RED_TOKEN 设置认证令牌 */
static const char *get_token(void)
{
    const char *t = getenv("NODE_RED_TOKEN");
    return t ? t : "";
}

/* ========== libcurl 回调：将响应写入内存 ========== */
typedef struct {
    char *data;
    size_t size;
} MemoryBuffer;

static size_t write_callback(void *contents, size_t sz, size_t n, void *userp)
{
    size_t total = sz * n;
    MemoryBuffer *buf = (MemoryBuffer *)userp;
    if (buf->size + total + 1 > MAX_RESPONSE)
        return 0;  /* 超限，丢弃 */
    buf->data = realloc(buf->data, buf->size + total + 1);
    if (!buf->data) return 0;
    memcpy(buf->data + buf->size, contents, total);
    buf->size += total;
    buf->data[buf->size] = '\0';
    return total;
}

/* 初始化内存缓冲区 */
static void buffer_init(MemoryBuffer *buf)
{
    buf->data = malloc(1);
    buf->data[0] = '\0';
    buf->size = 0;
}

/* 释放内存缓冲区 */
static void buffer_free(MemoryBuffer *buf)
{
    free(buf->data);
    buf->data = NULL;
    buf->size = 0;
}

/* ========== 通用 HTTP 请求 ========== */

/**
 * 发送 GET 请求
 * @param path  API 路径（如 "/flows"）
 * @param out   响应内容输出
 * @return 0 成功，非 0 失败
 */
static int http_get(const char *path, MemoryBuffer *out)
{
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    char url[512];
    snprintf(url, sizeof(url), "%s%s", NODE_RED_URL, path);

    buffer_init(out);

    /* 构造认证头 */
    char auth_header[1024];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", get_token());

    struct curl_slist *headers = NULL;
    if (strlen(get_token()) > 0)
        headers = curl_slist_append(headers, auth_header);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "  [错误] 请求失败: %s\n", curl_easy_strerror(res));
        buffer_free(out);
        return -1;
    }
    if (http_code != 200) {
        fprintf(stderr, "  [错误] HTTP %ld\n", http_code);
        buffer_free(out);
        return -1;
    }
    return 0;
}

/**
 * 发送 POST 请求（JSON body）
 * @param path  API 路径
 * @param body  JSON 字符串（可为 NULL）
 * @param out   响应内容输出
 * @return 0 成功，非 0 失败
 */
static int http_post(const char *path, const char *body, MemoryBuffer *out)
{
    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    char url[512];
    snprintf(url, sizeof(url), "%s%s", NODE_RED_URL, path);

    buffer_init(out);

    char auth_header[1024];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", get_token());

    struct curl_slist *headers = NULL;
    if (strlen(get_token()) > 0)
        headers = curl_slist_append(headers, auth_header);
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body ? body : "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "  [错误] 请求失败: %s\n", curl_easy_strerror(res));
        buffer_free(out);
        return -1;
    }
    if (http_code < 200 || http_code >= 300) {
        fprintf(stderr, "  [错误] HTTP %ld\n", http_code);
        buffer_free(out);
        return -1;
    }
    return 0;
}

/* ========== 子命令实现 ========== */

/** 列出所有流程 */
static int cmd_flows(void)
{
    printf("=== 列出所有 Node-RED 流程 ===\n");
    MemoryBuffer resp;
    if (http_get("/flows", &resp) != 0) return 1;

    cJSON *root = cJSON_Parse(resp.data);
    if (!root) {
        fprintf(stderr, "  [错误] JSON 解析失败\n");
        buffer_free(&resp);
        return 1;
    }

    /* Node-RED 返回格式: {"rev":"xxx","flows":[...]} */
    cJSON *flows = cJSON_GetObjectItem(root, "flows");
    int count = cJSON_GetArraySize(flows);
    printf("  修订版本: %s\n", cJSON_GetObjectItem(root, "rev")->valuestring);
    printf("  流程数量: %d\n\n", count);

    for (int i = 0; i < count; i++) {
        cJSON *f = cJSON_GetArrayItem(flows, i);
        const char *type = cJSON_GetObjectItem(f, "type")->valuestring;
        const char *id   = cJSON_GetObjectItem(f, "id")->valuestring;
        const char *name = cJSON_GetObjectItem(f, "name") ?
                           cJSON_GetObjectItem(f, "name")->valuestring : "(未命名)";
        const char *tab  = cJSON_GetObjectItem(f, "tab") ?
                           cJSON_GetObjectItem(f, "tab")->valuestring : NULL;

        /* 只显示顶层 flow (tab) 类型和普通节点 */
        if (strcmp(type, "tab") == 0) {
            printf("  [%s] 📑 %s\n", type, name);
        } else if (tab && strlen(tab) > 0) {
            /* 属于某个 tab 的节点，简要统计 */
            continue;  /* 太多节点时跳过，只显示 tab */
        } else {
            printf("  [%s] %s\n", type, name);
        }
    }

    /* 统计各类型节点数量 */
    cJSON *types_count = cJSON_CreateObject();
    for (int i = 0; i < count; i++) {
        cJSON *f = cJSON_GetArrayItem(flows, i);
        const char *t = cJSON_GetObjectItem(f, "type")->valuestring;
        if (strcmp(t, "tab") == 0) continue;
        int cnt = cJSON_GetObjectItem(types_count, t) ?
                  cJSON_GetObjectItem(types_count, t)->valueint : 0;
        cJSON_ReplaceItemInObject(types_count, t,
            cJSON_CreateNumber(cnt + 1));
    }
    printf("\n  节点类型统计:\n");
    cJSON *t = NULL;
    cJSON_ArrayForEach(t, types_count) {
        printf("    %-30s %d 个\n", t->string, t->valueint);
    }

    cJSON_Delete(types_count);
    cJSON_Delete(root);
    buffer_free(&resp);
    return 0;
}

/** 导出流程到文件 */
static int cmd_export(const char *filename)
{
    if (!filename) {
        fprintf(stderr, "  [错误] 请指定导出文件名\n");
        return 1;
    }
    printf("=== 导出流程到 %s ===\n", filename);

    MemoryBuffer resp;
    if (http_get("/flows", &resp) != 0) return 1;

    FILE *fp = fopen(filename, "w");
    if (!fp) {
        fprintf(stderr, "  [错误] 无法创建文件: %s\n", filename);
        buffer_free(&resp);
        return 1;
    }
    /* 美化 JSON 输出 */
    cJSON *root = cJSON_Parse(resp.data);
    if (root) {
        char *pretty = cJSON_Print(root);
        fputs(pretty, fp);
        free(pretty);
        cJSON_Delete(root);
    } else {
        fputs(resp.data, fp);
    }
    fclose(fp);
    printf("  ✓ 导出成功 (%zu bytes)\n", resp.size);
    buffer_free(&resp);
    return 0;
}

/** 从文件导入流程 */
static int cmd_import(const char *filename)
{
    if (!filename) {
        fprintf(stderr, "  [错误] 请指定导入文件名\n");
        return 1;
    }
    printf("=== 从 %s 导入流程 ===\n", filename);

    /* 读取文件内容 */
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "  [错误] 无法打开文件: %s\n", filename);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *body = malloc(fsize + 1);
    fread(body, 1, fsize, fp);
    body[fsize] = '\0';
    fclose(fp);

    /* 验证 JSON 格式 */
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        fprintf(stderr, "  [错误] 文件不是有效的 JSON\n");
        free(body);
        return 1;
    }
    /* Node-RED POST /flows 需要数组格式 */
    cJSON *flows = cJSON_GetObjectItem(root, "flows");
    char *payload;
    if (flows && cJSON_IsArray(flows)) {
        payload = cJSON_PrintUnformatted(flows);
    } else if (cJSON_IsArray(root)) {
        payload = cJSON_PrintUnformatted(root);
    } else {
        fprintf(stderr, "  [错误] JSON 格式不符合 Node-RED 要求\n");
        cJSON_Delete(root);
        free(body);
        return 1;
    }

    cJSON_Delete(root);
    free(body);

    MemoryBuffer resp;
    int ret = http_post("/flows", payload, &resp);
    free(payload);
    if (ret == 0) {
        printf("  ✓ 导入成功，请执行 deploy 使其生效\n");
        buffer_free(&resp);
    }
    return ret;
}

/** 部署流程 */
static int cmd_deploy(void)
{
    printf("=== 部署流程 ===\n");
    MemoryBuffer resp;
    int ret = http_post("/flows", "{}", &resp);
    if (ret == 0) {
        printf("  ✓ 部署成功\n");
        buffer_free(&resp);
    }
    return ret;
}

/** 列出已安装节点 */
static int cmd_nodes(void)
{
    printf("=== 列出已安装的 Node-RED 节点 ===\n");
    MemoryBuffer resp;
    if (http_get("/nodes", &resp) != 0) return 1;

    cJSON *root = cJSON_Parse(resp.data);
    if (!root) {
        fprintf(stderr, "  [错误] JSON 解析失败\n");
        buffer_free(&resp);
        return 1;
    }

    int count = cJSON_GetArraySize(root);
    printf("  已安装节点数: %d\n\n", count);

    for (int i = 0; i < count; i++) {
        cJSON *n = cJSON_GetArrayItem(root, i);
        const char *id    = cJSON_GetObjectItem(n, "id")->valuestring;
        const char *name  = cJSON_GetObjectItem(n, "name") ?
                           cJSON_GetObjectItem(n, "name")->valuestring : "";
        const char *types = cJSON_GetObjectItem(n, "types") ?
                           cJSON_GetObjectItem(n, "types")->valuestring : "";
        int enabled = cJSON_GetObjectItem(n, "enabled")->valueint;

        printf("  %s %-30s %s%s\n",
               enabled ? "✓" : "✗",
               id,
               name,
               types);
    }

    cJSON_Delete(root);
    buffer_free(&resp);
    return 0;
}

/** 显示 Node-RED 信息 */
static int cmd_info(void)
{
    printf("=== Node-RED 信息 ===\n");
    printf("  连接地址: %s\n", NODE_RED_URL);
    printf("  认证令牌: %s\n",
           strlen(get_token()) > 0 ? "（已配置）" : "（未配置）");

    /* 尝试获取 /flows 以验证连接 */
    MemoryBuffer resp;
    if (http_get("/flows", &resp) != 0) return 1;

    cJSON *root = cJSON_Parse(resp.data);
    if (!root) {
        fprintf(stderr, "  [错误] JSON 解析失败\n");
        buffer_free(&resp);
        return 1;
    }

    cJSON *rev = cJSON_GetObjectItem(root, "rev");
    cJSON *flows = cJSON_GetObjectItem(root, "flows");
    int flow_count = flows ? cJSON_GetArraySize(flows) : 0;
    int tab_count = 0;
    if (flows) {
        for (int i = 0; i < flow_count; i++) {
            cJSON *f = cJSON_GetArrayItem(flows, i);
            if (strcmp(cJSON_GetObjectItem(f, "type")->valuestring, "tab") == 0)
                tab_count++;
        }
    }

    printf("  状态: 已连接 ✓\n");
    printf("  修订版本: %s\n", rev ? rev->valuestring : "未知");
    printf("  流程标签页: %d 个\n", tab_count);
    printf("  总节点数: %d 个\n", flow_count);

    cJSON_Delete(root);
    buffer_free(&resp);
    return 0;
}

/* ========== 帮助信息 ========== */
static void print_usage(const char *prog)
{
    printf("Node-RED 流程管理客户端\n");
    printf("\n用法: %s <子命令> [参数]\n", prog);
    printf("\n子命令:\n");
    printf("  flows          列出所有流程\n");
    printf("  export [FILE]  导出流程到 JSON 文件\n");
    printf("  import <FILE>  从 JSON 文件导入流程\n");
    printf("  deploy         部署当前流程\n");
    printf("  nodes          列出已安装节点\n");
    printf("  info           显示连接信息\n");
    printf("\n环境变量:\n");
    printf("  NODE_RED_TOKEN  Node-RED 认证令牌\n");
    printf("  NODE_RED_URL    Node-RED 地址（默认 %s）\n", NODE_RED_URL);
}

/* ========== 主函数 ========== */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    /* 全局初始化 libcurl */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    int ret = 0;
    if (strcmp(cmd, "flows") == 0) {
        ret = cmd_flows();
    } else if (strcmp(cmd, "export") == 0) {
        ret = cmd_export(argc >= 3 ? argv[2] : NULL);
    } else if (strcmp(cmd, "import") == 0) {
        ret = cmd_import(argc >= 3 ? argv[2] : NULL);
    } else if (strcmp(cmd, "deploy") == 0) {
        ret = cmd_deploy();
    } else if (strcmp(cmd, "nodes") == 0) {
        ret = cmd_nodes();
    } else if (strcmp(cmd, "info") == 0) {
        ret = cmd_info();
    } else {
        fprintf(stderr, "未知子命令: %s\n\n", cmd);
        print_usage(argv[0]);
        ret = 1;
    }

    curl_global_cleanup();
    return ret;
}
