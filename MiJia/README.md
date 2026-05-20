# MiJia — 米家设备接入与控制

覆盖米家设备 11 种接入方式的完整 Demo 集 + 实用工具集。

## 项目结构

```
MiJia/
├── README.md                     ← 本文件
├── mijia-integration-guide.md    ← 11 种接入方式完整指南
│
├── demos/                        ← 11 种接入方式 Demo（Python + C 双版本）
│   ├── README.md                 ← Demo 使用说明
│   ├── requirements.txt          ← Python 依赖
│   │
│   ├── 01_miio_local_demo.py     # miIO 协议最小实现
│   ├── 01_c_demo/                # C: miIO 协议（共享基础库）
│   ├── 02_miot_local_demo.py     # miOT SIID/PIID 属性控制
│   ├── 02_c_demo/
│   ├── 03_python_miio_demo.py    # python-miio 库使用
│   ├── 03_c_demo/
│   ├── 04_micloud_demo.py        # 云端 API（设备列表/Token）
│   ├── 04_c_demo/
│   ├── 05_mqtt_bridge_demo.py    # MQTT 桥接
│   ├── 05_c_demo/
│   ├── 06_ble_monitor_demo.py    # BLE 蓝牙被动监听
│   ├── 06_c_demo/
│   ├── 07_zigbee2mqtt_demo.py    # Zigbee2MQTT 桥接
│   ├── 07_c_demo/
│   ├── 08_homeassistant_demo.py  # Home Assistant REST API
│   ├── 08_c_demo/
│   ├── 09_nodered_demo.py        # Node-RED 流程编排
│   ├── 09_c_demo/
│   ├── 10_mijia_app_scene_demo.py # 米家 App 场景回调
│   ├── 10_c_demo/
│   ├── 11_official_iot_platform_demo.py  # 官方 IoT 开放平台
│   └── 11_c_demo/
│
└── tools/                        ← 实用工具
    └── mijia_scanner/            # 设备探测器
        ├── mijia_scanner.py      # 主入口
        ├── mijia_scanner_lib/    # 核心库（模块化）
        ├── config.ini            # HA 配置（gitignore）
        └── README.md
```

## 11 种接入方式一览

| #   | 方式           | 协议/传输    | 适用场景                        | 复杂度   |
|-----|----------------|--------------|---------------------------------|----------|
| 01  | miIO 协议      | UDP 54321    | 老设备直接控制                  | ★★☆      |
| 02  | miOT 协议      | COAP UDP/TCP | 新设备 SIID/PIID 模型           | ★★★      |
| 03  | python-miio 库 | UDP 54321    | 最成熟的 Python 方案            | ★☆☆      |
| 04  | 云端 API       | HTTPS        | 远程控制、Token 获取            | ★★☆      |
| 05  | MQTT 桥接      | MQTT         | 与 Home Assistant/Node-RED 集成 | ★★☆      |
| 06  | BLE 监听       | BLE 广播     | 被动接收传感器数据              | ★★★      |
| 07  | Zigbee2MQTT    | MQTT         | Zigbee 子设备管理               | ★★★      |
| 08  | Home Assistant | HTTP REST    | 完整智能家居平台                | ★★☆      |
| 09  | Node-RED       | HTTP REST    | 可视化自动化编排                | ★☆☆      |
| 10  | 米家 App 场景  | HTTP 回调    | App 自动化事件触发              | ★★☆      |
| 11  | 官方 IoT 平台  | HTTPS + HMAC | 企业级设备管理                  | ★★★      |

详细对比和接入指南见 [mijia-integration-guide.md](./mijia-integration-guide.md)。

## 快速开始

### 设备扫描

```bash
cd tools/mijia_scanner

# 扫描局域网所有设备（~5 秒）
python3 mijia_scanner.py scan

# 扫描多个网段 + 显示 HA 房间区域
python3 mijia_scanner.py scan --range 192.168.6.0/24,192.168.7.0/24

# 深度扫描（需要 token）
python3 mijia_scanner.py deep --token <your_token>
```

### 运行 Demo

```bash
cd demos

# 安装依赖
pip install -r requirements.txt

# miIO 协议扫描
python3 01_miio_local_demo.py scan

# python-miio 库
python3 03_python_miio_demo.py scan

# Home Assistant 集成
python3 08_homeassistant_demo.py status --host 192.168.6.127 --token <token>

# C 版编译运行
cd 01_c_demo && make && ./miio_gateway scan
```

详细说明见 [demos/README.md](./demos/README.md)。

## 前置条件

- Python 3.8+
- 设备在同一局域网（关闭 AP 隔离）
- C 版编译需要 gcc, make, libcurl-dev, libssl-dev
- BLE demo 需要 root + 蓝牙适配器 + libbluetooth-dev
- Zigbee2MQTT demo 需要部署 Z2M + MQTT Broker + 协调器
