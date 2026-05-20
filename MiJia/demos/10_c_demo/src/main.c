/**
 * 10_c_demo — 米家 App 智能场景 HTTP 回调服务器
 *
 * 实现一个轻量级 HTTP 服务器，接收米家 App 智能场景/自动化规则的 HTTP 回调。
 * 使用 raw POSIX socket，无需第三方 HTTP 库。
 * 用法: ./mija_callback_server run [--port P]
 *        ./mija_callback_server test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include "cJSON.h"

/* ========== 配置 ========== */
#define DEFAULT_PORT    8090
#define MAX_REQUEST     8192       /* 最大请求大小 */
#define MAX_EVENTS      100        /* 最大事件日志条数 */
#define MAX_RESPONSE    4096       /* 最大响应大小 */
#define BACKLOG         16         /* 监听队列长度 */

/* ========== 回调事件日志 ========== */
typedef struct {
    time_t timestamp;              /* 事件时间戳 */
    char device[128];              /* 设备标识 */
    char action[128];              /* 动作名称 */
    char value[128];               /* 动作值 */
} CallbackEvent;

static CallbackEvent g_events[MAX_EVENTS];  /* 事件日志数组 */
static int g_event_count = 0;               /* 当前日志条数 */
static volatile int g_running = 1;          /* 服务器运行标志 */

/* ========== 工具函数 ========== */

/** 获取当前时间字符串 */
static const char *time_str(time_t t)
{
    static char buf[64];
    struct tm *tm = localtime(&t);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
    return buf;
}

/** 添加事件到日志 */
static void add_event(const char *device, const char *action, const char *value)
{
    if (g_event_count < MAX_EVENTS) {
        g_event_count++;
    }
    /* 滚动覆盖最旧的事件 */
    CallbackEvent *e = &g_events[g_event_count - 1];
    e->timestamp = time(NULL);
    strncpy(e->device, device ? device : "", sizeof(e->device) - 1);
    strncpy(e->action, action ? action : "", sizeof(e->action) - 1);
    strncpy(e->value, value ? value : "", sizeof(e->value) - 1);
}

/* ========== HTTP 解析器 ========== */

/**
 * 从请求中提取 HTTP 方法
 * @return "GET" 或 "POST" 或 NULL
 */
static const char *parse_method(const char *request)
{
    if (strncmp(request, "GET ", 4) == 0) return "GET";
    if (strncmp(request, "POST ", 5) == 0) return "POST";
    return NULL;
}

/**
 * 从请求中提取 URI 路径
 * @return 指向路径起始位置的指针（静态存储）
 */
static const char *parse_path(const char *request)
{
    /* 跳过 "GET " 或 "POST " 前缀 */
    const char *p = strchr(request, ' ');
    if (!p) return "/";
    p++;  /* 跳过空格 */
    /* 找到路径结束位置（空格或 HTTP/） */
    const char *end = strchr(p, ' ');
    if (!end) end = p + strlen(p);
    static char path_buf[256];
    size_t len = end - p;
    if (len >= sizeof(path_buf)) len = sizeof(path_buf) - 1;
    memcpy(path_buf, p, len);
    path_buf[len] = '\0';
    return path_buf;
}

/**
 * 从请求中提取 JSON body
 * @return 指向 body 的指针（在原始 buffer 内），无 body 则返回 NULL
 */
static const char *parse_body(const char *request)
{
    /* 找到 HTTP header 和 body 之间的空行 (\r\n\r\n) */
    const char *body = strstr(request, "\r\n\r\n");
    if (!body) return NULL;
    body += 4;  /* 跳过 \r\n\r\n */
    if (*body == '\0') return NULL;
    return body;
}

/* ========== HTTP 响应构建 ========== */

/**
 * 构建 HTTP 200 OK 响应
 * @param content_type  "application/json" 或 "text/plain"
 * @param body          响应正文
 * @param out           输出缓冲区
 * @param out_size      缓冲区大小
 * @return 写入的字节数
 */
static int build_response(const char *content_type, const char *body,
                          char *out, int out_size)
{
    return snprintf(out, out_size,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        content_type, strlen(body), body);
}

/**
 * 构建 HTTP 404 响应
 */
static int build_404(char *out, int out_size)
{
    const char *body = "{\"error\":\"not found\"}";
    return snprintf(out, out_size,
        "HTTP/1.1 404 Not Found\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        strlen(body), body);
}

/* ========== 路由处理 ========== */

/** GET /status — 返回运行状态和事件日志 */
static int handle_status(char *out, int out_size)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "running");
    cJSON_AddNumberToObject(root, "event_count", g_event_count);
    cJSON_AddNumberToObject(root, "max_events", MAX_EVENTS);

    /* 构建事件数组 */
    cJSON *events = cJSON_CreateArray();
    for (int i = g_event_count - 1; i >= 0; i--) {
        /* 从最新到最旧排列 */
        CallbackEvent *e = &g_events[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "time", time_str(e->timestamp));
        cJSON_AddStringToObject(item, "device", e->device);
        cJSON_AddStringToObject(item, "action", e->action);
        cJSON_AddStringToObject(item, "value", e->value);
        cJSON_AddItemToArray(events, item);
    }
    cJSON_AddItemToObject(root, "events", events);

    char *json_str = cJSON_Print(root);
    int ret = build_response("application/json", json_str, out, out_size);
    free(json_str);
    cJSON_Delete(root);
    return ret;
}

/** POST /callback — 接收米家回调 */
static int handle_callback(const char *body, char *out, int out_size)
{
    printf("\n  ┌─ 收到米家回调 ─────────────────────┐\n");

    /* 解析 JSON body */
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        printf("  │ JSON 解析失败                       │\n");
        printf("  │ 原始数据: %.60s%-10s      │\n",
               body, strlen(body) > 60 ? "..." : "");
        printf("  └─────────────────────────────────────┘\n");

        const char *err = "{\"error\":\"invalid json\"}";
        return snprintf(out, out_size,
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json\r\n"
            "Connection: close\r\n"
            "Content-Length: %zu\r\n"
            "\r\n%s", strlen(err), err);
    }

    /* 提取回调字段 */
    const char *device = cJSON_GetObjectItem(root, "device") ?
                         cJSON_GetObjectItem(root, "device")->valuestring : "";
    const char *action = cJSON_GetObjectItem(root, "action") ?
                         cJSON_GetObjectItem(root, "action")->valuestring : "";
    const char *value  = cJSON_GetObjectItem(root, "value") ?
                         cJSON_GetObjectItem(root, "value")->valuestring : "";

    printf("  │ 设备: %-30s│\n", device);
    printf("  │ 动作: %-30s│\n", action);
    printf("  │ 值:   %-30s│\n", value);
    printf("  │ 时间: %-30s│\n", time_str(time(NULL)));
    printf("  └─────────────────────────────────────┘\n\n");

    /* 保存到事件日志 */
    add_event(device, action, value);

    /* 构建成功响应 */
    cJSON_Delete(root);
    const char *ok = "{\"status\":\"ok\"}";
    return build_response("application/json", ok, out, out_size);
}

/* ========== 连接处理 ========== */

/** 处理单个客户端连接 */
static void handle_client(int client_fd)
{
    char request[MAX_REQUEST] = {0};
    char response[MAX_RESPONSE] = {0};

    /* 读取请求 */
    ssize_t n = recv(client_fd, request, sizeof(request) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    request[n] = '\0';

    /* 解析请求 */
    const char *method = parse_method(request);
    const char *path   = parse_path(request);
    const char *body   = parse_body(request);

    printf("[%.24s] %s %s\n", time_str(time(NULL)), method ? method : "???",
           path ? path : "/");

    int resp_len = 0;

    /* 路由分发 */
    if (strcmp(path, "/status") == 0 && method && strcmp(method, "GET") == 0) {
        resp_len = handle_status(response, sizeof(response));
    } else if (strcmp(path, "/callback") == 0 && method && strcmp(method, "POST") == 0) {
        resp_len = handle_callback(body, response, sizeof(response));
    } else if (strcmp(path, "/") == 0 && method && strcmp(method, "GET") == 0) {
        /* 根路径，返回简单说明 */
        const char *info = "{\"service\":\"MiJia Callback Server\",\"version\":\"1.0\""
            ",\"endpoints\":[\"GET /status\",\"POST /callback\"]}";
        resp_len = build_response("application/json", info, response, sizeof(response));
    } else {
        resp_len = build_404(response, sizeof(response));
    }

    /* 发送响应 */
    if (resp_len > 0) {
        send(client_fd, response, resp_len, 0);
    }
    close(client_fd);
}

/* ========== 信号处理 ========== */
static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
    printf("\n  收到退出信号，正在关闭服务器...\n");
}

/* ========== 服务器启动 ========== */

/**
 * 启动 HTTP 回调服务器
 * @param port  监听端口
 */
static int run_server(int port)
{
    /* 设置信号处理，优雅退出 */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* 创建 TCP socket */
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("  [错误] 创建 socket 失败");
        return 1;
    }

    /* 允许地址复用，避免 TIME_WAIT 状态导致绑定失败 */
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    /* 绑定地址 */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  /* 0.0.0.0 */
    addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "  [错误] 绑定端口 %d 失败: %s\n", port, strerror(errno));
        close(server_fd);
        return 1;
    }

    /* 开始监听 */
    if (listen(server_fd, BACKLOG) < 0) {
        perror("  [错误] 监听失败");
        close(server_fd);
        return 1;
    }

    printf("╔══════════════════════════════════════╗\n");
    printf("║   米家 HTTP 回调服务器已启动          ║\n");
    printf("║   监听地址: 0.0.0.0:%-16d║\n", port);
    printf("║   GET  /status   - 查看状态和事件日志  ║\n");
    printf("║   POST /callback - 接收米家回调        ║\n");
    printf("║   按 Ctrl+C 停止                      ║\n");
    printf("╚══════════════════════════════════════╝\n\n");

    /* 主循环 */
    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0) {
            if (g_running) perror("  [错误] accept 失败");
            continue;
        }

        /* 简单打印客户端信息 */
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("  [连接] %s:%d\n", client_ip, ntohs(client_addr.sin_port));

        handle_client(client_fd);
    }

    close(server_fd);
    printf("  服务器已停止。共接收 %d 个回调事件。\n", g_event_count);
    return 0;
}

/* ========== 测试子命令 ========== */

/**
 * 发送测试回调请求（使用 curl 命令）
 * @param port  服务器端口
 */
static int run_test(int port)
{
    printf("=== 发送测试回调 ===\n");
    printf("  目标: http://localhost:%d/callback\n\n", port);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "curl -s -X POST http://localhost:%d/callback "
        "-H 'Content-Type: application/json' "
        "-d '{\"device\":\"test_sensor_01\",\"action\":\"motion_detected\","
        "\"value\":\"true\"}'",
        port);

    printf("  请求: %s\n\n", cmd);

    /* 使用 system() 调用 curl 发送测试请求 */
    int ret = system(cmd);
    if (ret == 0) {
        printf("\n  ✓ 测试回调发送成功\n");

        /* 同时测试 /status 端点 */
        printf("\n  查询状态:\n");
        snprintf(cmd, sizeof(cmd),
            "curl -s http://localhost:%d/status", port);
        system(cmd);
        printf("\n");
    } else {
        printf("  ✗ 测试失败（服务器是否已启动？）\n");
    }
    return ret == 0 ? 0 : 1;
}

/* ========== 帮助信息 ========== */
static void print_usage(const char *prog)
{
    printf("米家 App 智能场景 HTTP 回调服务器\n");
    printf("\n用法:\n");
    printf("  %s run [--port P]   启动 HTTP 回调服务器（默认端口 %d）\n",
           prog, DEFAULT_PORT);
    printf("  %s test             发送测试回调请求\n", prog);
    printf("\n回调格式:\n");
    printf("  POST /callback\n");
    printf("  Content-Type: application/json\n");
    printf("  {\"device\":\"设备ID\",\"action\":\"动作\",\"value\":\"值\"}\n");
    printf("\nAPI:\n");
    printf("  GET  /status   - 查看运行状态和事件日志\n");
    printf("  POST /callback - 接收米家智能场景回调\n");
}

/* ========== 主函数 ========== */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];
    int port = DEFAULT_PORT;

    /* 解析 --port 参数 */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
            if (port <= 0 || port > 65535) {
                fprintf(stderr, "  [错误] 无效端口号: %s\n", argv[i]);
                return 1;
            }
        }
    }

    if (strcmp(cmd, "run") == 0) {
        return run_server(port);
    } else if (strcmp(cmd, "test") == 0) {
        return run_test(port);
    } else {
        fprintf(stderr, "未知子命令: %s\n\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
