# -*- coding: utf-8 -*-
"""
protocol.py — miIO 协议核心函数

提供 miIO 协议的广播发现、加解密、命令发送、深度扫描等功能。
依赖: color.py, device_db.py
"""

import socket
import struct
import json
import hashlib
import time
import sys

from .color import Color
from .device_db import lookup_device

# ═══════════════════════════════════════════════════════════
# miIO 协议常量
# ═══════════════════════════════════════════════════════════

MIIO_MAGIC       = 0x2131
MIIO_PORT        = 54321
MIIO_MULTICAST   = "224.0.0.50"
MIIO_HEADER_SIZE = 32   # 2+2+4+4+16+4（签名字段仅 4 字节明文 hello）
TOKEN_HEX_LEN    = 32

# 实际 miIO header = 2(magic) + 2(len) + 4(devid) + 4(ts) + 16(nonce) + 32(sig) = 60
# 但 hello 响应中 nonce 后面可能紧跟 token 区域，我们按 32 字节最小 header 处理
MIIO_HDR_MIN     = 32


def build_hello_packet():
    """构建 miIO Hello 广播报文（32字节明文）"""
    pkt = bytearray(32)
    struct.pack_into(">H", pkt, 0, MIIO_MAGIC)     # Magic
    struct.pack_into(">H", pkt, 2, 0x0020)          # Length = 32
    struct.pack_into(">I", pkt, 4, 0xFFFFFFFF)       # Device ID
    struct.pack_into(">I", pkt, 8, 0x00000000)       # Timestamp
    pkt[12:28] = b"\xff" * 16                        # Nonce
    pkt[28:32] = b"\x00" * 4                         # Token
    return bytes(pkt)


def discover_devices(timeout=5):
    """快速扫描：发送 Hello 广播，收集所有 miIO 设备响应"""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

    # 设置超时
    sock.settimeout(timeout)

    # 尝试加入组播组
    try:
        mreq = struct.pack("4sL", socket.inet_aton(MIIO_MULTICAST), socket.INADDR_ANY)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    except OSError:
        pass  # 组播加入失败不致命

    # 绑定到组播端口
    try:
        sock.bind(("", MIIO_PORT))
    except OSError:
        pass  # 端口占用时忽略（会自动接收）

    # 发送广播
    dest = (MIIO_MULTICAST, MIIO_PORT)
    hello = build_hello_packet()
    sock.sendto(hello, dest)

    # 收集响应
    devices = []
    seen_ids = set()
    start = time.time()

    while time.time() - start < timeout:
        remaining = timeout - (time.time() - start)
        if remaining <= 0:
            break
        sock.settimeout(min(remaining, 0.5))
        try:
            data, addr = sock.recvfrom(4096)
        except socket.timeout:
            continue
        except OSError:
            break

        if len(data) < 32:
            continue

        # 解析头部
        magic = struct.unpack_from(">H", data, 0)[0]
        if magic != MIIO_MAGIC:
            continue

        device_id = struct.unpack_from(">I", data, 4)[0]
        if device_id == 0xFFFFFFFF:
            continue  # 过滤掉自己的广播

        # 去重
        if device_id in seen_ids:
            continue
        seen_ids.add(device_id)

        ts = struct.unpack_from(">I", data, 8)[0]

        # 尝试提取 token（偏移 28，4 字节或更长）
        token_hex = ""
        if len(data) >= 32:
            # token 区域在 offset 28，尝试取 16 字节
            token_bytes = data[28:44] if len(data) >= 44 else data[28:]
            # 检查是否全零
            if any(b != 0 for b in token_bytes):
                token_hex = token_bytes.hex()

        # 尝试解析模型字符串（有些设备会在附加数据中携带）
        model = "unknown"
        if len(data) > 32:
            try:
                extra = data[32:]
                # 尝试 UTF-8 解码
                decoded = extra.decode("utf-8", errors="ignore").strip("\x00")
                # 查找 JSON 或 key=value 格式
                for line in decoded.split("\x00"):
                    line = line.strip()
                    if not line:
                        continue
                    if "model" in line.lower():
                        # 尝试 JSON 解析
                        try:
                            obj = json.loads(line)
                            if "model" in obj:
                                model = obj["model"]
                                break
                        except json.JSONDecodeError:
                            pass
                        # key=value 格式
                        for kv in line.split("&"):
                            if kv.startswith("model="):
                                model = kv.split("=", 1)[1]
                                break
                        if model != "unknown":
                            break
            except Exception:
                pass

        ip = addr[0]
        name, dtype = lookup_device(model)

        devices.append({
            "ip": ip,
            "port": MIIO_PORT,
            "device_id": device_id,
            "model": model,
            "name": name,
            "type": dtype,
            "token": token_hex,
            "timestamp": ts,
            "last_seen": time.strftime("%Y-%m-%d %H:%M:%S"),
            "protocol": "miIO",
        })

    sock.close()
    return devices


def derive_keys(token_hex):
    """从 token hex 字符串派生 aes_key, aes_iv, sign_key"""
    token_bytes = bytes.fromhex(token_hex)
    aes_key = hashlib.md5(token_bytes).digest()
    aes_iv = hashlib.md5(aes_key + token_bytes).digest()
    sign_key = hashlib.md5(aes_key + aes_iv + aes_key).digest()
    return aes_key, aes_iv, sign_key


def _pkcs7_pad(data, block_size=16):
    pad_len = block_size - (len(data) % block_size)
    return data + bytes([pad_len] * pad_len)


def _pkcs7_unpad(data):
    if not data:
        return data
    pad_val = data[-1]
    if 0 < pad_val <= 16 and all(b == pad_val for b in data[-pad_val:]):
        return data[:-pad_val]
    return data


def aes_encrypt(plaintext, key, iv):
    """AES-128-CBC 加密（需 pycryptodome）"""
    try:
        from Crypto.Cipher import AES
    except ImportError:
        try:
            from Cryptodome.Cipher import AES
        except ImportError:
            print(Color.red("错误: 需要安装 pycryptodome (pip install pycryptodome)"))
            sys.exit(1)
    padded = _pkcs7_pad(plaintext)
    cipher = AES.new(key, AES.MODE_CBC, iv)
    return cipher.encrypt(padded)


def aes_decrypt(ciphertext, key, iv):
    """AES-128-CBC 解密"""
    try:
        from Crypto.Cipher import AES
    except ImportError:
        try:
            from Cryptodome.Cipher import AES
        except ImportError:
            print(Color.red("错误: 需要安装 pycryptodome (pip install pycryptodome)"))
            sys.exit(1)
    cipher = AES.new(key, AES.MODE_CBC, iv)
    return _pkcs7_unpad(cipher.decrypt(ciphertext))


def send_miio_command(ip, port, token_hex, method, params="[]", req_id=1, timeout=10):
    """发送加密 miIO 命令并返回解密后的 JSON 响应"""
    aes_key, aes_iv, sign_key = derive_keys(token_hex)

    # 握手获取设备时间戳
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    hello = build_hello_packet()
    dest = (ip, port)
    sock.sendto(hello, dest)

    try:
        resp_data, _ = sock.recvfrom(4096)
    except socket.timeout:
        sock.close()
        return None, "握手超时"
    sock.close()

    if len(resp_data) < 16:
        return None, "握手响应太短"

    device_ts = struct.unpack_from(">I", resp_data, 8)[0]

    # 构建 JSON-RPC 载荷
    payload = json.dumps({"id": req_id, "method": method, "params": json.loads(params)},
                         separators=(",", ":")).encode("utf-8")

    # AES 加密
    encrypted = aes_encrypt(payload, aes_key, aes_iv)

    # 构建完整报文
    nonce = bytes([(i * 17 + 42) & 0xFF for i in range(16)])
    sign_data = struct.pack("<I", device_ts) + nonce
    signature = hashlib.md5(sign_data + sign_key).digest()

    # 组装 header
    total_len = 32 + len(encrypted)
    header = struct.pack(">HHII", MIIO_MAGIC, total_len, 0xFFFFFFFF, device_ts)
    header += nonce[:16]  # 实际 16 字节 nonce
    header += signature[:16]  # 签名取前 16 字节

    packet = header + encrypted

    # 发送命令
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    sock.sendto(packet, dest)

    try:
        resp, _ = sock.recvfrom(8192)
    except socket.timeout:
        sock.close()
        return None, "命令响应超时"
    sock.close()

    if len(resp) < 32:
        return None, "响应太短"

    # 解密响应
    enc_len = len(resp) - 32
    if enc_len == 0:
        return {}, "ok"

    try:
        decrypted = aes_decrypt(resp[32:], aes_key, aes_iv)
        result = json.loads(decrypted.decode("utf-8"))
        return result, "ok"
    except Exception as e:
        return None, f"解密失败: {e}"


def deep_scan_device(ip, token_hex, timeout=10):
    """深度扫描单台设备，发送 miIO.info 获取详细信息"""
    result, err = send_miio_command(ip, MIIO_PORT, token_hex, "miIO.info", "[]", 1, timeout)
    if err != "ok":
        return None, err

    info = {}
    if isinstance(result, dict):
        # 从 result 字段提取
        if "result" in result:
            info = result["result"][0] if isinstance(result["result"], list) and result["result"] else result["result"]
        else:
            info = result

    # 查询额外属性
    props_result, _ = send_miio_command(ip, MIIO_PORT, token_hex,
                                        "get_prop",
                                        '["model","fw_ver","mcu_firmware_ver","ap","ssid","bssid","rssi"]',
                                        2, timeout)
    if isinstance(props_result, dict) and "result" in props_result:
        props = props_result["result"]
        prop_names = ["model","fw_ver","mcu_firmware_ver","ap","ssid","bssid","rssi"]
        for i, val in enumerate(props):
            if i < len(prop_names) and val is not None:
                info[prop_names[i]] = val

    return info, "ok"
