# mijia_scanner — 米家设备网络探测器

Python + C 双版本，快速发现局域网内所有米家/小米智能设备。

## 功能

| 功能         | 说明                                                  |
|--------------|-------------------------------------------------------|
| 快速扫描     | ARP + miIO 广播 + mDNS 三重发现，线程池并行，~5s 完成 |
| 跨网段扫描   | 支持 CIDR / IP 范围 / 逗号分隔，fping 预筛 + 并行探测 |
| HA 区域显示  | 从 Home Assistant 获取设备房间信息，按区域分组展示    |
| 设备类型识别 | MAC OUI 厂商数据库 + HomeKit 类别 + miIO 型号数据库   |
| 深度扫描     | Token 加密通信，获取固件、型号、信号等详细信息        |
| 持续监控     | 定时扫描，实时检测设备上下线                          |
| 结果导出     | JSON / CSV 格式                                       |

## 目录结构

```
mijia_scanner/
├── mijia_scanner.py           # 主入口（CLI 解析 + 子命令）
├── mijia_scanner_lib/          # 核心库
│   ├── __init__.py
│   ├── color.py               # 终端彩色输出
│   ├── device_db.py           # 50+ 型号数据库
│   ├── protocol.py            # miIO 协议：广播、加解密、命令
│   ├── mdns.py                # mDNS/HomeKit 发现、MAC OUI、三重合并
│   ├── network.py             # ARP、ping 扫描、IP 范围解析、unicast 探测
│   ├── ha.py                  # Home Assistant REST API
│   └── output.py              # 表格打印、JSON/CSV 导出
├── src/mijia_scanner.c         # C 版源码
├── config.ini                 # HA 配置（token 等，已在 .gitignore）
├── Makefile                   # C 版编译
└── README.md
```

## 快速使用

```bash
# 基本扫描（自动检测网段，ARP + miIO + mDNS 三协议并行）
python3 mijia_scanner.py scan

# 扫描两个网段
python3 mijia_scanner.py scan --range 192.168.6.0/24,192.168.7.0/24

# 显示 Home Assistant 设备区域
#   方式1: 命令行传 token
python3 mijia_scanner.py scan --ha-token "your_token"
#   方式2: 写入 config.ini（推荐，见下方配置说明）
python3 mijia_scanner.py scan

# 深度扫描（需要设备 token）
python3 mijia_scanner.py deep --token 0123456789abcdef0123456789abcdef

# 持续监控（每 30 秒）
python3 mijia_scanner.py monitor --interval 30

# 查询单台设备
python3 mijia_scanner.py info 192.168.6.119 --token xxx

# 导出
python3 mijia_scanner.py scan --json    # JSON 输出
python3 mijia_scanner.py scan --csv     # CSV 输出
python3 mijia_scanner.py export --format json -o devices.json
```

## 配置文件

在 `config.ini` 中配置 Home Assistant 连接（不需要每次命令行传参）：

```ini
# config.ini（已在 .gitignore 中，不会被提交）
HA_URL=http://192.168.6.127:8123
HA_TOKEN=your_long_lived_access_token
```

HA Token 生成方法：HA 网页 → 左下角头像 → 创建长期访问令牌

## 发现协议

| 协议    | 方式                         | 适用设备                   |
|---------|------------------------------|----------------------------|
| ARP     | 读 `/proc/net/arp` + MAC OUI | 所有在线设备（零延迟）     |
| miIO    | UDP 广播 54321               | 老设备（一代插座、台灯等） |
| mDNS    | `_miot-central._tcp`         | 新设备（网关、新灯具等）   |
| HomeKit | `_hap._tcp`                  | HA Bridge 转发的子设备     |

默认三种协议并行执行，总耗时约 5 秒。

## C 版

```bash
make                # 编译
./mijia_scanner scan --timeout 10
./mijia_scanner deep --token xxx
make clean && make DEBUG=1   # 调试编译
```

## 依赖

Python 版：零可选依赖（核心功能）
- 可选：`zeroconf`（mDNS 发现）、`pycryptodome`（深度扫描 AES 加密）
- 可选：`fping`（跨网段快速预筛）

C 版：gcc, make, libcurl-dev, libssl-dev

## 注意事项

1. 需要与设备在同一局域网（关闭 AP 隔离）
2. 已绑定设备需要 token 才能深度扫描，获取方法见 `mijia-integration-guide.md`
3. 使用 `--no-color` 关闭彩色输出
