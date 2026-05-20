"""
Demo 9: Node-RED REST API 交互 — 通过 REST API 管理流程和节点

功能:
  - 获取 Node-RED 运行状态信息 (GET /info)
  - 列出所有流程 (GET /flows)
  - 导出流程为 JSON 文件或打印到标准输出
  - 从 JSON 文件导入流程 (POST /flows)
  - 部署当前流程
  - 列出可用节点类型 (GET /nodes)
  - 安装 Node-RED 节点包 (POST /palette/{package})
  - 展示示例流程 (小米设备控制)
  - 展示推荐的小米相关 Node-RED 节点包

架构:
  本脚本 ←(HTTP REST)→ Node-RED ←(各种节点)→ 智能设备 / MQTT / 小米云

Node-RED Admin API 端点:
  GET  /info                          — 获取 Node-RED 状态信息
  GET  /flows                         — 列出所有流程
  POST /flows                         — 导入/替换流程
  POST /flows/restart                 — 安全重启流程
  GET  /nodes                         — 列出可用节点类型
  POST /palette/{package}             — 安装节点包

前置条件:
  1. 已安装并运行 Node-RED: https://nodered.org/
  2. 启用时启用了 adminAuth（或本机无认证访问）
  3. 已安装 requests 库

依赖: pip install requests

用法:
  # 获取 Node-RED 状态
  python 09_nodered_demo.py status

  # 列出所有流程
  python 09_nodered_demo.py flows

  # 导出流程到文件
  python 09_nodered_demo.py export --output my_flows.json

  # 从文件导入流程
  python 09_nodered_demo.py import my_flows.json

  # 部署流程
  python 09_nodered_demo.py deploy

  # 列出可用节点
  python 09_nodered_demo.py nodes

  # 安装节点包
  python 09_nodered_demo.py install node-red-contrib-xiaomi-miot

  # 查看示例流程
  python 09_nodered_demo.py sample

  # 查看推荐的小米节点包
  python 09_nodered_demo.py packages

  # 指定主机和认证
  python 09_nodered_demo.py --host 192.168.1.100 --user admin --pass secret status
"""

import json
import sys
import os
import logging
from typing import Optional, Dict, List, Any, Union

try:
    import requests
except ImportError:
    print("请先安装依赖: pip install requests")
    sys.exit(1)


# ═══════════════════════════════════════════════════════
#  日志配置
# ═══════════════════════════════════════════════════════

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("nodered_demo")


# ═══════════════════════════════════════════════════════
#  工具函数
# ═══════════════════════════════════════════════════════

def print_separator(title: str = "", char: str = "=", width: int = 60):
    """打印分隔线"""
    if title:
        padding = width - len(title) - 2
        left = padding // 2
        right = padding - left
        print(f"\n{char * left} {title} {char * right}")
    else:
        print(char * width)


def pretty_json(data, indent: int = 2) -> str:
    """格式化 JSON 输出"""
    return json.dumps(data, indent=indent, ensure_ascii=False)


# ═══════════════════════════════════════════════════════
#  示例配置
# ═══════════════════════════════════════════════════════

SAMPLE_CONFIG = {
    "host": "localhost",         # Node-RED 主机地址
    "port": 1880,                # Node-RED 端口（默认 1880）
    "user": "",                  # HTTP Basic Auth 用户名（可选）
    "password": "",              # HTTP Basic Auth 密码（可选）
    "timeout": 15,               # 请求超时（秒）
    "use_ssl": False,            # 是否使用 HTTPS
}


# ═══════════════════════════════════════════════════════
#  推荐的小米相关 Node-RED 节点包
# ═══════════════════════════════════════════════════════

RECOMMENDED_PACKAGES = [
    {
        "name": "node-red-contrib-xiaomi-miot",
        "description": "小米 MIoT 设备控制节点（自动发现设备、属性读写）",
        "features": [
            "自动发现小米智能设备",
            "支持 MIoT 协议（蓝牙、Wi-Fi、Zigbee）",
            "属性读取与设置",
            "事件触发",
        ],
        "install": "cd ~/.node-red && npm install node-red-contrib-xiaomi-miot",
    },
    {
        "name": "node-red-contrib-miio",
        "description": "小米 MIoT 协议节点（扫地机器人、空气净化器等）",
        "features": [
            "支持 miio 协议直接控制设备",
            "扫地机器人控制（清扫、拖地、回充）",
            "空气净化器/风扇控制",
            "设备状态查询",
        ],
        "install": "cd ~/.node-red && npm install node-red-contrib-miio",
    },
    {
        "name": "node-red-contrib-xiaomi",
        "description": "小米设备通用节点（基于 miIO）",
        "features": [
            "通用小米设备控制",
            "支持多种设备类型",
            "简单的 JSON 消息接口",
        ],
        "install": "cd ~/.node-red && npm install node-red-contrib-xiaomi",
    },
    {
        "name": "node-red-contrib-yeelight",
        "description": "Yeelight 智能灯控制节点",
        "features": [
            "Yeelight 灯泡色温/亮度控制",
            "彩色灯光控制",
            "场景模式",
        ],
        "install": "cd ~/.node-red && npm install node-red-contrib-yeelight",
    },
    {
        "name": "node-red-contrib-mqtt-broker",
        "description": "增强版 MQTT 节点（配合 zigbee2mqtt 使用）",
        "features": [
            "MQTT 发布/订阅",
            "支持 QoS 和 retained 消息",
            "JSON 消息自动解析",
        ],
        "install": "cd ~/.node-red && npm install node-red-contrib-mqtt-broker",
    },
]


# ═══════════════════════════════════════════════════════
#  示例流程 — 小米设备控制
# ═══════════════════════════════════════════════════════

SAMPLE_FLOW = [
    {
        "id": "flow_xiaomi_control",
        "type": "tab",
        "label": "小米设备控制",
        "disabled": False,
        "info": "小米智能家居设备控制流程演示",
    },
    # ── 流程 1: 定时获取扫地机器人状态 ──
    {
        "id": "inject_timer_1",
        "type": "inject",
        "z": "flow_xiaomi_control",
        "name": "每5分钟",
        "topic": "",
        "payload": "",
        "payloadType": "date",
        "repeat": "300",
        "crontab": "",
        "once": False,
        "onceDelay": 0.1,
        "x": 120,
        "y": 80,
        "wires": [["miio_get_status"]],
    },
    {
        "id": "miio_get_status",
        "type": "miio-device",
        "z": "flow_xiaomi_control",
        "device": "192.168.1.50",
        "token": "YOUR_TOKEN_HERE",
        "name": "扫地机器人",
        "operation": "get_status",
        "x": 320,
        "y": 80,
        "wires": [["debug_status"]],
    },
    {
        "id": "debug_status",
        "type": "debug",
        "z": "flow_xiaomi_control",
        "name": "查看状态",
        "active": True,
        "tosidebar": True,
        "console": False,
        "tostatus": False,
        "complete": "payload",
        "targetType": "msg",
        "statusVal": "",
        "statusType": "auto",
        "x": 520,
        "y": 80,
        "wires": [],
    },
    # ── 流程 2: MQTT 订阅 Zigbee 设备状态 ──
    {
        "id": "mqtt_in_zigbee",
        "type": "mqtt in",
        "z": "flow_xiaomi_control",
        "name": "Zigbee设备状态",
        "topic": "zigbee2mqtt/+",
        "qos": "0",
        "datatype": "json",
        "broker": "mqtt_broker_config",
        "nl": False,
        "rap": True,
        "rh": 0,
        "x": 120,
        "y": 200,
        "wires": [["function_parse_json"]],
    },
    {
        "id": "function_parse_json",
        "type": "function",
        "z": "flow_xiaomi_control",
        "name": "解析JSON",
        "func": (
            "// 解析 MQTT 消息\n"
            "var topic = msg.topic.replace('zigbee2mqtt/', '');\n"
            "var payload = msg.payload;\n"
            "\n"
            "msg.device = topic;\n"
            "msg.battery = payload.battery || 'N/A';\n"
            "msg.linkquality = payload.linkquality || 'N/A';\n"
            "msg.contact = payload.contact || 'N/A';\n"
            "\n"
            "// 示例: 门磁传感器\n"
            "if (payload.contact !== undefined) {\n"
            "    msg.status = payload.contact === true ? '已关闭' : '已打开';\n"
            "}\n"
            "\n"
            "return msg;"
        ),
        "outputs": 1,
        "noerr": 0,
        "x": 320,
        "y": 200,
        "wires": [["debug_zigbee"]],
    },
    {
        "id": "debug_zigbee",
        "type": "debug",
        "z": "flow_xiaomi_control",
        "name": "查看Zigbee数据",
        "active": True,
        "tosidebar": True,
        "console": False,
        "tostatus": False,
        "complete": "payload",
        "targetType": "msg",
        "statusVal": "",
        "statusType": "auto",
        "x": 540,
        "y": 200,
        "wires": [],
    },
    # ── 流程 3: HTTP Webhook 控制小米设备 ──
    {
        "id": "http_in_webhook",
        "type": "http in",
        "z": "flow_xiaomi_control",
        "name": "Webhook 接口",
        "url": "/mi-control",
        "method": "post",
        "upload": False,
        "swaggerDoc": "",
        "x": 120,
        "y": 320,
        "wires": [["function_convert_miio"]],
    },
    {
        "id": "function_convert_miio",
        "type": "function",
        "z": "flow_xiaomi_control",
        "name": "转换为miIO命令",
        "func": (
            "// 将 HTTP 请求转换为 miIO 控制命令\n"
            "var body = msg.payload;\n"
            "\n"
            "if (!body || !body.device || !body.command) {\n"
            "    msg.statusCode = 400;\n"
            "    msg.payload = { error: '缺少 device 或 command 参数' };\n"
            "    return [null, msg];\n"
            "}\n"
            "\n"
            "msg.payload = {\n"
            "    device: body.device,\n"
            "    command: body.command,\n"
            "    params: body.params || {}\n"
            "};\n"
            "\n"
            "return [msg, null];"
        ),
        "outputs": 2,
        "noerr": 0,
        "x": 360,
        "y": 320,
        "wires": [["http_response_ok"], ["http_response_err"]],
    },
    {
        "id": "http_response_ok",
        "type": "http response",
        "z": "flow_xiaomi_control",
        "name": "HTTP 200",
        "statusCode": "200",
        "x": 580,
        "y": 290,
        "wires": [],
    },
    {
        "id": "http_response_err",
        "type": "http response",
        "z": "flow_xiaomi_control",
        "name": "HTTP 400",
        "statusCode": "400",
        "x": 580,
        "y": 350,
        "wires": [],
    },
]


# 示例流程 ASCII 图
SAMPLE_FLOW_DIAGRAM = """
  ┌─────────────────────────────────────────────────────────────────┐
  │                   小米设备控制流程 (Sample Flow)                │
  └─────────────────────────────────────────────────────────────────┘

  [流程 1] 定时获取扫地机器人状态
  ┌──────────┐     ┌──────────────────┐     ┌──────────────┐
  │  Inject  │────→│  miio-device     │────→│    Debug     │
  │ 每5分钟   │     │  扫地机器人       │     │  查看状态     │
  └──────────┘     │  192.168.1.50    │     └──────────────┘
                   └──────────────────┘
                   operation: get_status


  [流程 2] MQTT 订阅 Zigbee 设备状态
  ┌──────────┐     ┌──────────────┐     ┌──────────────┐
  │ MQTT-in  │────→│  Function    │────→│    Debug     │
  │zigbee2mqtt/│   │  解析JSON     │     │查看Zigbee数据 │
  └──────────┘     │ 提取设备/电量  │     └──────────────┘
                   └──────────────┘


  [流程 3] HTTP Webhook 控制小米设备
  ┌──────────┐     ┌──────────────┐     ┌──────────────┐
  │ HTTP-in  │────→│  Function    │──┬──→│  HTTP 200    │
  │/mi-control│    │转换为miIO命令 │  │   └──────────────┘
  │  POST    │     └──────────────┘  │
  └──────────┘                       └──→┌──────────────┐
                                      │   │  HTTP 400    │
                                      │   └──────────────┘
"""


# ═══════════════════════════════════════════════════════
#  Node-RED REST API 客户端
# ═══════════════════════════════════════════════════════

class NodeRedClient:
    """Node-RED Admin REST API 客户端

    通过 HTTP REST API 管理 Node-RED 流程和节点。
    使用 HTTP Basic Auth 进行认证（如果启用了 adminAuth）。

    认证方式:
      在 HTTP 请求头中添加:
        Authorization: Basic base64(user:password)

    启用认证:
      在 Node-RED 的 settings.js 中配置 adminAuth:
        adminAuth: {
            type: "credentials",
            users: [{
                username: "admin",
                password: "$2a$08$xxx...",  // 使用 node-red-admin hash-pw 生成
                permissions: "*"
            }]
        }

    参考: https://nodered.org/docs/api/admin/
    """

    def __init__(
        self,
        host: str = "localhost",
        port: int = 1880,
        user: str = "",
        password: str = "",
        timeout: int = 15,
        use_ssl: bool = False,
    ):
        """初始化 Node-RED 客户端

        Args:
            host: Node-RED 主机地址
            port: Node-RED 端口（默认 1880）
            user: HTTP Basic Auth 用户名（可选）
            password: HTTP Basic Auth 密码（可选）
            timeout: 请求超时时间（秒）
            use_ssl: 是否使用 HTTPS
        """
        self.host = host
        self.port = port
        self.user = user
        self.password = password
        self.timeout = timeout
        self.use_ssl = use_ssl

        # 构建 base URL
        scheme = "https" if use_ssl else "http"
        self.base_url = f"{scheme}://{host}:{port}"

        # 请求会话
        self.session = requests.Session()
        self.session.headers.update({
            "Content-Type": "application/json",
        })

        # 设置认证
        if user and password:
            self.session.auth = (user, password)

    def _url(self, path: str = "") -> str:
        """构建完整的 API URL

        Args:
            path: API 路径

        Returns:
            完整的 URL 字符串
        """
        url = self.base_url
        if path:
            url = f"{self.base_url}{path}"
        return url

    def _request(
        self,
        method: str = "GET",
        path: str = "",
        data: Optional[Union[dict, list]] = None,
    ) -> Optional[Union[dict, list]]:
        """发送 HTTP 请求到 Node-RED Admin API

        Args:
            method: HTTP 方法 (GET/POST)
            path: API 路径
            data: POST 请求的 JSON 数据

        Returns:
            响应的 JSON 字典或列表，出错返回 None
        """
        url = self._url(path)
        log.debug(f"{method} {url}")

        try:
            if method.upper() == "GET":
                resp = self.session.get(url, timeout=self.timeout)
            elif method.upper() == "POST":
                resp = self.session.post(url, json=data, timeout=self.timeout)
            elif method.upper() == "DELETE":
                resp = self.session.delete(url, timeout=self.timeout)
            else:
                log.error(f"不支持的 HTTP 方法: {method}")
                return None

            # 检查状态码
            if resp.status_code in (200, 201, 204):
                if resp.status_code == 204:
                    return {"status": "success"}
                try:
                    return resp.json()
                except (json.JSONDecodeError, ValueError):
                    return {"status_code": resp.status_code, "text": resp.text}

            elif resp.status_code == 401:
                log.error("认证失败: 用户名或密码错误")
                print("  [错误] 401 Unauthorized — 认证失败")
                print("  请确认:")
                print("    1. 用户名和密码正确")
                print("    2. Node-RED 已启用 adminAuth")
                print("    3. 用户有足够权限")
                return None

            elif resp.status_code == 404:
                log.error(f"资源未找到: {path}")
                print(f"  [错误] 404 Not Found — {path}")
                return None

            else:
                log.error(f"请求失败: HTTP {resp.status_code}")
                try:
                    error_msg = resp.json()
                except (json.JSONDecodeError, ValueError):
                    error_msg = resp.text
                print(f"  [错误] HTTP {resp.status_code}: {error_msg}")
                return None

        except requests.exceptions.ConnectionError:
            log.error(f"连接失败: 无法连接到 {self.host}:{self.port}")
            print(f"  [错误] 无法连接到 Node-RED ({self.host}:{self.port})")
            print("  请确认:")
            print("    1. Node-RED 正在运行")
            print("    2. 主机地址和端口正确")
            print(f"    3. 防火墙允许访问端口 {self.port}")
            return None

        except requests.exceptions.Timeout:
            log.error(f"请求超时: {method} {path}")
            print(f"  [错误] 请求超时（{self.timeout}秒）")
            print("  请尝试增大 --timeout 参数")
            return None

        except requests.exceptions.RequestException as e:
            log.error(f"请求异常: {e}")
            print(f"  [错误] 请求异常: {e}")
            return None

    # ═══════════════════════════════════════════════════
    #  业务功能
    # ═══════════════════════════════════════════════════

    def get_status(self) -> Optional[Union[dict, list]]:
        """获取 Node-RED 运行状态信息

        调用 GET /info 获取 Node-RED 基本信息。
        返回版本号、运行时间等。

        Returns:
            状态信息字典
        """
        print_separator("Node-RED 运行状态")

        data = self._request("GET", "/info")
        if data is None:
            return None

        # /info 端点返回 dict
        if not isinstance(data, dict):
            print("  [警告] 返回数据格式异常")
            return data

        print(f"\n  状态: {data.get('state', '未知')}")
        print(f"  版本: {data.get('version', '未知')}")
        print(f"  Node.js 版本: {data.get('node', {}).get('version', '未知')}")
        print(f"  平台: {data.get('node', {}).get('os', {}).get('platform', '未知')}")
        print(f"  架构: {data.get('node', {}).get('os', {}).get('arch', '未知')}")

        # 显示运行时间
        now = data.get("now", "")
        started = data.get("started", "")
        if now and started:
            try:
                uptime_ms = int(now) - int(started)
                uptime_s = uptime_ms / 1000
                days = int(uptime_s // 86400)
                hours = int((uptime_s % 86400) // 3600)
                minutes = int((uptime_s % 3600) // 60)
                if days > 0:
                    print(f"  运行时间: {days}天 {hours}小时 {minutes}分钟")
                else:
                    print(f"  运行时间: {hours}小时 {minutes}分钟")
            except (ValueError, TypeError):
                pass

        # 用户统计
        users = data.get("stats", {}).get("users", {})
        if users:
            print(f"\n  ┌─ 用户统计 ───────────────────────────────")
            for key, value in users.items():
                print(f"  │  {key}: {value}")
            print("  └──────────────────────────────────────────")

        # 流程统计
        flows = data.get("stats", {}).get("flows", {})
        if flows:
            print(f"\n  ┌─ 流程统计 ───────────────────────────────")
            for key, value in flows.items():
                print(f"  │  {key}: {value}")
            print("  └──────────────────────────────────────────")

        return data

    def list_flows(self) -> Optional[Union[list, dict]]:
        """列出所有流程

        调用 GET /flows 获取所有已部署的流程。

        Returns:
            流程列表
        """
        print_separator("列出所有流程")

        data = self._request("GET", "/flows")
        if data is None:
            return None

        if not isinstance(data, list):
            print("  [警告] 返回数据格式异常")
            return data

        # 按 type 分类
        tabs = [f for f in data if f.get("type") == "tab"]
        subflows = [f for f in data if f.get("type") == "subflow"]
        nodes = [f for f in data if f.get("type") not in ("tab", "subflow", "group")]
        groups = [f for f in data if f.get("type") == "group"]

        print(f"\n  总计: {len(tabs)} 个流程页, {len(subflows)} 个子流程, "
              f"{len(nodes)} 个节点, {len(groups)} 个分组")

        if tabs:
            print(f"\n  ┌─ 流程页 ──────────────────────────────────")
            for tab in tabs:
                tab_id = tab.get("id", "?")
                label = tab.get("label", "未命名")
                disabled = " [已禁用]" if tab.get("disabled") else ""
                info = tab.get("info", "")
                if info:
                    info = info[:50] + "..." if len(info) > 50 else info
                    print(f"  │  ● {label}{disabled}")
                    print(f"  │    ID: {tab_id}")
                    print(f"  │    说明: {info}")
                else:
                    print(f"  │  ● {label}{disabled}  (ID: {tab_id})")
            print("  └──────────────────────────────────────────")

        if subflows:
            print(f"\n  ┌─ 子流程 ──────────────────────────────────")
            for sf in subflows:
                sf_id = sf.get("id", "?")
                name = sf.get("name", "未命名")
                print(f"  │  ◇ {name}  (ID: {sf_id})")
            print("  └──────────────────────────────────────────")

        # 按节点类型统计
        type_counts: Dict[str, int] = {}
        for node in nodes:
            ntype = node.get("type", "unknown")
            type_counts[ntype] = type_counts.get(ntype, 0) + 1

        if type_counts:
            print(f"\n  ┌─ 节点类型统计 ───────────────────────────")
            for ntype, count in sorted(type_counts.items(), key=lambda x: -x[1]):
                print(f"  │  {ntype:30s}: {count:4d} 个")
            print("  └──────────────────────────────────────────")

        return data

    def export_flows(self, output_file: Optional[str] = None) -> Optional[Union[dict, list]]:
        """导出所有流程

        调用 GET /flows 获取流程并导出为 JSON。
        可输出到文件或标准输出。

        Args:
            output_file: 输出文件路径（可选），为 None 时打印到标准输出

        Returns:
            流程数据字典
        """
        print_separator("导出流程")

        data = self._request("GET", "/flows")
        if data is None:
            return None

        json_str = pretty_json(data, indent=2)

        if output_file:
            try:
                with open(output_file, "w", encoding="utf-8") as f:
                    f.write(json_str)
                print(f"\n  流程已导出到: {output_file}")
                print(f"  文件大小: {len(json_str)} 字节")

                # 统计信息
                if isinstance(data, list):
                    node_count = len([n for n in data if n.get("type") not in ("tab", "subflow", "group")])
                    tab_count = len([n for n in data if n.get("type") == "tab"])
                    print(f"  包含: {tab_count} 个流程页, {node_count} 个节点")
            except IOError as e:
                print(f"  [错误] 写入文件失败: {e}")
                return None
        else:
            print(f"\n{json_str}")

        return data

    def import_flows(self, file_path: str) -> bool:
        """从 JSON 文件导入流程

        调用 POST /flows 导入流程配置。
        注意: 这会替换当前所有流程！

        Args:
            file_path: JSON 文件路径

        Returns:
            是否成功
        """
        print_separator("导入流程")

        # 读取文件
        if not os.path.exists(file_path):
            print(f"  [错误] 文件不存在: {file_path}")
            return False

        try:
            with open(file_path, "r", encoding="utf-8") as f:
                flow_data = json.load(f)
        except json.JSONDecodeError as e:
            print(f"  [错误] JSON 解析失败: {e}")
            return False
        except IOError as e:
            print(f"  [错误] 读取文件失败: {e}")
            return False

        if not isinstance(flow_data, list):
            print(f"  [错误] 流程数据应为 JSON 数组（list），实际类型: {type(flow_data).__name__}")
            return False

        print(f"\n  从文件读取: {file_path}")
        print(f"  流程节点数: {len(flow_data)}")

        # 发送 POST 请求
        print("  正在导入流程...")
        result = self._request("POST", "/flows", data=flow_data)
        if result is None:
            return False

        print("  流程导入成功!")
        print("  注意: 新流程已保存但尚未部署")
        print("  请运行 'deploy' 命令部署流程使其生效")
        return True

    def deploy(self) -> bool:
        """部署当前流程

        调用 POST /flows 重新部署流程（带 reset 参数触发重启）。

        Returns:
            是否成功
        """
        print_separator("部署流程")

        # 先获取当前流程
        current_flows = self._request("GET", "/flows")
        if current_flows is None:
            print("  [错误] 无法获取当前流程")
            return False

        # 重新 POST 当前流程（这会触发部署）
        print("\n  正在部署流程...")
        result = self._request("POST", "/flows", data=current_flows)
        if result is None:
            return False

        print("  流程部署成功!")
        print("  所有流程已重新加载并运行")
        return True

    def restart_flows(self) -> bool:
        """安全重启 Node-RED 流程

        调用 POST /flows/restart 重启所有流程。

        Returns:
            是否成功
        """
        print_separator("重启流程")

        print("\n  正在重启 Node-RED 流程...")
        result = self._request("POST", "/flows/restart")
        if result is None:
            return False

        print("  流程重启成功!")
        return True

    def list_nodes(self) -> Optional[Union[list, dict]]:
        """列出所有可用节点类型

        调用 GET /nodes 获取已安装的所有节点类型信息。

        Returns:
            节点类型列表
        """
        print_separator("列出可用节点")

        data = self._request("GET", "/nodes")
        if data is None:
            return None

        if not isinstance(data, list):
            print("  [警告] 返回数据格式异常")
            return data

        # 按模块分组
        module_map: Dict[str, List[dict]] = {}
        for node in data:
            module = node.get("module", "node-red")
            if module not in module_map:
                module_map[module] = []
            module_map[module].append(node)

        print(f"\n  共 {len(data)} 个节点类型, 来自 {len(module_map)} 个模块\n")

        # 显示每个模块的节点
        for module, nodes in sorted(module_map.items()):
            node_names = [n.get("name", n.get("id", "?")) for n in nodes]
            version = nodes[0].get("version", "") if nodes else ""
            ver_str = f" (v{version})" if version else ""
            print(f"  ┌─ {module}{ver_str} ({len(nodes)} 个节点) ─────")
            # 每行显示 3 个
            for i in range(0, len(node_names), 3):
                row = node_names[i:i + 3]
                line = "  │  " + "  |  ".join(f"{n:28s}" for n in row)
                print(line)
            print("  └──────────────────────────────────────────")

        return data

    def install_package(self, package_name: str) -> bool:
        """安装 Node-RED 节点包

        调用 POST /palette/{package} 安装指定节点包。

        Args:
            package_name: npm 包名（如 'node-red-contrib-xiaomi-miot'）

        Returns:
            是否成功
        """
        print_separator(f"安装节点包: {package_name}")

        path = f"/palette/{package_name}"
        print(f"\n  包名: {package_name}")
        print("  正在安装...")

        result = self._request("POST", path)
        if result is None:
            return False

        print(f"  节点包 '{package_name}' 安装成功!")
        print("  可能需要重启 Node-RED 以加载新节点")
        print("  运行 'restart' 命令或手动重启 Node-RED")
        return True

    # ═══════════════════════════════════════════════════
    #  演示功能
    # ═══════════════════════════════════════════════════

    def show_sample_flow(self) -> None:
        """展示示例流程"""
        print_separator("小米设备控制 — 示例流程")

        print(SAMPLE_FLOW_DIAGRAM)

        print("  流程说明:")
        print("  ┌──────────────────────────────────────────────────────────")
        print("  │  流程 1: 定时获取扫地机器人状态")
        print("  │    每 5 分钟触发一次，通过 miio 协议获取扫地机器人状态")
        print("  │    输出到 debug 节点查看")
        print("  │")
        print("  │  流程 2: MQTT 订阅 Zigbee 设备状态")
        print("  │    订阅 zigbee2mqtt 主题，接收所有 Zigbee 设备消息")
        print("  │    通过 Function 节点解析 JSON，提取设备信息和状态")
        print("  │")
        print("  │  流程 3: HTTP Webhook 控制小米设备")
        print("  │    接收 POST 请求 /mi-control，参数: device, command, params")
        print("  │    转换为 miIO 控制命令发送给设备")
        print("  │    示例: curl -X POST http://host:1880/mi-control \\")
        print("  │            -d '{\"device\":\"192.168.1.50\",")
        print('  │               "command\":\"app_start\"}')
        print("  └──────────────────────────────────────────────────────────")

        print(f"\n  导出此流程为 JSON:")
        print(f"    python {os.path.basename(__file__)} sample --json")

    def show_sample_flow_json(self) -> None:
        """展示示例流程 JSON"""
        print_separator("示例流程 JSON")
        print(pretty_json(SAMPLE_FLOW, indent=2))

    def show_recommended_packages(self) -> None:
        """展示推荐的小米相关 Node-RED 节点包"""
        print_separator("推荐的小米相关 Node-RED 节点包")

        for i, pkg in enumerate(RECOMMENDED_PACKAGES, 1):
            print(f"\n  [{i}] {pkg['name']}")
            print(f"      {pkg['description']}")
            print(f"      功能:")
            for feature in pkg["features"]:
                print(f"        - {feature}")
            print(f"      安装:")
            print(f"        {pkg['install']}")

        print(f"\n  提示:")
        print(f"  ┌──────────────────────────────────────────────────────────")
        print(f"  │  1. 安装前建议先备份 ~/.node-red/ 目录")
        print(f"  │  2. 部分节点需要 Node.js >= 12")
        print(f"  │  3. 安装后需要重启 Node-RED")
        print(f"  │  4. 可通过本脚本的 'install' 命令安装:")
        print(f"  │     python {os.path.basename(__file__)} install node-red-contrib-miio")
        print(f"  └──────────────────────────────────────────────────────────")


# ═══════════════════════════════════════════════════════
#  命令行参数解析
# ═══════════════════════════════════════════════════════

def parse_args() -> Any:
    """解析命令行参数

    Returns:
        解析后的参数对象
    """
    import argparse

    parser = argparse.ArgumentParser(
        description="Node-RED REST API 交互演示 — 管理流程和节点",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=f"""
示例:
  # 获取 Node-RED 状态
  python {os.path.basename(__file__)} status

  # 列出所有流程
  python {os.path.basename(__file__)} flows

  # 导出流程到文件
  python {os.path.basename(__file__)} export --output my_flows.json

  # 从文件导入流程
  python {os.path.basename(__file__)} import my_flows.json

  # 部署流程
  python {os.path.basename(__file__)} deploy

  # 列出可用节点
  python {os.path.basename(__file__)} nodes

  # 安装节点包
  python {os.path.basename(__file__)} install node-red-contrib-xiaomi-miot

  # 查看示例流程图
  python {os.path.basename(__file__)} sample

  # 导出示例流程 JSON
  python {os.path.basename(__file__)} sample --json

  # 查看推荐节点包
  python {os.path.basename(__file__)} packages

  # 指定主机和认证
  python {os.path.basename(__file__)} --host 192.168.1.100 --user admin --pass secret status
        """,
    )

    # 全局参数
    parser.add_argument("--host", default=SAMPLE_CONFIG["host"], help="Node-RED 主机地址（默认: localhost）")
    parser.add_argument("--port", type=int, default=SAMPLE_CONFIG["port"], help=f"Node-RED 端口（默认: {SAMPLE_CONFIG['port']}）")
    parser.add_argument("--user", default="", help="HTTP Basic Auth 用户名")
    parser.add_argument("--pass", dest="password", default="", help="HTTP Basic Auth 密码")
    parser.add_argument("--timeout", type=int, default=SAMPLE_CONFIG["timeout"], help=f"请求超时秒数（默认: {SAMPLE_CONFIG['timeout']}）")
    parser.add_argument("--ssl", action="store_true", help="使用 HTTPS")

    # 子命令
    subparsers = parser.add_subparsers(dest="command", help="命令")

    # status 命令
    subparsers.add_parser("status", help="获取 Node-RED 运行状态")

    # flows 命令
    subparsers.add_parser("flows", help="列出所有流程")

    # export 命令
    export_parser = subparsers.add_parser("export", help="导出流程为 JSON")
    export_parser.add_argument("--output", "-o", default=None, help="输出文件路径（默认打印到标准输出）")

    # import 命令
    import_parser = subparsers.add_parser("import", help="从 JSON 文件导入流程")
    import_parser.add_argument("file", help="JSON 流程文件路径")

    # deploy 命令
    subparsers.add_parser("deploy", help="部署当前流程")

    # restart 命令
    subparsers.add_parser("restart", help="重启 Node-RED 流程")

    # nodes 命令
    subparsers.add_parser("nodes", help="列出所有可用节点类型")

    # install 命令
    install_parser = subparsers.add_parser("install", help="安装 Node-RED 节点包")
    install_parser.add_argument("package", help="npm 包名（如 node-red-contrib-xiaomi-miot）")

    # sample 命令
    sample_parser = subparsers.add_parser("sample", help="展示示例流程")
    sample_parser.add_argument("--json", action="store_true", help="输出 JSON 格式（而非 ASCII 图）")

    # packages 命令
    subparsers.add_parser("packages", help="展示推荐的小米相关节点包")

    args = parser.parse_args()

    # 如果没有指定命令，显示帮助
    if not args.command:
        parser.print_help()
        sys.exit(0)

    return args


# ═══════════════════════════════════════════════════════
#  主入口
# ═══════════════════════════════════════════════════════

def main():
    """主入口函数"""
    args = parse_args()

    # 从环境变量读取认证信息（如果未通过参数指定）
    user = args.user or os.environ.get("NODE_RED_USER") or SAMPLE_CONFIG.get("user", "")
    password = args.password or os.environ.get("NODE_RED_PASS") or SAMPLE_CONFIG.get("password", "")

    # 创建客户端
    client = NodeRedClient(
        host=args.host,
        port=args.port,
        user=user,
        password=password,
        timeout=args.timeout,
        use_ssl=args.ssl,
    )

    print_separator("Node-RED REST API 交互演示", char="-")
    print(f"\n  连接: {client.base_url}")
    if client.user:
        print(f"  认证: {client.user}:***")

    try:
        if args.command == "status":
            result = client.get_status()
            if result is None:
                sys.exit(1)

        elif args.command == "flows":
            result = client.list_flows()
            if result is None:
                sys.exit(1)

        elif args.command == "export":
            result = client.export_flows(output_file=args.output)
            if result is None:
                sys.exit(1)

        elif args.command == "import":
            success = client.import_flows(args.file)
            if not success:
                sys.exit(1)

        elif args.command == "deploy":
            success = client.deploy()
            if not success:
                sys.exit(1)

        elif args.command == "restart":
            success = client.restart_flows()
            if not success:
                sys.exit(1)

        elif args.command == "nodes":
            result = client.list_nodes()
            if result is None:
                sys.exit(1)

        elif args.command == "install":
            success = client.install_package(args.package)
            if not success:
                sys.exit(1)

        elif args.command == "sample":
            if getattr(args, "json", False):
                client.show_sample_flow_json()
            else:
                client.show_sample_flow()

        elif args.command == "packages":
            client.show_recommended_packages()

        print()  # 结尾空行

    except KeyboardInterrupt:
        print("\n\n操作已取消")
    except Exception as e:
        log.error(f"操作出错: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
