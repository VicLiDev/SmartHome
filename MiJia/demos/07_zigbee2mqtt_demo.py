"""
Demo 7: Zigbee2MQTT 交互 — 通过 MQTT 管理 Zigbee 设备

功能:
  - 列出所有已配对的 Zigbee 设备
  - 获取设备实时状态
  - 控制设备（开/关灯、调节亮度等）
  - 开启/关闭配对模式（允许新设备加入网络）
  - 移除设备
  - 健康检查（查看 Zigbee2MQTT 桥接状态）
  - 展示 Home Assistant 自动发现 Topic

架构:
  本脚本 ←(MQTT)→ Zigbee2MQTT ←(Zigbee)→ Zigbee 设备（灯泡、传感器、开关等）

Zigbee2MQTT Topic 结构:
  zigbee2mqtt/bridge/info                 — 桥接信息
  zigbee2mqtt/bridge/state                 — 桥接状态 (online/offline)
  zigbee2mqtt/bridge/config/devices        — 已配对设备列表
  zigbee2mqtt/bridge/config/devices/get    — 请求设备列表
  zigbee2mqtt/bridge/config/permit_join    — 配对模式状态
  zigbee2mqtt/bridge/request/permit_join   — 开启/关闭配对
  zigbee2mqtt/bridge/request/device/remove — 移除设备
  zigbee2mqtt/{friendly_name}              — 设备状态
  zigbee2mqtt/{friendly_name}/set          — 控制设备
  zigbee2mqtt/{friendly_name}/get          — 请求设备状态

Home Assistant 自动发现:
  homeassistant/{component}/{object_id}/config — HA 发现配置

前置条件:
  1. 已安装并运行 Zigbee2MQTT: https://www.zigbee2mqtt.io/
  2. 已安装 MQTT Broker（如 Mosquitto）
  3. Zigbee 适配器已连接

依赖: pip install paho-mqtt

用法:
  # 列出所有设备
  python 07_zigbee2mqtt_demo.py list

  # 获取设备状态
  python 07_zigbee2mqtt_demo.py state --device living_room_light

  # 控制设备 — 开灯
  python 07_zigbee2mqtt_demo.py set --device living_room_light --payload '{"state":"ON"}'

  # 控制设备 — 设置亮度
  python 07_zigbee2mqtt_demo.py set --device living_room_light --payload '{"state":"ON","brightness":128}'

  # 开启配对模式（60秒）
  python 07_zigbee2mqtt_demo.py permit_join --enable --time 60

  # 关闭配对模式
  python 07_zigbee2mqtt_demo.py permit_join --disable

  # 移除设备
  python 07_zigbee2mqtt_demo.py remove --device living_room_light --force

  # 健康检查
  python 07_zigbee2mqtt_demo.py health

  # 查看 HA 自动发现 Topic
  python 07_zigbee2mqtt_demo.py ha_discovery --device living_room_light

  # 指定 Broker
  python 07_zigbee2mqtt_demo.py --broker 192.168.1.100 --port 1883 list

  # 指定 Base Topic（非默认 zigbee2mqtt）
  python 07_zigbee2mqtt_demo.py --base-topic z2m list
"""

import json
import time
import sys
import logging
from typing import Optional

try:
    import paho.mqtt.client as mqtt
except ImportError:
    mqtt = None


# ═══════════════════════════════════════════════════════
#  日志配置
# ═══════════════════════════════════════════════════════

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("zigbee2mqtt_demo")


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
    "broker": "localhost",          # MQTT Broker 地址
    "port": 1883,                   # MQTT Broker 端口
    "username": None,               # MQTT 用户名（可选）
    "password": None,               # MQTT 密码（可选）
    "base_topic": "zigbee2mqtt",    # Zigbee2MQTT 基础 Topic
    "timeout": 10,                  # 操作超时（秒）
    "ha_discovery_prefix": "homeassistant",  # HA 自动发现 Topic 前缀
    # 常用设备控制 payload 示例
    "payload_examples": {
        "light_on": {"state": "ON"},
        "light_off": {"state": "OFF"},
        "light_brightness": {"state": "ON", "brightness": 128},
        "light_color_temp": {"state": "ON", "color_temp": 350},
        "light_color": {"state": "ON", "color": {"x": 0.3, "y": 0.3}},
        "switch_on": {"state": "ON"},
        "switch_off": {"state": "OFF"},
        "lock": {"state": "LOCK"},
        "unlock": {"state": "UNLOCK"},
        "cover_open": {"state": "OPEN"},
        "cover_close": {"state": "CLOSE"},
        "cover_stop": {"state": "STOP"},
        "cover_position": {"position": 50},
        "fan_on": {"state": "ON"},
        "fan_mode": {"state": "ON", "fan_mode": "on"},
        "thermostat_hvac": {"hvac_mode": "heat"},
        "thermostat_temp": {"current_heating_setpoint": 22.5},
    },
}


# ═══════════════════════════════════════════════════════
#  Zigbee2MQTT 客户端
# ═══════════════════════════════════════════════════════

class Zigbee2MQTTClient:
    """Zigbee2MQTT MQTT 客户端（同步 API）"""

    # MQTT 连接返回码
    RC_MAP = {
        0: "连接成功",
        1: "协议版本不正确",
        2: "客户端标识符无效",
        3: "Broker 不可用",
        4: "用户名或密码错误",
        5: "未授权",
    }

    def __init__(
        self,
        broker: str = "localhost",
        port: int = 1883,
        username: Optional[str] = None,
        password: Optional[str] = None,
        base_topic: str = "zigbee2mqtt",
        timeout: int = 10,
    ):
        """初始化 Zigbee2MQTT 客户端

        Args:
            broker: MQTT Broker 地址
            port: MQTT Broker 端口
            username: MQTT 认证用户名（可选）
            password: MQTT 认证密码（可选）
            base_topic: Zigbee2MQTT 基础 Topic（默认 zigbee2mqtt）
            timeout: 操作超时时间（秒）
        """
        self.broker = broker
        self.port = port
        self.base_topic = base_topic
        self.timeout = timeout
        self.client: Optional[mqtt.Client] = None
        self._connected = False
        self._received_messages = []   # 存储接收到的消息
        self._message_event = None     # threading.Event 用于等待消息

    # ─── Topic 构建辅助方法 ───

    def topic(self, *parts: str) -> str:
        """构建完整的 MQTT Topic"""
        return "/".join([self.base_topic] + list(parts))

    def device_topic(self, friendly_name: str) -> str:
        """构建设备状态 Topic"""
        return self.topic(friendly_name)

    def device_set_topic(self, friendly_name: str) -> str:
        """构建设备控制 Topic"""
        return self.topic(friendly_name, "set")

    def device_get_topic(self, friendly_name: str) -> str:
        """构建设备状态请求 Topic"""
        return self.topic(friendly_name, "get")

    def bridge_devices_topic(self) -> str:
        """桥接设备列表 Topic"""
        return self.topic("bridge", "devices")

    def bridge_devices_get_topic(self) -> str:
        """桥接设备列表请求 Topic"""
        return self.topic("bridge", "config", "devices", "get")

    def bridge_state_topic(self) -> str:
        """桥接状态 Topic"""
        return self.topic("bridge", "state")

    def bridge_request_topic(self, action: str) -> str:
        """桥接请求 Topic（permit_join, device/remove 等）"""
        return self.topic("bridge", "request", action)

    def bridge_info_topic(self) -> str:
        """桥接信息 Topic"""
        return self.topic("bridge", "info")

    # ─── MQTT 连接管理 ───

    def connect(self) -> bool:
        """连接到 MQTT Broker

        Returns:
            bool: 是否连接成功
        """
        import threading

        if self._connected and self.client and self.client.is_connected():
            return True

        self._message_event = threading.Event()
        self._received_messages.clear()

        # 创建 MQTT 客户端（兼容 v1 和 v2 API）
        try:
            self.client = mqtt.Client(
                client_id=f"z2m_demo_{int(time.time())}",
                protocol=mqtt.MQTTv311,
                clean_session=True,
            )
        except TypeError:
            # paho-mqtt v2 的 API 变化
            self.client = mqtt.Client(
                client_id=f"z2m_demo_{int(time.time())}",
                protocol=mqtt.MQTTv311,
            )

        if self.broker_username:
            assert self.client is not None, "MQTT client 未创建"
            self.client.username_pw_set(self.broker_username, self.broker_password)

        assert self.client is not None, "MQTT client 未创建"
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message

        log.info(f"正在连接 MQTT Broker: {self.broker}:{self.port} ...")
        try:
            self.client.connect(self.broker, self.port, keepalive=60)
            self.client.loop_start()

            # 等待连接回调
            start = time.time()
            while time.time() - start < self.timeout:
                if self._connected:
                    log.info("MQTT 连接成功")
                    return True
                time.sleep(0.1)

            log.error("MQTT 连接超时")
            return False
        except Exception as e:
            log.error(f"MQTT 连接失败: {e}")
            return False

    def disconnect(self):
        """断开 MQTT 连接"""
        if self.client:
            self.client.loop_stop()
            self.client.disconnect()
            self._connected = False
            log.info("已断开 MQTT 连接")

    @property
    def broker_username(self) -> Optional[str]:
        return getattr(self, '_username', SAMPLE_CONFIG["username"])

    @property
    def broker_password(self) -> Optional[str]:
        return getattr(self, '_password', SAMPLE_CONFIG["password"])

    def set_credentials(self, username: Optional[str], password: Optional[str]):
        """设置 MQTT 认证凭据"""
        self._username = username
        self._password = password

    def _on_connect(self, client, userdata, flags, rc, properties=None):
        """MQTT 连接回调"""
        if rc == 0:
            self._connected = True
            log.info("MQTT 已连接")
        else:
            reason = self.RC_MAP.get(rc, f"未知错误 (rc={rc})")
            log.error(f"MQTT 连接失败: {reason}")

    def _on_message(self, client, userdata, msg):
        """MQTT 消息回调 — 将消息存入缓冲区"""
        try:
            payload_str = msg.payload.decode("utf-8")
            try:
                payload = json.loads(payload_str)
            except json.JSONDecodeError:
                payload = payload_str
        except Exception:
            payload = msg.payload

        self._received_messages.append({
            "topic": msg.topic,
            "payload": payload,
            "timestamp": time.time(),
        })

        if self._message_event:
            self._message_event.set()

    def _wait_for_message(self, topic_filter: Optional[str] = None, timeout: Optional[float] = None) -> Optional[dict]:
        """等待一条消息

        Args:
            topic_filter: Topic 过滤（前缀匹配），None 则等待任意消息
            timeout: 超时时间（秒），None 则使用默认超时

        Returns:
            匹配的消息字典，超时返回 None
        """
        if timeout is None:
            timeout = self.timeout

        deadline = time.time() + timeout
        while time.time() < deadline:
            # 检查缓冲区中是否已有匹配的消息
            for i, msg in enumerate(self._received_messages):
                if topic_filter is None or msg["topic"].startswith(topic_filter):
                    return self._received_messages.pop(i)

            # 清除 event 并等待新消息
            if self._message_event:
                self._message_event.clear()

            remaining = deadline - time.time()
            if remaining > 0 and self._message_event:
                self._message_event.wait(timeout=min(remaining, 1.0))
            else:
                time.sleep(0.1)

        return None

    def _publish_and_wait(
        self,
        topic: str,
        payload,
        subscribe_topic: str,
        timeout: float = None,
    ) -> Optional[dict]:
        """发布消息并等待回复

        Args:
            topic: 发布的 Topic
            payload: 发布的内容（字典或字符串）
            subscribe_topic: 订阅等待的 Topic
            timeout: 超时时间

        Returns:
            收到的回复消息字典，超时返回 None
        """
        if not self._connected:
            log.error("未连接到 MQTT Broker")
            return None

        # 清空旧消息
        self._received_messages.clear()
        if self._message_event:
            self._message_event.clear()

        # 订阅回复 Topic
        self.client.subscribe(subscribe_topic)
        log.debug(f"订阅: {subscribe_topic}")

        # 发布消息
        if isinstance(payload, dict):
            msg_str = json.dumps(payload)
        else:
            msg_str = str(payload)

        result = self.client.publish(topic, msg_str)
        result.wait_for_publish(timeout=5)
        log.info(f"发布: {topic} → {msg_str[:200]}")

        # 等待回复
        msg = self._wait_for_message(subscribe_topic, timeout=timeout)
        return msg

    # ═══════════════════════════════════════════════════
    #  业务功能
    # ═══════════════════════════════════════════════════

    def list_devices(self) -> list:
        """列出所有已配对的 Zigbee 设备

        通过发送 get 请求到 bridge/config/devices/get 获取设备列表，
        然后订阅 zigbee2mqtt/bridge/devices 接收回复。

        Returns:
            设备列表（字典列表）
        """
        print_separator("列出所有 Zigbee 设备")

        # 订阅设备列表回复 Topic
        self.client.subscribe(self.bridge_devices_topic())
        self._received_messages.clear()

        # 发送请求
        get_topic = self.bridge_devices_get_topic()
        self.client.publish(get_topic, "")
        log.info(f"已发送设备列表请求: {get_topic}")

        # 等待回复
        msg = self._wait_for_message(self.bridge_devices_topic(), timeout=15)
        if msg is None:
            print("  [错误] 未收到设备列表回复")
            print("  请确认 Zigbee2MQTT 正在运行")
            return []

        devices = msg["payload"]
        if not isinstance(devices, list):
            # 旧版 Zigbee2MQTT 可能返回不同格式
            if isinstance(devices, dict):
                devices = devices.get("data", [])
            else:
                print(f"  [错误] 意外的回复格式: {type(devices)}")
                return []

        print(f"\n  共 {len(devices)} 个设备:\n")

        # 按类型分组统计
        type_counts = {}
        for dev in devices:
            dev_type = dev.get("type", "unknown")
            type_counts[dev_type] = type_counts.get(dev_type, 0) + 1

        print("  ┌─ 设备类型统计 ─────────────────────────")
        for dev_type, count in sorted(type_counts.items()):
            print(f"  │  {dev_type}: {count} 个")
        print("  └────────────────────────────────────────\n")

        # 打印每个设备的基本信息
        for i, dev in enumerate(devices):
            friendly_name = dev.get("friendly_name", "未知")
            model = dev.get("model", "未知")
            ieee_address = dev.get("ieee_address", "未知")
            dev_type = dev.get("type", "未知")
            manufacturer = dev.get("manufacturer", "未知")
            is_available = dev.get("available", None)

            # 状态标记
            if is_available is True:
                status_icon = "●"
            elif is_available is False:
                status_icon = "○"
            else:
                status_icon = "-"

            print(f"  [{i + 1:2d}] {status_icon} {friendly_name}")
            print(f"       型号: {model}")
            print(f"       厂商: {manufacturer}")
            print(f"       类型: {dev_type}")
            print(f"       IEEE: {ieee_address}")
            print()

        return devices

    def get_state(self, friendly_name: str) -> Optional[dict]:
        """获取设备实时状态

        订阅 zigbee2mqtt/{friendly_name}，然后发送空消息到
        zigbee2mqtt/{friendly_name}/get 主动获取最新状态。

        Args:
            friendly_name: 设备的友好名称

        Returns:
            设备状态字典，失败返回 None
        """
        print_separator(f"获取设备状态: {friendly_name}")

        state_topic = self.device_topic(friendly_name)
        get_topic = self.device_get_topic(friendly_name)

        # 订阅状态 Topic
        self.client.subscribe(state_topic)
        self._received_messages.clear()

        # 主动请求状态
        self.client.publish(get_topic, "")
        log.info(f"已发送状态请求: {get_topic}")

        # 等待回复
        msg = self._wait_for_message(state_topic, timeout=self.timeout)
        if msg is None:
            print(f"  [错误] 未收到设备 '{friendly_name}' 的状态")
            return None

        state = msg["payload"]
        if isinstance(state, str):
            try:
                state = json.loads(state)
            except json.JSONDecodeError:
                print(f"  状态（文本）: {state}")
                return {"raw": state}

        print(f"  设备: {friendly_name}")
        print(f"  状态: {pretty_json(state)}")
        return state

    def set_state(self, friendly_name: str, payload: dict) -> bool:
        """控制设备状态

        发布 JSON 消息到 zigbee2mqtt/{friendly_name}/set。

        Args:
            friendly_name: 设备的友好名称
            payload: 控制命令（字典），例如 {"state": "ON"}

        Returns:
            bool: 是否发布成功
        """
        print_separator(f"控制设备: {friendly_name}")

        set_topic = self.device_set_topic(friendly_name)

        # 短暂订阅状态变化以确认
        state_topic = self.device_topic(friendly_name)
        self.client.subscribe(state_topic)
        self._received_messages.clear()

        # 发布控制命令
        msg_str = json.dumps(payload, ensure_ascii=False)
        result = self.client.publish(set_topic, msg_str)
        result.wait_for_publish(timeout=5)
        log.info(f"发布控制命令: {set_topic} → {msg_str}")

        if result.rc != mqtt.MQTT_ERR_SUCCESS:
            print(f"  [错误] 发布失败，错误码: {result.rc}")
            return False

        print(f"  设备: {friendly_name}")
        print(f"  命令: {pretty_json(payload)}")
        print("  已发送 ✓")

        # 等待状态更新确认（可选，非阻塞）
        confirm_msg = self._wait_for_message(state_topic, timeout=5)
        if confirm_msg:
            confirm_state = confirm_msg["payload"]
            if isinstance(confirm_state, str):
                try:
                    confirm_state = json.loads(confirm_state)
                except json.JSONDecodeError:
                    pass
            print(f"  确认状态: {pretty_json(confirm_state) if isinstance(confirm_state, dict) else confirm_state}")

        return True

    def permit_join(self, enable: bool, time_s: int = 60) -> bool:
        """开启或关闭 Zigbee 配对模式

        发布到 zigbee2mqtt/bridge/request/permit_join 控制配对模式。
        开启配对后，新设备可以加入 Zigbee 网络。

        Args:
            enable: True 开启配对，False 关闭配对
            time_s: 配对持续时间（秒），仅开启时有效

        Returns:
            bool: 是否操作成功
        """
        action = "开启" if enable else "关闭"
        print_separator(f"{action} Zigbee 配对模式")

        request_topic = self.bridge_request_topic("permit_join")
        reply_topic = self.topic("bridge", "response", "permit_join")

        payload = {"value": enable}
        if enable:
            payload["time"] = time_s

        # 订阅回复
        self.client.subscribe(reply_topic)
        self._received_messages.clear()

        # 发送请求
        msg_str = json.dumps(payload, ensure_ascii=False)
        result = self.client.publish(request_topic, msg_str)
        result.wait_for_publish(timeout=5)
        log.info(f"发布配对请求: {request_topic} → {msg_str}")

        if result.rc != mqtt.MQTT_ERR_SUCCESS:
            print(f"  [错误] 发布失败")
            return False

        print(f"  配对模式: {'开启' if enable else '关闭'}")
        if enable:
            print(f"  持续时间: {time_s} 秒")
        print("  请求已发送 ✓")

        # 等待确认回复
        reply = self._wait_for_message(reply_topic, timeout=10)
        if reply:
            resp = reply["payload"]
            if isinstance(resp, str):
                try:
                    resp = json.loads(resp)
                except json.JSONDecodeError:
                    pass
            print(f"  回复: {pretty_json(resp) if isinstance(resp, dict) else resp}")
            return True
        else:
            print("  [警告] 未收到确认回复（可能已处理）")
            return None

    def remove_device(self, friendly_name: str, force: bool = False) -> bool:
        """移除 Zigbee 设备

        发布到 zigbee2mqtt/bridge/request/device/remove 移除设备。
        移除后设备将从 Zigbee 网络中删除（需要重新配对）。

        Args:
            friendly_name: 设备的友好名称
            force: 是否强制移除（设备离线时需要）

        Returns:
            bool: 是否操作成功
        """
        print_separator(f"移除设备: {friendly_name}")

        request_topic = self.bridge_request_topic("device/remove")
        reply_topic = self.topic("bridge", "response", "device/remove")

        payload = {"id": friendly_name}
        if force:
            payload["force"] = True
            payload["force_remove"] = True

        # 订阅回复
        self.client.subscribe(reply_topic)
        self._received_messages.clear()

        # 发送请求
        msg_str = json.dumps(payload, ensure_ascii=False)
        result = self.client.publish(request_topic, msg_str)
        result.wait_for_publish(timeout=5)
        log.info(f"发布移除请求: {request_topic} → {msg_str}")

        if result.rc != mqtt.MQTT_ERR_SUCCESS:
            print(f"  [错误] 发布失败")
            return False

        print(f"  设备: {friendly_name}")
        print(f"  强制移除: {'是' if force else '否'}")
        print("  请求已发送 ✓")

        # 等待确认
        reply = self._wait_for_message(reply_topic, timeout=15)
        if reply:
            resp = reply["payload"]
            if isinstance(resp, str):
                try:
                    resp = json.loads(resp)
                except json.JSONDecodeError:
                    pass
            print(f"  回复: {pretty_json(resp) if isinstance(resp, dict) else resp}")
            # 检查是否成功
            if isinstance(resp, dict):
                if resp.get("status") == "ok" or resp.get("data", {}).get("removed"):
                    print("  设备移除成功 ✓")
                    return True
                else:
                    print(f"  [警告] 回复状态: {resp.get('status', 'unknown')}")
                    return None
        else:
            print("  [警告] 未收到确认回复")
            return None

    def health_check(self) -> dict:
        """健康检查 — 查看 Zigbee2MQTT 桥接状态

        订阅以下 Topic 检查服务状态:
          - zigbee2mqtt/bridge/state  — 在线状态
          - zigbee2mqtt/bridge/info   — 版本信息

        Returns:
            健康状态字典
        """
        print_separator("Zigbee2MQTT 健康检查")

        # 订阅状态和 info topic
        self.client.subscribe(self.bridge_state_topic())
        self.client.subscribe(self.bridge_info_topic())
        self._received_messages.clear()

        # 请求 bridge info
        self.client.publish(self.topic("bridge", "config", "info", "get"), "")

        # 等待回复
        results = {"online": False, "info": None, "coordinator": None}

        start = time.time()
        while time.time() - start < self.timeout:
            msg = self._wait_for_message(self.base_topic + "/bridge/", timeout=3)
            if msg is None:
                break

            topic = msg["topic"]
            payload = msg["payload"]

            if topic == self.bridge_state_topic():
                results["online"] = (payload == "online")
                print(f"  桥接状态: {'在线 ✓' if payload == 'online' else f'离线 ({payload}) ✗'}")

            elif topic == self.bridge_info_topic():
                if isinstance(payload, str):
                    try:
                        payload = json.loads(payload)
                    except json.JSONDecodeError:
                        pass

                if isinstance(payload, dict):
                    results["info"] = payload
                    print(f"  版本: {payload.get('version', '未知')}")
                    print(f"  协调器: {payload.get('coordinator', {}).get('type', '未知')}")

                    # 打印网络信息
                    network = payload.get("network", {})
                    if network:
                        print(f"  网络 PAN ID: {network.get('pan_id', '未知')}")
                        print(f"  通道: {network.get('channel', '未知')}")
                        results["coordinator"] = network

                    # 打印启动信息
                    config = payload.get("config", {})
                    if config:
                        permit_join = config.get("permit_join", False)
                        print(f"  配对模式: {'开启' if permit_join else '关闭'}")
                        print(f"  MQTT Base Topic: {config.get('mqtt', {}).get('base_topic', self.base_topic)}")

        if not results["online"] and results["info"] is None:
            print("  [错误] 未收到 Zigbee2MQTT 响应")
            print("  请确认:")
            print("    1. Zigbee2MQTT 正在运行")
            print("    2. MQTT Broker 可访问")
            print("    3. Base Topic 配置正确")

        return results

    def show_ha_discovery(self, friendly_name: str) -> list:
        """展示 Home Assistant 自动发现 Topic

        查看指定设备的 HA 自动发现配置。Zigbee2MQTT 默认会将设备自动发现
        信息发布到 homeassistant/{component}/{object_id}/config。

        此函数展示设备对应的 HA 发现 Topic 结构和示例配置。

        Args:
            friendly_name: 设备的友好名称

        Returns:
            HA 发现 Topic 列表
        """
        print_separator(f"HA 自动发现 Topic: {friendly_name}")

        ha_prefix = SAMPLE_CONFIG["ha_discovery_prefix"]

        # 先获取设备信息以确定组件类型
        self.client.subscribe(self.bridge_devices_topic())
        self._received_messages.clear()
        self.client.publish(self.bridge_devices_get_topic(), "")

        devices_msg = self._wait_for_message(self.bridge_devices_topic(), timeout=10)
        device_info = None
        if devices_msg:
            devices = devices_msg["payload"]
            if isinstance(devices, list):
                for dev in devices:
                    if dev.get("friendly_name") == friendly_name:
                        device_info = dev
                        break

        if device_info is None:
            print(f"  [警告] 未找到设备 '{friendly_name}' 的信息")
            print("  以下为通用 HA 发现 Topic 示例:\n")
        else:
            print(f"  设备型号: {device_info.get('model', '未知')}")
            print(f"  设备类型: {device_info.get('type', '未知')}")
            print(f"  IEEE 地址: {device_info.get('ieee_address', '未知')}\n")

        # 根据设备类型推断 HA 组件
        discovery_topics = []
        device_type = device_info.get("type", "EndDevice") if device_info else "unknown"
        model = device_info.get("modelID", "") if device_info else ""

        # 推断 HA 组件类型
        component_map = {
            "Router": [
                ("light", "light"),
                ("switch", "switch"),
            ],
            "EndDevice": [
                ("sensor", "sensor"),
                ("binary_sensor", "binary_sensor"),
            ],
            "Coordinator": [],
        }

        # 根据模型名称进一步推断
        model_lower = model.lower() if model else ""
        if any(kw in model_lower for kw in ["light", "bulb", "led", "lamp", "strip"]):
            components = [("light", f"{friendly_name}_light")]
        elif any(kw in model_lower for kw in ["plug", "socket", "outlet"]):
            components = [("switch", f"{friendly_name}_switch")]
        elif any(kw in model_lower for kw in ["sensor", "temp", "humidity", "motion"]):
            components = [
                ("sensor", f"{friendly_name}_sensor"),
                ("binary_sensor", f"{friendly_name}_occupancy"),
            ]
        elif any(kw in model_lower for kw in ["switch", "button"]):
            components = [("switch", f"{friendly_name}_switch")]
        elif any(kw in model_lower for kw in ["lock"]):
            components = [("lock", f"{friendly_name}_lock")]
        elif any(kw in model_lower for kw in ["fan"]):
            components = [("fan", f"{friendly_name}_fan")]
        elif any(kw in model_lower for kw in ["cover", "curtain", "blind"]):
            components = [("cover", f"{friendly_name}_cover")]
        elif any(kw in model_lower for kw in ["thermostat", "climate"]):
            components = [("climate", f"{friendly_name}_climate")]
        else:
            components = [("sensor", f"{friendly_name}_sensor")]

        # 设备标识符（用于 HA 的 device registry）
        ieee = device_info.get("ieee_address", f"0x{friendly_name}") if device_info else f"0x{friendly_name}"

        print("  ┌─ HA 自动发现 Topic 结构 ─────────────────────────")
        for component, object_id in components:
            config_topic = f"{ha_prefix}/{component}/{object_id}/config"
            state_topic = self.device_topic(friendly_name)

            # 示例配置
            example_config = {
                "name": friendly_name,
                "state_topic": state_topic,
                "availability_topic": f"{state_topic}/availability",
                "device": {
                    "identifiers": [f"zigbee2mqtt_{ieee}"],
                    "name": friendly_name,
                    "manufacturer": device_info.get("manufacturer", "Xiaomi") if device_info else "Xiaomi",
                    "model": model if model else "unknown",
                },
            }

            # 针对不同组件添加特定字段
            if component == "light":
                example_config["command_topic"] = self.device_set_topic(friendly_name)
                example_config["brightness"] = True
                example_config["color_mode"] = True
                example_config["schema"] = "json"
            elif component == "switch":
                example_config["command_topic"] = self.device_set_topic(friendly_name)
                example_config["payload_on"] = "ON"
                example_config["payload_off"] = "OFF"
            elif component == "sensor":
                example_config["json_attributes_topic"] = state_topic
            elif component == "binary_sensor":
                example_config["payload_on"] = True
                example_config["payload_off"] = False

            discovery_topics.append({
                "component": component,
                "config_topic": config_topic,
                "example_config": example_config,
            })

            print(f"  │")
            print(f"  │  组件类型: {component}")
            print(f"  │  配置 Topic: {config_topic}")
            print(f"  │  示例配置:")
            config_lines = pretty_json(example_config).split("\n")
            for line in config_lines:
                print(f"  │    {line}")

        print("  └────────────────────────────────────────────────────")
        print()
        print("  监听这些 Topic 的命令:")
        print(f"    mosquitto_sub -h {self.broker} -t '{ha_prefix}/#/{friendly_name}/config' -v")

        return discovery_topics


# ═══════════════════════════════════════════════════════
#  CLI 入口
# ═══════════════════════════════════════════════════════

def main():
    """命令行入口"""
    import argparse

    parser = argparse.ArgumentParser(
        description="Zigbee2MQTT 交互演示",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python 07_zigbee2mqtt_demo.py list
  python 07_zigbee2mqtt_demo.py state --device light_1
  python 07_zigbee2mqtt_demo.py set --device light_1 --payload '{"state":"ON","brightness":200}'
  python 07_zigbee2mqtt_demo.py permit_join --enable --time 120
  python 07_zigbee2mqtt_demo.py permit_join --disable
  python 07_zigbee2mqtt_demo.py remove --device old_sensor --force
  python 07_zigbee2mqtt_demo.py health
  python 07_zigbee2mqtt_demo.py ha_discovery --device light_1
        """,
    )

    # 全局参数
    parser.add_argument("--broker", default=SAMPLE_CONFIG["broker"],
                        help="MQTT Broker 地址 (默认: localhost)")
    parser.add_argument("--port", type=int, default=SAMPLE_CONFIG["port"],
                        help="MQTT Broker 端口 (默认: 1883)")
    parser.add_argument("--username", default=None,
                        help="MQTT 用户名")
    parser.add_argument("--password", default=None,
                        help="MQTT 密码")
    parser.add_argument("--base-topic", default=SAMPLE_CONFIG["base_topic"],
                        help="Zigbee2MQTT Base Topic (默认: zigbee2mqtt)")
    parser.add_argument("--timeout", type=int, default=SAMPLE_CONFIG["timeout"],
                        help="操作超时时间（秒，默认: 10）")

    # 子命令
    subparsers = parser.add_subparsers(dest="command", help="操作命令")

    # list 命令
    subparsers.add_parser("list", help="列出所有已配对的 Zigbee 设备")

    # state 命令
    state_parser = subparsers.add_parser("state", help="获取设备状态")
    state_parser.add_argument("--device", "-d", required=True,
                              help="设备友好名称")

    # set 命令
    set_parser = subparsers.add_parser("set", help="控制设备")
    set_parser.add_argument("--device", "-d", required=True,
                            help="设备友好名称")
    set_parser.add_argument("--payload", "-p", required=True,
                            help="控制命令（JSON 字符串），例如 '{\"state\":\"ON\"}'")

    # permit_join 命令
    pj_parser = subparsers.add_parser("permit_join", help="开启/关闭配对模式")
    pj_group = pj_parser.add_mutually_exclusive_group(required=True)
    pj_group.add_argument("--enable", "-e", action="store_true",
                          help="开启配对模式")
    pj_group.add_argument("--disable", "-x", action="store_true",
                          help="关闭配对模式")
    pj_parser.add_argument("--time", "-t", type=int, default=60,
                           help="配对持续时间（秒，默认: 60）")

    # remove 命令
    rm_parser = subparsers.add_parser("remove", help="移除设备")
    rm_parser.add_argument("--device", "-d", required=True,
                           help="设备友好名称")
    rm_parser.add_argument("--force", "-f", action="store_true",
                           help="强制移除（设备离线时）")

    # health 命令
    subparsers.add_parser("health", help="健康检查")

    # ha_discovery 命令
    ha_parser = subparsers.add_parser("ha_discovery", help="展示 HA 自动发现 Topic")
    ha_parser.add_argument("--device", "-d", required=True,
                           help="设备友好名称")

    args = parser.parse_args()

    if not mqtt:
        print("请先安装依赖: pip install paho-mqtt", file=sys.stderr)
        sys.exit(1)

    # 如果没有指定命令，显示帮助
    if not args.command:
        parser.print_help()
        print()
        print("可用命令: list, state, set, permit_join, remove, health, ha_discovery")
        print()
        print("快速开始:")
        print(f"  python {sys.argv[0]} --broker localhost list")
        print(f"  python {sys.argv[0]} health")
        return

    # 创建客户端
    client = Zigbee2MQTTClient(
        broker=args.broker,
        port=args.port,
        base_topic=args.base_topic,
        timeout=args.timeout,
    )
    client.set_credentials(args.username, args.password)

    # 连接
    if not client.connect():
        print("\n[错误] 无法连接到 MQTT Broker，请检查配置后重试。")
        sys.exit(1)

    try:
        # 根据命令执行相应操作
        if args.command == "list":
            devices = client.list_devices()
            if devices:
                print(f"\n提示: 使用 'state --device <名称>' 查看设备详情")

        elif args.command == "state":
            state = client.get_state(args.device)
            if state is None:
                sys.exit(1)

        elif args.command == "set":
            try:
                payload = json.loads(args.payload)
            except json.JSONDecodeError as e:
                print(f"[错误] JSON 解析失败: {e}")
                print(f"  输入: {args.payload}")
                print('  示例: \'{"state":"ON"}\'')
                sys.exit(1)
            success = client.set_state(args.device, payload)
            if not success:
                sys.exit(1)

        elif args.command == "permit_join":
            enable = args.enable
            client.permit_join(enable=enable, time_s=args.time)
            if enable:
                print(f"\n提示: 配对模式已开启 {args.time} 秒，请将设备设为配对状态")
                print("      新设备会自动被 Zigbee2MQTT 发现并配对")

        elif args.command == "remove":
            client.remove_device(friendly_name=args.device, force=args.force)
            print(f"\n提示: 设备 {args.device} 已从 Zigbee 网络中移除")
            print("      如需重新使用，请开启配对模式并重新配对")

        elif args.command == "health":
            health = client.health_check()
            if not health["online"] and health["info"] is None:
                sys.exit(1)

        elif args.command == "ha_discovery":
            topics = client.show_ha_discovery(args.device)

    except KeyboardInterrupt:
        print("\n\n操作已取消")
    except Exception as e:
        log.error(f"操作出错: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        client.disconnect()


if __name__ == "__main__":
    main()
