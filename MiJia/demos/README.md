# 米家接入 Demo

一套完整的米家设备接入方式 demo，覆盖全部 11 种接入方案，每种均提供 Python 和 C 两个版本。

## 目录结构

```
demos/
├── 01_c_demo/                  # C: miIO 协议网关（共享基础库）
│   ├── inc/                    #   头文件（miio_protocol.h, miio_crypto.h, discovery.h, command.h）
│   ├── src/                    #   源码
│   ├── third_party/cJSON/      #   JSON 库
│   └── Makefile
├── 01_miio_local_demo.py       # Py: miIO 协议最小实现
├── 02_c_demo/                  # C: miOT 本地协议
├── 02_miot_local_demo.py       # Py: miOT SIID/PIID 控制
├── 03_c_demo/                  # C: python-miio CLI 封装
├── 03_python_miio_demo.py      # Py: python-miio 库使用
├── 04_c_demo/                  # C: 云端 API（micloud）
├── 04_micloud_demo.py          # Py: 云端设备列表/token
├── 05_c_demo/                  # C: MQTT 桥接
├── 05_mqtt_bridge_demo.py      # Py: miIO → MQTT 中转
├── 06_c_demo/                  # C: BLE 蓝牙被动监听
├── 06_ble_monitor_demo.py      # Py: BLE 广播帧解析
├── 07_c_demo/                  # C: Zigbee2MQTT 桥接
├── 07_zigbee2mqtt_demo.py      # Py: Zigbee 设备管理
├── 08_c_demo/                  # C: Home Assistant REST API
├── 08_homeassistant_demo.py    # Py: HA 实体/服务/自动化
├── 09_c_demo/                  # C: Node-RED 流程管理
├── 09_nodered_demo.py          # Py: 流程导入导出/示例
├── 10_c_demo/                  # C: 米家 App HTTP 回调服务器
├── 10_mijia_app_scene_demo.py  # Py: 零依赖回调服务器
├── 11_c_demo/                  # C: 官方 IoT 开放平台
├── 11_official_iot_platform_demo.py  # Py: OAuth2 + HMAC 签名
├── requirements.txt            # Python 依赖
└── README.md
```

## 快速开始 — Python 版

```bash
pip install -r requirements.txt

python 01_miio_local_demo.py scan                    # 扫描设备
python 01_miio_local_demo.py info <IP> <TOKEN>      # 查询信息
python 01_miio_local_demo.py power <IP> <TOKEN> on  # 开关控制

python 02_miot_local_demo.py spec zhimi.airpurifier.mb4
python 02_miot_local_demo.py control <IP> <TOKEN> --siid 2 --piid 1 --value true

python 03_python_miio_demo.py scan
python 03_python_miio_demo.py interactive <IP> <TOKEN>

python 04_micloud_demo.py devices <USER> <PASS>
python 04_micloud_demo.py tokens <USER> <PASS>

python 05_mqtt_bridge_demo.py --generate-config
python 05_mqtt_bridge_demo.py --broker localhost

sudo python 06_ble_monitor_demo.py scan
sudo python 06_ble_monitor_demo.py monitor --mqtt localhost

python 07_zigbee2mqtt_demo.py list
python 07_zigbee2mqtt_demo.py set <name> '{"state":"ON"}'

python 08_homeassistant_demo.py status --host 192.168.1.100 --token <TOKEN>
python 08_homeassistant_demo.py call light turn_on --entity light.bedroom

python 09_nodered_demo.py flows
python 09_nodered_demo.py export --output my_flows.json

python 10_mijia_app_scene_demo.py start
python 10_mijia_app_scene_demo.py guide

python 11_official_iot_platform_demo.py authorize
python 11_official_iot_platform_demo.py get <DID> <SIID> <PIID>
```

## 快速开始 — C 版

```bash
# 编译依赖: gcc, libcurl, openssl, libbluetooth-dev(06)
# 所有 02-11 的 C demo 共享 01_c_demo 中的基础代码

cd 01_c_demo && make          # 编译 miio_gateway
cd 02_c_demo && make          # 编译 miot_local_demo
cd 03_c_demo && make          # 编译 miio_cli
cd 04_c_demo && make          # 编译 micloud
cd 05_c_demo && make          # 编译 mqtt_bridge
cd 06_c_demo && make          # 编译 ble_monitor (需 libbluetooth-dev)
cd 07_c_demo && make          # 编译 z2m_bridge
cd 08_c_demo && make          # 编译 ha_client
cd 09_c_demo && make          # 编译 nodered_client
cd 10_c_demo && make          # 编译 mijia_callback
cd 11_c_demo && make          # 编译 iot_platform

# 使用示例
./01_c_demo/miio_gateway scan
./01_c_demo/miio_gateway info 192.168.1.100 --token abcdef...
./02_c_demo/miot_local_demo list-models
./02_c_demo/miot_local_demo get 192.168.1.100 <TOKEN> --siid 2 --piid 1
./02_c_demo/miot_local_demo set 192.168.1.100 <TOKEN> --siid 2 --piid 1 --value true
./03_c_demo/miio_cli info 192.168.1.100 --token abcdef...
./03_c_demo/miio_cli power 192.168.1.100 --token abcdef... on
./04_c_demo/micloud login --user xxx --pass xxx
./04_c_demo/micloud devices
./05_c_demo/mqtt_bridge pub --topic mihome/xxx/set --message '{"power":"on"}'
./05_c_demo/mqtt_bridge list
sudo ./06_c_demo/ble_monitor scan
sudo ./06_c_demo/ble_monitor parse --hex AABBCC...
./07_c_demo/z2m_bridge devices
./07_c_demo/z2m_bridge set <name> --json '{"state":"ON"}'
./08_c_demo/ha_client states --host 192.168.1.100 --token <TOKEN>
./08_c_demo/ha_client get light.bedroom --host 192.168.1.100 --token <TOKEN>
./09_c_demo/nodered_client flows --host localhost:1880
./09_c_demo/nodered_client export --output my_flows.json
./10_c_demo/mijia_callback run --port 8090
./10_c_demo/mijia_callback test
./11_c_demo/iot_platform config --key <APP_KEY> --secret <APP_SECRET>
./11_c_demo/iot_platform devices
```

## Demo 说明

| # | 接入方式 | Python | C | 传输/依赖 | 说明 |
|---|----------|--------|---|-----------|------|
| 01 | miIO 协议 | socket + pycryptodome | raw UDP + OpenSSL | UDP 54321 | 纯协议实现，01_c_demo 是其他 C demo 的共享基础库 |
| 02 | miOT 协议 | pycryptodome | libcurl + cJSON | UDP 54321 (同 miIO) | 新设备 SIID/PIID 属性模型 |
| 03 | python-miio | python-miio 库 | popen(miiocli) | UDP 54321 | 最成熟的 Python 库，C 版封装 CLI 调用 |
| 04 | 云端 API | requests | libcurl | HTTPS | 设备列表/Token 获取/云端控制 |
| 05 | MQTT 桥接 | paho-mqtt | popen(mosquitto) | MQTT | miIO ↔ MQTT 中转 |
| 06 | BLE 监听 | bleak | BlueZ HCI | BLE 广播 | 被动接收 MiBeacon 广播帧 |
| 07 | Zigbee2MQTT | paho-mqtt | popen(mosquitto) | MQTT | Zigbee 子设备管理/配对 |
| 08 | Home Assistant | requests | libcurl | HTTP REST | HA 实体/服务/自动化/历史 |
| 09 | Node-RED | requests | libcurl | HTTP REST | 流程导入导出/节点管理 |
| 10 | 米家 App 场景 | stdlib (零依赖) | POSIX socket | HTTP | 回调服务器接收 App 场景事件 |
| 11 | 官方 IoT 平台 | requests | libcurl + OpenSSL | HTTPS + HMAC | OAuth2 授权 + 设备属性读写 |

## C Demo 共享架构

```
01_c_demo/                     ← 所有 02-11 的共享基础库
├── inc/miio_protocol.h        ← 协议常量、数据结构
├── inc/miio_crypto.h          ← AES-128-CBC 加解密 + MD5 密钥派生
├── inc/discovery.h            ← UDP 组播设备发现
├── inc/command.h              ← JSON-RPC 命令构建与发送
├── third_party/cJSON/         ← JSON 解析库
└── src/*.c                    ← 实现

02-11_c_demo/Makefile          ← 编译时自动链接 01_c_demo 的 .c 文件
```

## Python ↔ C 函数对照

| Python (01_miio_local_demo.py) | C (01_c_demo) | 文件 |
|--------------------------------|---------------|------|
| `discover()`                   | `miio_discover()` | discovery.c |
| `handshake()`                  | `miio_handshake()` | discovery.c |
| `send_command()`               | `miio_send_command()` | command.c |
| `get_info()`                   | `miio_get_info()` | command.c |
| `derive_keys()`                | `derive_keys()` | miio_crypto.c |
| `aes_encrypt/decrypt()`        | `encrypt/decrypt()` | miio_crypto.c |

## 前置条件

- **Token**: 本地控制（Demo 1-3, 5）需要设备 Token，获取方法见 `mijia-integration-guide.md`
- **网络**: 设备必须在同一局域网（关闭 AP 隔离）
- **C 编译**: gcc, make, libcurl-dev, libssl-dev；06 额外需要 libbluetooth-dev
- **BLE**: Demo 06 需要 root 权限和蓝牙适配器
- **Zigbee**: Demo 07 需要部署 Zigbee2MQTT + MQTT Broker + Zigbee 协调器
- **Home Assistant**: Demo 08 需要运行中的 HA 实例和长期访问令牌
- **Node-RED**: Demo 09 需要运行中的 Node-RED 实例
- **官方 IoT**: Demo 11 需要小米 IoT 开放平台开发者账号
