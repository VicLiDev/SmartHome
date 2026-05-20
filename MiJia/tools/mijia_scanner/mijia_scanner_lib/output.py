# -*- coding: utf-8 -*-
"""
output.py — 表格打印、导出

提供设备列表的彩色表格打印、JSON 导出、CSV 导出功能。
依赖: color.py
"""

import csv
import json
import sys
import io

from .color import Color, pad_cjk, display_width


def print_device_table(devices, show_mac=False):
    """彩色表格打印设备列表"""
    if not devices:
        print(Color.yellow("  未发现任何设备"))
        return

    # 表头
    has_rooms = any(d.get("room") for d in devices)
    h_num = pad_cjk("#", 3)
    h_ip = pad_cjk("IP", 16)
    h_proto = pad_cjk("协议", 10)
    h_type = pad_cjk("类型", 14)
    h_name = pad_cjk("名称", 32)
    h_vendor = pad_cjk("厂商", 14)
    h_mac = pad_cjk("MAC", 18)

    if has_rooms:
        h_room = pad_cjk("区域", 6)
        hdr = f"  {h_num}  {h_ip}  {h_proto}  {h_room}  {h_type}  {h_name}  {h_vendor}  {h_mac}"
    else:
        hdr = f"  {h_num}  {h_ip}  {h_proto}  {h_type}  {h_name}  {h_vendor}  {h_mac}"
    print(Color.bold(hdr))
    print(Color.dim("  " + "-" * (len(hdr) + 2)))

    for i, d in enumerate(devices, 1):
        mac = d.get("mac", "")
        vendor = d.get("vendor", "")
        name = d.get("name", "")
        dtype = d.get("type", "")
        protocol = d.get("protocol", "")
        token = d.get("token", "")

        # 协议颜色映射
        proto_colors = {
            "miIO": "33", "mDNS": "36", "HomeKit": "35", "ARP": "33",
            "miIO + mDNS": "34",
        }
        tc = proto_colors.get(protocol, "0")
        vtc = "32" if vendor else "0"

        # HomeKit 设备：name 里已经有 "HAP: xxx (Bridge)" 格式，拆出来显示
        if protocol == "HomeKit" and name.startswith("HAP: "):
            # name = "HAP: HASS Bridge 0R (Bridge)"
            # dtype = "Bridge"
            # 提取纯名称（去掉 HAP: 前缀和末尾类别括号）
            hap_name = name[4:]  # 去掉 "HAP: "
            display_name = hap_name
            display_type = dtype
        else:
            display_name = name if name else vendor
            display_type = dtype

        name_short = display_name[:30] + ".." if display_width(display_name) > 32 else display_name
        type_short = display_type[:12] + ".." if display_width(display_type) > 14 else display_type
        mac_display = mac if mac else ""
        room = d.get("room", "")

        ip_pad = pad_cjk(d['ip'], 16)
        mac_pad = pad_cjk(mac_display, 18)
        num_pad = pad_cjk(str(i), 3)

        if has_rooms:
            line = f"  {num_pad}  {ip_pad}  {Color._cpad(tc, protocol, 10)}  {Color._cpad('32', room, 6)}  "
            line += f"{Color._cpad(tc, type_short, 14)}  {Color._cpad(tc, name_short, 32)}  "
            line += f"{Color._cpad(vtc, vendor, 14)}  {Color.dim(mac_pad)}"
        else:
            line = f"  {num_pad}  {ip_pad}  {Color._cpad(tc, protocol, 10)}  {Color._cpad(tc, type_short, 14)}  "
            line += f"{Color._cpad(tc, name_short, 32)}  {Color._cpad(vtc, vendor, 14)}  {Color.dim(mac_pad)}"
        print(line)

    # 协议统计
    proto_counts = {}
    for d in devices:
        p = d.get("protocol", "unknown")
        proto_counts[p] = proto_counts.get(p, 0) + 1

    print()
    parts = []
    for p, c in sorted(proto_counts.items(), key=lambda x: -x[1]):
        parts.append(f"{p}: {c}")
    print(Color.dim(f"  按协议: {', '.join(parts)}"))

    # token 汇总
    devices_with_token = [d for d in devices if d.get("token")]
    if devices_with_token:
        print()
        print(Color.green(f"  [*] {len(devices_with_token)} 台设备返回了 Token（可能未绑定）:"))
        for d in devices_with_token:
            print(f"      {d['ip']}  {d.get('model','')}  Token: {Color.yellow(d['token'])}")


def export_json(devices, output=None):
    """导出 JSON 格式"""
    text = json.dumps(devices, ensure_ascii=False, indent=2)
    if output:
        with open(output, "w", encoding="utf-8") as f:
            f.write(text)
        print(f"  已导出到 {output}")
    else:
        print(text)


def export_csv(devices, output=None):
    """导出 CSV 格式"""
    buf = io.StringIO()
    fields = ["ip", "port", "device_id", "model", "name", "type", "token", "timestamp", "last_seen"]
    writer = csv.DictWriter(buf, fieldnames=fields, extrasaction="ignore")
    writer.writeheader()
    for d in devices:
        writer.writerow(d)

    text = buf.getvalue()
    if output:
        with open(output, "w", encoding="utf-8", newline="") as f:
            f.write(text)
        print(f"  已导出到 {output}")
    else:
        print(text, end="")
