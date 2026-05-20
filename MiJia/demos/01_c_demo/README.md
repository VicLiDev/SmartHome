# 米家（Xiaomi Smart Home）接入完全指南

> 本文档覆盖 4 个方向的米家设备接入方案，从学习研究到生产部署。
>
> 适用场景：Rockchip 开发板嵌入式网关、Home Assistant 集成、自动化脚本控制
>
> 作者：lhj | 创建日期：2026-05-19

---

## 目录

1. [背景知识](#1-背景知识)
2. [方案一：学习研究 — 协议原理与 Demo](#2-方案一学习研究)
3. [方案二：Rockchip 嵌入式网关 — C 语言实现](#3-方案二rockchip-嵌入式网关)
4. [方案三：Home Assistant 统一管理](#4-方案三home-assistant-统一管理)
5. [方案四：Python 自动化脚本](#5-方案四python-自动化脚本)
6. [实战案例：常见设备控制命令](#6-实战案例)
7. [附录：Token 获取方法](#7-附录token-获取方法)

---

## 1. 背景知识

### 1.1 米家生态体系

```
┌─────────────────────────────────────────────────────┐
│                    云端层 (Cloud)                     │
│   小米云服务器 / AIoT 平台 / open.mi.com 开放平台     │
├─────────────────────────────────────────────────────┤
│                    通信协议层                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────┐  │
│  │ miIO     │  │ MIoT     │  │ BLE Mesh         │  │
│  │ UDP:54321│  │ WiFi/BLE │  │ 蓝牙网关          │  │
│  │ 局域网    │  │ 新设备    │  │ 低功耗传感器       │  │
│  └──────────┘  └──────────┘  └──────────────────┘  │
├─────────────────────────────────────────────────────┤
│                    设备层                             │
│  空调 / 扫地机 / 灯泡 / 插座 / 传感器 / 摄像头 ...    │
└─────────────────────────────────────────────────────┘
```

### 1.2 三种核心协议对比

| 特性 | miIO | MIoT | Yeelight |
|------|------|------|----------|
| **传输层** | UDP 54321 | HTTP/HTTPS + CoAP | UDP 55443 |
| **加密方式** | MD5 + AES-128-CBC | TLS + Token 认证 | AES-CBC |
| **数据格式** | JSON-RPC 2.0 | JSON-RPC 2.0 | 自定义 JSON |
| **适用设备** | 大部分老款/中端设备 | 2020年后新款设备 | Yeelight 子品牌 |
| **开源库** | python-miio | miot-spec | python-yeelight |
| **局域网直连** | ✅ 需要 token | ⚠️ 部分支持 | ✅ 无需 token |
| **云端依赖** | 可脱离 | 强依赖 | 可脱离 |

### 1.3 关键概念

#### Device Model（设备型号）
```
格式：品牌.类型.版本
示例：
  zhimi.airpurifier.m6      — 智米空气净化器 Pro H
  roborock.vacuum.s5        — 石头扫地机 S5
  yeelink.light.color3       — Yeelight 彩光灯泡
  chuangmi.ir.v2             — 创米万能遥控器
  aircondition.miot.ac04     — 米家空调（MIoT）
```

#### Token（设备令牌）
- **32位十六进制字符串**，每个设备唯一
- 用于 miIO 协议的加解密密钥派生
- 从米家 App 备份获取或设备重置后重新配网获得

#### Device ID（did）
- 设备在小米云端的唯一标识
- 格式：数字字符串（如 `"123456789"`）

---

## 2. 方案一：学习研究 — 协议原理与 Demo

> 目标：理解 miIO 协议原理，用 Python 写一个最小可运行的 demo。

### 2.1 环境准备

```bash
# 安装 Python 依赖
pip install python-miio cryptography pycryptodome crypto

# 网络工具（用于抓包分析）
sudo apt install wireshark nmap tcpdump
```

### 2.2 最小 Demo：发现并查询设备

```python
"""
miio_minimal.py — miIO 协议最小实现
功能：UDP 广播发现 → 握手 → 查询设备属性
"""

import socket
import json
import hashlib
import struct
import time
from Crypto.Cipher import AES
from Crypto.Util.Padding import pad, unpad

# ═══ 配置 ═══
BROADCAST_IP = "224.0.0.50"   # miIO 组播地址
MIIO_PORT = 54321
TIMEOUT = 10

# ═══ 加密工具 ═══

def md5(data: bytes) -> bytes:
    return hashlib.md5(data).digest()

def derive_keys(token_hex: str) -> tuple:
    """从 token 派生加密密钥和签名密钥"""
    token = bytes.fromhex(token_hex)
    key = md5(token)           # AES 密钥
    iv = md5(key + token)      # IV 向量
    sign_key = md5(key + iv + key)  # 签名密钥
    return key, iv, sign_key

def encrypt(plaintext: bytes, key: bytes, iv: bytes) -> bytes:
    cipher = AES.new(key, AES.MODE_CBC, iv=iv)
    return cipher.encrypt(pad(plaintext, 16))

def decrypt(ciphertext: bytes, key: bytes, iv: bytes) -> bytes:
    cipher = AES.new(key, AES.MODE_CBC, iv=iv)
    return unpad(cipher.decrypt(ciphertext), 16)

def build_header(device_id: int = 0xFFFFFFFF,
                 timestamp: int = 0,
                 token_hex: str = "") -> dict:
    """构建 miIO 报文头"""
    now = int(time.time()) if not timestamp else timestamp
    header = {
        "device_id": device_id,
        "time": now,
        "nonce": b"".join([bytes([i]) for i in range(16)]),
        "sign": b"\x00" * 32,
        "data": b"",
    }
    # 如果有 token，计算签名
    if token_hex:
        _, _, sign_key = derive_keys(token_hex)
        raw = struct.pack("<I", header["time"]) + header["nonce"]
        header["sign"] = md5(sign_key + raw)
    return header

def build_request(method: str, params: list,
                 request_id: int = 1) -> bytes:
    """构建 JSON-RPC 请求体"""
    payload = {
        "id": request_id,
        "method": method,
        "params": params,
    }
    return json.dumps(payload).encode()

# ═══ 步骤1：设备发现（无需 token）═══

def discover_devices(timeout: float = 5.0) -> list:
    """
    通过 UDP 组播发现局域网内 miIO 设备。
    
    返回：
        [{"ip": str, "port": int, "device_id": str, "model": str}, ...]
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(timeout)

    # 构造 Hello 报文（明文）
    hello = b'\x21\x31\x00\x20\xff\xff\xff\xff' \
            b'\x00\x00\x00' + b'\xff' * 32 + b'\x00' * 32

    devices = []
    try:
        sock.sendto(hello, (BROADCAST_IP, MIIO_PORT))
        start = time.time()
        while time.time() - start < timeout:
            try:
                data, addr = sock.recvfrom(4096)
                # 解析响应（前 32 字节是头部）
                if len(data) >= 32:
                    dev_id = data[8:12].hex()
                    ts = struct.unpack_from("<I", data, 12)[0]
                    # 尝试解析 JSON 部分
                    try:
                        body = json.loads(data[32:].decode())
                        model = body.get("result", [{}])[0].get("model", "unknown")
                    except:
                        model = "unknown"
                    devices.append({
                        "ip": addr[0],
                        "port": addr[1],
                        "device_id": dev_id,
                        "timestamp": ts,
                        "model": model,
                    })
            except socket.timeout:
                break
    finally:
        sock.close()

    return devices

# ═══ 步骤2：握手（Handshake）═══

def handshake(ip: str, port: int = MIIO_PORT,
              timeout: int = 10) -> tuple:
    """
    与设备握手，获取初始响应。
    返回：(response_data, device_time)
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)

    # 发送 Hello
    hello = b'\x21\x31\x00\x20\xff\xff\xff\xff' \
            b'\x00\x00\x00' + b'\xff' * 32 + b'\x00' * 32
    sock.sendto(hello, (ip, port))

    resp, _ = sock.recvfrom(4096)
    sock.close()

    device_ts = struct.unpack_from("<I", resp, 12)[0]
    encrypted_data = resp[32:]  # 加密的响应体
    return encrypted_data, device_ts

# ═══ 步骤3：发送加密命令 ═══

def send_command(ip: str, token_hex: str,
                 method: str, params: list,
                 request_id: int = 1,
                 port: int = MIIO_PORT,
                 timeout: int = 10) -> dict:
    """
    发送加密的 miIO 命令并返回结果。
    
    参数:
        ip:       设备 IP
        token_hex: 32位十六进制 token
        method:   方法名（如 "get_prop"、"set_power"）
        params:   参数列表
    """
    key, iv, sign_key = derive_keys(token_hex)

    # 1. 先握手获取设备时间戳
    enc_resp, device_ts = handshake(ip, port, timeout)

    # 2. 构建请求体并加密
    payload = build_request(method, params, request_id)
    encrypted = encrypt(payload, key, iv)

    # 3. 构建完整报文
    header = build_header(0xFFFFFFFF, device_ts, token_hex)
    packet = (
        b'\x21\x31'                          # Magic
        b'\x00\x20'                          # Length (placeholder)
        b'\xff\xff\xff\xff'                   # Device ID
        struct.pack("<I", device_ts)          # Timestamp
        header["nonce"]                       # Nonce (16 bytes)
        header["sign"]                        # Signature (32 bytes)
        encrypted                            # Encrypted payload
    )

    # 更新长度字段
    length = len(packet) - 4
    packet = packet[:2] + struct.pack(">H", length) + packet[4:]

    # 4. 发送
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout)
    sock.sendto(packet, (ip, port))

    response, _ = sock.recvfrom(4096)
    sock.close()

    # 5. 解密响应
    resp_encrypted = response[32:]
    decrypted = decrypt(resp_encrypted, key, iv)
    return json.loads(decrypted.decode())


# ═══ 主程序 ═══

if __name__ == "__main__":
    print("=" * 60)
    print("  miIO 协议最小 Demo — 学习研究版")
    print("=" * 60)

    # Step 1: 发现设备
    print("\n[Step 1] 正在扫描局域网内的 miIO 设备...")
    devices = discover_devices(timeout=5)

    if not devices:
        print("  ❌ 未发现任何设备！请检查：")
        print("     • 设备是否在同一局域网")
        print("     • 是否关闭了 AP 隔离（路由器设置）")
        exit(1)

    print(f"  ✓ 发现 {len(devices)} 个设备：\n")
    for i, d in enumerate(devices):
        print(f"  [{i}] IP: {d['ip']:<16} "
              f"ID: {d['device_id']:<18} "
              f"Model: {d['model']}")

    # Step 2: 查询设备信息（需要 token）
    print("\n[Step 2] 查询设备信息需要 Token")
    print("  Token 获取方法见本文档「附录」章节\n")

    # 示例：如果有 token，可以这样调用
    # result = send_command(
    #     ip="192.168.1.100",
    #     token_hex="你的32位token",
    #     method="miIO.info",
    #     params=[]
    # )
    # print(json.dumps(result, indent=2, ensure_ascii=False))

    print("\n" + "=" * 60)
    print("  下一步：获取 Token 后即可发送加密命令")
    print("  参考：python-miio 库提供更完整的实现")
    print("=" * 60)
```

### 2.3 用 Wireshark 分析协议帧

```bash
# 抓取 miIO 流量（以 UDP 54321 为过滤条件）
sudo wireshark -k -f "udp port 54321"

# 或命令行直接 dump
sudo tcpdump -i any udp port 54321 -w miio_capture.pcap
```

**miIO 报文结构（32 字节头部 + 变长载荷）：**

```
偏移  长度  字段              说明
─────────────────────────────────────────
0x00   2    Magic             固定值 0x2131
0x02   2    Length            整个报文长度（大端）
0x04   4    Device ID         设备 ID（小端）
0x08   4    Timestamp         Unix 时间戳（小端）
0x0C   16   Nonce             随机数（用于加密 IV）
0x1C   32   Signature         MD5 签名
0x3C   N    Payload           AES-128-CBC 加密的 JSON-RPC
```

### 2.4 推荐学习资源

| 资源 | 链接 | 说明 |
|------|------|------|
| python-miio | github.com/rytilahti/python-miio | 最成熟的 Python 库，必读源码 |
| miIO 协议逆向 | github.com/nicekwell/xiaomi_robot_vacuum_protocol | 扫地机协议文档 |
| MIoT Spec | miot-spec.org | 官方 MIoT 设备规范 |
| Wireshark 解析器 | wiki.openwrt.to/en/usecases/xiaomi.devices.wireshark | miIO dissector 插件 |

---

## 3. 方案二：Rockchip 嵌入式网关 — C 语言实现

> 目标：在 RK3588/RK3576 等 Rockchip 开发板上运行一个轻量级网关服务，
> 通过局域网直连控制米家设备，不依赖云端。

### 3.1 项目结构

```
Projects/XiaomiGateway/
├── README.md                  # 本文件
├── Makefile                   # 交叉编译 Makefile
├── prjBuild.sh                # 构建脚本（aarch64 / armhf）
│
├── inc/                       # 头文件
│   ├── miio_protocol.h        # miIO 协议定义与常量
│   ├── miio_crypto.h          # MD5/AES 加解密接口
│   ├── discovery.h            # UDP 设备发现接口
│   ├── command.h              # JSON-RPC 命令构建/解析
│   ├── gateway.h              # 网关主服务接口
│   └── http_api.h            # HTTP REST API 接口
│
├── src/                       # 源文件
│   ├── main.c                 # 入口 / CLI 解析
│   ├── miio_crypto.c          # OpenSSL MD5 + AES 实现
│   ├── discovery.c            # UDP 广播发现
│   ├── command.c              # 命令收发核心逻辑
│   ├── gateway.c              # 网关守护进程
│   └── http_api.c            # 内嵌 HTTP API（可选）
│
├── third_party/               # 第三方库
│   └── cJSON/
│       ├── cJSON.c
│       └── cJSON.h
│
├── config/                    # 配置文件
│   ├── gateway.conf           # 网关配置（端口/token 列表等）
│   └── devices.json           # 已知设备列表
│
└── tools/                     # 辅助工具
    ├── miio_scan              # 扫描工具编译产物
    └── miio_gateway           # 网关服务编译产物
```

### 3.2 核心模块设计

#### 3.2.1 协议层 (`miio_protocol.h`)

```c
/*
 * miio_protocol.h — miIO 协议常量与数据结构
 */

#ifndef MIIO_PROTOCOL_H
#define MIIO_PROTOCOL_H

#include <stdint.h>

/* ═══ 协议常量 ═══ */
#define MIIO_PORT           54321
#define MIIO_MULTICAST      "224.0.0.50"
#define MIIO_MAGIC          0x2131
#define MIIO_HEADER_SIZE    32
#define NONCE_SIZE          16
#define SIGNATURE_SIZE      32
#define TOKEN_HEX_LEN       32       /* 32 字符 = 128 bit */
#define MAX_PAYLOAD_SIZE    16384
#define MAX_DEVICE_COUNT    64
#define MAX_IP_STRLEN       16
#define MAX_MODEL_LEN       64

/* ═══ 数据结构 ═══ */

/* 设备信息 */
typedef struct {
    char         ip[MAX_IP_STRLEN];
    uint16_t     port;
    uint32_t     device_id;
    char         model[MAX_MODEL_LEN];
    char         token[TOKEN_HEX_LEN + 1];
    uint32_t     last_seen;      /* Unix 时间戳 */
    int          online;         /* 1=在线 0=离线 */
} MiioDevice;

/* miIO 报文头 */
typedef struct __attribute__((packed)) {
    uint16_t     magic;
    uint16_t     length;
    uint32_t     device_id;
    uint32_t     timestamp;
    uint8_t      nonce[NONCE_SIZE];
    uint8_t     signature[SIGNATURE_SIZE];
} MiioHeader;

/* JSON-RPC 请求/响应 */
typedef struct {
    int          id;             /* 请求 ID */
    char         method[128];    /* 方法名 */
    char        *params_json;    /* 参数 JSON 字符串 */
} MiioRequest;

typedef struct {
    int          id;
    int          error_code;     /* 0=成功 */
    char         error_msg[256];
    char        *result_json;    /* 结果 JSON */
} MiioResponse;

/* 网关配置 */
typedef struct {
    int          listen_port;    /* API 监听端口 */
    int          scan_timeout;   /* 扫描超时（秒） */
    int          cmd_timeout;    /* 命令超时（秒） */
    char         config_path[256];
    char         db_path[256];   /* 设备数据库路径 */
} GatewayConfig;

#endif /* MIIO_PROTOCOL_H */
```

#### 3.2.2 加密层 (`miio_crypto.h` + `miio_crypto.c`)

```c
/*
 * miio_crypto.h — miIO 加解密接口
 *
 * 使用 OpenSSL 实现：
 *   - MD5: 密钥派生 + 签名
 *   - AES-128-CBC: 载荷加解密
 */

#ifndef MIIO_CRYPTO_H
#define MIIO_CRYPTO_H

#include "miio_protocol.h"
#include <openssl/md5.h>
#include <openssl/aes.h>

/* 派生密钥三元组 */
typedef struct {
    unsigned char aes_key[16];     /* MD5(token) */
    unsigned char aes_iv[16];      /* MD5(aes_key + token) */
    unsigned char sign_key[16];    /* MD5(aes_key + iv + aes_key) */
} MiioKeys;

/**
 * 从 token hex 字符串派生密钥
 * @return 0 成功, -1 失败
 */
int miio_derive_keys(const char *token_hex, MiioKeys *keys);

/**
 * AES-128-CBC 加密
 * @param plaintext 明文及长度
 * @param keys      派生的密钥
 * @param out       输出缓冲区（需至少 plaintext_len + 16 字节）
 * @param out_len   输出长度
 * @return 0 成功
 */
int miio_encrypt(const uint8_t *plaintext, size_t plaintext_len,
                 const MiioKeys *keys,
                 uint8_t *out, size_t *out_len);

/**
 * AES-128-CBC 解密
 * @return 0 成功
 */
int miio_decrypt(const uint8_t *ciphertext, size_t ciphertext_len,
                 const MiioKeys *keys,
                 uint8_t *out, size_t *out_len);

/**
 * 计算 miIO 签名
 * @param timestamp 时间戳（4字节小端）
 * @param nonce     16字节随机数
 * @param keys      派生密钥
 * @param out_sign  输出 32 字节签名
 */
void miio_sign(uint32_t timestamp, const uint8_t nonce[16],
               const MiioKeys *keys, uint8_t out_sign[32]);

#endif /* MIIO_CRYPTO_H */
```

```c
/*
 * miio_crypto.c — 基于 OpenSSL 的加解密实现
 */

#include "miio_crypto.h"
#include <string.h>
#include <stdio.h>

int miio_derive_keys(const char *token_hex, MiioKeys *keys)
{
    if (!token_hex || strlen(token_hex) != TOKEN_HEX_LEN || !keys)
        return -1;

    /* 将 hex token 转为 bytes */
    unsigned char token_bytes[16];
    for (int i = 0; i < 16; i++) {
        unsigned int val;
        sscanf(token_hex + i*2, "%02x", &val);
        token_bytes[i] = (unsigned char)val;
    }

    /* aes_key = MD5(token) */
    MD5(token_bytes, 16, keys->aes_key);

    /* aes_iv = MD5(aes_key + token) */
    {
        unsigned char buf[32];
        memcpy(buf, keys->aes_key, 16);
        memcpy(buf + 16, token_bytes, 16);
        MD5(buf, 32, keys->aes_iv);
    }

    /* sign_key = MD5(aes_key + iv + aes_key) */
    {
        unsigned char buf[48];
        memcpy(buf, keys->aes_key, 16);
        memcpy(buf + 16, keys->aes_iv, 16);
        memcpy(buf + 32, keys->aes_key, 16);
        MD5(buf, 48, keys->sign_key);
    }

    return 0;
}

int miio_encrypt(const uint8_t *pt, size_t pt_len,
                 const MiioKeys *keys,
                 uint8_t *out, size_t *out_len)
{
    if (!pt || pt_len == 0 || !keys || !out || !out_len) return -1;

    AES_KEY aes_key;
    AES_set_encrypt_key(keys->aes_key, 128, &aes_key);

    /* PKCS7 padding */
    size_t padded_len = ((pt_len / 16) + 1) * 16;
    uint8_t *padded = calloc(1, padded_len);
    memcpy(padded, pt, pt_len);
    padded[padded_len - 1] = (uint8_t)(16 - (pt_len % 16));

    AES_cbc_encrypt(padded, out, padded_len, &aes_key, keys->aes_iv, AES_ENCRYPT);
    *out_len = padded_len;

    free(padded);
    return 0;
}

int miio_decrypt(const uint8_t *ct, size_t ct_len,
                 const MiioKeys *keys,
                 uint8_t *out, size_t *out_len)
{
    if (!ct || ct_len == 0 || ct_len % 16 != 0 || !keys || !out) return -1;

    AES_KEY aes_key;
    AES_set_decrypt_key(keys->aes_key, 128, &aes_key);

    uint8_t iv_copy[16];
    memcpy(iv_copy, keys->aes_iv, 16);  /* AES_cbc_encrypt 会修改 iv */

    AES_cbc_encrypt(ct, out, ct_len, &aes_key, iv_copy, AES_DECRYPT);

    /* 去除 PKCS7 padding */
    uint8_t pad_val = out[ct_len - 1];
    if (pad_val > 0 && pad_val <= 16) {
        *out_len = ct_len - pad_val;
    } else {
        *out_len = ct_len;
    }

    return 0;
}

void miio_sign(uint32_t ts, const uint8_t nonce[16],
               const MiioKeys *keys, uint8_t sign_out[32])
{
    unsigned char buf[20];  /* 4(ts) + 16(nonce) */
    memcpy(buf, &ts, 4);
    memcpy(buf + 4, nonce, 16);
    MD5((unsigned char *)buf, 20, sign_out);
}
```

#### 3.2.3 设备发现 (`discovery.h` + `discovery.c`)

```c
/*
 * discovery.h — UDP 广播发现 miIO 设备
 */

#ifndef DISCOVERY_H
#define DISCOVERY_H

#include "miio_protocol.h"

/**
 * 发送 Hello 广播，收集在线设备
 * @param results   设备数组输出
 * @param max_count 数组容量
 * @param timeout_s 超时秒数
 * @return 实际发现的设备数（>= 0），错误返回负数
 */
int miio_discover(MiioDevice *results, int max_count, int timeout_s);

/**
 * 单台设备握手 + 获取基本信息
 * @param ip    目标 IP
 * @param token 设备 token（可为空，仅做 hello 握手）
 * @param info  输出设备信息
 * @return 0 成功
 */
int miio_handshake(const char *ip, const char *token, MiioDevice *info);

#endif
```

```c
/*
 * discovery.c — UDP 发现实现
 *
 * 核心流程：
 *   1. 向 224.0.0.50:54321 发送 Hello 报文
 *   2. 收集所有响应，解析设备 ID 和 model
 *   3. 返回设备列表
 */

#include "discovery.h"
#include "miio_crypto.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

/* Hello 报文模板（明文，无 token） */
static const uint8_t HELLO_PACKET[] = {
    0x21, 0x31,                           /* magic */
    0x00, 0x20,                           /* length = 32 */
    0xFF, 0xFF, 0xFF, 0xFF,               /* device_id = broadcast */
    0x00, 0x00, 0x00, 0x00,               /* timestamp = 0 */
    /* 16 bytes nonce (all 0xFF) */
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /* 32 bytes signature (all 0x00) */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

int miio_discover(MiioDevice *results, int max_count, int timeout_s)
{
    if (!results || max_count <= 0) return -1;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return -1;

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));

    struct timeval tv = {.tv_sec = timeout_s, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* 加入组播组 */
    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr(MIIO_MULTICAST);
    mreq.imr_interface.s_addr = INADDR_ANY;
    setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(MIIO_PORT);
    dest.sin_addr.s_addr = inet_addr(MIIO_MULTICAST);

    sendto(sock, HELLO_PACKET, sizeof(HELLO_PACKET), 0,
           (struct sockaddr *)&dest, sizeof(dest));

    int count = 0;
    uint8_t buf[4096];

    while (count < max_count) {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                              (struct sockaddr *)&from, &fromlen);
        if (n < (ssize_t)MIIO_HEADER_SIZE) break;

        /* 解析头部 */
        MiioHeader hdr;
        memcpy(&hdr, buf, MIIO_HEADER_SIZE);

        if (hdr.magic != MIIO_MAGIC) continue;

        MiioDevice *d = &results[count];
        strncpy(d->ip, inet_ntoa(from.sin_addr), MAX_IP_STRLEN - 1);
        d->port = MIIO_PORT;
        d->device_id = hdr.device_id;
        d->last_seen = hdr.timestamp;
        d->online = 1;
        d->token[0] = '\0';
        strcpy(d->model, "unknown");

        /* 尝试解析载荷中的 model 信息 */
        if (n > MIIO_HEADER_SIZE) {
            /* 注意：Hello 响应可能是加密的，
             * 这里仅做简单处理，详细解析需 token */
            (void)0;
        }

        count++;
    }

    close(sock);
    return count;
}
```

#### 3.2.4 命令层 (`command.h` + `command.c`)

```c
/*
 * command.h — JSON-RPC 命令构建与发送
 */

#ifndef COMMAND_H
#define COMMAND_H

#include "miio_protocol.h"
#include "miio_crypto.h"

/**
 * 发送 miIO 命令到指定设备
 *
 * @param ip       设备 IP
 * @param port     设备端口（通常 54321）
 * @param token    设备 token
 * @param method   RPC 方法名
 * @param params   JSON 格式的参数字符串
 * @param req_id   请求 ID（用于匹配响应）
 * @param response 输出响应
 * @param timeout  超时秒数
 * @return 0 成功，负数失败
 */
int miio_send_command(const char *ip, uint16_t port,
                      const char *token,
                      const char *method, const char *params,
                      int req_id,
                      MiioResponse *response,
                      int timeout);

/** 常用快捷方法 */
int miio_get_info(const char *ip, const char *token, char *json_out, size_t len);
int miio_get_prop(const char *ip, const char *token,
                  const char *props[], int count,
                  char *json_out, size_t len);
int miio_set_power(const char *ip, const char *token,
                   const char *state);  /* "on" / "off" */

#endif
```

### 3.3 Makefile（交叉编译）

```makefile
# Makefile — Xiaomi Gateway (aarch64 / ARM Rockchip)
#
# 编译目标平台:
#   make              — 本机 x86_64 测试
#   make ARCH=arm     — ARM 32位 (RK3288/RK3399)
#   make ARCH=aarch64 — ARM 64位 (RK3588/RK3576/RK3568)

CC       ?= gcc
CFLAGS   := -Wall -Wextra -std=c11 -D_GNU_SOURCE
LDFLAGS  := -lssl -lcrypto -lpthread

# 交叉编译工具链
ifeq ($(ARCH),arm)
CC  := arm-linux-gnueabihf-gcc
endif
ifeq ($(ARCH),aarch64)
CC  := aarch64-linux-gnu-gcc
endif

# 目录
SRC_DIR   := src
INC_DIR   := inc
OBJ_DIR   := build
THIRD_DIR := third_party/cJSON
TARGET    := bin/miio_gateway

# 源文件
SRCS := $(wildcard $(SRC_DIR)/*.c)
SRCS += $(THIRD_DIR)/cJSON.c

OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(filter $(SRC_DIR)/%.c,$(SRCS)))
OBJS += $(patsubst $(THIRD_DIR)/%.c,$(OBJ_DIR)/%.o,$(filter $(THIRD_DIR)/%.c,$(SRCS)))

CFLAGS += -I$(INC_DIR) -I$(THIRD_DIR)

.PHONY: all clean install run

all: info $(TARGET)
	@echo ""
	@echo "✓ 编译完成: $(TARGET)"

info:
	@echo "━━━ Xiaomi Gateway Build ━━━"
	@echo "  CC:      $(CC)"
	@echo "  ARCH:    $(ARCH:host)"
	@echo "  Sources: $(words $(OBJS)) files"
	@echo ""

$(TARGET): $(OBJS) | bin
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@echo "编译: $< → $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(THIRD_DIR)/%.c | $(OBJ_DIR)
	@echo "编译(3rd): $< → $@"
	$(CC) $(CFLAGS) -w -c $< -o $@

$(OBJ_DIR) bin:
	mkdir -p $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
	@echo "✓ 清理完成"

run: $(TARGET)
	./$(TARGET)

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/miio_gateway
	@echo "✓ 已安装到 /usr/local/bin/"
```

### 3.4 部署到 Rockchip 板子

```bash
# 1. 交叉编译
make ARCH=aarch64 clean && make ARCH=aarch64

# 2. 推送到板子
adb push bin/miio_gateway /data/local/tmp/
adb shell chmod +x /data/local/tmp/miio_gateway

# 3. 在板子上运行
adb shell "/data/local/tmp/miio_gateway --config /data/local/tmp/gateway.conf"

# 或者作为 systemd 服务持久化
cat > /tmp/miio-gateway.service << 'EOF'
[Unit]
Description=Xiaomi MIoT Gateway Service
After=network.target

[Service]
Type=simple
ExecStart=/usr/local/bin/miio_gateway --config /etc/miio/gateway.conf
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF
```

---

## 4. 方案三：Home Assistant 统一管理

> 目标：通过 Home Assistant 统一管理所有米家设备，支持自动化和可视化面板。

### 4.1 安装 Home Assistant

#### 方式 A：Docker（推荐）

```bash
# 创建目录
mkdir -p ~/ha/config && cd ~/ha

# 启动容器
docker run -d \
  --name homeassistant \
  --privileged \
  --restart unless-stopped \
  -e TZ=Asia/Shanghai \
  -v ./config:/config \
  -p 8123:8123 \
  homeassistant/home-assistant:stable

# 查看日志
docker logs -f homeassistant
```

#### 方式 B：HA OS（专用系统）

```bash
# 下载镜像（适用于树莓派/N1/RK 板子）
# https://www.home-assistant.io/os/

# 写入 SD 卡 / eMMC
xz -dc ha-rpi4-64.img.xz | sudo dd of=/dev/sdX bs=4M status=progress
```

### 4.2 集成 Xiaomi Miot Auto

这是目前最强大的米家 HA 集成插件，支持自动发现和自定义映射。

```yaml
# configuration.yaml（HA 主配置）

# 方法1：HACS 安装（推荐）
# HA → HACS → 右上角三个点 → 自定义存储库
# 搜索 "Xiaomi Miot Auto" → 下载
# 重启 HA → 设置 → 设备与服务 → 添加集成 → 搜索 Xiaomi Miot Auto

# 方法2：手动安装
# 将插件放到 /config/custom_components/xiaomi_miot_auto/
# 然后在 HA 中添加集成

xiaomi_miot_auto:
  # 登录方式（选一种）
  login_method: xiaomi_cloud  # 小米云账号登录
  # username: your_xiaomi_account
  # password: your_password
  
  # 过滤设备（可选）
  filter_devices:
    - model: "aircondition.*"    # 只添加空调
    - model: "yeelink.light.*"  # 和灯泡
    
  # 自定义属性映射（高级用法）
  customizing:
    - entity: climate.ac_living_room
      attributes:
        temperature_unit: C
```

### 4.3 自动化示例

```yaml
# automations.yaml — 常用自动化规则

# 示例1：回家自动开灯
- alias: 回家开灯
  trigger:
    - platform: state
      entity_id: person.lhj
      to: "home"
  action:
    - service: light.turn_on
      target:
        entity_id: light.yeelink_main_light
      data:
        brightness_pct: 80
        color_temp: 4000

# 示例2：温度超过28度自动开空调
- alias: 高温自动开空调
  trigger:
    - platform: numeric_state
      entity_id: sensor.temperature_living_room
      above: 28
  condition:
    - condition: state
      entity_id: person.lhj
      state: "home"
  action:
    - service: climate.turn_on
      target:
        entity_id: climate.ac_living_room
    - service: climate.set_temperature
      target:
        entity_id: climate.ac_living_room
      data:
        temperature: 24
        hvac_mode: cool

# 示例3：出门全关
- alias: 出门全关
  trigger:
    - platform: state
      entity_id: person.lhj
      to: "not_home"
      for:
        minutes: 5
  action:
    - service: light.turn_off
      target:
        entity_id: all
    - service: switch.turn_off
      target:
        entity_id: switch.plug_tv

# 示例4：定时任务 — 每天 23:00 关所有灯
- alias: 晚间熄灯
  trigger:
    - platform: time
      at: "23:00:00"
  action:
    - service: light.turn_off
      target:
        entity_id: all
```

### 4.4 Dashboard 面板配置

```yaml
# dashboards/ui_mijia.yaml — 米家控制面板

views:
  - title: 米家总览
    cards:
      # 温湿度卡片
      - type: sensor
        entity: sensor.temperature_living_room
        name: 客厅温度
        graph: line
        detail: 2

      # 空调控制卡片
      - type: thermostat
        entity: climate.ac_living_room
        name: 客厅空调

      # 灯光控制
      - type: entities
        title: 灯光
        entities:
          - entity: light.yeelink_main_light
            name: 主灯
          - entity: light.yeelink_bedroom
            name: 卧室灯
          - entity: light.desk_lamp
            name: 台灯

      # 开关状态
      - type: entities
        title: 插座开关
        entities:
          - switch.plug_tv
          - switch.plug_computer
          - switch.plug_heater
```

### 4.5 HA 与自建网关联动

```yaml
# 让 HA 调用我们自己的 REST API（方案二的网关）
rest:
  - resource: http://localhost:8888/api/devices
    scan_interval: 30
    sensor:
      - name: "MiIO Devices Online"
        value_template: "{{ value_json.online_count }}"
      - name: "MiIO Total Devices"
        value_template: "{{ value_json.total }}"

  - resource: http://localhost:8888/api/device/{{ states('input_text.target_ip') }}/command
    method: POST
    payload: '{"method":"get_prop","params":["power","temperature"]}'
```

---

## 5. 方案四：Python 自动化脚本

> 目标：写 Python 脚本定时/按条件控制米家设备，适合 cron 任务和简单自动化。

### 5.1 基础环境

```bash
pip install python-miio schedule requests

# 验证安装
python -c "from miio import Device; print('OK')"
```

### 5.2 设备扫描脚本

```python
#!/usr/bin/env python3
"""
miio_scan.py — 扫描局域网内所有米家设备
用法:
  python miio_scan.py                    # 快速扫描
  python miio_scan.py --deep             # 深度扫描（含模型识别）
  python miio_scan.py --save devices.json # 保存结果
"""

import asyncio
import json
import argparse
from datetime import datetime
from miio import Device, DeviceException

async def scan_network():
    """使用 python-miio 发现设备"""
    from miio.discovery import Discovery

    results = []

    def on_device(addr, device_type, info):
        results.append({
            "ip": addr[0],
            "port": addr[1],
            "type": device_type,
            "info": info.__dict__ if info else {},
            "discovered_at": datetime.now().isoformat(),
        })

    discovery = Discovery(on_device, loop=None)
    await discovery.discover()

    return results

async def deep_scan(ip: str, token: str = None):
    """深度扫描单台设备（需要 token）"""
    if not token:
        return {"error": "Token required for deep scan"}

    try:
        dev = Device(ip, token)
        info = await dev.info()
        props = await dev.get_properties(["power", "temperature", "mode"], max_properties=10)
        return {
            "info": info.serialize(),
            "properties": props,
        }
    except DeviceException as e:
        return {"error": str(e)}

async def main():
    parser = argparse.ArgumentParser(description="米家设备扫描工具")
    parser.add_argument("--deep", action="store_true", help="深度扫描")
    parser.add_argument("--save", help="保存结果到文件")
    parser.add_argument("--token", help="设备 Token（深度扫描用）")
    args = parser.parse_args()

    print(f"[{datetime.now().strftime('%H:%M:%S')}] 开始扫描...")
    devices = await scan_network()

    if not devices:
        print("❌ 未发现设备")
        return

    print(f"\n✓ 发现 {len(devices)} 个设备:\n")
    print(f"{'IP':<16} {'Type':<15} {'Model':<30}")
    print("-" * 65)

    for d in devices:
        ip = d["ip"]
        dtype = d.get("type", "unknown")
        model = d.get("info", {}).get("model", "unknown")
        print(f"{ip:<16} {dtype:<15} {model:<30}")

    if args.deep and args.token:
        print("\n--- 深度扫描 ---")
        for d in devices:
            result = await deep_scan(d["ip"], args.token)
            print(f"\n{d['ip']}:")
            print(json.dumps(result, indent=2, ensure_ascii=False))

    if args.save:
        with open(args.save, "w") as f:
            json.dump(devices, f, indent=2, ensure_ascii=False)
        print(f"\n💾 结果已保存到 {args.save}")

if __name__ == "__main__":
    asyncio.run(main())
```

### 5.3 定时控制脚本

```python
#!/usr/bin/env python3
"""
miio_scheduler.py — 米家设备定时控制
支持:
  - Cron 式定时任务
  - 条件触发（温度/时间/状态）
  - 多设备联动
  - 日志记录
"""

import asyncio
import json
import logging
import signal
import sys
from datetime import datetime, time as dtime
from pathlib import Path

from miio import Device, DeviceException
from miio.airconditioningmiot import AirConditioningMiot
from miio.chuangmiplug import ChuangmiPlug
from miio.yeelink.light import Yeelight

# ═══ 日志 ═══
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    handlers=[
        logging.StreamHandler(),
        logging.FileHandler("/var/log/miio_scheduler.log"),
    ]
)
log = logging.getLogger("miio_sched")

# ═══ 配置 ═══
CONFIG_PATH = Path(__file__).parent / "devices.json"

# ═══ 设备管理器 ═══

class DeviceManager:
    """统一管理所有米家设备"""

    def __init__(self, config_path: Path):
        self.devices = {}
        self.load_config(config_path)

    def load_config(self, path: Path):
        """加载设备配置文件"""
        if not path.exists():
            log.warning(f"配置文件不存在: {path}")
            log.info("请创建 devices.json，格式见 README")
            return

        with open(path) as f:
            config = json.load(f)

        for name, cfg in config.items():
            self.devices[name] = {
                "ip": cfg["ip"],
                "token": cfg["token"],
                "model": cfg.get("model", ""),
                "type": cfg.get("type", "generic"),
                "instance": None,
            }
        log.info(f"已加载 {len(self.devices)} 个设备")

    async def get_device(self, name: str) -> Device:
        """获取或创建设备实例"""
        if name not in self.devices:
            raise ValueError(f"未知设备: {name}")

        d = self.devices[name]
        if d["instance"] is None:
            d["instance"] = Device(d["ip"], d["token"])
        return d["instance"]

    async def execute(self, device_name: str, action: str, **kwargs):
        """执行设备动作"""
        dev = await self.get_device(device_name)
        try:
            if action == "power_on":
                await dev.on()
            elif action == "power_off":
                await dev.off()
            elif action == "toggle":
                status = await dev.status()
                if getattr(status, 'power', False) or status.is_on:
                    await dev.off()
                else:
                    await dev.on()
            elif action == "set_temp":
                ac = AirConditioningMiot(
                    self.devices[device_name]["ip"],
                    self.devices[device_name]["token"]
                )
                await ac.set_temperature(kwargs["temp"])
                await ac.on()
            elif action == "set_brightness":
                light = Yeelight(
                    self.devices[device_name]["ip"],
                    self.devices[device_name]["token"]
                )
                await light.set_brightness(kwargs["brightness"])
            else:
                # 通用属性设置
                await dev.send(action, kwargs.get("params", []))

            log.info(f"✓ {device_name}: {action} {kwargs}")
            return True

        except DeviceException as e:
            log.error(f"✗ {device_name}: {action} 失败 — {e}")
            return False

# ═══ 任务调度器 ═══

class Scheduler:
    """基于时间的任务调度"""

    def __init__(self, manager: DeviceManager):
        self.manager = manager
        self.tasks = []

    def add_task(self, name: str, trigger: str,
                 device: str, action: str, **kwargs):
        """添加定时任务
        
        trigger 格式:
          - "cron:0 8 * * *"     — 每天 8:00
          - "interval:3600"      — 每 3600 秒
          - "once:2026-05-20T09:00" — 一次性
        """
        self.tasks.append({
            "name": name,
            "trigger": trigger,
            "device": device,
            "action": action,
            "kwargs": kwargs,
            "last_run": None,
        })
        log.info(f"已注册任务: {name} ({trigger})")

    async def run_forever(self):
        """持续运行调度循环"""
        log.info("调度器启动，按 Ctrl+C 停止")

        while True:
            now = datetime.now()

            for task in self.tasks:
                if self._should_run(task, now):
                    log.info(f"▶ 执行任务: {task['name']}")
                    await self.manager.execute(
                        task["device"],
                        task["action"],
                        **task.get("kwargs", {})
                    )
                    task["last_run"] = now

            await asyncio.sleep(30)  # 每 30 秒检查一次

    def _should_run(self, task: dict, now: datetime) -> bool:
        """判断任务是否应该触发"""
        trigger = task["trigger"]

        if trigger.startswith("cron:"):
            # 简化 cron 匹配（实际可用 croniter 库）
            expr = trigger[5:]
            h, m = expr.split()[1:3]
            return now.hour == int(h) and now.minute == int(m)

        elif trigger.startswith("interval:"):
            seconds = int(trigger.split(":")[1])
            if task["last_run"] is None:
                return True
            elapsed = (now - task["last_run"]).total_seconds()
            return elapsed >= seconds

        return False

# ═══ 预设场景 ═══

class Scenes:
    """常用自动化场景"""

    def __init__(self, manager: DeviceManager, scheduler: Scheduler):
        self.mgr = manager
        self.sched = scheduler

    def setup_morning(self):
        """早晨场景：7:30 开灯 + 打开插座"""
        self.sched.add_task(
            "morning_lights",
            "cron:0 7 * * *",
            "main_light", "power_on"
        )
        self.sched.add_task(
            "morning_plug",
            "cron:0 7 * * *",
            "desk_plug", "power_on"
        )

    def setup_night(self):
        """夜间场景：23:00 全关"""
        self.sched.add_task(
            "night_all_off",
            "cron:0 23 * * *",
            "main_light", "power_off"
        )
        self.sched.add_task(
            "night_ac_off",
            "cron:0 23 * * *",
            "ac_living", "power_off"
        )

    def setup_smart_ac(self, temp_threshold: float = 28.0):
        """智能空调：温度超阈值自动开启"""
        # 这个需要温湿度传感器配合，这里做框架预留
        pass

# ═══ 主入口 ═══

async def main():
    manager = DeviceManager(CONFIG_PATH)
    scheduler = Scheduler(manager)
    scenes = Scenes(manager, scheduler)

    # 注册预设场景
    scenes.setup_morning()
    scenes.setup_night()

    # 优雅退出
    stop_event = asyncio.Event()

    def signal_handler(sig, frame):
        log.info("收到退出信号，正在停止...")
        stop_event.set()

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    # 运行调度器
    await scheduler.run_forever()
    log.info("调度器已停止")

if __name__ == "__main__":
    asyncio.run(main())
```

### 5.4 设备配置文件模板

```json
// devices.json — 设备配置（填入你的真实 IP 和 Token）
{
  "living_room_light": {
    "ip": "192.168.1.101",
    "token": "abcdef1234567890abcdef1234567890",
    "model": "yeelink.light.color3",
    "type": "light"
  },
  "bedroom_ac": {
    "ip": "192.168.1.102",
    "token": "fedcba0987654321fedcba0987654321",
    "model": "zhimi.aircondition.va1",
    "type": "ac"
  },
  "desk_plug": {
    "ip": "192.168.1.103",
    "token": "0123456789abcdef0123456789abcdef",
    "model": "chuangmi.plug.v1",
    "type": "plug"
  },
  "ir_remote": {
    "ip": "192.168.1.104",
    "token": "cafebabedeadbeefcafebabedeadbeef",
    "model": "chuangmi.ir.v2",
    "type": "ir_remote"
  },
  "air_purifier": {
    "ip": "192.168.1.105",
    "token": "1234abcd5678efgh1234abcd5678efgh",
    "model": "zhimi.airpurifier.m6",
    "type": "purifier"
  }
}
```

### 5.5 Systemd 服务（开机自启）

```ini
# /etc/systemd/system/miio-scheduler.service
[Unit]
Description=Xiaomi Mijio Scheduler
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=root
WorkingDirectory=/opt/miio_scheduler
ExecStart=/usr/bin/python3 /opt/miio_scheduler/miio_scheduler.py
Restart=always
RestartSec=10
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

```bash
# 安装服务
sudo cp miio-scheduler.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable miio-scheduler
sudo systemctl start miio-scheduler

# 查看日志
sudo journalctl -u miio-scheduler -f
```

---

## 6. 实战案例：常见设备控制命令

### 6.1 空调（Air Conditioner）

```python
# python-miio 方式
from miio.airconditioningmiot import AirConditioningMiot

ac = AirConditioningMiot("192.168.1.102", "your_token")

# 查询状态
status = await ac.status()  # 温度、模式、风速等

# 控制
await ac.on()                                    # 开机
await ac.off()                                   # 关机
await ac.set_temperature(24)                     # 设定 24°C
await ac.set_mode(AirConditioningMiot.Mode.Cool)  # 制冷
await ac.set_mode(AirConditioningMiot.Mode.Heat)  # 制热
await ac.set_mode(AirConditioningMiot.Mode.Auto)  # 自动
await ac.set_fan_level(AirConditioningMiot.FanLevel.Level4)  # 风速4档

# C 语言方式（原始 miIO 命令）
// 开机
send_command(ip, token, "set_power", ["on"])
// 设定制冷 26°C
send_command(ip, token, "set_power", ["on"])
send_command(ip, token, "set_temperature", ["26"])
send_command(ip, token, "set_mode", ["cool"])
// 查询当前状态
send_command(ip, token, "get_prop", ["power","temperature","mode"])
```

### 6.2 智能灯泡（Yeelight / Mijia Light）

```python
from miio.yeelink.light import Yeelight

light = Yeelight("192.168.1.101", "your_token")

await light.on()                          # 开灯
await light.off()                         # 关灯
await light.set_brightness(80)            # 亮度 80%
await light.set_rgb(255, 0, 0)           # 红色
await light.set_color_temp(4000)          # 色温 4000K（暖白→冷白）
await light.set_brightness(30, transition=2000)  # 2秒渐变到30%

# C 语言方式
send_command(ip, token, "set_power", ["on"])
send_command(ip, token, "set_bright", ["80"])
send_command(ip, token, "set_ct_abx", ["4000", "smooth", "500"])
```

### 6.3 万能红外遥控器（Chuangmi IR Remote）

```python
from miio.chuangmi_ir import ChuangmiIrV2, ChuangmiIrRemote

ir = ChuangmiIrV2("192.168.1.104", "your_token")

# 学习红外信号（对准遥控器按下按键）
learned = await ir.learn()  # 返回 base64 编码的红外码

# 发射红外信号
await ir.play(learned)  # 发射之前学习的信号

# 常用家电红外码型
# 空调（格力/美的/海尔）— 需要自行学习或导入码库
# 电视（TCL/Sony/LG）— 同上
# 机顶盒 — 同上

# C 语言方式
// 学习
send_command(ip, token, "learn", [])
// 发射（base64 编码的红外码）
send_command(ip, token, "play", ["base64_encoded_ir_code"])
```

### 6.4 扫地机器人（Roborock / Dreame / Viomi）

```python
from miio.vacuum import Vacuum

vac = Vacuum("192.168.1.106", "your_token")

# 基础操作
await vac.start()           # 开始清扫
await vac.pause()           # 暂停
await vac.home()            # 回充
await vac.stop()            # 停止
await vac.spot()            # 局部清扫
await vac.find()            # 寻找机器人

# 高级操作
status = await vac.status()  # 电量、面积、耗时等
await vac.fan_speed_presets()  # 支持的风力档位
await vac.fan_speed("quiet")   # 安静模式
await vac.fan_speed("turbo")   # 强力模式

# C 语言方式
send_command(ip, token, "method", ["start"])
send_command(ip, token, "method", ["charge"])
send_command(ip, token, "get_prop", ["battery_level", "clean_area", "clean_time"])
```

### 6.5 空气净化器（Zhimi Air Purifier）

```python
from miio.airpurifier import AirPurifier

ap = AirPurifier("192.168.1.105", "your_token")

await ap.on()
await ap.off()
await ap.favorite_level(2)        # 风量等级 0-13
await ap.set_led(brightness=True) # LED 亮度
await ap.set_led_brightness(0)    # LED 关闭
await ap.set_buzzer_on()          // 蜂鸣器开

# 查询 AQI
status = await ap.status()
print(f"AQI: {status.aqi}")  # PM2.5 数值
print(f"Humidity: {status.humidity}%")
print(f"Filter life: {status.filter_hours_remaining}h")
```

### 6.6 智能插座（Chuangmi Plug / QCY Plug）

```python
from miio.chuangmiplug import ChuangmiPlug

plug = ChuangmiPlug("192.168.1.103", "your_token")

await plug.on()    # 通电
await plug.off()   # 断电
await plug.usb_on()  # USB 口通电（如果支持）

# 查看电量统计
status = await plug.status()
print(f"Power: {status.power}W")
print(f"Voltage: {status.voltage}V")
print(f"Today kWh: {status.electricity_today}kWh")
```

---

## 7. 附录：Token 获取方法

### 方法1：Android 手机备份（最可靠）

```bash
# 1. 手机开启 USB 调试，连接电脑
adb backup -noapk com.xiaomi.smarthome

# 2. 会生成 backup.ab 文件，解压
# 安装 android-backup-extractor
java -jar abe.jar unpack backup.ab backup.tar

# 3. 解压 tar
tar xf backup.tar

# 4. 找到 miio2.db（SQLite 数据库）
#    路径通常是: apps/com.xiaomi.smart_home/db/miio2.db

# 5. 查询 token
sqlite3 miio2.db "SELECT name, token FROM DeviceInfo;"
# 或
sqlite3 miio2.db "SELECT ZTOKEN FROM ZDEVICE;"
```

### 方法2：Root 手机直接读取

```bash
# 直接访问数据库
adb shell "su -c 'cat /data/data/com.xiaomi.smarthome/databases/miio2_db'" \
  > miio2.db
sqlite3 miio2.db "SELECT name, token FROM DeviceInfo;"
```

### 方法3：miio-token-extractor 工具

```bash
# GitHub 项目
git clone https://github.com/PiotrMachowski/xiaomi-tokens.git
cd xiaomi-tokens

# Windows 用户可以用 GUI 版本
# Linux/Mac 用 Python 版
pip install protobuf
python3 extract_tokens.py -i backup.ab -o tokens.txt
```

### 方法4：设备重置 + 抓包

```bash
# 1. 重置设备（长按重置键 5秒）
# 2. 用米家 App 重新配网
# 3. 同时用 tcpdump 抓包
sudo tcpdump -i wlan0 host 192.168.1.xxx and udp port 54321 -w token.pcap

# 4. 在 Wireshark 中查看 Hello 响应包
#    Token 有时会出现在未加密的响应中
```

### 方法5：利用 python-miio 自动获取

```bash
# 部分旧固件设备可以通过 cloud token 获取
miiocli extract-tokens --username your_xiaomi_account --password your_password
# 或
miiocli device --cloud-ip your_device_ip --cloud-token your_cloud_token
```

---

## 附录 B：常用设备型号速查表

| 型号 | 品牌 | 类型 | 特殊功能 |
|------|------|------|----------|
| `yeelink.light.color3` | Yeelight | 彩光灯 | RGB + 色温 + 渐变 |
| `yeelink.light.ceiling4` | Yeelight | 吸顶灯 | 双色温 + 月光模式 |
| `yeelink.light.strip1` | Yeelight | 灯带 | RGB + 音乐律动 |
| `chuangmi.plug.v1/v3` | 创米 | 插座 | 电量计量 + USB |
| `chuangmi.ir.v2` | 创米 | 红外遥控 | 全向 + 学习 |
| `zhimi.airpurifier.m6` | 智米 | 净化器 | PM2.5 显示屏 |
| `zhimi.aircondition.va1` | 智米 | 空调（变频） | MIoT 协议 |
| `roborock.vacuum.s5` | 石头 | 扫地机 | 激光导航 + 电控水箱 |
| `roborock.vacuum.a08` | 石头 | 扫地机 S8 | 自动集尘 |
| `dreame.vacuum.p2041` | 追觅 | 扫地机 | 视觉导航 |
| `aircondition.miot.ac04` | 米家 | 空调（新） | MIoT 协议 |
| `lumi.gateway.mgl03` | 绿米 | Zigbee 网关 | 多模网关 |

---

## 附录 C：网络排查指南

| 问题 | 可能原因 | 解决方法 |
|------|----------|----------|
| 设备发现不到 | AP 隔离 / 不同 VLAN | 路由器关闭 AP 隔离 |
| Token 错误 | 设备被重置过 | 重新配网获取新 token |
| 命令超时 | 设备休眠 / 网络延迟 | 增加 timeout 到 15-30s |
| 加密失败 | Token 格式错误 | 确认是 32 位纯十六进制 |
| 权限拒绝 | 设备绑定其他账号 | 先在 App 中解绑或共享设备 |
| HA 集成找不到设备 | 云端同步延迟 | 等待 5-10 分钟或重启 HA |

---

## 总结：四方案选择指南

```
你的需求是什么？
│
├─ 只想快速控制几个设备
│  └─→ 方案四：Python 脚本（最快上手，pip install 即可）
│
├─ 想统一管理全家智能设备 + 自动化
│  └─→ 方案三：Home Assistant（最成熟生态，3000+ 集成）
│
├─ 想在开发板上跑一个独立服务（不依赖外部服务器）
│  └─→ 方案二：C 语言网关（适合嵌入式，可集成到 SDK）
│
├─ 想深入理解协议原理 / 做二次开发
│  └─→ 方案一：学习研究（Wireshark 抓包 + 最小 demo）
│
└─ 全都要 😄
   └─→ 按顺序 ①→②→③→④，逐步叠加
```

---

*文档版本：v1.0 | 最后更新：2026-05-19*
