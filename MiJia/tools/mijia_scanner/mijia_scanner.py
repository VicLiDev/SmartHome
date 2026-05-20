#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
mijia_scanner.py — 米家设备网络探测器（主程序入口）

功能：快速扫描 / 深度扫描 / 持续监控 / 设备信息 / 型号查询 / 结果导出
协议：同时支持 miIO (UDP 54321) 和 miOT (mDNS) 两种发现协议
依赖：zeroconf（mDNS 发现，可选）/ pycryptodome（深度扫描，可选）
"""

import sys
import os
import signal
import argparse
import time
import socket
import struct

# 从库中导入各模块
from mijia_scanner_lib.color import Color, pad_cjk
from mijia_scanner_lib.device_db import DEVICE_DATABASE, lookup_device
from mijia_scanner_lib.protocol import (
    MIIO_PORT, MIIO_MAGIC,
    build_hello_packet, discover_devices, derive_keys,
    aes_decrypt, send_miio_command, deep_scan_device,
)
from mijia_scanner_lib.mdns import (
    discover_mdns, discover_all, lookup_mac_vendor,
)
from mijia_scanner_lib.network import (
    parse_ip_ranges, arp_scan, _ping_sweep,
    discover_devices_range, discover_devices_from_alive, arp_lookup,
)
from mijia_scanner_lib.ha import ha_get_all_devices
from mijia_scanner_lib.output import print_device_table, export_json, export_csv


# ═══════════════════════════════════════════════════════════
# 子命令实现
# ═══════════════════════════════════════════════════════════

def cmd_scan(args):
    """快速扫描子命令"""
    timeout = args.timeout

    # 如果指定了 --range，走跨网段扫描 + mDNS
    if args.range:
        ip_list = parse_ip_ranges(args.range)
        if not ip_list:
            print(Color.red("  未解析到有效 IP 地址"))
            return

        total = len(ip_list)
        print(Color.bold("  米家设备跨网段扫描"))
        print(Color.dim(f"  范围: {args.range}  ({total} 个 IP)"))
        print(Color.dim("  ──────────────────────────────────────────────────────"))

        # 先 ping 筛活 IP
        print(Color.dim(f"  [1/3] ping 预筛存活 IP..."))
        alive = _ping_sweep(ip_list)
        print(Color.dim(f"        存活 {len(alive)}/{total} 台"))

        if not alive:
            print(Color.yellow("  无存活 IP"))
            return

        # miIO + mDNS 并行
        from concurrent.futures import ThreadPoolExecutor, as_completed

        print(Color.dim(f"  [2/3] miIO + mDNS 并行扫描中..."))
        miio_devices = []
        mdns_devices = []

        with ThreadPoolExecutor(max_workers=2) as pool:
            f_miio = pool.submit(discover_devices_from_alive, alive, timeout)
            f_mdns = pool.submit(discover_mdns, 5)
            for future in as_completed([f_miio, f_mdns]):
                try:
                    result = future.result()
                    if future is f_miio:
                        miio_devices = result
                        print(Color.dim(f"  [miIO] 发现 {len(miio_devices)} 台设备"))
                    else:
                        mdns_devices = result
                        print(Color.dim(f"  [mDNS] 发现 {len(mdns_devices)} 台设备"))
                except Exception as e:
                    print(Color.dim(f"  异常: {e}"))

        # 合并
        devices = list(miio_devices)
        seen_keys = set()
        for d in devices:
            seen_keys.add(f"{d['ip']}:{d.get('model','')}")
        for d in mdns_devices:
            key = f"hap:{d.get('model','')}" if d.get("protocol") == "HomeKit" else f"{d['ip']}:{d.get('model','')}"
            if key not in seen_keys:
                seen_keys.add(key)
                devices.append(d)

        # ARP 补充
        arp_map = arp_scan()
        discovered_ips = {d["ip"] for d in devices}
        arp_count = 0
        for ip, mac in sorted(arp_map.items()):
            if ip not in discovered_ips:
                vendor, dtype = lookup_mac_vendor(mac)
                if vendor:
                    devices.append({
                        "ip": ip, "port": 0, "device_id": 0, "model": "",
                        "name": vendor, "type": dtype, "token": "",
                        "timestamp": 0, "last_seen": "", "protocol": "ARP",
                        "mac": mac, "vendor": vendor,
                    })
                    arp_count += 1
        if arp_count:
            print(Color.dim(f"  [ARP] 补充发现 {arp_count} 台设备"))

        # 补充 MAC/厂商
        for d in devices:
            if not d.get("mac"):
                mac = arp_map.get(d["ip"], "")
                d["mac"] = mac
                vendor, dtype = lookup_mac_vendor(mac)
                d["vendor"] = vendor
                if not d.get("type"):
                    d["type"] = dtype

        print(Color.dim(f"        去重后共 {len(devices)} 台设备\n"))
    elif args.mdns_only:
        print(Color.bold("  米家设备 mDNS 扫描") + Color.dim(f" (超时: {timeout}s)"))
        print(Color.dim("  ──────────────────────────────────────────────────────"))
        devices = discover_mdns(timeout)
    else:
        print(Color.bold("  米家设备扫描") + Color.dim(f" (超时: {timeout}s)"))
        print(Color.dim("  ──────────────────────────────────────────────────────"))
        devices = discover_all(timeout)

    if args.json:
        export_json(devices)
        return
    if args.csv:
        export_csv(devices)
        return

    # HA 区域补充
    # HA 配置：命令行 > config.ini
    ha_url = args.ha_url or ""
    ha_token = args.ha_token or ""
    if not ha_token:
        cfg_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.ini")
        if os.path.isfile(cfg_path):
            cfg = {}
            for line in open(cfg_path):
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    k, v = line.split("=", 1)
                    cfg[k.strip()] = v.strip()
            ha_url = ha_url or cfg.get("HA_URL", "")
            ha_token = ha_token or cfg.get("HA_TOKEN", "")

    if ha_token:
        print()
        ha_devices = ha_get_all_devices(ha_url, ha_token)
        if ha_devices:
            print()
            print(Color.bold("  Home Assistant 设备列表（含区域）"))
            print(Color.dim("  " + "-" * 110))
            # 按房间分组显示
            by_room = {}
            for hd in ha_devices:
                r = hd["room"]
                if r not in by_room:
                    by_room[r] = []
                by_room[r].append(hd)
            
            room_order = ["客厅", "书房", "厨房", "主卧", "次卧", "卧室", "玄关", "入户玄关", "入户",
                          "休闲阳台", "生活阳台", "阳台", "卫生间", "走廊"]
            
            for room in sorted(by_room.keys(), key=lambda x: room_order.index(x) if x in room_order else 99):
                devs = by_room[room]
                print()
                print(Color.bold(f"    [{room}] ({len(devs)} 个设备)"))
                for d in devs:
                    print(f"      {Color._cpad('36', d['type'], 8)} {d['name']}")

    print()
    print_device_table(devices, show_mac=True)
    print()
    print(Color.dim(f"  共发现 {len(devices)} 台设备  [{time.strftime('%Y-%m-%d %H:%M:%S')}]"))


def cmd_deep(args):
    """深度扫描子命令"""
    token = args.token
    timeout = args.timeout

    print(Color.bold("  米家设备深度扫描"))
    print(Color.dim("  ──────────────────────────────────────────────────────"))

    # 第一步：快速扫描
    print(Color.dim(f"  [1/2] 正在扫描设备 (超时 {timeout}s)..."))
    devices = discover_all(timeout)

    if not devices:
        print(Color.yellow("  未发现设备"))
        return

    print(Color.green(f"  发现 {len(devices)} 台设备"))

    # 第二步：逐台深度查询
    print(Color.dim(f"  [2/2] 正在获取设备详细信息..."))
    results = []

    for i, d in enumerate(devices, 1):
        ip = d["ip"]
        # 优先使用命令行 token，否则使用设备返回的 token
        dev_token = token or d.get("token", "")

        if not dev_token:
            print(Color.yellow(f"  [{i}/{len(devices)}] {ip} - 无 token，跳过深度扫描"))
            results.append(d)
            continue

        model = d.get("model", "unknown")
        name, dtype = lookup_device(model)
        print(Color.dim(f"  [{i}/{len(devices)}] {ip} ({model}) ..."), end=" ", flush=True)

        info, err = deep_scan_device(ip, dev_token, timeout)
        if err != "ok":
            print(Color.red(f"失败: {err}"))
            results.append(d)
            continue

        print(Color.green("成功"))

        # 合并信息
        enriched = dict(d)
        if isinstance(info, dict):
            if "model" in info:
                enriched["model"] = info["model"]
                name, dtype = lookup_device(info["model"])
            enriched["name"] = name
            enriched["type"] = dtype
            for key in ["fw_ver", "mcu_firmware_ver", "hw_ver", "ap", "ssid", "bssid", "rssi"]:
                if key in info:
                    enriched[key] = info[key]
        results.append(enriched)

    # 输出
    print()
    if args.json:
        export_json(results)
        return
    if args.csv:
        export_csv(results)
        return

    for r in results:
        print(f"  {Color.bold(r['ip'])}  {r.get('model','unknown')}")
        print(f"    名称:   {r.get('name','未知')}")
        print(f"    类型:   {r.get('type','未知')}")
        print(f"    ID:     {r['device_id']}")
        if r.get("fw_ver"):
            print(f"    固件:   {r['fw_ver']}")
        if r.get("mcu_firmware_ver"):
            print(f"    MCU:    {r['mcu_firmware_ver']}")
        if r.get("hw_ver"):
            print(f"    硬件:   {r['hw_ver']}")
        if r.get("rssi"):
            print(f"    信号:   {r['rssi']} dBm")
        if r.get("ssid"):
            print(f"    WiFi:   {r['ssid']}")
        if r.get("token"):
            print(f"    Token:  {Color.yellow(r['token'])}")
        print()

    print(Color.dim(f"  共 {len(results)} 台设备  [{time.strftime('%Y-%m-%d %H:%M:%S')}]"))


def cmd_monitor(args):
    """持续监控子命令"""
    interval = args.interval
    print(Color.bold(f"  米家设备监控") + Color.dim(f" (间隔: {interval}s, Ctrl+C 退出)"))
    print(Color.dim("  ──────────────────────────────────────────────────────"))
    print()

    # 信号处理：优雅退出
    running = [True]

    def signal_handler(sig, frame):
        print(Color.dim("\n  监控已停止"))
        running[0] = False
        sys.exit(0)

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    # 第一轮扫描，建立基准
    prev_ips = set()

    while running[0]:
        now = time.strftime("%Y-%m-%d %H:%M:%S")
        devices = discover_all(3)  # 监控模式用短超时
        curr_ips = set(d["ip"] for d in devices)

        # 检测上线
        new_ips = curr_ips - prev_ips
        for ip in new_ips:
            d = next((x for x in devices if x["ip"] == ip), None)
            model = d["model"] if d else "?"
            name = d.get("name", "") if d else ""
            print(f"  {Color.green('[+ ONLINE]')}  {now}  {ip}  {model}  {name}")

        # 检测离线
        gone_ips = prev_ips - curr_ips
        for ip in gone_ips:
            print(f"  {Color.red('[- OFFLINE]')}  {now}  {ip}")

        if not new_ips and not gone_ips and prev_ips:
            print(f"  {Color.dim('[  ...    ]')}  {now}  无变化 ({len(curr_ips)} 台在线)")

        prev_ips = curr_ips
        time.sleep(interval)


def cmd_info(args):
    """查询单台设备信息"""
    ip = args.ip
    token = args.token
    timeout = args.timeout

    print(Color.bold(f"  设备信息: {ip}"))
    print(Color.dim("  ──────────────────────────────────────────────────────"))

    if not token:
        print(Color.yellow("  未提供 token，尝试 Hello 握手..."))
        hello = build_hello_packet()
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(timeout)
        try:
            sock.sendto(hello, (ip, MIIO_PORT))
            resp, _ = sock.recvfrom(4096)
            if len(resp) >= 32:
                devid = struct.unpack_from(">I", resp, 4)[0]
                ts = struct.unpack_from(">I", resp, 8)[0]
                print(f"  Device ID: {devid}")
                print(f"  Timestamp: {ts}")

                # 尝试提取 token
                if len(resp) >= 44:
                    tok = resp[28:44]
                    if any(b != 0 for b in tok):
                        print(f"  Token:     {Color.yellow(tok.hex())}")
                        print(Color.yellow("  发现明文 token，可用于深度查询"))
        except socket.timeout:
            print(Color.red("  Hello 握手超时"))
        except Exception as e:
            print(Color.red(f"  错误: {e}"))
        sock.close()
    else:
        print(Color.dim(f"  Token: {token[:8]}...{token[-8:]}"))
        print(Color.dim("  发送 miIO.info 命令..."))

        info, err = deep_scan_device(ip, token, timeout)
        if err != "ok":
            print(Color.red(f"  失败: {err}"))
            return

        if isinstance(info, dict):
            for key, val in info.items():
                if isinstance(val, str) or isinstance(val, (int, float)):
                    label_map = {
                        "model": "型号", "fw_ver": "固件版本",
                        "mcu_firmware_ver": "MCU 固件", "hw_ver": "硬件版本",
                        "life": "使用时长(h)", "rssi": "WiFi 信号(dBm)",
                        "ssid": "WiFi 名称", "bssid": "AP MAC",
                        "ap": "AP 类型", "token": "Token",
                    }
                    label = label_map.get(key, key)
                    if key == "token":
                        print(f"  {label}: {Color.yellow(str(val))}")
                    else:
                        print(f"  {label}: {val}")


def cmd_models(args):
    """打印已知型号数据库"""
    print(Color.bold("  miIO 设备型号数据库"))
    print(Color.dim(f"  共 {len(DEVICE_DATABASE)} 个型号"))
    print()

    # 按类型分组
    categories = {}
    for prefix, name, dtype in DEVICE_DATABASE:
        if dtype not in categories:
            categories[dtype] = []
        categories[dtype].append((prefix, name))

    type_order = ["智能灯", "净化器", "风扇", "插座", "开关", "传感器", "网关",
                  "摄像头", "扫地机", "空调伴侣", "取暖器", "加湿器", "新风机",
                  "厨电", "家电", "洗衣机", "冰箱", "空调", "电视", "路由器",
                  "中继器", "投影仪", "窗帘", "遥控器", "门锁", "门铃",
                  "净水器", "健康", "个护", "电源", "未知"]

    for cat in type_order:
        items = categories.get(cat)
        if not items:
            continue
        print(f"  {Color.cyan(f'[{cat}]')} ({len(items)} 个)")
        for prefix, name in items:
            print(f"    {prefix:<45} {name}")
        print()


def cmd_export(args):
    """导出子命令（先扫描后导出）"""
    fmt = args.format
    output = args.output

    print(Color.dim("  正在扫描设备..."))
    devices = discover_all(args.timeout)

    if not devices:
        print(Color.yellow("  未发现设备，无内容可导出"))
        return

    if fmt == "json":
        export_json(devices, output)
    elif fmt == "csv":
        export_csv(devices, output)
    else:
        print(Color.red(f"  不支持的格式: {fmt}"))


# ═══════════════════════════════════════════════════════════
# 命令行参数解析
# ═══════════════════════════════════════════════════════════

def build_parser():
    """构建命令行参数解析器"""
    parser = argparse.ArgumentParser(
        prog="mijia_scanner",
        description="米家 (miIO) 设备网络探测器",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  %(prog)s scan                    扫描局域网（miIO 广播 + mDNS 双协议）
  %(prog)s scan --timeout 10       扫描超时 10 秒
  %(prog)s scan --mdns-only        仅 mDNS 扫描（发现新协议设备）
  %(prog)s scan --range 192.168.1.0/24     跨网段扫描
  %(prog)s scan --range 10.0.0.1-254        IP 范围扫描
  %(prog)s scan --json             JSON 格式输出
  %(prog)s deep --token xxxxxx     深度扫描（获取固件型号等信息）
  %(prog)s monitor --interval 30   每 30 秒监控设备上下线
  %(prog)s info 192.168.1.100      查询单台设备信息
  %(prog)s models                  打印已知型号数据库
  %(prog)s export --format csv -o devices.csv

协议说明:
  miIO (旧) — UDP 广播 54321，适用于老设备（一代插座、台灯、扫地机等）
  miOT (新) — mDNS _miot-central._tcp.local.，适用于新设备（网关、新灯具等）
  默认同时使用两种协议发现，确保最大覆盖
        """)

    parser.add_argument("--no-color", action="store_true", help="禁用彩色输出")

    subparsers = parser.add_subparsers(dest="command", help="子命令")

    # scan
    p_scan = subparsers.add_parser("scan", help="快速扫描（miIO 广播 + mDNS 双协议）")
    p_scan.add_argument("--timeout", type=int, default=5, help="超时秒数 (默认: 5)")
    p_scan.add_argument("--range", type=str, default="", help="跨网段扫描 (CIDR/范围/逗号分隔)")
    p_scan.add_argument("--mdns-only", action="store_true", help="仅使用 mDNS 发现（跳过 miIO 广播）")
    p_scan.add_argument("--ha-url", type=str, default="", help="Home Assistant 地址 (默认从 config.ini 读取)")
    p_scan.add_argument("--ha-token", type=str, default="", help="Home Assistant token (默认从 config.ini 读取)")
    p_scan.add_argument("--json", action="store_true", help="JSON 输出")
    p_scan.add_argument("--csv", action="store_true", help="CSV 输出")

    # deep
    p_deep = subparsers.add_parser("deep", help="深度扫描")
    p_deep.add_argument("--token", type=str, default="", help="设备 token (32 位 hex)")
    p_deep.add_argument("--timeout", type=int, default=5, help="扫描超时秒数 (默认: 5)")
    p_deep.add_argument("--json", action="store_true", help="JSON 输出")
    p_deep.add_argument("--csv", action="store_true", help="CSV 输出")

    # monitor
    p_mon = subparsers.add_parser("monitor", help="持续监控")
    p_mon.add_argument("--interval", type=int, default=10, help="扫描间隔秒数 (默认: 10)")

    # info
    p_info = subparsers.add_parser("info", help="查询设备信息")
    p_info.add_argument("ip", help="设备 IP 地址")
    p_info.add_argument("--token", type=str, default="", help="设备 token")
    p_info.add_argument("--timeout", type=int, default=10, help="超时秒数 (默认: 10)")

    # models
    subparsers.add_parser("models", help="打印已知型号数据库")

    # export
    p_exp = subparsers.add_parser("export", help="扫描并导出")
    p_exp.add_argument("--format", choices=["json", "csv"], default="json", help="导出格式 (默认: json)")
    p_exp.add_argument("--output", "-o", type=str, default=None, help="输出文件路径")
    p_exp.add_argument("--timeout", type=int, default=5, help="扫描超时秒数")

    return parser


# ═══════════════════════════════════════════════════════════
# 主入口
# ═══════════════════════════════════════════════════════════

def main():
    parser = build_parser()
    args = parser.parse_args()

    # 颜色控制
    if args.no_color:
        Color.ENABLED = False

    # 路由子命令
    cmd_map = {
        "scan":    cmd_scan,
        "deep":    cmd_deep,
        "monitor": cmd_monitor,
        "info":    cmd_info,
        "models":  cmd_models,
        "export":  cmd_export,
    }

    handler = cmd_map.get(args.command)
    if handler:
        handler(args)
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
