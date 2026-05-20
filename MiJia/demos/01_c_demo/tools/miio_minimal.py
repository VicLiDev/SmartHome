#!/usr/bin/env python3
"""
miio_minimal.py — miIO 协议最小实现（学习研究版）

功能：
  1. UDP 组播发现局域网内米家设备
  2. 与设备握手获取时间戳
  3. 发送加密命令并解密响应

依赖：pip install pycryptodome

用法：
  python3 miio_minimal.py                    # 发现设备
  python3 miio_minimal.py --token xxx        # 发现 + 尝试查询
  python3 miio_minimal.py --ip x.x.x.x      # 查询指定设备
"""

import socket
import json
import hashlib
import struct
import time
import argparse
import sys
from Crypto.Cipher import AES
from Crypto.Util.Padding import pad, unpad

# ═══ 常量 ═══
BROADCAST_IP = "224.0.0.50"
MIIO_PORT = 54321
MIIO_MAGIC = 0x2131
HEADER_SIZE = 32
NONCE_SIZE = 16
SIGN_SIZE = 32
TOKEN_HEX_LEN = 32


# ═══ 加密工具 ═══

def md5(data: bytes) -> bytes:
    """计算 MD5 哈希"""
    return hashlib.md5(data).digest()


def hex_to_bytes(hex_str: str) -> bytes:
    """十六进制字符串 → bytes"""
    return bytes.fromhex(hex_str)


def derive_keys(token_hex: str) -> tuple:
    """
    从 token 派生密钥三元组。
    
    返回：(aes_key[16], aes_iv[16], sign_key[16])
    算法来源：python-miio 源码
    """
    token = hex_to_bytes(token_hex)
    aes_key = md5(token)
    aes_iv = md5(aes_key + token)
    sign_key = md5(aes_key + aes_iv + aes_key)
    return aes_key, aes_iv, sign_key


def aes_encrypt(plaintext: bytes, key: bytes, iv: bytes) -> bytes:
    """AES-128-CBC 加密（PKCS7 padding）"""
    cipher = AES.new(key, AES.MODE_CBC, iv=iv)
    return cipher.encrypt(pad(plaintext, 16))


def aes_decrypt(ciphertext: bytes, key: bytes, iv: bytes) -> bytes:
    """AES-128-CBC 解密（PKCS7 unpadding）"""
    cipher = AES.new(key, AES.MODE_CBC, iv=iv)
    return unpad(cipher.decrypt(ciphertext), 16)


def compute_sign(timestamp: int, nonce: bytes, sign_key: bytes) -> bytes:
    """计算 miIO 报文签名"""
    raw = struct.pack("<I", timestamp) + nonce
    return md5(sign_key + raw)


# ═══ 报文构建 ═══

def build_hello() -> bytes:
    """构建明文 Hello 广播报文（用于设备发现）"""
    header = b''
    header += struct.pack(">H", MIIO_MAGIC)          # Magic (big-endian)
    header += struct.pack(">H", HEADER_SIZE)         # Length = 32
    header += struct.pack("<I", 0xFFFFFFFF)           # Device ID (broadcast)
    header += struct.pack("<I", 0)                    # Timestamp = 0
    header += b'\xff' * NONCE_SIZE                    # Nonce (all 0xFF)
    header += b'\x00' * SIGN_SIZE                     # Signature (all zeros)
    return header


def build_encrypted_packet(token_hex: str, device_ts: int,
                           method: str, params: list,
                           req_id: int = 1) -> bytes:
    """
    构建加密的 miIO 命令报文
    
    流程：
      1. 从 token 派生密钥
      2. 构建 JSON-RPC 载荷
      3. AES 加密载荷
      4. 计算签名
      5. 组装完整报文
    """
    key, iv, sign_key = derive_keys(token_hex)

    # JSON-RPC 载荷
    payload = json.dumps({
        "id": req_id,
        "method": method,
        "params": params,
    }, separators=(',', ':')).encode()

    # 加密
    encrypted = aes_encrypt(payload, key, iv)

    # 随机 nonce
    nonce = bytes(range(16))

    # 签名
    signature = compute_sign(device_ts, nonce, sign_key)

    # 组装报文
    packet = b''
    packet += struct.pack(">H", MIIO_MAGIC)
    payload_len = HEADER_SIZE + len(encrypted)
    packet += struct.pack(">H", payload_len)         # Total length
    packet += struct.pack("<I", 0xFFFFFFFF)           # Device ID
    packet += struct.pack("<I", device_ts)            # Timestamp
    packet += nonce                                   # Nonce
    packet += signature                               # Signature
    packet += encrypted                              # Encrypted payload

    return packet


def parse_response(raw: bytes, token_hex: str) -> dict:
    """解析并解密设备响应"""
    if len(raw) < HEADER_SIZE:
        raise ValueError(f"响应太短 ({len(raw)} 字节)")

    # 解析头部
    magic = struct.unpack_from(">H", raw, 0)[0]
    length = struct.unpack_from(">H", raw, 2)[0]
    device_id = struct.unpack_from("<I", raw, 4)[0]
    ts = struct.unpack_from("<I", raw, 8)[0]

    print(f"  [头部] magic=0x{magic:04X} len={length} "
          f"device_id=0x{device_id:08X} ts={ts}")

    # 提取加密载荷
    encrypted_payload = raw[HEADER_SIZE:]
    if len(encrypted_payload) == 0:
        return {"raw_header_only": True}

    # 解密
    key, iv, _ = derive_keys(token_hex)
    decrypted = aes_decrypt(encrypted_payload, key, iv)
    return json.loads(decrypted.decode())


# ═══ 核心操作 ═══

def discover(timeout: float = 5.0) -> list:
    """
    通过 UDP 组播发现 miIO 设备
    
    返回设备列表，每项包含 ip、device_id、timestamp
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(timeout)

    # 加入组播组
    mreq = socket.inet_aton(BROADCAST_IP) + socket.inet_aton("0.0.0.0")
    sock.setsockopt(socket.IPPROTO_IP, IP_ADD_MEMBERSHIP, mreq)

    hello = build_hello()
    dest = (BROADCAST_IP, MIIO_PORT)
    sock.sendto(hello, dest)

    devices = []
    start = time.time()

    while time.time() - start < timeout:
        try:
            data, addr = sock.recvfrom(4096)
            if len(data) >= HEADER_SIZE:
                dev_id = struct.unpack_from("<I", data, 4)[0]
                ts = struct.unpack_from("<I", data, 8)[0]

                devices.append({
                    "ip": addr[0],
                    "port": addr[1],
                    "device_id": f"{dev_id:08X}",
                    "timestamp": ts,
                })
        except socket.timeout:
            break

    sock.close()
    return devices


def do_handshake(ip: str, port: int = MIIO_PORT,
                 timeout: int = 10) -> tuple:
    """
    与单台设备握手，返回 (encrypted_response, device_timestamp)
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)

    hello = build_hello()
    sock.sendto(hello, (ip, port))
    resp, addr = sock.recvfrom(4096)
    sock.close()

    device_ts = struct.unpack_from("<I", resp, 8)[0]
    enc_data = resp[HEADER_SIZE:] if len(resp) > HEADER_SIZE else b""
    return enc_data, device_ts


def send_command(ip: str, token_hex: str,
                 method: str, params: list,
                 req_id: int = 1,
                 timeout: int = 10) -> dict:
    """
    完整的发送命令流程：握手 → 构建加密包 → 发送 → 解密响应
    """
    # Step 1: 握手获取设备时间戳
    _, device_ts = do_handshake(ip, timeout=timeout)

    # Step 2: 构建并发送加密命令
    packet = build_encrypted_packet(token_hex, device_ts,
                                    method, params, req_id)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    sock.sendto(packet, (ip, MIIO_PORT))

    response, addr = sock.recvfrom(4096)
    sock.close()

    # Step 3: 解密响应
    return parse_response(response, token_hex)


# ═══ CLI 入口 ═══

def main():
    parser = argparse.ArgumentParser(
        description="miIO 协议最小 Demo — 学习研究版",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  %(prog)s                          # 扫描局域网设备
  %(prog)s --token abc123...        # 扫描后尝试查询每个设备
  %(prog)s --ip 192.168.1.100 --token abc123  # 查询指定设备
        """,
    )
    parser.add_argument("--token", help="设备 Token（32位十六进制）")
    parser.add_argument("--ip", help="指定设备 IP（跳过扫描）")
    parser.add_argument("-t", "--timeout", type=float, default=5.0,
                        help="扫描超时秒数（默认 5）")
    args = parser.parse_args()

    print("=" * 60)
    print("  miIO 协议最小 Demo — 学习研究版")
    print("=" * 60)

    if args.ip:
        # 单台设备模式
        if not args.token:
            print("❌ 指定 --ip 时必须同时提供 --token")
            sys.exit(1)

        print(f"\n📡 连接 {args.ip}...")
        try:
            result = send_command(args.ip, args.token, "miIO.info", [])
            print(json.dumps(result, indent=2, ensure_ascii=False))
        except Exception as e:
            print(f"❌ 错误: {e}")
        return

    # 扫描模式
    print(f"\n📡 正在扫描局域网（超时 {args.timeout}s）...")
    devices = discover(timeout=args.timeout)

    if not devices:
        print("\n❌ 未发现任何设备！")
        print("   请检查：")
        print("   • 设备是否在同一局域网")
        print("   • 是否关闭了 AP 隔离（路由器设置）")
        print("   • 部分设备休眠后可能不响应广播")
        sys.exit(1)

    print(f"\n✅ 发现 {len(devices)} 个设备:\n")
    for i, d in enumerate(devices):
        print(f"  [{i}] IP: {d['ip']:<16} "
              f"ID: {d['device_id']:<12} "
              f"TS: {d['timestamp']}")

    # 如果提供了 token，尝试查询
    if args.token:
        print(f"\n🔑 使用 Token 查询设备信息...")
        for i, d in enumerate(devices):
            print(f"\n--- [{i}] {d['ip']} ---")
            try:
                result = send_command(d["ip"], args.token,
                                      "miIO.info", [])
                print(json.dumps(result, indent=2, ensure_ascii=False))
            except Exception as e:
                print(f"  ⚠️ 查询失败: {e}")

    print("\n" + "=" * 60)
    print("💡 下一步:")
    print("   1. 用 Wireshark 抓包分析协议帧: sudo wireshark -f 'udp port 54321'")
    print("   2. 阅读 python-miio 源码了解完整实现")
    print("   3. 查看 README.md 了解更多方案")
    print("=" * 60)


if __name__ == "__main__":
    main()
