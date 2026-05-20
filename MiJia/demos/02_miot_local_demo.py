"""
Demo 2: miOT 协议本地控制

miOT 是小米新一代物联网协议，用于 2019 年后的新设备。
使用标准化的 SIID/PIID 描述设备能力。

注意:
  - 不是所有设备都开放本地端口
  - 新设备越来越多强制走云端
  - 本 demo 尝试通过 COAP/HTTP 本地通信

依赖: pip install requests

用法:
  python 02_miot_local_demo.py discover <IP>
  python 02_miot_local_demo.py spec <MODEL>
  python 02_miot_local_demo.py control <IP> <TOKEN> --siid 2 --piid 1 --value true
"""

import json
import struct
import socket
import time
import hashlib
import sys
import argparse

try:
    import requests
except ImportError:
    print("请先安装依赖: pip install requests")
    sys.exit(1)


# ═══════════════════════════════════════════════════════
#  miOT 规范查询（从 miot-spec.org 获取）
# ═══════════════════════════════════════════════════════

MIOT_SPEC_API = "https://miot-spec.org/miot-spec-v2/instances"


def query_device_spec(model: str) -> dict:
    """
    从 miot-spec.org 查询设备的 SIID/PIID 映射

    Args:
        model: 设备型号（如 "zhimi.airpurifier.mb4"）

    Returns:
        设备规格定义（包含所有 Service/Property/Action）
    """
    try:
        resp = requests.get(f"{MIOT_SPEC_API}?model={model}", timeout=10)
        if resp.status_code == 200:
            data = resp.json()
            if data.get("result"):
                return data["result"][0] if isinstance(data["result"], list) else data["result"]
    except Exception as e:
        print(f"查询失败: {e}")

    return {}


def print_spec(spec: dict):
    """打印设备规格"""
    if not spec:
        print("未找到设备规格")
        return

    print(f"\n设备型号: {spec.get('model', 'unknown')}")
    print(f"设备名称: {spec.get('name', 'unknown')}")
    print(f"类型: {spec.get('type', 'unknown')}")
    print()

    services = spec.get("services", [])
    print("服务列表:")
    print(f"{'SIID':<6} {'类型':<10} {'名称':<20} {'描述'}")
    print("─" * 60)

    for svc in services:
        siid = svc.get("iid", "?")
        stype = svc.get("type", "?")
        sname = svc.get("name", "?")
        sdesc = svc.get("description", "")

        type_map = {
            "urn:miot-spec-v2:service:device-information:00000001": "设备信息",
            "urn:miot-spec-v2:service:air-conditioner:00000001": "空调",
            "urn:miot-spec-v2:service:light:00000001": "灯光",
            "urn:miot-spec-v2:service:switch:00000001": "开关",
            "urn:miot-spec-v2:service:outlet:00000001": "插座",
            "urn:miot-spec-v2:service:fan:00000001": "风扇",
            "urn:miot-spec-v2:service:curtain:00000001": "窗帘",
            "urn:miot-spec-v2:service:sensor:00000001": "传感器",
            "urn:miot-spec-v2:service:humidifier:00000001": "加湿器",
            "urn:miot-spec-v2:service:air-purifier:00000001": "净化器",
        }
        nice_type = type_map.get(stype, stype.split(":")[-2] if ":" in stype else stype)
        print(f"{siid:<6} {nice_type:<20} {sname:<20} {sdesc}")

        # 打印属性
        props = svc.get("properties", [])
        for prop in props:
            piid = prop.get("iid", "?")
            pname = prop.get("name", "?")
            pfmt = prop.get("format", "?")
            paccess = prop.get("access", [])
            punit = prop.get("unit", "")
            access_str = ",".join(paccess) if isinstance(paccess, list) else str(paccess)
            print(f"  └─ PIID={piid:<4} {pname:<20} 格式={pfmt:<8} 访问={access_str} {punit}")


# ═══════════════════════════════════════════════════════
#  miOT 本地发现
# ═══════════════════════════════════════════════════════

def miot_discover(ip: str, timeout: float = 5.0) -> dict:
    """
    尝试发现 miOT 设备

    通过 UDP 广播到 54321 端口，检查设备是否响应。
    miOT 设备也会响应 miIO Hello（因为兼容）。
    """
    hello = (
        b'\x21\x31'
        + b'\x00\x20'
        + b'\xff\xff\xff\xff'
        + b'\x00\x00\x00\x00'
        + b'\xff' * 16
        + b'\x00' * 32
    )

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)

    try:
        sock.sendto(hello, (ip, 54321))
        resp, _ = sock.recvfrom(4096)

        if len(resp) >= 32:
            device_id = struct.unpack_from("<I", resp, 4)[0]
            ts = struct.unpack_from("<I", resp, 8)[0]
            return {
                "ip": ip,
                "device_id": f"{device_id:08X}",
                "timestamp": ts,
                "raw_response_len": len(resp),
                "has_encrypted_payload": len(resp) > 32,
            }
    except socket.timeout:
        return {"error": "超时，设备无响应"}
    except Exception as e:
        return {"error": str(e)}
    finally:
        sock.close()

    return {"error": "未知错误"}


# ═══════════════════════════════════════════════════════
#  miOT 属性控制（通过加密 miIO 协议下发 miOT 命令）
# ═══════════════════════════════════════════════════════

def derive_keys(token_hex: str):
    """从 token 派生 miIO 密钥"""
    token_bytes = bytes.fromhex(token_hex)
    aes_key = hashlib.md5(token_bytes).digest()
    aes_iv = hashlib.md5(aes_key + token_bytes).digest()
    return aes_key, aes_iv


def miot_set_prop(ip: str, token_hex: str,
                  siid: int, piid: int, value) -> dict:
    """
    通过 miIO 协议发送 miOT 属性设置命令

    使用 miIO 协议的 "set_properties" 方法下发 miOT 格式的参数。

    Args:
        ip: 设备 IP
        token_hex: 设备 token
        siid: 服务 ID
        piid: 属性 ID
        value: 属性值

    Returns:
        响应字典
    """
    from Crypto.Cipher import AES
    from Crypto.Util.Padding import pad, unpad

    aes_key, aes_iv = derive_keys(token_hex)

    # 1. 握手
    hello = (
        b'\x21\x31' + b'\x00\x20' + b'\xff\xff\xff\xff'
        + b'\x00\x00\x00\x00' + b'\xff' * 16 + b'\x00' * 32
    )
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(10)
    sock.sendto(hello, (ip, 54321))
    resp, _ = sock.recvfrom(4096)
    device_ts = struct.unpack_from("<I", resp, 8)[0]
    sock.close()

    # 2. 构建 miOT 格式的参数
    payload_dict = {
        "id": 1,
        "method": "set_properties",
        "params": [{
            "did": f"set_{siid}_{piid}",
            "siid": siid,
            "piid": piid,
            "value": value,
        }],
    }
    payload = json.dumps(payload_dict).encode()

    # 3. 加密
    cipher = AES.new(aes_key, AES.MODE_CBC, iv=aes_iv)
    encrypted = cipher.encrypt(pad(payload, 16))

    # 4. 构建报文
    nonce = bytes([i * 17 + 42 for i in range(16)])
    sign_data = aes_iv + struct.pack("<I", device_ts) + nonce
    signature = hashlib.md5(sign_data).digest()

    packet = bytearray()
    packet += struct.pack(">H", 0x2131)
    packet += b'\x00\x00'
    packet += struct.pack("<I", 0xFFFFFFFF)
    packet += struct.pack("<I", device_ts)
    packet += nonce
    packet += signature
    packet += encrypted

    total_len = len(packet)
    packet[2] = (total_len >> 8) & 0xFF
    packet[3] = total_len & 0xFF

    # 5. 发送
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(10)
    try:
        sock.sendto(bytes(packet), (ip, 54321))
        resp_data, _ = sock.recvfrom(4096)
    finally:
        sock.close()

    # 6. 解密响应
    resp_enc = resp_data[32:]
    if len(resp_enc) == 0:
        return {"result": "header_only"}

    cipher = AES.new(aes_key, AES.MODE_CBC, iv=aes_iv)
    decrypted = unpad(cipher.decrypt(resp_enc), 16)
    return json.loads(decrypted.decode())


def miot_get_prop(ip: str, token_hex: str,
                  props: list) -> dict:
    """
    获取 miOT 属性

    Args:
        ip: 设备 IP
        token_hex: 设备 token
        props: 属性列表 [{"did": "...", "siid": 2, "piid": 1}, ...]

    Returns:
        响应字典
    """
    from Crypto.Cipher import AES
    from Crypto.Util.Padding import pad, unpad

    aes_key, aes_iv = derive_keys(token_hex)

    hello = (
        b'\x21\x31' + b'\x00\x20' + b'\xff\xff\xff\xff'
        + b'\x00\x00\x00\x00' + b'\xff' * 16 + b'\x00' * 32
    )
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(10)
    sock.sendto(hello, (ip, 54321))
    resp, _ = sock.recvfrom(4096)
    device_ts = struct.unpack_from("<I", resp, 8)[0]
    sock.close()

    payload_dict = {
        "id": 1,
        "method": "get_properties",
        "params": props,
    }
    payload = json.dumps(payload_dict).encode()

    cipher = AES.new(aes_key, AES.MODE_CBC, iv=aes_iv)
    encrypted = cipher.encrypt(pad(payload, 16))

    nonce = bytes([i * 17 + 42 for i in range(16)])
    sign_data = aes_iv + struct.pack("<I", device_ts) + nonce
    signature = hashlib.md5(sign_data).digest()

    packet = bytearray()
    packet += struct.pack(">H", 0x2131)
    packet += b'\x00\x00'
    packet += struct.pack("<I", 0xFFFFFFFF)
    packet += struct.pack("<I", device_ts)
    packet += nonce
    packet += signature
    packet += encrypted

    total_len = len(packet)
    packet[2] = (total_len >> 8) & 0xFF
    packet[3] = total_len & 0xFF

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(10)
    try:
        sock.sendto(bytes(packet), (ip, 54321))
        resp_data, _ = sock.recvfrom(4096)
    finally:
        sock.close()

    resp_enc = resp_data[32:]
    if len(resp_enc) == 0:
        return {"result": "header_only"}

    cipher = AES.new(aes_key, AES.MODE_CBC, iv=aes_iv)
    decrypted = unpad(cipher.decrypt(resp_enc), 16)
    return json.loads(decrypted.decode())


# ═══════════════════════════════════════════════════════
#  常见设备 miOT 属性速查
# ═══════════════════════════════════════════════════════

# 灯具通用属性
LIGHT_PROPS = {
    "on":      {"siid": 2, "piid": 1},   # 开关 (bool)
    "mode":    {"siid": 2, "piid": 2},   # 模式
    "bright":  {"siid": 2, "piid": 3},   # 亮度 (0-100)
    "color_t": {"siid": 2, "piid": 4},   # 色温
}

# 空调通用属性
AC_PROPS = {
    "on":      {"siid": 2, "piid": 1},   # 开关
    "mode":    {"siid": 2, "piid": 2},   # 模式 (0=auto,1=cool,2=heat,3=dry,4=fan)
    "temp":    {"siid": 2, "piid": 3},   # 目标温度
    "fan":     {"siid": 2, "piid": 4},   # 风速
}

# 插座通用属性
PLUG_PROPS = {
    "on":      {"siid": 2, "piid": 1},   # 开关
    "power":   {"siid": 3, "piid": 1},   # 当前功率 (W)
    "voltage": {"siid": 3, "piid": 2},   # 电压 (V)
}


# ═══════════════════════════════════════════════════════
#  CLI
# ═══════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(
        description="miOT 协议本地控制 Demo",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  # 查询设备规格
  python 02_miot_local_demo.py spec zhimi.airpurifier.mb4

  # 发现设备
  python 02_miot_local_demo.py discover 192.168.1.100

  # 设置属性（开灯）
  python 02_miot_local_demo.py control 192.168.1.100 TOKEN --siid 2 --piid 1 --value true

  # 获取属性
  python 02_miot_local_demo.py get 192.168.1.100 TOKEN --props 2:1 2:3
        """
    )

    sub = parser.add_subparsers(dest="command")

    sub.add_parser("spec", help="查询设备 miOT 规格定义").add_argument("model")

    p_disc = sub.add_parser("discover", help="发现 miOT 设备")
    p_disc.add_argument("ip")

    p_ctrl = sub.add_parser("control", help="设置设备属性")
    p_ctrl.add_argument("ip")
    p_ctrl.add_argument("token")
    p_ctrl.add_argument("--siid", type=int, required=True)
    p_ctrl.add_argument("--piid", type=int, required=True)
    p_ctrl.add_argument("--value", required=True, help="属性值 (true/false/数字/字符串)")

    p_get = sub.add_parser("get", help="获取设备属性")
    p_get.add_argument("ip")
    p_get.add_argument("token")
    p_get.add_argument("--props", nargs="+", required=True,
                       help="属性列表，格式 siid:piid (如 2:1 2:3)")

    args = parser.parse_args()

    if args.command == "spec":
        spec = query_device_spec(args.model)
        print_spec(spec)

    elif args.command == "discover":
        result = miot_discover(args.ip)
        print(json.dumps(result, indent=2, ensure_ascii=False))

    elif args.command == "control":
        # 解析 value 类型
        val = args.value
        if val.lower() == "true":
            val = True
        elif val.lower() == "false":
            val = False
        elif val.isdigit():
            val = int(val)
        else:
            try:
                val = float(val)
            except ValueError:
                pass  # 保持字符串

        print(f"设置 {args.ip} SIID={args.siid} PIID={args.piid} → {val}")
        result = miot_set_prop(args.ip, args.token, args.siid, args.piid, val)
        print(json.dumps(result, indent=2, ensure_ascii=False))

    elif args.command == "get":
        props = []
        for p in args.props:
            siid, piid = p.split(":")
            props.append({"siid": int(siid), "piid": int(piid)})

        print(f"获取 {args.ip} 属性: {props}")
        result = miot_get_prop(args.ip, args.token, props)
        print(json.dumps(result, indent=2, ensure_ascii=False))

    else:
        parser.print_help()


if __name__ == "__main__":
    main()
