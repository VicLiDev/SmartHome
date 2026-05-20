# -*- coding: utf-8 -*-
"""
network.py — ARP、ping、范围扫描

提供 ARP 表查询、IP 范围解析、ping 扫描、miIO unicast 探测等功能。
依赖: color.py, protocol.py (build_hello_packet, lookup_device, derive_keys, aes_decrypt, MIIO_PORT, MIIO_MAGIC)
"""

import socket
import struct
import json
import time
import subprocess
import ipaddress

from .color import Color
from .device_db import lookup_device
from .protocol import build_hello_packet, MIIO_PORT, MIIO_MAGIC


def parse_ip_ranges(spec):
    """
    解析 IP 范围字符串，返回 ipaddress.IPv4Address 列表。
    
    支持格式:
      192.168.1.0/24        — CIDR
      192.168.1.1-254       — 起止范围
      192.168.1.5           — 单个 IP
      192.168.1.0/24,10.0.0.1-10   — 逗号分隔多段
    
    Returns:
        list of IPv4Address
    """
    results = []
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        if "/" in part:
            # CIDR 格式
            try:
                network = ipaddress.IPv4Network(part, strict=False)
                for ip in network.hosts():
                    results.append(ip)
            except ValueError as e:
                print(Color.red(f"  无效的 CIDR: {part} ({e})"))
        elif "-" in part:
            # 起止范围: 192.168.1.1-254
            try:
                base_str, end_str = part.rsplit(".", 1)
                # 可能是 192.168.1.1-192.168.1.254
                if "." in end_str:
                    start_ip = ipaddress.IPv4Address(part.split("-")[0].strip())
                    end_ip = ipaddress.IPv4Address(part.split("-")[1].strip())
                    current = start_ip
                    while current <= end_ip:
                        results.append(current)
                        current = ipaddress.IPv4Address(int(current) + 1)
                else:
                    base = base_str + "."
                    start = int(end_str.split("-")[0])
                    end = int(end_str.split("-")[1])
                    for i in range(start, end + 1):
                        results.append(ipaddress.IPv4Address(base + str(i)))
            except (ValueError, IndexError) as e:
                print(Color.red(f"  无效的范围: {part} ({e})"))
        else:
            # 单个 IP
            try:
                results.append(ipaddress.IPv4Address(part))
            except ValueError as e:
                print(Color.red(f"  无效的 IP: {part} ({e})"))
    return results


def arp_scan():
    """
    读取系统 ARP 表，获取所有已知在线设备的 IP 和 MAC 地址。
    不主动发包，零延迟。

    Returns:
        dict: {ip: mac} 映射
    """
    arp_map = {}
    try:
        with open("/proc/net/arp", "r") as f:
            header = f.readline()  # 跳过表头
            for line in f:
                parts = line.split()
                if len(parts) >= 6:
                    ip = parts[0]
                    mac = parts[3]
                    flags = parts[2]
                    # 只保留已解析的以太网条目（flags 0x2 = completed）
                    if mac != "00:00:00:00:00:00" and flags in ("0x2", "0x6"):
                        arp_map[ip] = mac
    except FileNotFoundError:
        # 降级: 用 arp 命令
        import re
        try:
            result = subprocess.run(["arp", "-an"], capture_output=True, text=True, timeout=3)
            for line in result.stdout.split("\n"):
                m = re.search(r'\((\d+\.\d+\.\d+\.\d+)\) at ([0-9a-fA-F:]+)', line)
                if m:
                    arp_map[m.group(1)] = m.group(2).lower()
        except Exception:
            pass
    return arp_map


def _ping_sweep(ip_list, timeout=2):
    """
    用 fping 快速扫描整个 IP 列表，返回存活 IP 列表。
    fping 并行发 ICMP，254 个 IP 只需约 2 秒。

    降级: 如果没装 fping，用系统 ping 逐个探测。

    Args:
        ip_list: IPv4Address 列表
        timeout: fping 超时秒数

    Returns:
        存活 IP 的字符串列表
    """
    if not ip_list:
        return []

    ip_strs = [str(ip) for ip in ip_list]

    # 优先用 fping（并行，速度快）
    import shutil
    if shutil.which("fping"):
        try:
            # -t500: 每个 host 500ms 超时，并行发送，整网段约 2-3 秒
            result = subprocess.run(
                ["fping", "-a", "-q", "-t500"] + ip_strs,
                capture_output=True, text=True, timeout=max(15, int(len(ip_strs) * 0.02))
            )
            if result.returncode == 0 or result.stdout:
                alive = result.stdout.strip().split("\n")
                return [ip for ip in alive if ip]
        except (subprocess.TimeoutExpired, FileNotFoundError):
            pass

    # 降级: 逐个 ICMP ping（很慢，仅备用）
    print(Color.dim("  (未安装 fping，使用逐个 ping，速度较慢，建议: apt install fping)"))
    alive = []
    for ip in ip_strs:
        try:
            r = subprocess.run(
                ["ping", "-c", "1", "-W", str(timeout), ip],
                capture_output=True, timeout=timeout + 1
            )
            if r.returncode == 0:
                alive.append(ip)
        except Exception:
            pass
    return alive


def discover_devices_range(ip_list, timeout=1):
    """
    跨网段扫描：先 ping 筛活 IP，再对存活 IP 发送 unicast Hello 探测。

    Args:
        ip_list: IPv4Address 列表
        timeout: 单个 IP 的超时秒数

    Returns:
        设备列表 (同 discover_devices 格式)
    """
    if not ip_list:
        return []

    # 阶段0: 用 fping 快速筛出存活 IP（秒级完成整个网段）
    alive_ips = _ping_sweep(ip_list)
    return discover_devices_from_alive(alive_ips, timeout)


def _probe_one_ip(ip_str, hello, timeout):
    """探测单个 IP，返回设备 dict 或 None（线程安全）"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    try:
        sock.sendto(hello, (ip_str, MIIO_PORT))
        data, addr = sock.recvfrom(4096)
    except (socket.timeout, OSError):
        sock.close()
        return None
    sock.close()

    if len(data) < 32:
        return None

    magic = struct.unpack_from(">H", data, 0)[0]
    if magic != MIIO_MAGIC:
        return None

    device_id = struct.unpack_from(">I", data, 4)[0]
    if device_id == 0xFFFFFFFF:
        return None

    ts = struct.unpack_from(">I", data, 8)[0]

    # 提取 token
    token_hex = ""
    if len(data) >= 44:
        token_bytes = data[28:44]
        if any(b != 0 for b in token_bytes):
            token_hex = token_bytes.hex()

    # 尝试解析模型
    model = "unknown"
    if len(data) > 32:
        try:
            extra = data[32:]
            decoded = extra.decode("utf-8", errors="ignore").strip("\x00")
            for line in decoded.split("\x00"):
                line = line.strip()
                if not line:
                    continue
                if "model" in line.lower():
                    try:
                        obj = json.loads(line)
                        if "model" in obj:
                            model = obj["model"]
                            break
                    except json.JSONDecodeError:
                        pass
                    for kv in line.split("&"):
                        if kv.startswith("model="):
                            model = kv.split("=", 1)[1]
                            break
                    if model != "unknown":
                        break
        except Exception:
            pass

    name, dtype = lookup_device(model)

    return {
        "ip": ip_str,
        "port": MIIO_PORT,
        "device_id": device_id,
        "model": model,
        "name": name,
        "type": dtype,
        "token": token_hex,
        "timestamp": ts,
        "last_seen": time.strftime("%Y-%m-%d %H:%M:%S"),
    }


def discover_devices_from_alive(ip_strs, timeout=1):
    """
    对已知存活的 IP 列表并行发送 miIO Hello 探测。
    每个一个线程，总耗时 ≈ timeout（而非 len(ip_strs) * timeout）。

    Args:
        ip_strs: IP 字符串列表（已经确认存活）
        timeout: 单个 IP 的超时秒数

    Returns:
        设备列表 (同 discover_devices 格式)
    """
    if not ip_strs:
        return []

    hello = build_hello_packet()
    total = len(ip_strs)
    print(Color.dim(f"  并行探测 {total} 个 IP (线程池)..."))

    from concurrent.futures import ThreadPoolExecutor, as_completed

    devices = []
    seen_ids = set()
    done = 0

    with ThreadPoolExecutor(max_workers=min(total, 64)) as pool:
        futures = {pool.submit(_probe_one_ip, ip, hello, timeout): ip for ip in ip_strs}
        for future in as_completed(futures):
            done += 1
            if total > 1 and (done % 20 == 0 or done == total):
                print(Color.dim(f"\r  探测进度: {done}/{total}"), end="", flush=True)

            result = future.result()
            if result is None:
                continue

            device_id = result["device_id"]
            if device_id in seen_ids:
                continue
            seen_ids.add(device_id)
            devices.append(result)

    if total > 1:
        print()  # 换行
    return devices


def arp_lookup(ip):
    """通过 /proc/net/arp 查询 MAC 地址"""
    try:
        with open("/proc/net/arp", "r") as f:
            for line in f:
                parts = line.split()
                if len(parts) >= 4 and parts[0] == ip:
                    mac = parts[3]
                    if mac.upper() != "00:00:00:00:00:00":
                        return mac.upper()
    except (IOError, OSError):
        pass
    return None
