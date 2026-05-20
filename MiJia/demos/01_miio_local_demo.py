"""
Demo 1: miIO 协议本地控制（纯 Python socket 实现）

功能:
  - UDP 组播发现设备
  - 握手获取设备时间戳
  - AES-128-CBC 加密通信
  - 发送 JSON-RPC 命令并解密响应

依赖: pip install pycryptodome

用法:
  python 01_miio_local_demo.py scan
  python 01_miio_local_demo.py info <IP> <TOKEN>
  python 01_miio_local_demo.py command <IP> <TOKEN> <METHOD> [PARAMS]
  python 01_miio_local_demo.py power <IP> <TOKEN> <on|off>

与你的 C 语言实现 (XiaomiGateway/) 功能完全对应:
  scan   → discovery.c  miio_discover()
  info   → command.c    miio_get_info()
  command → command.c   miio_send_command()
  power  → command.c    miio_set_power()
"""

import socket
import json
import hashlib
import struct
import time
import sys
import os

try:
    from Crypto.Cipher import AES
    from Crypto.Util.Padding import pad, unpad
except ImportError:
    try:
        from Cryptodome.Cipher import AES
        from Cryptodome.Util.Padding import pad, unpad
    except ImportError:
        print("请先安装依赖: pip install pycryptodome")
        sys.exit(1)


# ═══════════════════════════════════════════════════════
#  常量
# ═══════════════════════════════════════════════════════

MIIO_PORT = 54321
MIIO_MULTICAST = "224.0.0.50"
MIIO_MAGIC = 0x2131
HELLO_PACKET = (
    b'\x21\x31'                            # Magic
    + b'\x00\x20'                          # Length = 32
    + b'\xff\xff\xff\xff'                  # Device ID = broadcast
    + b'\x00\x00\x00\x00'                 # Timestamp = 0
    + b'\xff' * 16                         # Nonce (16 bytes)
    + b'\x00' * 32                         # Signature (32 bytes)
)


# ═══════════════════════════════════════════════════════
#  加密模块 (对应 miio_crypto.c)
# ═══════════════════════════════════════════════════════

def md5(data: bytes) -> bytes:
    """计算 MD5 摘要"""
    return hashlib.md5(data).digest()


def derive_keys(token_hex: str) -> tuple:
    """
    从 token 派生密钥三元组

    算法（与你的 C 实现完全一致）:
      aes_key  = MD5(token_bytes)
      aes_iv   = MD5(aes_key || token_bytes)
      sign_key = MD5(aes_key || aes_iv || aes_key)

    Args:
        token_hex: 32字符十六进制 token

    Returns:
        (aes_key, aes_iv, sign_key) 各 16 字节
    """
    if len(token_hex) != 32:
        raise ValueError(f"Token 长度错误: 期望32，实际{len(token_hex)}")

    token_bytes = bytes.fromhex(token_hex)
    aes_key = md5(token_bytes)
    aes_iv = md5(aes_key + token_bytes)
    sign_key = md5(aes_key + aes_iv + aes_key)

    return aes_key, aes_iv, sign_key


def aes_encrypt(plaintext: bytes, key: bytes, iv: bytes) -> bytes:
    """AES-128-CBC 加密 + PKCS7 padding"""
    cipher = AES.new(key, AES.MODE_CBC, iv=iv)
    return cipher.encrypt(pad(plaintext, 16))


def aes_decrypt(ciphertext: bytes, key: bytes, iv: bytes) -> bytes:
    """AES-128-CBC 解密 + PKCS7 unpadding"""
    cipher = AES.new(key, AES.MODE_CBC, iv=iv)
    return unpad(cipher.decrypt(ciphertext), 16)


def calc_sign(timestamp: int, nonce: bytes, sign_key: bytes) -> bytes:
    """
    计算 miIO 签名

    与 C 代码 miio_sign() 一致:
      sign = MD5(sign_key || timestamp_LE(4B) || nonce(16B))

    注意: python-miio 的实现稍有不同（只用 MD5(timestamp+nonce)），
    这里保持与你的 C 实现一致。
    """
    data = sign_key + struct.pack("<I", timestamp) + nonce
    return md5(data)


# ═══════════════════════════════════════════════════════
#  设备发现 (对应 discovery.c miio_discover)
# ═══════════════════════════════════════════════════════

def discover(timeout: float = 5.0) -> list:
    """
    通过 UDP 组播发现局域网内 miIO 设备

    Returns:
        [{"ip": str, "device_id": str, "timestamp": int}, ...]
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(timeout)

    # 尝试加入组播组
    try:
        mreq = struct.pack("4sL",
            socket.inet_aton(MIIO_MULTICAST),
            socket.INADDR_ANY)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    except OSError:
        pass

    devices = []
    seen_ids = set()

    try:
        sock.sendto(HELLO_PACKET, (MIIO_MULTICAST, MIIO_PORT))
        start = time.time()

        while time.time() - start < timeout:
            try:
                data, addr = sock.recvfrom(4096)
            except socket.timeout:
                break

            if len(data) < 32:
                continue

            magic = struct.unpack_from(">H", data, 0)[0]
            if magic != MIIO_MAGIC:
                continue

            device_id = struct.unpack_from("<I", data, 4)[0]
            timestamp = struct.unpack_from("<I", data, 8)[0]

            if device_id == 0xFFFFFFFF:
                continue
            if device_id in seen_ids:
                continue

            seen_ids.add(device_id)
            devices.append({
                "ip": addr[0],
                "device_id": f"{device_id:08X}",
                "timestamp": timestamp,
            })

    finally:
        sock.close()

    return devices


# ═══════════════════════════════════════════════════════
#  握手 (对应 command.c do_handshake)
# ═══════════════════════════════════════════════════════

def handshake(ip: str, port: int = MIIO_PORT, timeout: int = 10) -> int:
    """
    与设备握手，获取设备时间戳

    Args:
        ip: 设备 IP
        port: 设备端口（默认 54321）
        timeout: 超时秒数

    Returns:
        设备时间戳 (uint32)

    Raises:
        ConnectionError: 握手失败
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)

    try:
        sock.sendto(HELLO_PACKET, (ip, port))
        resp, _ = sock.recvfrom(4096)

        if len(resp) < 32:
            raise ConnectionError(f"响应太短: {len(resp)} 字节")

        magic = struct.unpack_from(">H", resp, 0)[0]
        if magic != MIIO_MAGIC:
            raise ConnectionError(f"Magic 错误: 0x{magic:04X}")

        device_ts = struct.unpack_from("<I", resp, 8)[0]
        return device_ts

    finally:
        sock.close()


# ═══════════════════════════════════════════════════════
#  发送加密命令 (对应 command.c miio_send_command)
# ═══════════════════════════════════════════════════════

def send_command(ip: str, token_hex: str,
                 method: str, params: list,
                 request_id: int = 1,
                 port: int = MIIO_PORT,
                 timeout: int = 10) -> dict:
    """
    发送加密的 miIO 命令并返回解密后的 JSON 响应

    完整流程（与 C 代码一致）:
      1. Hello 握手 → 获取设备时间戳
      2. 派生密钥 (token → aes_key, iv, sign_key)
      3. 构建 JSON-RPC 载荷 → AES 加密
      4. 计算签名 → 组装完整报文
      5. UDP 发送 → 接收响应 → 解密

    Args:
        ip: 设备 IP
        token_hex: 32位十六进制 token
        method: RPC 方法名（如 "miIO.info", "get_prop", "set_power"）
        params: 参数列表
        request_id: 请求 ID
        port: 设备端口
        timeout: 超时秒数

    Returns:
        JSON-RPC 响应字典

    Raises:
        ConnectionError: 通信失败
        ValueError: Token 格式错误
    """
    # Step 1: 握手
    device_ts = handshake(ip, port, timeout)

    # Step 2: 派生密钥
    aes_key, aes_iv, sign_key = derive_keys(token_hex)

    # Step 3: 构建并加密 JSON-RPC 载荷
    payload = json.dumps({
        "id": request_id,
        "method": method,
        "params": params,
    }).encode()
    encrypted = aes_encrypt(payload, aes_key, aes_iv)

    # Step 4: 构建完整报文
    nonce = bytes([i * 17 + 42 for i in range(16)])
    signature = calc_sign(device_ts, nonce, sign_key)

    packet = bytearray()
    packet += struct.pack(">H", MIIO_MAGIC)       # Magic (big-endian)
    packet += b'\x00\x00'                          # Length (placeholder)
    packet += struct.pack("<I", 0xFFFFFFFF)        # Device ID
    packet += struct.pack("<I", device_ts)         # Timestamp
    packet += nonce                                # Nonce (16B)
    packet += signature                            # Signature (32B)
    packet += encrypted                            # Encrypted payload

    # 填充长度字段
    total_len = len(packet)
    packet[2] = (total_len >> 8) & 0xFF
    packet[3] = total_len & 0xFF

    # Step 5: 发送并接收
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)

    try:
        sock.sendto(bytes(packet), (ip, port))
        resp_data, _ = sock.recvfrom(4096)
    finally:
        sock.close()

    if len(resp_data) < 32:
        raise ConnectionError(f"响应太短: {len(resp_data)} 字节")

    resp_enc = resp_data[32:]
    if len(resp_enc) == 0:
        return {"id": request_id, "result": "header_only"}

    # Step 6: 解密响应
    try:
        decrypted = aes_decrypt(resp_enc, aes_key, aes_iv)
        return json.loads(decrypted.decode())
    except Exception as e:
        raise ConnectionError(f"解密失败: {e}")


# ═══════════════════════════════════════════════════════
#  便捷方法
# ═══════════════════════════════════════════════════════

def get_info(ip: str, token: str) -> dict:
    """获取设备信息 (miIO.info)"""
    return send_command(ip, token, "miIO.info", [])


def get_prop(ip: str, token: str, props: list) -> dict:
    """获取设备属性 (get_prop)"""
    return send_command(ip, token, "get_prop", props)


def set_power(ip: str, token: str, state: str) -> dict:
    """设置电源状态 (set_power)"""
    return send_command(ip, token, "set_power", [state])


# ═══════════════════════════════════════════════════════
#  CLI 入口
# ═══════════════════════════════════════════════════════

def print_separator():
    print("━" * 60)


def cmd_scan(args):
    """扫描局域网设备"""
    timeout = int(args[0]) if len(args) > 0 else 5

    print_separator()
    print("  miIO 设备扫描")
    print_separator()
    print(f"超时: {timeout}秒\n")

    devices = discover(timeout)

    if not devices:
        print("未发现任何设备。请检查:")
        print("  • 设备是否在同一局域网")
        print("  • 是否关闭了 AP 隔离（路由器设置）")
        return

    print(f"{'IP':<18} {'Device ID':<14} {'Timestamp'}")
    print("─" * 50)
    for d in devices:
        print(f"{d['ip']:<18} {d['device_id']:<14} {d['timestamp']}")
    print(f"\n共发现 {len(devices)} 个设备")


def cmd_info(args):
    """查询设备信息"""
    if len(args) < 2:
        print("用法: python 01_miio_local_demo.py info <IP> <TOKEN>")
        return

    ip, token = args[0], args[1]

    print_separator()
    print(f"  查询设备信息: {ip}")
    print_separator()

    try:
        result = get_info(ip, token)
        print(json.dumps(result, indent=2, ensure_ascii=False))
    except Exception as e:
        print(f"错误: {e}")


def cmd_command(args):
    """发送自定义命令"""
    if len(args) < 3:
        print("用法: python 01_miio_local_demo.py command <IP> <TOKEN> <METHOD> [PARAMS_JSON]")
        return

    ip, token, method = args[0], args[1], args[2]
    params = json.loads(args[3]) if len(args) > 3 else []

    print_separator()
    print(f"  发送命令: {method} {params}")
    print(f"  目标: {ip}")
    print_separator()

    try:
        result = send_command(ip, token, method, params)
        print(json.dumps(result, indent=2, ensure_ascii=False))
    except Exception as e:
        print(f"错误: {e}")


def cmd_power(args):
    """开关电源"""
    if len(args) < 3:
        print("用法: python 01_miio_local_demo.py power <IP> <TOKEN> <on|off>")
        return

    ip, token, state = args[0], args[1], args[2]
    if state not in ("on", "off"):
        print("状态必须是 on 或 off")
        return

    print_separator()
    print(f"  设置电源: {ip} → {state}")
    print_separator()

    try:
        result = set_power(ip, token, state)
        print(json.dumps(result, indent=2, ensure_ascii=False))
    except Exception as e:
        print(f"错误: {e}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(0)

    cmd = sys.argv[1]

    if cmd in ("--help", "-h", "help"):
        print(__doc__)
        sys.exit(0)

    args = sys.argv[2:]

    commands = {
        "scan": cmd_scan,
        "info": cmd_info,
        "command": cmd_command,
        "power": cmd_power,
    }

    if cmd in commands:
        commands[cmd](args)
    else:
        print(f"未知命令: {cmd}")
        print(f"可用命令: {', '.join(commands.keys())}")
