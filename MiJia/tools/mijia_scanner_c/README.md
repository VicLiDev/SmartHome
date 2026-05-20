# 米家设备扫描器 C 版 (mijia_scanner_c)

C 语言实现的米家设备网络探测器，与 [Python 版](../mijia_scanner/) 功能对齐。

## 特性

- **mDNS 发现** — 通过 avahi-browse 发现 miOT/HomeKit 设备
- **miIO 单播探测** — 对存活 IP 发送 unicast Hello（非广播，对齐 Python 版）
- **ARP 扫描** — 读取系统 ARP 缓存，零延迟发现在线设备
- **MAC OUI 查询** — 内置厂商识别数据库
- **HA 集成** — 通过 libcurl 从 Home Assistant REST API 获取设备列表和房间信息
- **型号数据库** — 内置 50+ 常见小米生态链设备
- **CJK 终端对齐** — display_width + pad_to，中文/全角字符正确对齐
- **导出** — JSON/CSV 格式导出
- **并行扫描** — fork mDNS 子进程 + 主线程 unicast 线程池并行

## 编译

```bash
cd tools/mijia_scanner_c
make
```

依赖:
- libcurl (`libcurl4-openssl-dev`) — HA API，可选（自动检测）
- pthread (glibc 自带)
- avahi-browse (运行时，可选，用于 mDNS 发现)
- fping (运行时，可选，用于快速 ping sweep)

## 用法

```bash
# 快速扫描（mDNS + ARP + HA API）
./mijia_scanner scan

# 指定超时
./mijia_scanner scan --timeout 10

# 仅 ARP + unicast（跳过 mDNS）
./mijia_scanner scan --no-mdns

# 跨网段扫描
./mijia_scanner scan --range 192.168.1.0/24

# JSON 输出
./mijia_scanner scan --json

# 打印型号数据库
./mijia_scanner models

# 导出
./mijia_scanner scan --json -o devices.json
./mijia_scanner scan --csv -o devices.csv

# 禁用彩色输出
NO_COLOR=1 ./mijia_scanner scan
```

## 项目结构

```
mijia_scanner_c/
├── Makefile
├── README.md
├── config.ini         — HA URL/Token 配置（gitignore）
└── src/
    ├── common.h       — 公共类型、常量、函数声明
    ├── main.c         — CLI 入口 + 命令行解析
    ├── color.c        — ANSI 颜色 + CJK display_width
    ├── device_db.c    — 型号数据库（50+ 条目）
    ├── protocol.c     — miIO 协议（单播探测）
    ├── network.c      — ARP 解析 + fping sweep + 线程池
    ├── mdns.c         — mDNS (avahi-browse fork)
    ├── ha.c           — HA REST API (libcurl)
    └── output.c       — 表格打印 + JSON/CSV 导出
```

## 与 Python 版的差异

| 功能 | Python 版 | C 版 |
|------|-----------|------|
| miIO 发现 | socket + threading | socket + pthread |
| mDNS 发现 | zeroconf | avahi-browse (fork) |
| ARP 扫描 | /proc/net/arp | /proc/net/arp |
| Ping sweep | fping subprocess | fping subprocess |
| 加密 | pycryptodome | 暂未实现 |
| HA API | urllib | libcurl |
| JSON 解析 | json 模块 | 手动解析 |
| CJK 对齐 | wcwidth | 自实现 display_width |
| 并行 | mDNS + unicast 线程 | mDNS fork + unicast 线程池 |

## 许可证

与主项目一致。
