# -*- coding: utf-8 -*-
"""
mdns.py — mDNS/HomeKit 发现

提供 mDNS/DNS-SD 发现（miOT 新协议设备）、MAC OUI 查询、三重发现等功能。
依赖: color.py, protocol.py (discover_devices), network.py (arp_scan)
"""

import socket
import time

from .color import Color
from .device_db import lookup_device
from .protocol import discover_devices
from .network import arp_scan

# ═══════════════════════════════════════════════════════════
# mDNS / DNS-SD 发现（miOT 新协议设备）
# ═══════════════════════════════════════════════════════════

# 小米设备常用的 mDNS 服务类型
MDNS_SERVICE_TYPES = [
    "_miio._udp.local.",           # miIO 旧协议设备
    "_miio._tcp.local.",           # miIO TCP 变体
    "_miot-central._tcp.local.",   # miOT 新协议设备（网关、新灯具等）
    "_yeelight._tcp.local.",       # Yeelight 灯具
    "_meshcop._udp.local.",        # Thread/Zigbee Border Router
    "_hap._tcp.local.",            # HomeKit（米家子设备通过 HA Bridge 暴露）
]

# HomeKit category code 映射
HAP_CATEGORIES = {
    1: "Other", 2: "Bridge", 3: "Fan", 4: "Garage Door", 5: "Lightbulb",
    6: "Door Lock", 7: "Outlet", 8: "Switch", 9: "Thermostat",
    10: "Sensor", 11: "Alarm System", 14: "Door", 17: "Accessory",
    22: "Camera", 24: "Air Purifier", 28: "Heater", 32: "Window Covering",
    36: "Security System", 45: "Valve", 48: "Humidifier",
}

# MAC 地址 OUI 厂商映射（前缀 → 厂商/产品线）
MAC_OUI = {
    # 小米 WiFi 设备（网关、摄像头、扫地机等）
    "b8:88:80": ("小米", "网关/路由"),
    "7c:c2:94": ("小米", "IoT 子设备"),
    "54:ef:44": ("小米", "IoT 子设备"),
    "80:ae:54": ("小米", "IoT 子设备"),
    "cc:da:20": ("小米", "IoT 子设备"),
    "90:fb:5d": ("小米", "涂鸦平台"),
    "78:11:dc": ("小米", "IoT 子设备"),
    "a4:cf:12": ("小米", "IoT 子设备"),
    "0c:1a:94": ("小米", "IoT 子设备"),
    "28:6c:07": ("小米", "IoT 子设备"),
    "f0:b4:29": ("小米", "IoT 子设备"),
    "2c:f4:32": ("小米", "IoT 子设备"),
    "64:b4:73": ("小米", "IoT 子设备"),
    "50:ec:49": ("小米", "IoT 子设备"),
    "24:f7:42": ("小米", "IoT 子设备"),
    "18:09:6a": ("小米", "IoT 子设备"),
    "48:3b:38": ("小米", "IoT 子设备"),
    "e4:be:ed": ("小米", "IoT 子设备"),
    "d4:a0:1c": ("小米", "IoT 子设备"),
    "04:cf:8c": ("小米", "IoT 子设备"),
    "64:09:80": ("小米", "IoT 子设备"),
    "98:f5:a9": ("小米", "IoT 子设备"),
    "34:ce:c8": ("小米", "IoT 子设备"),
    "ac:84:c6": ("小米", "IoT 子设备"),
    "f8:e4:e3": ("小米", "IoT 子设备"),
    "b4:7c:9c": ("小米", "IoT 子设备"),
    "28:e3:4f": ("小米", "IoT 子设备"),
    "5c:87:9c": ("小米", "IoT 子设备"),
    "a0:20:a6": ("小米", "IoT 子设备"),
    "78:67:37": ("小米", "IoT 子设备"),
    "e8:87:11": ("小米", "IoT 子设备"),
    "dc:a6:32": ("小米", "ESP32 设备"),
    "00:12:41": ("小米", "蓝牙设备"),
    # Aqara
    "00:15:8d": ("Aqara", "Zigbee 子设备"),
    # Yeelight
    "3c:05:2d": ("Yeelight", "灯具"),
    "7c:49:eb": ("Yeelight", "灯具"),
    # Apple
    "ac:8c:46": ("Apple", "iPhone/iPad"),
    "4e:83:09": ("Apple", "Apple 设备"),
    "ea:67:4f": ("Apple", "Apple 设备"),
    "ec:30:8e": ("Apple", "Apple 设备"), "f8:e4:3b": ("Apple", "Apple 设备"), "a4:b1:97": ("Apple", "Apple 设备"),
    "64:a2:f9": ("Apple", "Apple 设备"), "68:a8:6d": ("Apple", "Apple 设备"), "3c:22:fb": ("Apple", "Apple 设备"),
    "5c:f9:38": ("Apple", "Apple 设备"), "88:1f:a1": ("Apple", "Apple 设备"), "7c:04:d0": ("Apple", "Apple 设备"),
    "dc:2b:61": ("Apple", "Apple 设备"), "fc:65:de": ("Apple", "Apple 设备"), "08:f6:9f": ("Apple", "Apple 设备"),
    # 华为
    "08:ee:ab": ("华为", "IoT 设备"), "b8:27:eb": ("华为", "IoT 设备"), "5c:cf:7f": ("华为", "IoT 设备"),
    "4c:11:bf": ("华为", "IoT 设备"), "cc:96:a0": ("华为", "IoT 设备"), "e4:b0:68": ("华为", "IoT 设备"),
    # TP-Link
    "a8:42:a1": ("TP-Link", "路由器/插座"), "b0:4e:26": ("TP-Link", "路由器/插座"),
    "5c:63:bf": ("TP-Link", "路由器/插座"), "60:e3:27": ("TP-Link", "路由器/插座"),
    "20:4e:7f": ("TP-Link", "路由器/插座"), "ec:17:2f": ("TP-Link", "路由器/插座"),
    # Espressif (IoT 芯片)
    "24:0a:c4": ("Espressif", "IoT 设备"), "30:ae:a4": ("Espressif", "IoT 设备"),
    "bc:dd:c2": ("Espressif", "IoT 设备"),
    # Philips Hue
    "00:17:88": ("Philips Hue", "灯具"), "ec:b5:fa": ("Philips Hue", "灯具"),
    # Sonoff
    "00:1a:22": ("Sonoff", "IoT 设备"), "dc:4f:22": ("Sonoff", "IoT 设备"),
    "18:b4:30": ("Sonoff", "IoT 设备"),
    # Home Assistant
    "7c:b0:c2": ("Home Assistant", "服务器"), "6c:1f:f7": ("Home Assistant", "服务器"),
}


def lookup_mac_vendor(mac):
    """根据 MAC 前缀查询厂商和设备类型，返回 (vendor, device_type)"""
    if not mac or mac == "??":
        return "", ""
    prefix = mac.lower().replace("-", ":")[:8]
    entry = MAC_OUI.get(prefix)
    if isinstance(entry, tuple):
        return entry
    if isinstance(entry, str):
        return entry, entry  # 兼容旧格式
    return "", ""


def discover_mdns(timeout=5):
    """
    通过 mDNS/DNS-SD 发现局域网内的米家设备。

    新一代小米设备（网关、灯具、传感器等）通过 mDNS 广播自身，
    服务类型 _miot-central._tcp.local. 是小米 IoT 平台的标准。

    依赖: zeroconf (pip install zeroconf)

    Args:
        timeout: 监听时长（秒）

    Returns:
        设备列表 (同 discover_devices 格式)
    """
    try:
        import zeroconf
    except ImportError:
        print(Color.yellow("  mDNS 发现需要 zeroconf: pip install zeroconf"))
        return []

    devices = []
    seen_ips = set()
    seen_names = set()  # HomeKit 设备 IP 相同，用 name 去重

    class _Listener:
        """mDNS 服务监听器"""
        def __init__(self):
            self.results = []

        def add_service(self, zc, type_, name):
            try:
                info = zc.get_service_info(type_, name)
                if not info:
                    return
            except Exception:
                return

            for addr_bytes in info.addresses:
                ip = socket.inet_ntoa(addr_bytes)
                # HomeKit 设备共享 IP（HA Bridge），用 name 去重
                is_hap = type_ == "_hap._tcp.local."
                if is_hap:
                    if name in seen_names:
                        continue
                    seen_names.add(name)
                else:
                    if ip in seen_ips:
                        continue
                    seen_ips.add(ip)

                # 从 mDNS 名称中提取型号
                # 格式: xiaomi-gateway-hub1-4069._miot-central._tcp.local.
                model = name.split("._")[0]
                # 去掉末尾序列号: xiaomi-gateway-hub1-4069 -> xiaomi-gateway-hub1
                parts = model.rsplit("-", 1)
                if len(parts) == 2 and parts[-1].isdigit():
                    model = parts[0]

                display_name = info.server.rstrip(".local.") if info.server else model
                dev_name, dev_type = lookup_device(model)

                # 提取 txt properties
                props = {}
                for k, v in (info.properties or {}).items():
                    key = k.decode() if isinstance(k, bytes) else str(k)
                    if isinstance(v, bytes):
                        try:
                            props[key] = v.decode("utf-8")
                        except UnicodeDecodeError:
                            props[key] = v.hex()
                    else:
                        props[key] = str(v)

                # 区分 HomeKit 设备和普通 mDNS 设备
                is_hap = type_ == "_hap._tcp.local."
                if is_hap:
                    # HomeKit 设备：从 props 提取信息
                    hap_name = props.get("md", "") or display_name
                    hap_ci = int(props.get("ci", "1"))
                    hap_id = props.get("id", "")
                    hap_category = HAP_CATEGORIES.get(hap_ci, f"Category-{hap_ci}")
                    device_name = f"HAP: {hap_name} ({hap_category})"
                    device_model = hap_id  # 用 MAC 作为标识
                    device_type = hap_category
                    protocol_tag = "HomeKit"
                else:
                    device_name = dev_name if dev_name != "未知设备" else display_name
                    device_model = model
                    device_type = dev_type
                    protocol_tag = "mDNS"

                devices.append({
                    "ip": ip,
                    "port": info.port,
                    "device_id": 0,
                    "model": device_model,
                    "name": device_name,
                    "type": device_type,
                    "token": "",
                    "timestamp": 0,
                    "last_seen": time.strftime("%Y-%m-%d %H:%M:%S"),
                    "protocol": protocol_tag,
                    "mdns_type": type_,
                    "mdns_name": name,
                    "mdns_server": info.server,
                    "mdns_props": props,
                })

        def update_service(self, zc, type_, name):
            pass

        def remove_service(self, zc, type_, name):
            pass

    try:
        zc = zeroconf.Zeroconf()
        listener = _Listener()
        browsers = []
        for st in MDNS_SERVICE_TYPES:
            try:
                browser = zeroconf.ServiceBrowser(zc, st, listener)
                browsers.append(browser)
            except Exception:
                pass

        time.sleep(timeout)

        for browser in browsers:
            try:
                browser.cancel()
            except Exception:
                pass
        zc.close()
    except Exception as e:
        print(Color.dim(f"  mDNS 发现异常: {e}"))

    return devices


def discover_all(timeout=5):
    """
    三重发现：ARP（所有在线设备）+ miIO 广播 + mDNS（含 HomeKit），并行执行。
    ARP 零延迟，miIO 和 mDNS 并行等待，总耗时 ≈ max(timeout, mDNS_listen)。

    Returns:
        设备列表
    """
    from concurrent.futures import ThreadPoolExecutor, as_completed

    # 阶段0: ARP 扫描（零延迟，读取系统缓存）
    arp_map = arp_scan()
    if arp_map:
        print(Color.dim(f"  [ARP] 系统缓存中发现 {len(arp_map)} 台在线设备"))

    # 并行: miIO 广播 + mDNS 发现
    print(Color.dim("  [miIO + mDNS] 并行扫描中..."))

    miio_devices = []
    mdns_devices = []

    with ThreadPoolExecutor(max_workers=2) as pool:
        future_miio = pool.submit(discover_devices, timeout)
        future_mdns = pool.submit(discover_mdns, timeout)

        for future in as_completed([future_miio, future_mdns]):
            try:
                result = future.result()
                if future is future_miio:
                    miio_devices = result
                    print(Color.dim(f"  [miIO] 发现 {len(miio_devices)} 台设备"))
                else:
                    mdns_devices = result
                    print(Color.dim(f"  [mDNS] 发现 {len(mdns_devices)} 台设备"))
            except Exception as e:
                print(Color.dim(f"  [scan] 异常: {e}"))

    # 合并去重
    all_devices = []
    seen_keys = set()

    for d in miio_devices:
        key = f"{d['ip']}:{d.get('model','')}"
        if key not in seen_keys:
            seen_keys.add(key)
            d.setdefault("protocol", "miIO")
            mac = arp_map.get(d["ip"], "")
            d["mac"] = mac
            vendor, dtype = lookup_mac_vendor(mac)
            d["vendor"] = vendor
            if not d.get("type"):
                d["type"] = dtype
            all_devices.append(d)

    for d in mdns_devices:
        if d.get("protocol") == "HomeKit":
            key = f"hap:{d.get('model','')}"
        else:
            key = f"{d['ip']}:{d.get('model','')}"
        if key not in seen_keys:
            seen_keys.add(key)
            mac = arp_map.get(d["ip"], "")
            d["mac"] = mac or d.get("model", "")
            vendor, dtype = lookup_mac_vendor(mac)
            d["vendor"] = vendor
            if not d.get("type"):
                d["type"] = dtype
            all_devices.append(d)
        else:
            for existing in all_devices:
                ek = f"hap:{existing.get('model','')}" if existing.get("protocol") == "HomeKit" else f"{existing['ip']}:{existing.get('model','')}"
                if ek == key:
                    existing.setdefault("mdns_type", d.get("mdns_type", ""))
                    existing.setdefault("mdns_props", d.get("mdns_props", {}))
                    if existing.get("protocol") != "HomeKit":
                        existing["protocol"] = "miIO + mDNS"
                    break

    # ARP 补充发现
    discovered_ips = set()
    for d in all_devices:
        if d.get("ip"):
            discovered_ips.add(d["ip"])
    arp_only = []
    for ip, mac in sorted(arp_map.items()):
        if ip not in discovered_ips:
            vendor, dtype = lookup_mac_vendor(mac)
            if vendor:
                arp_only.append({
                    "ip": ip, "port": 0, "device_id": 0, "model": "",
                    "name": vendor, "type": dtype, "token": "",
                    "timestamp": 0, "last_seen": "", "protocol": "ARP",
                    "mac": mac, "vendor": vendor,
                })
    if arp_only:
        all_devices.extend(arp_only)
        print(Color.dim(f"  [ARP] 补充发现 {len(arp_only)} 台设备"))

    print(Color.dim(f"  去重后共 {len(all_devices)} 台设备"))
    return all_devices
