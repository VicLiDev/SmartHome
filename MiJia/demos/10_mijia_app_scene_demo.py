"""
Demo 10: 米家 App 智能场景 → Webhook 回调服务器

功能:
  - 轻量级 HTTP 回调服务器（基于 stdlib http.server，无需 Flask）
  - 接收米家 App 智能场景触发的 HTTP POST 事件
  - 多端点支持: webhook、设备列表、设备控制、事件日志、健康检查
  - 内存事件存储（最多 1000 条，带时间戳）
  - Webhook → miIO 命令联动（与 Demo 01 集成）
  - TLS 支持说明及 openssl 自签名证书命令

架构:
  米家 App ──(HTTP POST)──→ Webhook 服务器 ──(miIO/脚本)──→ 智能设备
                                    │
                              事件日志存储

米家 App 智能场景设置示例:
  1. 温度传感器 > 28°C → 发送 HTTP POST 到 webhook
  2. 门窗传感器打开 → 发送 HTTP POST 到 webhook
  3. 按键按下 → 发送 HTTP POST 到 webhook
  4. 湿度 < 30% → 发送 HTTP POST 到 webhook

依赖: 仅 Python 标准库（无第三方依赖）

用法:
  # 启动 Webhook 服务器（默认 0.0.0.0:8080）
  python 10_mijia_app_scene_demo.py start
  python 10_mijia_app_scene_demo.py start --host 0.0.0.0 --port 9090

  # 发送测试 Webhook 事件
  python 10_mijia_app_scene_demo.py test
  python 10_mijia_app_scene_demo.py test --device temp_sensor_01 --action high_temp

  # 查看事件日志
  python 10_mijia_app_scene_demo.py events
  python 10_mijia_app_scene_demo.py events --limit 20

  # 测试用 curl 命令
  curl -X POST http://localhost:8080/webhook/mijia \
       -H "Content-Type: application/json" \
       -d '{"device":"temp_01","model":"cgllc.temp","action":"high_temp","value":28.5}'

  curl http://localhost:8080/devices
  curl http://localhost:8080/events?limit=10
  curl http://localhost:8080/health

  # TLS 支持（生成自签名证书）
  openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes
"""

import json
import sys
import os
import time
import threading
import urllib.parse
from datetime import datetime
from http.server import HTTPServer, BaseHTTPRequestHandler
from typing import Optional, Dict, List, Any


# ═══════════════════════════════════════════════════════
#  常量
# ═══════════════════════════════════════════════════════

DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 8080
MAX_EVENTS = 1000          # 最大事件存储数量
MIIO_DEMO_SCRIPT = "01_miio_local_demo.py"  # Demo 01 脚本路径


# ═══════════════════════════════════════════════════════
#  事件存储
# ═══════════════════════════════════════════════════════

class EventStore:
    """线程安全的事件日志存储

    最多保存 MAX_EVENTS 条事件记录，超过时自动丢弃最旧的记录。
    每条事件包含: 时间戳、来源 IP、事件数据。
    """

    def __init__(self, max_size: int = MAX_EVENTS):
        """初始化事件存储

        Args:
            max_size: 最大事件数量（默认 1000）
        """
        self._events: List[Dict[str, Any]] = []
        self._max_size = max_size
        self._lock = threading.Lock()

    def add(self, source_ip: str, event_data: Dict[str, Any]) -> Dict[str, Any]:
        """添加一条事件记录

        Args:
            source_ip: 事件来源 IP 地址
            event_data: 事件数据字典

        Returns:
            完整的事件记录（含时间戳和 ID）
        """
        record = {
            "id": len(self._events) + 1,
            "timestamp": datetime.now().isoformat(),
            "source_ip": source_ip,
            "data": event_data,
        }

        with self._lock:
            self._events.append(record)
            if len(self._events) > self._max_size:
                self._events = self._events[-self._max_size:]

        return record

    def get_recent(self, limit: int = 50) -> List[Dict[str, Any]]:
        """获取最近的事件记录

        Args:
            limit: 返回的最大事件数量

        Returns:
            事件记录列表（最新的在前）
        """
        with self._lock:
            return list(reversed(self._events[-limit:]))

    def clear(self) -> int:
        """清空所有事件

        Returns:
            被清除的事件数量
        """
        with self._lock:
            count = len(self._events)
            self._events.clear()
            return count

    @property
    def total_count(self) -> int:
        """当前存储的事件总数"""
        with self._lock:
            return len(self._events)


# ═══════════════════════════════════════════════════════
#  设备注册表
# ═══════════════════════════════════════════════════════

class DeviceRegistry:
    """设备注册表

    维护已知设备的列表，支持注册、查询和模拟控制。
    用于与 miIO 本地控制（Demo 01）集成。
    """

    def __init__(self):
        """初始化设备注册表，预置一些示例设备"""
        self._devices: Dict[str, Dict[str, Any]] = {
            "temp_sensor_01": {
                "name": "客厅温度传感器",
                "model": "cgllc.temp",
                "ip": "192.168.1.101",
                "status": "online",
            },
            "door_sensor_01": {
                "name": "大门门窗传感器",
                "model": "lumi.sensor_magnet",
                "ip": "",
                "status": "online",
            },
            "plug_01": {
                "name": "客厅智能插座",
                "model": "chuangmi.plug.m1",
                "ip": "192.168.1.102",
                "token": "ffffffffffffffffffffffffffffffff",
                "status": "online",
            },
            "light_01": {
                "name": "卧室台灯",
                "model": "yeelight.white",
                "ip": "192.168.1.103",
                "token": "ffffffffffffffffffffffffffffffff",
                "status": "online",
            },
        }
        self._lock = threading.Lock()

    def list_devices(self) -> List[Dict[str, Any]]:
        """获取所有已注册设备列表

        Returns:
            设备信息列表（不含 token）
        """
        with self._lock:
            result = []
            for did, info in self._devices.items():
                dev = dict(info)
                dev["device_id"] = did
                dev.pop("token", None)  # 安全: 不返回 token
                result.append(dev)
            return result

    def get_device(self, device_id: str) -> Optional[Dict[str, Any]]:
        """获取指定设备的信息

        Args:
            device_id: 设备 ID

        Returns:
            设备信息字典，不存在返回 None
        """
        with self._lock:
            return dict(self._devices.get(device_id, {})) or None

    def register(self, device_id: str, info: Dict[str, Any]) -> bool:
        """注册一个新设备

        Args:
            device_id: 设备 ID
            info: 设备信息

        Returns:
            是否注册成功
        """
        with self._lock:
            self._devices[device_id] = info
            return True

    def control(self, device_id: str, action: str) -> Dict[str, Any]:
        """模拟控制设备（可扩展为调用 miIO 命令）

        Args:
            device_id: 设备 ID
            action: 控制动作（如 "on", "off", "toggle"）

        Returns:
            控制结果字典
        """
        with self._lock:
            device = self._devices.get(device_id)

        if not device:
            return {
                "success": False,
                "error": f"设备 {device_id} 未注册",
            }

        # 记录控制状态
        if action in ("on", "off"):
            device["state"] = action
            return {
                "success": True,
                "device_id": device_id,
                "action": action,
                "state": action,
                "message": f"设备 {device.get('name', device_id)} 已{('开启' if action == 'on' else '关闭')}",
            }
        elif action == "toggle":
            current = device.get("state", "off")
            new_state = "off" if current == "on" else "on"
            device["state"] = new_state
            return {
                "success": True,
                "device_id": device_id,
                "action": "toggle",
                "state": new_state,
                "message": f"设备 {device.get('name', device_id)} 已切换为 {new_state}",
            }
        else:
            return {
                "success": True,
                "device_id": device_id,
                "action": action,
                "message": f"已发送动作 '{action}' 到设备 {device.get('name', device_id)}",
            }


# ═══════════════════════════════════════════════════════
#  Webhook 回调处理器
# ═══════════════════════════════════════════════════════

class MijiaActionHandler:
    """米家场景事件处理器

    处理来自米家 App 智能场景的 webhook 回调，
    并根据预设规则触发后续动作（如调用 miIO 控制设备）。
    """

    def __init__(self, event_store: EventStore, device_registry: DeviceRegistry):
        """初始化事件处理器

        Args:
            event_store: 事件存储实例
            device_registry: 设备注册表实例
        """
        self.event_store = event_store
        self.device_registry = device_registry

        # 场景联动规则: 事件条件 → 自动执行动作
        self.rules = [
            {
                "name": "高温开风扇",
                "condition": {"device_model": "cgllc.temp", "action": "high_temp"},
                "target_device": "plug_01",
                "target_action": "on",
                "description": "温度传感器 > 28°C → 开启智能插座（接风扇）",
            },
            {
                "name": "门窗打开报警",
                "condition": {"device_model": "lumi.sensor_magnet", "action": "open"},
                "target_device": None,
                "target_action": None,
                "description": "门窗传感器打开 → 记录日志（可扩展为推送通知）",
            },
            {
                "name": "按键开灯",
                "condition": {"action": "button_press"},
                "target_device": "light_01",
                "target_action": "toggle",
                "description": "按键按下 → 切换卧室台灯",
            },
        ]

    def process_webhook(self, source_ip: str, payload: Dict[str, Any]) -> Dict[str, Any]:
        """处理米家 App 发来的 webhook 事件

        流程:
          1. 解析并验证事件数据
          2. 存储事件到事件日志
          3. 匹配联动规则
          4. 执行对应的联动动作

        Args:
            source_ip: 来源 IP
            payload: Webhook POST 的 JSON 数据

        Returns:
            处理结果（含匹配的规则和执行结果）
        """
        # 存储事件
        record = self.event_store.add(source_ip, payload)

        # 匹配规则
        matched_rules = []
        executed_actions = []

        for rule in self.rules:
            cond = rule["condition"]
            # 检查条件是否匹配
            match = True
            if "device_model" in cond:
                if payload.get("model") != cond["device_model"]:
                    match = False
            if "action" in cond:
                if payload.get("action") != cond["action"]:
                    match = False

            if match:
                matched_rules.append(rule["name"])
                # 执行联动
                if rule["target_device"] and rule["target_action"]:
                    result = self.device_registry.control(
                        rule["target_device"], rule["target_action"]
                    )
                    executed_actions.append(result)

        return {
            "success": True,
            "event_id": record["id"],
            "matched_rules": matched_rules,
            "executed_actions": executed_actions,
            "message": f"收到事件，匹配 {len(matched_rules)} 条规则",
        }


# ═══════════════════════════════════════════════════════
#  HTTP 请求处理器
# ═══════════════════════════════════════════════════════

class WebhookHandler(BaseHTTPRequestHandler):
    """HTTP 请求处理器

    支持的端点:
      POST /webhook/mijia           — 接收米家 App 场景触发
      GET  /devices                  — 列出已注册设备
      POST /control/<device_id>/<action>  — 模拟控制设备
      GET  /events                   — 查看最近事件日志
      GET  /events?limit=N           — 查看最近 N 条事件
      GET  /health                   — 健康检查
    """

    # 类变量: 由服务器注入
    event_store: Optional[EventStore] = None
    device_registry: Optional[DeviceRegistry] = None
    action_handler: Optional[MijiaActionHandler] = None

    def log_message(self, format, *args):
        """自定义日志格式"""
        print(f"[{datetime.now().strftime('%H:%M:%S')}] {args[0]}")

    def _send_json(self, data: Any, status: int = 200):
        """发送 JSON 响应

        Args:
            data: 要返回的 Python 对象（会被序列化为 JSON）
            status: HTTP 状态码
        """
        body = json.dumps(data, ensure_ascii=False, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_body(self) -> bytes:
        """读取请求体"""
        length = int(self.headers.get("Content-Length", 0))
        return self.rfile.read(length)

    def do_GET(self):
        """处理 GET 请求"""
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path.rstrip("/")
        params = urllib.parse.parse_qs(parsed.query)

        if path == "/health":
            self._handle_health()
        elif path == "/devices":
            self._handle_list_devices()
        elif path == "/events":
            limit = int(params.get("limit", [50])[0])
            self._handle_get_events(limit)
        else:
            self._send_json({"error": "未知的端点", "path": path}, 404)

    def do_POST(self):
        """处理 POST 请求"""
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path.rstrip("/")

        if path == "/webhook/mijia":
            self._handle_webhook()
        elif path.startswith("/control/"):
            self._handle_control(path)
        else:
            self._send_json({"error": "未知的端点", "path": path}, 404)

    # ── 端点处理方法 ──

    def _handle_health(self):
        """健康检查端点 GET /health

        返回服务器状态信息，包括运行时间、事件数量、设备数量。
        """
        self._send_json({
            "status": "ok",
            "service": "mijia-webhook-server",
            "uptime": "running",
            "events_stored": self.event_store.total_count,
            "devices_registered": len(self.device_registry.list_devices()),
            "timestamp": datetime.now().isoformat(),
        })

    def _handle_list_devices(self):
        """设备列表端点 GET /devices

        返回所有已注册设备的列表。
        """
        devices = self.device_registry.list_devices()
        self._send_json({
            "total": len(devices),
            "devices": devices,
        })

    def _handle_get_events(self, limit: int = 50):
        """事件日志端点 GET /events?limit=N

        Args:
            limit: 返回的最大事件数量
        """
        events = self.event_store.get_recent(limit)
        self._send_json({
            "total": self.event_store.total_count,
            "returned": len(events),
            "events": events,
        })

    def _handle_webhook(self):
        """Webhook 接收端点 POST /webhook/mijia

        接收米家 App 智能场景的 HTTP POST 回调。
        请求体格式（JSON）:
          {
            "device": "temp_sensor_01",
            "model": "cgllc.temp",
            "action": "high_temp",
            "value": 28.5,
            "extra": {}
          }

        处理流程:
          1. 解析 JSON 请求体
          2. 存储事件到 EventStore
          3. 匹配联动规则
          4. 执行对应的自动动作
          5. 返回处理结果
        """
        try:
            body = self._read_body()
            payload = json.loads(body.decode("utf-8")) if body else {}
        except (json.JSONDecodeError, UnicodeDecodeError) as e:
            self._send_json({"error": f"无效的 JSON 数据: {e}"}, 400)
            return

        source_ip = self.client_address[0]
        print(f"[Webhook] 收到事件 from {source_ip}: {json.dumps(payload, ensure_ascii=False)}")

        result = self.action_handler.process_webhook(source_ip, payload)

        if result.get("executed_actions"):
            for action in result["executed_actions"]:
                print(f"[联动] {action.get('message', '')}")

        self._send_json(result)

    def _handle_control(self, path: str):
        """设备控制端点 POST /control/<device_id>/<action>

        Args:
            path: 请求路径，格式为 /control/<device_id>/<action>
        """
        parts = path.split("/")
        if len(parts) < 4:
            self._send_json({"error": "路径格式: /control/<device_id>/<action>"}, 400)
            return

        device_id = parts[2]
        action = parts[3]

        result = self.device_registry.control(device_id, action)
        status = 200 if result.get("success") else 404
        self._send_json(result, status)


# ═══════════════════════════════════════════════════════
#  Webhook 服务器
# ═══════════════════════════════════════════════════════

class WebhookCallbackServer:
    """米家 App 智能场景 Webhook 回调服务器

    使用 Python 标准库 http.server 实现，无需 Flask 等第三方依赖。
    提供 HTTP REST API 接收米家 App 智能场景触发事件，
    并支持联动控制其他智能设备。

    使用示例:
        server = WebhookCallbackServer(host="0.0.0.0", port=8080)
        server.start()  # 阻塞运行

    TLS 启用:
        # 1. 生成自签名证书
        openssl req -x509 -newkey rsa:2048 \\
            -keyout key.pem -out cert.pem \\
            -days 365 -nodes

        # 2. 使用 HTTPS 启动
        server = WebhookCallbackServer(ssl_cert="cert.pem", ssl_key="key.pem")
        server.start()
    """

    def __init__(
        self,
        host: str = DEFAULT_HOST,
        port: int = DEFAULT_PORT,
        ssl_cert: Optional[str] = None,
        ssl_key: Optional[str] = None,
    ):
        """初始化 Webhook 服务器

        Args:
            host: 监听地址（默认 0.0.0.0）
            port: 监听端口（默认 8080）
            ssl_cert: TLS 证书文件路径（可选）
            ssl_key: TLS 私钥文件路径（可选）
        """
        self.host = host
        self.port = port
        self.ssl_cert = ssl_cert
        self.ssl_key = ssl_key

        # 初始化组件
        self.event_store = EventStore()
        self.device_registry = DeviceRegistry()
        self.action_handler = MijiaActionHandler(self.event_store, self.device_registry)

        # 注入到 Handler 类
        WebhookHandler.event_store = self.event_store
        WebhookHandler.device_registry = self.device_registry
        WebhookHandler.action_handler = self.action_handler

        self._server: Optional[HTTPServer] = None

    def start(self):
        """启动 Webhook 服务器（阻塞运行）

        支持 TLS: 如果提供了 ssl_cert 和 ssl_key，将自动启用 HTTPS。
        """
        self._server = HTTPServer((self.host, self.port), WebhookHandler)

        # TLS 支持
        if self.ssl_cert and self.ssl_key:
            import ssl
            ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            ctx.load_cert_chain(self.ssl_cert, self.ssl_key)
            self._server.socket = ctx.wrap_socket(
                self._server.socket, server_side=True
            )
            print(f"[TLS] 已启用 HTTPS (证书: {self.ssl_cert})")

        scheme = "https" if self.ssl_cert else "http"
        print(f"\n{'━' * 60}")
        print(f"  米家 App 场景 Webhook 回调服务器")
        print(f"{'━' * 60}")
        print(f"  监听地址: {scheme}://{self.host}:{self.port}")
        print(f"  Webhook:  {scheme}://{self.host}:{self.port}/webhook/mijia")
        print(f"  设备列表: {scheme}://{self.host}:{self.port}/devices")
        print(f"  事件日志: {scheme}://{self.host}:{self.port}/events")
        print(f"  健康检查: {scheme}://{self.host}:{self.port}/health")
        print(f"{'━' * 60}")
        print(f"  已注册 {len(self.device_registry.list_devices())} 个设备")
        print(f"  联动规则: {len(self.action_handler.rules)} 条")
        print(f"{'━' * 60}\n")
        print("等待米家 App 场景触发... (Ctrl+C 停止)\n")

        try:
            self._server.serve_forever()
        except KeyboardInterrupt:
            print("\n\n服务器已停止。")
        finally:
            self._server.server_close()

    def stop(self):
        """停止服务器"""
        if self._server:
            self._server.shutdown()


# ═══════════════════════════════════════════════════════
#  miIO 联动示例
# ═══════════════════════════════════════════════════════

def send_test_webhook(
    host: str = "localhost",
    port: int = DEFAULT_PORT,
    device: str = "temp_sensor_01",
    action: str = "high_temp",
    value: Any = 28.5,
):
    """发送测试 webhook 事件到服务器

    模拟米家 App 场景触发发送 HTTP POST 到 webhook。

    Args:
        host: 服务器地址
        port: 服务器端口
        device: 设备 ID
        action: 触发动作
        value: 触发值
    """
    import urllib.request

    url = f"http://{host}:{port}/webhook/mijia"
    payload = json.dumps({
        "device": device,
        "model": "cgllc.temp",
        "action": action,
        "value": value,
        "timestamp": datetime.now().isoformat(),
        "source": "mijia_app_scene",
    }).encode("utf-8")

    req = urllib.request.Request(
        url,
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )

    print(f"发送测试 Webhook 到 {url}")
    print(f"数据: {payload.decode('utf-8')}\n")

    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            result = json.loads(resp.read().decode("utf-8"))
            print("服务器响应:")
            print(json.dumps(result, indent=2, ensure_ascii=False))
            return result
    except Exception as e:
        print(f"请求失败（服务器可能未启动）: {e}")
        return None


def show_miio_integration_example():
    """展示 Webhook → miIO 联动集成示例

    说明如何将 webhook 事件与 Demo 01 的 miIO 本地控制集成，
    实现米家 App 场景触发 → 自动 miIO 控制设备。
    """
    print("""
╔══════════════════════════════════════════════════════════════╗
║          Webhook → miIO 联动集成说明                          ║
╠══════════════════════════════════════════════════════════════╣
║                                                              ║
║  架构:                                                       ║
║                                                              ║
║    米家 App ──(场景触发)──→ Webhook 服务器 ──(subprocess)──→  ║
║                              │                    miIO 控制   ║
║                         匹配联动规则           (Demo 01)     ║
║                              │                    │          ║
║                        执行设备控制 ←────────────┘          ║
║                                                              ║
║  扩展 MijiaActionHandler.process_webhook():                  ║
║                                                              ║
║    # 在 matched rule 执行处添加:                              ║
║    import subprocess                                          ║
║    result = subprocess.run([                                  ║
║        "python", "01_miio_local_demo.py", "power",           ║
║        device_ip, device_token, "on"                          ║
║    ], capture_output=True, text=True)                         ║
║                                                              ║
║  注意事项:                                                    ║
║    • 确保 01_miio_local_demo.py 在同一目录                    ║
║    • 需要设备的 IP 和 Token（在 DeviceRegistry 中配置）        ║
║    • miIO 控制仅适用于局域网设备                               ║
║    • subprocess 方式适合低频触发，高频场景建议用异步            ║
╚══════════════════════════════════════════════════════════════╝
""")


def show_tls_instructions():
    """展示 TLS/HTTPS 配置说明"""
    print("""
╔══════════════════════════════════════════════════════════════╗
║          TLS/HTTPS 配置说明                                  ║
╠══════════════════════════════════════════════════════════════╣
║                                                              ║
║  1. 生成自签名证书:                                           ║
║                                                              ║
║     openssl req -x509 -newkey rsa:2048 \\                     ║
║         -keyout key.pem -out cert.pem \\                     ║
║         -days 365 -nodes                                     ║
║                                                              ║
║  2. 启动 HTTPS 服务器:                                       ║
║                                                              ║
║     python 10_mijia_app_scene_demo.py start --ssl             ║
║                                                              ║
║  3. 测试 HTTPS Webhook:                                       ║
║                                                              ║
║     curl -k -X POST https://localhost:8080/webhook/mijia \\   ║
║          -H "Content-Type: application/json" \\               ║
║          -d '{"device":"temp_01","action":"high_temp"}'      ║
║                                                              ║
║  注意:                                                        ║
║    • -k 参数忽略证书验证（自签名证书需要）                     ║
║    • 生产环境建议使用 Let's Encrypt 等正规证书                 ║
║    • 米家 App 场景中配置 Webhook URL 时需要用 https://         ║
║    • 如果米家 App 不支持自签名证书，可用 Nginx 反向代理         ║
╚══════════════════════════════════════════════════════════════╝
""")


def show_mijia_app_setup():
    """展示米家 App 智能场景配置指南"""
    print("""
╔══════════════════════════════════════════════════════════════╗
║          米家 App 智能场景配置指南                             ║
╠══════════════════════════════════════════════════════════════╣
║                                                              ║
║  场景 1: 温度超限自动开风扇                                   ║
║  ─────────────────────────────                               ║
║  步骤:                                                       ║
║    1. 打开米家 App → 智能场景 → 添加场景                      ║
║    2. 条件: 温度传感器 > 28°C                                 ║
║    3. 动作: 网络请求 → HTTP POST                              ║
║    4. URL: http://<你的IP>:8080/webhook/mijia                ║
║    5. Body:                                                   ║
║       {                                                       ║
║         "device": "temp_sensor_01",                           ║
║         "model": "cgllc.temp",                                ║
║         "action": "high_temp",                                ║
║         "value": 28.5                                         ║
║       }                                                       ║
║                                                              ║
║  场景 2: 门窗打开报警                                         ║
║  ─────────────────────────────                               ║
║  步骤:                                                       ║
║    1. 条件: 门窗传感器 → 打开                                 ║
║    2. 动作: 网络请求 → HTTP POST                              ║
║    3. URL: http://<你的IP>:8080/webhook/mijia                ║
║    4. Body:                                                   ║
║       {                                                       ║
║         "device": "door_sensor_01",                           ║
║         "model": "lumi.sensor_magnet",                        ║
║         "action": "open"                                      ║
║       }                                                       ║
║                                                              ║
║  场景 3: 按键切换灯光                                         ║
║  ─────────────────────────────                               ║
║  步骤:                                                       ║
║    1. 条件: 无线按键 → 单击                                   ║
║    2. 动作: 网络请求 → HTTP POST                              ║
║    3. URL: http://<你的IP>:8080/webhook/mijia                ║
║    4. Body:                                                   ║
║       {                                                       ║
║         "device": "button_01",                                ║
║         "model": "lumi.sensor_switch",                        ║
║         "action": "button_press"                              ║
║       }                                                       ║
║                                                              ║
║  场景 4: 湿度过低开启加湿器                                   ║
║  ─────────────────────────────                               ║
║  步骤:                                                       ║
║    1. 条件: 温湿度传感器 → 湿度 < 30%                         ║
║    2. 动作: 网络请求 → HTTP POST                              ║
║    3. Body:                                                   ║
║       {                                                       ║
║         "device": "humid_sensor_01",                          ║
║         "model": "cgllc.ht",                                  ║
║         "action": "low_humidity",                             ║
║         "value": 28                                           ║
║       }                                                       ║
║                                                              ║
║  注意事项:                                                    ║
║    • 米家 App 的"网络请求"功能需要设备在线                     ║
║    • 某些旧版固件可能不支持"发送网络请求"动作                   ║
║    • Webhook URL 必须是公网可达地址（用 ngrok/frp 内网穿透）    ║
║    • 建议使用 HTTPS（参考 TLS 配置说明）                       ║
╚══════════════════════════════════════════════════════════════╝
""")


# ═══════════════════════════════════════════════════════
#  CLI 入口
# ═══════════════════════════════════════════════════════

def print_separator(title: str = "", char: str = "━", width: int = 60):
    """打印分隔线"""
    if title:
        padding = width - len(title) - 2
        left = padding // 2
        right = padding - left
        print(f"\n{char * left} {title} {char * right}")
    else:
        print(char * width)


def cmd_start(args):
    """启动 Webhook 服务器"""
    host = DEFAULT_HOST
    port = DEFAULT_PORT
    ssl_cert = None
    ssl_key = None

    i = 0
    while i < len(args):
        if args[i] == "--host" and i + 1 < len(args):
            host = args[i + 1]
            i += 2
        elif args[i] == "--port" and i + 1 < len(args):
            port = int(args[i + 1])
            i += 2
        elif args[i] == "--ssl":
            ssl_cert = "cert.pem"
            ssl_key = "key.pem"
            i += 1
        else:
            i += 1

    server = WebhookCallbackServer(
        host=host,
        port=port,
        ssl_cert=ssl_cert,
        ssl_key=ssl_key,
    )
    server.start()


def cmd_test(args):
    """发送测试 Webhook"""
    host = "localhost"
    port = DEFAULT_PORT
    device = "temp_sensor_01"
    action = "high_temp"
    value = 28.5

    i = 0
    while i < len(args):
        if args[i] == "--host" and i + 1 < len(args):
            host = args[i + 1]
            i += 2
        elif args[i] == "--port" and i + 1 < len(args):
            port = int(args[i + 1])
            i += 2
        elif args[i] == "--device" and i + 1 < len(args):
            device = args[i + 1]
            i += 2
        elif args[i] == "--action" and i + 1 < len(args):
            action = args[i + 1]
            i += 2
        else:
            i += 1

    print_separator("测试 Webhook 事件")
    send_test_webhook(host, port, device, action, value)


def cmd_events(args):
    """查看事件日志"""
    import urllib.request

    host = "localhost"
    port = DEFAULT_PORT
    limit = 50

    i = 0
    while i < len(args):
        if args[i] == "--host" and i + 1 < len(args):
            host = args[i + 1]
            i += 2
        elif args[i] == "--port" and i + 1 < len(args):
            port = int(args[i + 1])
            i += 2
        elif args[i] == "--limit" and i + 1 < len(args):
            limit = int(args[i + 1])
            i += 2
        else:
            i += 1

    print_separator("事件日志")
    url = f"http://{host}:{port}/events?limit={limit}"

    try:
        with urllib.request.urlopen(url, timeout=5) as resp:
            data = json.loads(resp.read().decode("utf-8"))
            print(f"总事件数: {data['total']}")
            print(f"返回条数: {data['returned']}\n")
            for evt in data["events"]:
                print(f"  #{evt['id']} [{evt['timestamp']}] "
                      f"from {evt['source_ip']}")
                print(f"       {json.dumps(evt['data'], ensure_ascii=False)}")
    except Exception as e:
        print(f"请求失败（服务器可能未启动）: {e}")


def cmd_guide(args):
    """显示配置指南"""
    which = args[0] if args else "all"
    if which == "mijia":
        show_mijia_app_setup()
    elif which == "tls":
        show_tls_instructions()
    elif which == "miio":
        show_miio_integration_example()
    else:
        show_mijia_app_setup()
        show_tls_instructions()
        show_miio_integration_example()


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
        "start": cmd_start,
        "test": cmd_test,
        "events": cmd_events,
        "guide": cmd_guide,
    }

    if cmd in commands:
        commands[cmd](args)
    else:
        print(f"未知命令: {cmd}")
        print(f"可用命令: {', '.join(commands.keys())}")
