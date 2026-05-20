# 米家（Xiaomi Mi Home）全接入方式指南

> 覆盖所有可用的米家设备接入方式，从官方 API 到社区开源方案，从本地协议到云端桥接。
> 适用于 SmartHome 项目集成、嵌入式网关开发、Home Assistant 对接等场景。
>
> 创建日期：2026-05-20 | 作者：lhj

---

## 目录

1. [总览：接入方式全景图](#1-总览接入方式全景图)
2. [方式一：miIO 协议本地控制（你已实现的方案）](#2-方式一miio-协议本地控制)
3. [方式二：miOT 协议本地控制（新设备）](#3-方式二miot-协议本地控制)
4. [方式三：python-miio 库（最成熟的 Python 方案）](#4-方式三python-miio-库)
5. [方式四：小米云端 API — micloud](#5-方式四小米云端-api--micloud)
6. [方式五：小米官方 IoT 开放平台](#6-方式五小米官方-iot-开放平台)
7. [方式六：MQTT 桥接方案](#7-方式六mqtt-桥接方案)
8. [方式七：Zigbee2MQTT — Zigbee 子设备脱离小米生态](#8-方式七zigbee2mqtt)
9. [方式八：BLE 蓝牙设备被动监听](#9-方式八ble-蓝牙设备被动监听)
10. [方式九：Home Assistant 集成（最全面）](#10-方式九home-assistant-集成)
11. [方式十：Node-RED 可视化流程编排](#11-方式十node-red-可视化编排)
12. [方式十一：米家 App 智能场景 + HTTP 回调](#12-方式十一米家-app-智能场景)
13. [Token 获取方法汇总](#13-token-获取方法)
14. [方案对比与选择指南](#14-方案对比与选择指南)

---

## 1. 总览：接入方式全景图

```
┌─────────────────────────────────────────────────────────────────────┐
│                        你的 SmartHome 系统                          │
│                    (C 网关 / Python 后端 / HA)                      │
├──────────┬──────────┬──────────┬──────────┬──────────┬──────────────┤
│  miIO    │  miOT    │ python-  │  Cloud   │   MQTT   │   BLE        │
│  本地协议│  本地协议│  miio库  │  云端API │   桥接   │   监听       │
│ (UDP)    │ (COAP)   │ (Python) │ (HTTP)   │ (MQTT)   │ (蓝牙)       │
├──────────┴──────────┴──────────┴──────────┴──────────┴──────────────┤
│                     通信协议层                                      │
├─────────────────────────────────────────────────────────────────────┤
│                     设备层                                          │
│  WiFi 设备  │  BLE 设备  │  Zigbee 设备  │  Mesh 灯具  │  蓝牙Mesh  │
│  (插座/灯/  │ (温湿度/   │ (Aqara 传感   │ (Yeelight   │ (米家灯具  │
│   空调/扫   │  门磁/人   │  器/开关)     │  系列)      │  系列)     │
│   地机)     │  体传感)   │               │             │            │
└─────────────────────────────────────────────────────────────────────┘
```

### 各方式一句话总结

| #   | 方式           | 原理                         | 适用设备       | 本地化     | 难度   |
|-----|----------------|------------------------------|----------------|------------|--------|
| 1   | miIO 协议      | UDP 54321 + AES 加密         | 早期 WiFi 设备 | 完全本地   | 中     |
| 2   | miOT 协议      | COAP/TCP + 标准化属性        | 2019+ 新设备   | 完全本地   | 高     |
| 3   | python-miio    | 封装 miIO/miOT 的 Python 库  | 200+ 设备型号  | 完全本地   | 低     |
| 4   | micloud        | 逆向小米云端 API             | 账号下所有设备 | 需联网     | 中     |
| 5   | 官方 IoT 平台  | OAuth2 + RESTful API         | 已开放设备     | 需联网     | 中     |
| 6   | MQTT 桥接      | miIO/miOT → MQTT 中转        | 取决于底层     | 完全本地   | 中     |
| 7   | Zigbee2MQTT    | 替代小米网关的 Zigbee 协调器 | Zigbee 子设备  | 完全本地   | 低     |
| 8   | BLE 监听       | 被动接收蓝牙广播             | BLE 传感器     | 完全本地   | 中     |
| 9   | Home Assistant | 综合集成平台                 | 3000+ 设备     | 部分本地   | 低     |
| 10  | Node-RED       | 可视化流程 + 小米节点        | 多种           | 取决于节点 | 低     |
| 11  | 米家 App 场景  | App 内置自动化 + HTTP 回调   | App 内设备     | 需联网     | 低     |

---

## 2. 方式一：miIO 协议本地控制

### 2.1 协议原理

这是小米最早期的设备通信协议，你已经在 `XiaomiGateway/` 中实现了 C 语言版本。

```
通信流程:
  App/网关                    设备
     │                          │
     │── Hello (明文 UDP) ─────>│  广播 224.0.0.50:54321
     │<── 响应 (device_id+ts) ──│
     │                          │
     │── 加密命令 (AES-CBC) ───>│  单播 IP:54321
     │<── 加密响应 ─────────────│
```

**报文结构（60 字节头 + 变长载荷）:**
```
偏移   长度   字段          说明
0x00   2      Magic         固定 0x2131
0x02   2      Length        报文总长（大端）
0x04   4      Device ID     设备 ID（小端）
0x08   4      Timestamp     Unix 时间戳（小端）
0x0C   16     Nonce         随机数
0x1C   32     Signature     MD5 签名
0x3C   N      Payload       AES-128-CBC 加密的 JSON-RPC
```

**密钥派生:**
```
token_hex (32字符) → token_bytes (16字节)
aes_key  = MD5(token_bytes)
aes_iv   = MD5(aes_key + token_bytes)
sign_key = MD5(aes_key + aes_iv + aes_key)
```

### 2.2 你的已有实现

项目路径: `MiJia/XiaomiGateway/`（相对于项目根目录 `/home/lhj/Projects/SmartHome/`）

已实现模块（相对于 `MiJia/XiaomiGateway/`）:
- `src/miio_crypto.c` — OpenSSL MD5 + AES-128-CBC 加解密
- `src/discovery.c` — UDP 组播发现设备
- `src/command.c` — JSON-RPC 命令构建与发送
- `src/main.c` — CLI 工具（scan / info / command / gateway）
- `inc/miio_protocol.h` — 协议常量与数据结构（60 字节头定义）

### 2.3 适用设备

- 空气净化器（zhimi.airpurifier 系列）
- 扫地机器人（roborock.vacuum 系列）
- 智能风扇（zhimi.fan 系列）
- 台灯（yeelink.light 系列）
- 插座（chuangmi.plug 系列）
- 万能遥控器（chuangmi.ir 系列）

### 2.4 Demo

见: `demos/mijia/01_miio_local_demo.py`（相对于项目根目录）

---

## 3. 方式二：miOT 协议本地控制

### 3.1 协议原理

小米新一代物联网协议，2019 年后的新设备主要使用此协议。

> **注意**: miOT 的传输层与 miIO 不同。miOT 使用 COAP over UDP（端口不固定）或 TCP/TLS，
> 并非简单复用 miIO 的 UDP 54321。部分新设备完全关闭本地端口，只能通过云端控制。

**核心概念:**
- **SIID** (Service ID): 服务标识，如"电源服务" SIID=2
- **PIID** (Property ID): 属性标识，如"开关" PIID=1
- **AIID** (Action ID): 动作标识，如"重启" AIID=1
- **EIID** (Event ID): 事件标识，如"按键" EIID=1

**通信方式:**
```
方式 A: 本地 COAP/TCP (部分设备开放)
  - 设备监听端口（不固定，常见 54321/80/443）
  - 使用 token 或证书认证

方式 B: 通过小米云中转 (大部分新设备)
  - 必须通过云端 API 下发
  - 设备可能不开放本地端口
```

**属性设置请求示例:**
```json
{
  "did": "123456789",
  "siid": 2,
  "piid": 1,
  "value": true
}
```

### 3.2 miOT 规范查询

官方规范库: https://miot-spec.org/

每个设备型号的 SIID/PIID 映射都可以在规范库查到。

### 3.3 适用设备

- 2019 年后生产的 WiFi 设备
- 米家空调伴侣
- 智能窗帘电机
- 新款灯具、插座、传感器
- 部分摄像头、门锁

### 3.4 局限性

- 新设备越来越多关闭本地端口，强制走云端
- 部分设备需要证书认证，本地难以模拟
- 社区逆向不如 miIO 成熟

### 3.5 Demo

见: `demos/mijia/02_miot_local_demo.py`

---

## 4. 方式三：python-miio 库

### 4.1 简介

GitHub: https://github.com/rytilahti/python-miio (9000+ stars)

最成熟的 Python 米家设备控制库，支持 200+ 设备型号，纯本地通信。

### 4.2 安装

```bash
pip install python-miio

# 验证
python -c "from miio import Device; print('OK')"
```

### 4.3 常用命令行工具

```bash
# 发现设备
miiocli discover

# 查询设备信息
miiocli vacuum --ip 192.168.1.100 --token xxx status
miiocli airpurifier --ip 192.168.1.101 --token xxx status
miiocli chuangmiplug --ip 192.168.1.102 --token xxx status

# 从云端获取 token
miiocli cloud --username xxx --password xxx token
```

### 4.4 设备类型映射

```python
from miio import Device                    # 通用设备
from miio import Vacuum                   # 扫地机
from miio import AirPurifier              # 空气净化器
from miio.airconditioningmiot import AirConditioningMiot  # 空调
from miio.chuangmiplug import ChuangmiPlug  # 插座
from miio.chuangmi_ir import ChuangmiIrV2   # 红外遥控
from miio.yeelink.light import Yeelight     # 灯泡
from miio.fan import Fan                   # 风扇
from miio.humidifier import Humidifier     # 加湿器
```

### 4.5 Demo

见: `demos/mijia/03_python_miio_demo.py`

---

## 5. 方式四：小米云端 API — micloud

### 5.1 原理

通过逆向米家 App 的 HTTP/HTTPS 接口，模拟登录和设备控制流程。

**认证流程:**
```
1. 获取登录 nonce
   POST https://account.xiaomi.com/pass/serviceLogin

2. 用户名密码加密登录
   → 获取 serviceToken

3. 用 serviceToken 调用设备 API
   GET https://api.io.mi.com/app/home/device_list
```

**关键 API 端点:**
```
设备列表:    GET  /app/home/device_list
设备规格:    GET  /app/home/device/spec?did=xxx
设备属性:    POST /miotspec/prop/get
设置属性:    POST /miotspec/prop/set
设备 Token:  在 device_list 返回中包含
```

### 5.2 两种云端方案

**方案 A: python-miio 内置 cloud 模块（推荐）**

```bash
# 获取所有设备的 token（最方便）
miiocli cloud --username your_account --password your_password token

# 列出所有设备
miiocli cloud --username your_account --password your_password devices
```

**方案 B: 独立 micloud 包**

```bash
pip install micloud
python3 -c "
from micloud import MiCloud
mc = MiCloud('user', 'pass')
mc.login()
print([d['name'] for d in mc.get_devices()])
"
```

> **区别**: python-miio 的 cloud 模块集成了 token 获取和设备管理功能，更新更频繁。
> 独立 `micloud` 包是早期逆向项目，API 可能不稳定。建议优先使用方案 A。

### 5.3 适用场景

- 批量获取设备 token
- 控制不开放本地端口的纯云端设备
- 获取账号下完整设备列表
- 查询设备 miOT 规格定义（SIID/PIID）

### 5.4 注意事项

- 非官方接口，可能随时变化
- 有账号安全风险，建议使用小号
- 必须联网

### 5.5 Demo

见: `demos/mijia/04_micloud_demo.py`

---

## 6. 方式五：小米官方 IoT 开放平台

### 6.1 平台地址

https://iot.mi.com/new/doc/

### 6.2 工作原理

基于小米 IoT 云的 RESTful API，OAuth2.0 认证。

```
开发者注册 → 创建应用 → 获取 app_key/app_secret
     ↓
用户授权 → 获取 access_token
     ↓
调用 API → 设备列表 / 属性读写 / 事件订阅
```

### 6.3 关键 API

```
设备管理:
  GET  /v2/home/device_list          — 获取设备列表
  GET  /v2/home/device/spec          — 获取设备规格
  POST /miotspec/prop/set            — 设置属性
  POST /miotspec/prop/get            — 获取属性

消息推送:
  MQTT  /v2/device/property          — 属性变更推送
  WebSocket                          — 实时事件
```

### 6.4 优缺点

优点:
- 官方支持，稳定可靠
- 完整文档和 SDK（Java/Python/iOS/Android）
- 不依赖手动获取 token

缺点:
- 必须走云端，延迟较高
- 需要企业认证（个人开发者权限受限）
- 部分设备未在开放平台注册
- 申请流程较复杂

### 6.5 适用场景

- 商业 SaaS 平台对接
- 企业级 SmartHome 解决方案
- 需要官方技术支持的正式项目

---

## 7. 方式六：MQTT 桥接方案

### 7.1 架构

```
小米设备 ←(miIO/miOT)→ 自建桥接服务 ←(MQTT)→ SmartHome 系统
                                  ↑
                            EMQX / Mosquitto
```

### 7.2 MQTT Topic 设计

```
xiaomi/{device_id}/state       — 设备状态 (JSON, retain)
xiaomi/{device_id}/set         — 控制指令 (JSON)
xiaomi/{device_id}/available   — 在线状态 ("online"/"offline")
xiaomi/{device_id}/attributes  — 属性列表
xiaomi/bridge/status           — 桥接服务状态
xiaomi/bridge/devices          — 设备列表
```

### 7.3 技术栈

```bash
# MQTT Broker
sudo apt install mosquitto mosquitto-clients

# Python 依赖
pip install paho-mqtt python-miio
```

### 7.4 适用场景

- 将小米设备接入已有 MQTT 生态（Node-RED、openHAB）
- 多系统联动（SmartHome + HA + 自定义后端）
- 需要消息队列和异步处理

### 7.5 Demo

见: `demos/mijia/05_mqtt_bridge_demo.py`

---

## 8. 方式七：Zigbee2MQTT

### 8.1 原理

部分小米设备使用 Zigbee 协议（通过绿米网关连接）。用兼容 Zigbee 协调器替代小米网关，
配合 Zigbee2MQTT 实现完全本地化控制。

### 8.2 所需硬件

- Zigbee 协调器（推荐）:
  - Sonoff Zigbee 3.0 USB Dongle Plus (CC2652)
  - SLZB-06 (CC2652 + PoE)
  - Texas Instruments CC2652R
- 旧款（CC2531）仍可用但不推荐新项目

### 8.3 安装

```bash
# Docker 方式（推荐）
docker run -d \
  --name zigbee2mqtt \
  --privileged \
  -v ./data:/app/data \
  -v /dev/ttyUSB0:/dev/ttyUSB0 \
  -p 8080:8080 \
  koenkk/zigbee2mqtt

# 配置 data/configuration.yaml
```

### 8.4 配置示例

```yaml
# data/configuration.yaml
mqtt:
  base_topic: zigbee2mqtt
  server: mqtt://localhost:1883

serial:
  port: /dev/ttyUSB0

advanced:
  network_key: GENERATE  # 自动生成加密密钥

devices:
  '0x00158d000xxxxxxx':
    friendly_name: living_room_temp
    retain: true
```

### 8.5 配对流程

```
1. 小米设备从绿米网关解绑（长按重置）
2. Zigbee2MQTT 前端 → Permit Join → 开启
3. 设备靠近协调器，等待配对
4. 配对成功后自动出现在 MQTT topic
```

### 8.6 适用设备

- Aqara 温湿度传感器 (WSDCGQ11LM)
- Aqara 门窗传感器 (MCCGQ11LM)
- Aqara 人体传感器 (RTCGQ11LM)
- Aqara 水浸传感器 (SJCGQ11LM)
- Aqara 按键开关 (WXKG11LM)
- Aqara 电动窗帘 (ZNCLDJ12LM)

### 8.7 局限性

- 需要额外购买 Zigbee 协调器
- 部分设备可能无法从小米网关解绑
- 不支持 WiFi 设备
- 部分私有 Zigbee 设备兼容性有限

---

## 9. 方式八：BLE 蓝牙设备被动监听

### 9.1 原理

部分小米传感器通过蓝牙 BLE 广播数据，无需连接即可被动接收。

**MiBeacon 协议 (加密广播帧):**
```
BLE 广播帧结构 (Service Data):
  Frame Control (1B) | Protocol Version (1B) | Random (2B) | Product ID (2B)
  | MAC Address (6B, 可选) | Capability (1B) | Data (变长, 加密)
```

> 不同固件版本的 MiBeacon 帧格式有差异（v2/v4/v5），上为通用结构。
> 刷入 pvvx/MiThermometer 自定义固件后支持标准 ATC 格式（明文温度/湿度）。

### 9.2 硬件需求

- 蓝牙 4.0+ 适配器（内置或 USB）
- Linux 需启用 BLE 扫描: `sudo hciconfig hci0 lestates`

### 9.3 软件方案

```bash
# 方案 A: pvvx/MiThermometer 固件（自定义传感器固件）
# GitHub: https://github.com/pvvx/MiThermometer
# 刷入自定义固件后支持标准 ATC 格式广播

# 方案 B: 蓝牙被动扫描
pip install bleak bleson

# 方案 C: Home Assistant BLE Tracker（自动发现）
# HA → 设置 → 设备与服务 → 添加集成 → Bluetooth LE Tracker
```

### 9.4 适用设备

- 蓝牙温湿度传感器 (LYWSD03MMC)
- 蓝牙门磁 (MCCGQ02LM)
- 蓝牙人体传感器 (RTCGQ02LM)
- 部分手环/体重秤（仅数据读取）

### 9.5 Demo

见: `demos/mijia/06_ble_monitor_demo.py`

---

## 10. 方式九：Home Assistant 集成

### 10.1 简介

最成熟的智能家居平台，小米设备支持最丰富。

### 10.2 推荐的小米集成

| 集成名称          | 来源      | 设备覆盖    | 说明                  |
|-------------------|-----------|-------------|-----------------------|
| Xiaomi Miot Auto  | HACS 社区 | 3000+ 型号  | 最推荐，云+本地双模式 |
| Xiaomi Miio       | 官方      | 200+ 型号   | 基于 python-miio      |
| Xiaomi BLE Sensor | 官方      | BLE 传感器  | 被动监听              |
| Xiaomi Gateway    | 官方      | 网关子设备  | 逐渐不推荐            |
| Zigbee2MQTT       | HACS      | Zigbee 设备 | 需协调器              |

### 10.3 安装 Xiaomi Miot Auto

```
1. 安装 HACS (Home Assistant Community Store)
2. HACS → 集成 → 搜索 "Xiaomi Miot Auto" → 下载
3. 重启 HA
4. 设置 → 设备与服务 → 添加集成 → 搜索 "Xiaomi Miot Auto"
5. 输入小米账号密码 → 自动同步设备
```

### 10.4 安装方式

```bash
# Docker（推荐）
docker run -d \
  --name homeassistant \
  --privileged \
  --restart unless-stopped \
  -e TZ=Asia/Shanghai \
  -v ./config:/config \
  -p 8123:8123 \
  homeassistant/home-assistant:stable
```

### 10.5 与自建网关联动

HA 可以通过 REST 调用你的 C 网关:

```yaml
# configuration.yaml
rest:
  - resource: http://YOUR_GATEWAY_IP:8888/api/devices
    scan_interval: 30
    sensor:
      - name: "MiIO Online Count"
        value_template: "{{ value_json.online_count }}"
```

---

## 11. 方式十：Node-RED 可视化编排

### 11.1 安装

```bash
# Docker
docker run -d --name nodered -p 1880:1880 nodered/node-red

# 小米节点
# 在 Node-RED 管理面板 → 管理面板 → 安装
# node-red-contrib-xiaomi-miot
# node-red-contrib-miio
```

### 11.2 功能

- 可视化拖拽流程
- 小米云端 API 节点
- miIO 本地控制节点
- MQTT 桥接节点
- 定时、条件、联动等逻辑节点

### 11.3 适用场景

- 快速原型验证
- 非程序员使用
- 复杂自动化流程可视化

---

## 12. 方式十一：米家 App 智能场景

### 12.1 原理

米家 App 内置的规则引擎，支持:
- 条件触发 + 动作执行
- 联动 IFTTT / HTTP 请求
- 定时任务
- 设备间联动

### 12.2 HTTP 回调

通过米家场景的"网络请求"动作，可以向你的服务器发送 HTTP 请求:

```
触发: 温度 > 28°C
动作: 发送 HTTP POST → http://your-server:8080/api/turn_on_ac
```

### 12.3 局限性

- 逻辑能力有限
- 无法做复杂的数据处理
- 依赖米家 App 和云端
- 不适合作为 SmartHome 项目的核心

### 12.4 适用场景

- 作为辅助触发器（传感器事件 → HTTP 回调 → 你的后端）
- 不想写代码的简单自动化

---

## 13. Token 获取方法

### 方法一：micloud 命令行（最方便）

```bash
miiocli cloud --username your_account --password your_password token
```

### 方法二：Android 备份提取（Android 9 及以下）

> **注意**: Android 10+ 已移除 `adb backup` 对大部分应用的支持。此方法仅适用于 Android 9 及以下。
> Android 10+ 建议使用方法三（Token Extractor App）。

```bash
# 1. 手机 USB 调试 + 连接电脑
adb backup -noapk com.xiaomi.smarthome

# 2. 解包
java -jar abe.jar unpack backup.ab backup.tar
tar xf backup.tar

# 3. 查询
sqlite3 apps/com.xiaomi.smart_home/db/miio2.db \
  "SELECT name, token FROM DeviceInfo;"
```

### 方法三：Android App 提取（推荐）

```bash
# Xiaomi Cloud Tokens Extractor
# https://github.com/PiotrMachura/xiaomi-cloud-tokens-extractor
# 支持 GUI（Windows）和 CLI（Linux/Mac），Android 10+ 兼容
```

### 方法四：网络抓包

```bash
sudo tcpdump -i wlan0 host DEVICE_IP and udp port 54321 -w token.pcap
# Wireshark 分析 Hello 响应
```

### 方法五：设备重置 + 配网抓包

```bash
# 1. 长按重置键 5 秒
# 2. 米家 App 重新配网
# 3. 同时抓包
sudo tcpdump -i any udp port 54321 -w reset.pcap
```

---

## 14. 方案对比与选择指南

### 按需求选择

```
你的需求？
│
├─ 最快上手，控制几个设备
│  └─→ python-miio (pip install 即用)
│
├─ 完全本地化，不依赖云端
│  ├─ WiFi 设备 → miIO 协议 / python-miio
│  ├─ Zigbee 设备 → Zigbee2MQTT
│  └─ BLE 传感器 → 蓝牙被动监听
│
├─ 最广设备覆盖，统一管理
│  └─→ Home Assistant + Xiaomi Miot Auto
│
├─ 嵌入式网关，Rockchip 板子
│  └─→ C 语言 miIO 协议实现（你已有的方案）
│
├─ 批量获取 token / 控制纯云端设备
│  └─→ micloud 云端 API
│
├─ 多系统联动，消息队列
│  └─→ MQTT 桥接
│
└─ 商业项目，需要官方支持
   └─→ 小米 IoT 开放平台
```

### 综合评分

| 方式           | 设备覆盖     | 本地化     | 稳定性   | 上手难度  | 维护成本  | 推荐度   |
|----------------|--------------|------------|----------|-----------|-----------|----------|
| miIO 协议      | 中(旧设备)   | ★★★★★      | ★★★★     | ★★★       | ★★★       | ★★★★     |
| miOT 协议      | 广(新设备)   | ★★★★       | ★★★      | ★★        | ★★★★      | ★★★      |
| python-miio    | 中(200+)     | ★★★★★      | ★★★★★    | ★★★★★     | ★★★★      | ★★★★★    |
| micloud        | 极广         | ★          | ★★★      | ★★★★      | ★★★       | ★★★★     |
| 官方 IoT 平台  | 广(需授权)   | ★          | ★★★★★    | ★★★       | ★         | ★★★      |
| MQTT 桥接      | 取决于底层   | ★★★★★      | ★★★★     | ★★★       | ★★★       | ★★★★     |
| Zigbee2MQTT    | Zigbee子设备 | ★★★★★      | ★★★★★    | ★★★★      | ★★★★      | ★★★★     |
| BLE 监听       | BLE传感器    | ★★★★★      | ★★★      | ★★★       | ★★★       | ★★★      |
| Home Assistant | 极广(3000+)  | ★★★        | ★★★★★    | ★★★★★     | ★★★★★     | ★★★★★    |
| Node-RED       | 多种         | 取决于节点 | ★★★      | ★★★★★     | ★★★       | ★★★      |
| 米家 App 场景  | App 内设备   | ★          | ★★★★     | ★★★★★     | ★★★★★     | ★★★      |

### 关键注意事项

1. **新设备趋势**: 2024+ 新设备越来越多关闭本地端口，强制走云端
2. **Token 安全**: token 等同于设备控制权，切勿泄露
3. **账号风险**: 逆向云端 API 建议用小号，有被封风险
4. **固件更新**: 小米可能通过固件关闭本地控制端口
5. **设备分类**: 集成前先确认设备协议类型（miIO / miOT / BLE / Zigbee）

---

## Demo 文件清单

```
demos/mijia/
├── 01_miio_local_demo.py            # miIO 协议最小实现（纯 socket）
├── 02_miot_local_demo.py            # miOT 协议本地控制 demo
├── 03_python_miio_demo.py           # python-miio 库使用 demo
├── 04_micloud_demo.py               # 小米云端 API demo
├── 05_mqtt_bridge_demo.py           # MQTT 桥接 demo
├── 06_ble_monitor_demo.py           # BLE 蓝牙传感器监听 demo
├── 07_zigbee2mqtt_demo.py           # Zigbee2MQTT 设备管理 demo
├── 08_homeassistant_demo.py         # Home Assistant REST API demo
├── 09_nodered_demo.py               # Node-RED 流程管理 demo
├── 10_mijia_app_scene_demo.py       # 米家 App 场景 HTTP 回调 demo
├── 11_official_iot_platform_demo.py # 小米官方 IoT 开放平台 demo
├── requirements.txt                 # Python 依赖
└── README.md                        # Demo 使用说明
```

---

*文档版本：v2.1 | 最后更新：2026-05-21 | 修正协议描述、路径、补全评分表*
