/**
 * 03_c_demo — python-miio CLI 封装
 *
 * 通过 popen 调用 miiocli（python-miio 的命令行工具），
 * 封装常用操作：info / power / status / discover。
 * 使用 cJSON 解析 miiocli --json 输出。
 *
 * 编译: make
 * 用法: ./miio_cli <子命令> [参数...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

#include "cJSON.h"

/* miiocli 可执行文件名 */
#define MIIOCLI "miiocli"

/* popen 读缓冲区大小 */
#define BUF_SIZE 8192

/* ========== 工具函数 ========== */

/**
 * 检查 miiocli 是否可用，不可用时打印安装指引并退出
 */
static void check_miiocli(void)
{
    if (access(MIIOCLI, X_OK) != 0) {
        fprintf(stderr,
            "[错误] 未找到 '%s'，请先安装 python-miio:\n"
            "  pip3 install python-miio\n"
            "或:\n"
            "  pip install python-miio\n",
            MIIOCLI);
        exit(1);
    }
}

/**
 * 执行 miiocli 命令，将 stdout 读入动态分配的缓冲区。
 * 返回 NULL 表示执行失败。
 * 调用者需 free() 返回的指针。
 */
static char *run_miiocli(const char *fmt, ...)
{
    char cmd[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);

    /* 拼接 --json 参数以获取 JSON 输出 */
    strncat(cmd, " --json 2>/dev/null", sizeof(cmd) - strlen(cmd) - 1);

    FILE *fp = popen(cmd, "r");
    if (!fp) {
        fprintf(stderr, "[错误] 无法执行: %s\n", cmd);
        return NULL;
    }

    char *buf = malloc(BUF_SIZE);
    if (!buf) { pclose(fp); return NULL; }
    buf[0] = '\0';

    size_t total = 0;
    size_t cap = BUF_SIZE;
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (total + len + 1 > cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) { free(buf); pclose(fp); return NULL; }
            buf = tmp;
        }
        memcpy(buf + total, line, len);
        total += len;
        buf[total] = '\0';
    }

    int status = pclose(fp);
    if (status != 0 && total == 0) {
        fprintf(stderr, "[警告] 命令返回非零 (status=%d)\n", status);
    }

    return buf;
}

/**
 * 安全地格式化 IP+Token 参数段
 * " --ip %s --token %s"
 */
static void append_ip_token(char *cmd, size_t sz, const char *ip, const char *token)
{
    char tmp[256];
    snprintf(tmp, sizeof(tmp), " --ip %s --token %s", ip, token);
    strncat(cmd, tmp, sz - strlen(cmd) - 1);
}

/**
 * 漂亮打印 cJSON 对象
 */
static void print_json(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        /* 非 JSON 格式，直接原样输出 */
        printf("%s", json_str);
        return;
    }
    char *pretty = cJSON_Print(root);
    if (pretty) {
        printf("%s\n", pretty);
        free(pretty);
    }
    cJSON_Delete(root);
}

/* ========== 子命令实现 ========== */

/** info <IP> --token <TOKEN> */
static int cmd_info(const char *ip, const char *token)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd), MIIOCLI " genericmiotool");
    append_ip_token(cmd, sizeof(cmd), ip, token);
    /* 使用 info 子命令获取设备信息 */
    snprintf(cmd, sizeof(cmd), MIIOCLI " gateway --ip %s --token %s", ip, token);

    char *out = run_miiocli("%s", cmd);
    if (!out) return 1;
    printf("=== 设备信息 (%s) ===\n", ip);
    print_json(out);
    free(out);
    return 0;
}

/** power <IP> --token <TOKEN> [on|off] */
static int cmd_power(const char *ip, const char *token, const char *state)
{
    if (!state || (strcmp(state, "on") != 0 && strcmp(state, "off") != 0)) {
        fprintf(stderr, "用法: miio_cli power <IP> --token <TOKEN> [on|off]\n");
        return 1;
    }

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        MIIOCLI " plug --ip %s --token %s %s", ip, token, state);

    char *out = run_miiocli("%s", cmd);
    if (!out) return 1;

    printf("=== 电源控制 ===\n");
    printf("设备: %s\n", ip);
    printf("操作: %s\n", state);
    print_json(out);
    free(out);
    return 0;
}

/** status <IP> --token <TOKEN> */
static int cmd_status(const char *ip, const char *token)
{
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        MIIOCLI " plug --ip %s --token %s", ip, token);

    char *out = run_miiocli("%s", cmd);
    if (!out) return 1;

    printf("=== 设备状态 (%s) ===\n", ip);
    print_json(out);
    free(out);
    return 0;
}

/** discover — 发现局域网内米家设备 */
static int cmd_discover(void)
{
    printf("=== 发现局域网设备 ===\n");
    printf("（扫描中，请等待约 10 秒...）\n\n");

    char *out = run_miiocli(MIIOCLI " discover");
    if (!out) return 1;

    /* 逐行解析，每行是一个 JSON 对象 */
    cJSON *item = NULL;
    cJSON *root = cJSON_Parse(out);
    if (root && cJSON_IsArray(root)) {
        cJSON_ArrayForEach(item, root) {
            cJSON *addr = cJSON_GetObjectItem(item, "address");
            cJSON *model = cJSON_GetObjectItem(item, "model");
            cJSON *dev_id = cJSON_GetObjectItem(item, "identifier");
            printf("  IP: %-16s Model: %-30s ID: %s\n",
                addr && addr->valuestring ? addr->valuestring : "?",
                model && model->valuestring ? model->valuestring : "?",
                dev_id && dev_id->valuestring ? dev_id->valuestring : "?");
        }
        cJSON_Delete(root);
    } else {
        /* 非 JSON 格式，原样输出 */
        printf("%s", out);
    }

    free(out);
    return 0;
}

/* ========== 帮助与入口 ========== */

static void usage(const char *prog)
{
    printf("用法: %s <子命令> [参数...]\n\n"
           "子命令:\n"
           "  info    <IP> --token <TOKEN>        获取设备信息\n"
           "  power   <IP> --token <TOKEN> [on|off]  开关控制\n"
           "  status  <IP> --token <TOKEN>        查询设备状态\n"
           "  discover                            发现局域网设备\n"
           "\n"
           "示例:\n"
           "  %s info 192.168.1.100 --token abcdef1234567890\n"
           "  %s power 192.168.1.100 --token abcdef1234567890 on\n"
           "  %s discover\n",
           prog, prog, prog, prog);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    check_miiocli();

    const char *cmd = argv[1];

    /* info <IP> --token <TOKEN> */
    if (strcmp(cmd, "info") == 0) {
        if (argc < 5) {
            fprintf(stderr, "用法: miio_cli info <IP> --token <TOKEN>\n");
            return 1;
        }
        const char *ip = argv[2];
        const char *token = (argc >= 5 && strcmp(argv[3], "--token") == 0) ? argv[4] : "";
        return cmd_info(ip, token);
    }

    /* power <IP> --token <TOKEN> [on|off] */
    if (strcmp(cmd, "power") == 0) {
        if (argc < 6) {
            fprintf(stderr, "用法: miio_cli power <IP> --token <TOKEN> [on|off]\n");
            return 1;
        }
        const char *ip = argv[2];
        const char *token = (argc >= 5 && strcmp(argv[3], "--token") == 0) ? argv[4] : "";
        const char *state = argv[5];
        return cmd_power(ip, token, state);
    }

    /* status <IP> --token <TOKEN> */
    if (strcmp(cmd, "status") == 0) {
        if (argc < 5) {
            fprintf(stderr, "用法: miio_cli status <IP> --token <TOKEN>\n");
            return 1;
        }
        const char *ip = argv[2];
        const char *token = (argc >= 5 && strcmp(argv[3], "--token") == 0) ? argv[4] : "";
        return cmd_status(ip, token);
    }

    /* discover */
    if (strcmp(cmd, "discover") == 0) {
        return cmd_discover();
    }

    /* 未知子命令 */
    fprintf(stderr, "[错误] 未知子命令: %s\n\n", cmd);
    usage(argv[0]);
    return 1;
}
