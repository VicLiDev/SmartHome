/*
 * main.c — Xiaomi MIoT Gateway 主程序
 *
 * 子命令:
 *   scan          扫描局域网设备
 *   info <IP>     查询设备信息
 *   command <IP>  发送自定义命令
 *   gateway       启动守护进程模式（HTTP API）
 *
 * 用法:
 *   ./miio_gateway scan --timeout 5
 *   ./miio_gateway info 192.168.1.100 --token abcdef...
 *   ./miio_gateway command 192.168.1.100 --token abc... --method get_prop --params '["power"]'
 */

#include "miio_protocol.h"
#include "discovery.h"
#include "command.h"

/* 程序名（用于帮助信息） */
#ifndef PROG_NAME
#define PROG_NAME "miio_gateway"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <signal.h>
#include <unistd.h>

/* ═══ 全局状态 ═══ */
static volatile int g_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
    printf("\n[INFO] 收到退出信号\n");
}

/* ═══ 子命令: scan ═══ */

static int cmd_scan(int argc, char **argv)
{
    int timeout = 5;
    int json_output = 0;

    static struct option long_options[] = {
        {"timeout", required_argument, 0, 't'},
        {"json",    no_argument,       0, 'j'},
        {0, 0, 0, 0}
    };

    optind = 2;  /* 跳过 "gateway scan" */
    int c;
    while ((c = getopt_long(argc, argv, "t:j", long_options, NULL)) != -1) {
        switch (c) {
            case 't': timeout = atoi(optarg); break;
            case 'j': json_output = 1; break;
            default: return 1;
        }
    }

    printf("━━━ miIO 设备扫描 ━━━\n");
    printf("超时: %d秒 | 输出格式: %s\n\n",
           timeout, json_output ? "JSON" : "表格");

    MiioDevice devices[MAX_DEVICE_COUNT];
    int count = miio_discover(devices, MAX_DEVICE_COUNT, timeout);

    if (count <= 0) {
        if (json_output)
            printf("{\"devices\":[],\"count\":0}\n");
        else
            printf("❌ 未发现任何设备\n");
        return count == 0 ? 0 : 1;
    }

    if (json_output) {
        printf("{\"devices\":[\n");
        for (int i = 0; i < count; i++) {
            MiioDevice *d = &devices[i];
            printf("  {\"ip\":\"%s\",\"device_id\":\"%08X\","
                   "\"model\":\"%s\",\"online\":%d}%s\n",
                   d->ip, d->device_id, d->model,
                   d->online,
                   (i < count - 1) ? "," : "");
        }
        printf("],\"count\":%d}\n", count);
    } else {
        printf("%-18s %-12s %-20s %-8s\n",
               "IP", "Device ID", "Model", "Online");
        printf("─────────────────────────────────────────────\n");
        for (int i = 0; i < count; i++) {
            MiioDevice *d = &devices[i];
            printf("%-18s %08X      %-20s %s\n",
                   d->ip, d->device_id, d->model,
                   d->online ? "✅" : "❌");
        }
        printf("\n共发现 %d 个设备\n", count);
    }

    return 0;
}

/* ═══ 子命令: info ═══ */

static int cmd_info(int argc, char **argv)
{
    const char *ip = NULL;
    const char *token = NULL;

    static struct option long_options[] = {
        {"token", required_argument, 0, 't'},
        {0, 0, 0, 0}
    };

    optind = 2;
    int c;
    while ((c = getopt_long(argc, argv, "t:", long_options, NULL)) != -1) {
        switch (c) {
            case 't': token = optarg; break;
            default: return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "用法: miio_gateway info <IP> [--token TOKEN]\n");
        return 1;
    }
    ip = argv[optind];

    if (!token || strlen(token) != TOKEN_HEX_LEN) {
        fprintf(stderr, "❌ 需要提供有效的 Token（32位十六进制）\n");
        fprintf(stderr, "获取方法见 README.md 附录 A\n");
        return 1;
    }

    printf("━━━ 查询设备信息 ━━━\n");
    printf("目标: %s (Token: %.8s...)\n\n", ip, token);

    char json_out[4096];
    int ret = miio_get_info(ip, token, json_out, sizeof(json_out));

    if (ret == 0) {
        printf("%s\n", json_out);
    } else {
        printf("❌ 查询失败 (error=%d)\n", ret);
    }

    return ret;
}

/* ═══ 子命令: command ═══ */

static int cmd_command(int argc, char **argv)
{
    const char *ip = NULL;
    const char *token = NULL;
    const char *method = NULL;
    const char *params = "[]";

    static struct option long_options[] = {
        {"token",  required_argument, 0, 't'},
        {"method", required_argument, 0, 'm'},
        {"params", required_argument, 0, 'p'},
        {0, 0, 0, 0}
    };

    optind = 2;
    int c;
    while ((c = getopt_long(argc, argv, "t:m:p:", long_options, NULL)) != -1) {
        switch (c) {
            case 't': token = optarg; break;
            case 'm': method = optarg; break;
            case 'p': params = optarg; break;
            default: return 1;
        }
    }

    if (optind >= argc) { ip = argv[optind]; }
    if (!ip || !token || !method) {
        fprintf(stderr,
                "用法: miio_gateway command <IP> "
                "--token T --method M [--params '[]']\n");
        return 1;
    }

    printf("━━━ 发送命令 ━━━\n");
    printf("目标: %s | 方法: %s | 参数: %s\n\n", ip, method, params);

    MiioResponse resp;
    memset(&resp, 0, sizeof(resp));
    int ret = miio_send_command(ip, MIIO_PORT, token,
                                method, params, 1, &resp, 10);

    if (ret == 0 && resp.error_code == 0) {
        printf("✅ 响应:\n%s\n",
               resp.result_json ? resp.result_json : "(空)");
    } else {
        printf("❌ 错误: %s (code=%d)\n",
               resp.error_msg, resp.error_code);
    }

    if (resp.result_json) free(resp.result_json);
    return ret;
}

/* ═══ 子命令: gateway（守护进程）═══ */

static int cmd_gateway(int argc, char **argv)
{
    int port = 8888;

    static struct option long_options[] = {
        {"port", required_argument, 0, 'p'},
        {0, 0, 0, 0}
    };

    optind = 2;
    int c;
    while ((c = getopt_long(argc, argv, "p:", long_options, NULL)) != -1) {
        switch (c) {
            case 'p': port = atoi(optarg); break;
            default: return 1;
        }
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("━━━ Xiaomi MIoT Gateway ━━━\n");
    printf("API 端口: %d\n", port);
    printf("按 Ctrl+C 停止\n\n");

    /* TODO: 实现 HTTP API 服务端 */
    /* 当前仅作为框架预留 */
    printf("[INFO] 守护进程模式待实现（HTTP API 服务端）\n");
    printf("[INFO] 当前可用: scan / info / command 子命令\n");

    while (g_running) {
        sleep(1);
    }

    printf("[INFO] Gateway 已停止\n");
    return 0;
}

/* ═══ 使用帮助 ═══ */

static void print_usage(void)
{
    printf(
        "Xiaomi MIoT Gateway v1.0 — 米家设备管理工具\n"
        "\n"
        "用法:\n"
        "  %s scan [选项]              扫描局域网设备\n"
        "  %s info <IP> [选项]          查询设备信息\n"
        "  %s command <IP> [选项]       发送自定义命令\n"
        "  %s gateway [选项]            启动守护进程\n"
        "\n"
        "扫描选项:\n"
        "  -t, --timeout SEC   扫描超时（默认5秒）\n"
        "  -j, --json          JSON 格式输出\n"
        "\n"
        "查询/命令选项:\n"
        "  -t, --token TOKEN   设备 Token（32位十六进制）\n"
        "  -m, --method METHOD RPC 方法名\n"
        "  -p, --params PARAMS JSON 参数字符串\n"
        "\n"
        "网关选项:\n"
        "  -p, --port PORT     API 监听端口（默认8888）\n"
        "\n"
        "示例:\n"
        "  %s scan --timeout 3\n"
        "  %s info 192.168.1.100 --token abcdef1234...\n"
        "  %s command 192.168.1.100 -t abc... -m get_prop -p '[\"power\"]'\n"
        "\n"
        "文档: 见 README.md\n",
        PROG_NAME, PROG_NAME, PROG_NAME, PROG_NAME,
        PROG_NAME, PROG_NAME, PROG_NAME
    );
}

/* ═══ 入口 ═══ */

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage();
        return 0;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "scan") == 0)
        return cmd_scan(argc, argv);
    else if (strcmp(cmd, "info") == 0)
        return cmd_info(argc, argv);
    else if (strcmp(cmd, "command") == 0)
        return cmd_command(argc, argv);
    else if (strcmp(cmd, "gateway") == 0)
        return cmd_gateway(argc, argv);
    else if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        print_usage();
        return 0;
    } else {
        fprintf(stderr, "未知子命令: %s\n", cmd);
        print_usage();
        return 1;
    }
}
