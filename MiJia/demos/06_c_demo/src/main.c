/**
 * 06_c_demo - BLE 蓝牙被动监听
 *
 * 使用 Linux BlueZ HCI 接口被动监听小米 BLE 广播帧（MiBeacon）。
 * 支持三种子命令:
 *   scan          - 持续监听 BLE 广播，解析 MiBeacon 帧
 *   parse --hex H - 解析给定的十六进制 MiBeacon 帧数据
 *   info          - 打印 MiBeacon 协议结构说明
 *
 * 编译: make
 * 运行: sudo ./ble_monitor scan        (需要 root 权限访问 HCI)
 *       ./ble_monitor parse --hex 5020...
 *       ./ble_monitor info
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

/* ========== MiBeacon 帧常量 ========== */

/* 小米 BLE 服务 UUID (16-bit): 0xFE95，但广播中是大端 0x95FE */
#define MIBEACON_SERVICE_UUID  0xFE95

/* MiBeacon 帧控制字段 (Frame Control) 位定义 */
#define MIBEACON_FC_IS_ENCRYPTED    (1 << 4)   /* Bit4: 数据是否加密 */
#define MIBEACON_FC_HAS_MAC         (1 << 3)   /* Bit3: 是否包含 MAC */
#define MIBEACON_FC_HAS_CAP         (1 << 2)   /* Bit2: 是否包含能力标志 */
#define MIBEACON_FC_IS_OBJ          (1 << 1)   /* Bit1: 是否包含对象 */
#define MIBEACON_FC_HAS_NW_KEY      (1 << 0)   /* Bit0: 是否包含网络密钥ID */

/* MiBeacon 帧结构 (不含 2 字节 Service UUID) */
typedef struct {
    uint8_t  frame_ctrl;      /* 帧控制 */
    uint8_t  proto_version;   /* 协议版本 */
    uint16_t random;          /* 随机数 (2B) */
    uint16_t product_id;      /* 产品 ID (2B) */
    uint8_t  mac[6];          /* 设备 MAC (6B, 小端序) */
    uint8_t  capability;      /* 能力标志 (1B) */
    /* 后续为可变长度: Data (可能加密) + Misc */
} mibeacon_header_t;

/* 全局退出标志，用于 Ctrl+C 优雅退出 */
static volatile int g_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
    printf("\n[*] 正在停止监听...\n");
}

/* ========== 工具函数 ========== */

/**
 * 将单个十六进制字符转换为数值
 */
static int hex_char_to_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * 将十六进制字符串解析为字节数组
 * 返回: 解析出的字节数，-1 表示格式错误
 */
static int hex_to_bytes(const char *hex_str, uint8_t *out, int max_len)
{
    int len = strlen(hex_str);
    if (len % 2 != 0) return -1;
    int byte_count = len / 2;
    if (byte_count > max_len) byte_count = max_len;

    for (int i = 0; i < byte_count; i++) {
        int hi = hex_char_to_val(hex_str[i * 2]);
        int lo = hex_char_to_val(hex_str[i * 2 + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return byte_count;
}

/**
 * 打印 MAC 地址 (蓝牙地址格式)
 */
static void print_mac(const uint8_t *mac)
{
    /* 蓝牙广播中 MAC 通常是倒序存储的 (小端) */
    printf("%02X:%02X:%02X:%02X:%02X:%02X",
           mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
}

/**
 * 获取当前时间字符串
 */
static const char *time_str(void)
{
    static char buf[32];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, sizeof(buf), "%H:%M:%S", tm_info);
    return buf;
}

/* ========== MiBeacon 解析 ========== */

/**
 * 尝试从原始广播数据中查找并解析 MiBeacon 帧
 *
 * BLE 广播数据格式 (GAP):
 *   [len][type][data...] [len][type][data...] ...
 *   其中 type=0x16 表示 Service Data (16-bit UUID)
 *
 * 参数:
 *   data     - 原始 HCI 事件中的广播数据
 *   data_len - 广播数据长度
 *
 * 返回: 1 表示找到并解析了 MiBeacon, 0 表示不是 MiBeacon
 */
static int parse_mibeacon(const uint8_t *data, int data_len)
{
    int offset = 0;
    while (offset + 2 < data_len) {
        uint8_t field_len = data[offset];
        uint8_t field_type = data[offset + 1];

        if (field_len < 2 || offset + field_len > data_len) {
            break;
        }

        /* 类型 0x16 = Service Data (16-bit UUID) */
        if (field_type == 0x16 && field_len >= 4) {
            uint16_t uuid = (uint16_t)(data[offset + 3] << 8 | data[offset + 2]);

            if (uuid == MIBEACON_SERVICE_UUID) {
                /* 找到 MiBeacon 数据，开始解析 */
                const uint8_t *frame = data + offset + 4;
                int frame_len = field_len - 4;

                if (frame_len < 5) {
                    printf("  [!] MiBeacon 帧过短 (%d 字节)\n", frame_len);
                    return 1;
                }

                uint8_t frame_ctrl = frame[0];
                uint8_t proto_ver  = frame[1];
                uint16_t random    = (uint16_t)(frame[3] << 8 | frame[2]);
                uint16_t product_id = (uint16_t)(frame[5] << 8 | frame[4]);

                printf("  时间: %s\n", time_str());
                printf("  协议版本: %d\n", proto_ver);
                printf("  产品ID:   0x%04X\n", product_id);
                printf("  随机数:   0x%04X\n", random);
                printf("  帧控制:   0x%02X [", frame_ctrl);
                printf("加密=%s", (frame_ctrl & MIBEACON_FC_IS_ENCRYPTED) ? "是" : "否");
                printf(", MAC=%s",   (frame_ctrl & MIBEACON_FC_HAS_MAC) ? "有" : "无");
                printf(", 能力=%s",  (frame_ctrl & MIBEACON_FC_HAS_CAP) ? "有" : "无");
                printf(", 对象=%s",  (frame_ctrl & MIBEACON_FC_IS_OBJ) ? "有" : "无");
                printf(", NWKey=%s]\n", (frame_ctrl & MIBEACON_FC_HAS_NW_KEY) ? "有" : "无");

                int pos = 6;

                /* 解析 MAC 地址 (如果存在) */
                if (frame_ctrl & MIBEACON_FC_HAS_MAC) {
                    if (pos + 6 <= frame_len) {
                        printf("  MAC: ");
                        print_mac(frame + pos);
                        printf("\n");
                        pos += 6;
                    }
                }

                /* 解析能力标志 (如果存在) */
                if (frame_ctrl & MIBEACON_FC_HAS_CAP) {
                    if (pos + 1 <= frame_len) {
                        printf("  能力标志: 0x%02X\n", frame[pos]);
                        pos += 1;
                    }
                }

                /* 解析剩余数据 */
                int data_remaining = frame_len - pos;
                if (data_remaining > 0) {
                    printf("  数据长度: %d 字节\n", data_remaining);
                    printf("  数据(hex): ");
                    for (int i = 0; i < data_remaining && i < 32; i++) {
                        printf("%02X ", frame[pos + i]);
                    }
                    if (data_remaining > 32) printf("...");
                    printf("\n");

                    /* 尝试解析包含对象的 TLV 数据 */
                    if (frame_ctrl & MIBEACON_FC_IS_OBJ) {
                        int obj_pos = 0;
                        while (obj_pos + 2 < data_remaining) {
                            uint8_t obj_len = frame[pos + obj_pos];
                            uint8_t obj_type = frame[pos + obj_pos + 1];
                            printf("    TLV: type=0x%02X len=%d\n", obj_type, obj_len);
                            obj_pos += obj_len + 1;
                            if (obj_pos >= data_remaining) break;
                        }
                    }
                }

                printf("  ─────────────────────────\n");
                return 1;
            }
        }
        offset += field_len + 1;
    }
    return 0;
}

/**
 * 解析命令行给定的十六进制帧数据
 */
static int cmd_parse(const char *hex)
{
    uint8_t buf[512];
    int len = hex_to_bytes(hex, buf, sizeof(buf));

    if (len < 0) {
        fprintf(stderr, "[!] 无效的十六进制字符串: %s\n", hex);
        return -1;
    }

    printf("[*] 解析 %d 字节 MiBeacon 帧数据:\n", len);
    printf("══════════════════════════════════\n");

    int found = parse_mibeacon(buf, len);
    if (!found) {
        /* 如果没有 Service UUID 包装，尝试直接解析为 MiBeacon 帧体 */
        printf("[*] 未找到 0xFE95 Service UUID 包装，尝试直接解析帧体...\n");
        /* 构造一个假的 Service Data 包装头 */
        uint8_t wrapped[520];
        int wlen = 0;
        /* AD Type 0x16, Length = frame_len + 2 (UUID) */
        wrapped[wlen++] = len + 2;   /* AD Length */
        wrapped[wlen++] = 0x16;      /* AD Type: Service Data (16-bit) */
        wrapped[wlen++] = 0x95;      /* UUID low byte */
        wrapped[wlen++] = 0xFE;      /* UUID high byte */
        memcpy(wrapped + wlen, buf, len);
        wlen += len;
        parse_mibeacon(wrapped, wlen);
    }

    return 0;
}

/**
 * 打印 MiBeacon 协议说明
 */
static void cmd_info(void)
{
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║              MiBeacon 协议结构说明                       ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║ BLE 广播帧 (AD Structure):                              ║\n");
    printf("║   [Length][Type=0x16][UUID: 0x95FE][MiBeacon Data...]   ║\n");
    printf("║                                                          ║\n");
    printf("║ MiBeacon 数据结构:                                       ║\n");
    printf("║   ┌──────────────┬──────┬────────┬────────────┐         ║\n");
    printf("║   │ Frame Control│ Proto│ Random │ Product ID │         ║\n");
    printf("║   │   (1 byte)   │(1B)  │ (2B)   │   (2B)     │         ║\n");
    printf("║   ├──────────────┴──────┴────────┴────────────┤         ║\n");
    printf("║   │   MAC (6B, 可选)  │ Capability (1B, 可选) │         ║\n");
    printf("║   ├───────────────────┴───────────────────────┤         ║\n");
    printf("║   │         Data / Encrypted Data (可变)      │         ║\n");
    printf("║   └───────────────────────────────────────────┘         ║\n");
    printf("║                                                          ║\n");
    printf("║ Frame Control 位定义:                                    ║\n");
    printf("║   Bit 0: 包含网络密钥 ID                                 ║\n");
    printf("║   Bit 1: 包含对象数据                                    ║\n");
    printf("║   Bit 2: 包含能力标志                                    ║\n");
    printf("║   Bit 3: 包含 MAC 地址                                   ║\n");
    printf("║   Bit 4: 数据已加密                                     ║\n");
    printf("║   Bit 5-7: 保留                                          ║\n");
    printf("║                                                          ║\n");
    printf("║ 常见小米 BLE 产品 ID 示例:                               ║\n");
    printf("║   0x0043 - 温湿度传感器 (MJ_HT_V1)                      ║\n");
    printf("║   0x01AA - 花花草草植物传感器                             ║\n");
    printf("║   0x047B - 米家门锁                                      ║\n");
    printf("║   0x0347 - 米家蓝牙温湿度计2                             ║\n");
    printf("║   0x0576 - 米家蓝牙网关                                  ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
}

/**
 * 使用 HCI raw socket 被动监听 BLE 广播
 */
static int cmd_scan(void)
{
    int dev_id = hci_get_route(NULL);
    if (dev_id < 0) {
        fprintf(stderr, "[!] 未找到蓝牙适配器: %s\n", strerror(errno));
        fprintf(stderr, "[!] 请确保蓝牙已启用，或尝试: sudo hciconfig hci0 up\n");
        return -1;
    }

    int dd = hci_open_dev(dev_id);
    if (dd < 0) {
        fprintf(stderr, "[!] 无法打开 HCI 设备: %s\n", strerror(errno));
        return -1;
    }

    /* 设置 LE 扫描参数 */
    int err;
    err = hci_le_set_scan_parameters(dd, 0x01, htobs(0x0010), htobs(0x0010),
                                     0x00, 0x00, 1000);
    if (err < 0) {
        fprintf(stderr, "[!] 设置扫描参数失败: %s\n", strerror(errno));
        hci_close_dev(dd);
        return -1;
    }

    /* 启用 LE 扫描 (被动扫描: 0x00) */
    err = hci_le_set_scan_enable(dd, 0x01, 0x00, 1000);
    if (err < 0) {
        fprintf(stderr, "[!] 启用 LE 扫描失败: %s\n", strerror(errno));
        hci_close_dev(dd);
        return -1;
    }

    printf("[*] 开始 BLE 广播监听 (HCI dev %d, 被动模式)...\n", dev_id);
    printf("[*] 按 Ctrl+C 停止\n");
    printf("══════════════════════════════════════════════════════════\n");

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    struct hci_filter nf, of;
    socklen_t olen = sizeof(of);
    getsockopt(dd, SOL_HCI, HCI_FILTER, &of, &olen);

    hci_filter_clear(&nf);
    hci_filter_set_ptype(HCI_EVENT_PKT, &nf);
    hci_filter_set_event(EVT_LE_META_EVENT, &nf);
    setsockopt(dd, SOL_HCI, HCI_FILTER, &nf, sizeof(nf));

    unsigned char buf[HCI_MAX_EVENT_SIZE];
    int device_count = 0;

    while (g_running) {
        ssize_t len = read(dd, buf, sizeof(buf));
        if (len < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "[!] 读取 HCI 事件失败: %s\n", strerror(errno));
            break;
        }

        /* 解析 HCI 事件头 */
        if (buf[0] != HCI_EVENT_PKT) continue;

        hci_event_hdr *hdr = (hci_event_hdr *)(buf + 1);
        if (hdr->evt != EVT_LE_META_EVENT) continue;

        evt_le_meta_event *meta = (evt_le_meta_event *)(buf + 1 + HCI_EVENT_HDR_SIZE);
        if (meta->subevent != EVT_LE_ADVERTISING_REPORT) continue;

        /* 解析 LE Advertising Report (可能包含多个报告) */
        le_advertising_info *info = (le_advertising_info *)(meta->data + 1);
        int report_len = len - (meta->data - buf) - 1;

        while (report_len >= sizeof(le_advertising_info)) {
            /* 尝试解析为 MiBeacon */
            if (parse_mibeacon(info->data, info->length)) {
                device_count++;
            }

            int next_len = info->length + sizeof(le_advertising_info);
            report_len -= next_len;
            if (report_len <= 0) break;
            info = (le_advertising_info *)((uint8_t *)info + next_len);
        }
    }

    /* 恢复过滤器 */
    setsockopt(dd, SOL_HCI, HCI_FILTER, &of, sizeof(of));

    /* 停止扫描 */
    hci_le_set_scan_enable(dd, 0x00, 0x00, 1000);
    hci_close_dev(dd);

    printf("[*] 监听结束，共发现 %d 个 MiBeacon 设备广播\n", device_count);
    return 0;
}

/* ========== 主函数 ========== */

static void print_usage(const char *prog)
{
    printf("用法: %s <命令> [选项]\n\n", prog);
    printf("命令:\n");
    printf("  scan              被动监听 BLE 广播，解析 MiBeacon 帧\n");
    printf("  parse --hex H     解析十六进制 MiBeacon 帧数据\n");
    printf("  info              打印 MiBeacon 协议结构说明\n");
    printf("\n示例:\n");
    printf("  sudo %s scan\n", prog);
    printf("  %s parse --hex 1695FE5020001843B1562711A6FF8041004001F6AF2\n", prog);
    printf("  %s info\n", prog);
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "scan") == 0) {
        return cmd_scan();
    }
    else if (strcmp(cmd, "parse") == 0) {
        if (argc < 4 || strcmp(argv[2], "--hex") != 0) {
            fprintf(stderr, "[!] 用法: %s parse --hex <十六进制数据>\n", argv[0]);
            return 1;
        }
        return cmd_parse(argv[3]);
    }
    else if (strcmp(cmd, "info") == 0) {
        cmd_info();
        return 0;
    }
    else {
        fprintf(stderr, "[!] 未知命令: %s\n", cmd);
        print_usage(argv[0]);
        return 1;
    }
}
