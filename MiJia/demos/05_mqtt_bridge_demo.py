"""
Demo 5: MQTT 桥接 — 将小米设备接入 MQTT 生态

功能:
  - 本地 miIO/miOT 设备控制 → 映射到 MQTT topic
  - MQTT 订阅 → 转发为设备控制命令
  - 设备状态定期上报到 MQTT
  - 支持 Home Assistant MQTT 自动发现

架构:
  小米设备 ←(miIO UDP)→ 本桥接服务 ←(MQTT)→ SmartHome / HA / Node-RED

MQTT Topic 设计:
  xiaomi/{device_id}/state       — 设备状态 (JSON, retain)
  xiaomi/{device_id}/set         — 控制指令 (JSON)
  xiaomi/{device_id}/available   — 在线状态 (online/offline)
  xiaomi/{device_id}/attributes  — 属性列表
  xiaomi/bridge/status           — 桥接服务状态
  xiaomi/bridge/devices          — 已注册设备列表

依赖: pip install paho-mqtt python-miio

用法:
  # 启动桥接服务
  python 05_mqtt_bridge_demo.py --broker localhost --port 1883

  # 带设备配置
  python 05_mqtt_bridge_demo.py --broker localhost --config devices.json

  # 测试
  # 订阅所有 xiaomi topic:
  #   mosquitto_sub -h localhost -t 'xiaomi/#' -v

  # 开灯:
  #   mosquitto_pub -h localhost -t 'xiaomi/LIGHT_001/set' \
  #     -m '{"method":"set_power","params":["on"]}'
"""

import asyncio
import json
import time
import signal
import sys
import os
import logging
from pathlib import Path
from dataclasses import dataclass, field
from typing import Optional

try:
    import paho.mqtt.client as mqtt
except ImportError:
    mqtt = None

try:
    from miio import Device, DeviceException
except ImportError:
    Device = None

try:
    from Crypto.Cipher import AES
    from Crypto.Util.Padding import pad, unpad
except ImportError:
    try:
        from Cryptodome.Cipher import AES
        from Cryptodome.Util.Padding import pad, unpad
    except ImportError:
        AES = None


# ═══════════════════════════════════════════════════════
#  配置
# ═══════════════════════════════════════════════════════

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("miio2mqtt")


@dataclass
class DeviceConfig:
    """设备配置"""
    name: str
    ip: str
    token: str
    model: str = ""
    device_type: str = "generic"  # generic, light, plug, fan, vacuum
    poll_interval: int = 60       # 状态上报间隔（秒）
    ha_discovery: bool = True     # 是否启用 HA 自动发现


@dataclass
class BridgeConfig:
    """桥接配置"""
    broker: str = "localhost"
    port: int = 1883
    username: Optional[str] = None
    password: Optional[str] = None
    topic_prefix: str = "xiaomi"
    poll_interval: int = 60
    devices: list = field(default_factory=list)
    config_path: str = "devices.json"


# ═══════════════════════════════════════════════════════
#  miIO 加密通信（纯 Python，不依赖 python-miio 的异步接口）
# ═══════════════════════════════════════════════════════

class MiIOClient:
    """轻量级 miIO 通信客户端"""

    PORT = 54321
    HELLO = (
        b'\x21\x31' + b'\x00\x20' + b'\xff\xff\xff\xff'
        + b'\x00\x00\x00\x00' + b'\xff' * 16 + b'\x00' * 32
    )

    def __init__(self, ip: str, token: str):
        self.ip = ip
        self.token = token
        token_bytes = bytes.fromhex(token)
        self.aes_key = __import__('hashlib').md5(token_bytes).digest()
        self.aes_iv = __import__('hashlib').md5(self.aes_key + token_bytes).digest()

    def _handshake(self, timeout: int = 5) -> int:
        """获取设备时间戳"""
        import socket, struct
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(timeout)
        try:
            sock.sendto(self.HELLO, (self.ip, self.PORT))
            resp, _ = sock.recvfrom(4096)
            return struct.unpack_from("<I", resp, 8)[0]
        finally:
            sock.close()

    def send(self, method: str, params: list, timeout: int = 10) -> dict:
        """发送 miIO 命令"""
        import socket, struct, hashlib

        device_ts = self._handshake(timeout)

        # 构建 JSON-RPC
        payload = json.dumps({"id": 1, "method": method, "params": params}).encode()

        # 加密
        cipher = AES.new(self.aes_key, AES.MODE_CBC, iv=self.aes_iv)
        encrypted = cipher.encrypt(pad(payload, 16))

        # 构建报文
        nonce = bytes([i * 17 + 42 for i in range(16)])
        sign_data = self.aes_iv + struct.pack("<I", device_ts) + nonce
        signature = hashlib.md5(sign_data).digest()

        packet = bytearray()
        packet += struct.pack(">H", 0x2131)
        packet += b'\x00\x00'
        packet += struct.pack("<I", 0xFFFFFFFF)
        packet += struct.pack("<I", device_ts)
        packet += nonce + signature + encrypted

        total_len = len(packet)
        packet[2] = (total_len >> 8) & 0xFF
        packet[3] = total_len & 0xFF

        # 发送
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(timeout)
        try:
            sock.sendto(bytes(packet), (self.ip, self.PORT))
            resp_data, _ = sock.recvfrom(4096)
        finally:
            sock.close()

        # 解密
        resp_enc = resp_data[32:]
        if len(resp_enc) == 0:
            return {"result": "header_only"}

        cipher = AES.new(self.aes_key, AES.MODE_CBC, iv=self.aes_iv)
        decrypted = unpad(cipher.decrypt(resp_enc), 16)
        return json.loads(decrypted.decode())


# ═══════════════════════════════════════════════════════
#  MQTT 桥接服务
# ═══════════════════════════════════════════════════════

class MiioMqttBridge:
    """miIO → MQTT 桥接服务"""

    def __init__(self, config: BridgeConfig):
        self.config = config
        self.devices = {}      # name → MiIOClient
        self.device_configs = {}
        self.mqtt_client = None
        self.running = False

    def load_devices(self):
        """加载设备配置"""
        config_path = Path(self.config.config_path)
        if config_path.exists():
            with open(config_path) as f:
                data = json.load(f)

            for name, cfg in data.items():
                dc = DeviceConfig(
                    name=name,
                    ip=cfg["ip"],
                    token=cfg["token"],
                    model=cfg.get("model", ""),
                    device_type=cfg.get("type", "generic"),
                    poll_interval=cfg.get("poll_interval", self.config.poll_interval),
                )
                self.device_configs[name] = dc
                self.devices[name] = MiIOClient(dc.ip, dc.token)

            log.info(f"已加载 {len(self.devices)} 个设备")

        elif self.config.devices:
            for dc in self.config.devices:
                self.device_configs[dc.name] = dc
                self.devices[dc.name] = MiIOClient(dc.ip, dc.token)

        else:
            log.warning("未找到设备配置。创建 devices.json 或通过 --config 指定。")

    def _topic(self, *parts: str) -> str:
        """构建 MQTT topic"""
        return "/".join([self.config.topic_prefix] + list(parts))

    def _publish(self, topic: str, payload: dict, retain: bool = False):
        """发布 MQTT 消息"""
        if self.mqtt_client and self.mqtt_client.is_connected():
            msg = json.dumps(payload, ensure_ascii=False)
            self.mqtt_client.publish(topic, msg, retain=retain)
            log.debug(f"发布: {topic} → {msg[:100]}")

    def _on_connect(self, client, userdata, flags, rc, properties=None):
        """MQTT 连接回调"""
        if rc == 0:
            log.info(f"已连接到 MQTT Broker: {self.config.broker}:{self.config.port}")

            # 订阅所有设备的 set topic
            for name in self.devices:
                topic = self._topic(name, "set")
                client.subscribe(topic)
                log.info(f"订阅: {topic}")

            # 发布桥接状态
            self._publish(self._topic("bridge", "status"),
                         {"status": "online", "devices": list(self.devices.keys())},
                         retain=True)
            self._publish(self._topic("bridge", "devices"),
                         {name: {"ip": dc.ip, "model": dc.model, "type": dc.device_type}
                          for name, dc in self.device_configs.items()},
                         retain=True)

            # 发布设备在线状态
            for name in self.devices:
                self._publish(self._topic(name, "available"),
                             "online", retain=True)
        else:
            log.error(f"MQTT 连接失败: rc={rc}")

    def _on_message(self, client, userdata, msg):
        """MQTT 消息回调"""
        topic = msg.topic
        payload = msg.payload.decode()

        # 解析设备名
        parts = topic.split("/")
        if len(parts) >= 3 and parts[2] == "set":
            device_name = parts[1]
            if device_name in self.devices:
                try:
                    data = json.loads(payload)
                    method = data.get("method", "send")
                    params = data.get("params", [])

                    log.info(f"控制 {device_name}: {method}({params})")
                    result = self.devices[device_name].send(method, params)

                    # 发布结果
                    self._publish(self._topic(device_name, "result"), result)

                except Exception as e:
                    log.error(f"控制失败 {device_name}: {e}")
                    self._publish(self._topic(device_name, "error"),
                                 {"error": str(e)})

    def _poll_device(self, name: str):
        """轮询设备状态并发布到 MQTT"""
        if name not in self.devices:
            return

        try:
            result = self.devices[name].send("get_prop", ["power"])
            self._publish(self._topic(name, "state"), result, retain=True)
            log.debug(f"状态上报 {name}: {result}")
        except Exception as e:
            log.warning(f"轮询失败 {name}: {e}")
            self._publish(self._topic(name, "available"), "offline", retain=True)

    def _publish_ha_discovery(self):
        """发布 Home Assistant MQTT 自动发现消息"""
        ha_prefix = "homeassistant"

        for name, dc in self.device_configs.items():
            if not dc.ha_discovery:
                continue

            # 为每个设备发布 HA 发现配置
            base_topic = self._topic(name)
            ha_topic = f"{ha_prefix}/switch/{name}/config"

            discovery_config = {
                "name": name,
                "command_topic": f"{base_topic}/set",
                "state_topic": f"{base_topic}/state",
                "availability_topic": f"{base_topic}/available",
                "payload_on": '{"method":"set_power","params":["on"]}',
                "payload_off": '{"method":"set_power","params":["off"]}',
                "device": {
                    "identifiers": [f"miio_{name}"],
                    "name": name,
                    "model": dc.model,
                    "manufacturer": "Xiaomi",
                },
            }

            self._publish(ha_topic, discovery_config, retain=True)

        log.info(f"已发布 {len(self.device_configs)} 个 HA 发现配置")

    def start(self):
        """启动桥接服务"""
        self.load_devices()

        if not self.devices:
            log.error("没有配置任何设备，退出")
            return

        # 创建 MQTT 客户端
        self.mqtt_client = mqtt.Client(
            client_id="miio2mqtt_bridge",
            protocol=mqtt.MQTTv5,
        )

        if self.config.username and self.config.password:
            self.mqtt_client.username_pw_set(
                self.config.username, self.config.password)

        self.mqtt_client.on_connect = self._on_connect
        self.mqtt_client.on_message = self._on_message

        # 连接
        try:
            self.mqtt_client.connect(self.config.broker, self.config.port, 60)
        except Exception as e:
            log.error(f"无法连接 MQTT Broker: {e}")
            return

        # 发布 HA 发现
        self.mqtt_client.loop_start()

        # 等待连接建立
        time.sleep(2)
        self._publish_ha_discovery()

        # 启动轮询
        self.running = True
        log.info("桥接服务已启动，按 Ctrl+C 停止")

        try:
            while self.running:
                for name, dc in self.device_configs.items():
                    self._poll_device(name)
                time.sleep(dc.poll_interval)
        except KeyboardInterrupt:
            log.info("正在停止...")
        finally:
            self.running = False
            # 发布离线状态
            for name in self.devices:
                self._publish(self._topic(name, "available"), "offline", retain=True)
            self._publish(self._topic("bridge", "status"),
                         {"status": "offline"}, retain=True)
            self.mqtt_client.loop_stop()
            self.mqtt_client.disconnect()
            log.info("桥接服务已停止")


# ═══════════════════════════════════════════════════════
#  设备配置文件模板
# ═══════════════════════════════════════════════════════

SAMPLE_CONFIG = """{
  "living_room_light": {
    "ip": "192.168.1.101",
    "token": "your_token_here_32chars",
    "model": "yeelink.light.color3",
    "type": "light",
    "poll_interval": 30
  },
  "bedroom_plug": {
    "ip": "192.168.1.102",
    "token": "your_token_here_32chars",
    "model": "chuangmi.plug.v3",
    "type": "plug",
    "poll_interval": 60
  },
  "air_purifier": {
    "ip": "192.168.1.103",
    "token": "your_token_here_32chars",
    "model": "zhimi.airpurifier.m6",
    "type": "generic",
    "poll_interval": 120
  }
}"""


# ═══════════════════════════════════════════════════════
#  CLI
# ═══════════════════════════════════════════════════════

def main():
    import argparse

    parser = argparse.ArgumentParser(
        description="miIO → MQTT 桥接服务",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    parser.add_argument("--broker", default="localhost", help="MQTT Broker 地址")
    parser.add_argument("--port", type=int, default=1883, help="MQTT Broker 端口")
    parser.add_argument("--username", help="MQTT 用户名")
    parser.add_argument("--password", help="MQTT 密码")
    parser.add_argument("--prefix", default="xiaomi", help="MQTT Topic 前缀")
    parser.add_argument("--config", default="devices.json", help="设备配置文件路径")
    parser.add_argument("--poll-interval", type=int, default=60, help="轮询间隔（秒）")
    parser.add_argument("--generate-config", action="store_true",
                        help="生成设备配置文件模板")

    args = parser.parse_args()

    # 依赖检查
    if not mqtt:
        print("请先安装依赖: pip install paho-mqtt", file=sys.stderr)
        sys.exit(1)
    if not Device:
        print("请先安装依赖: pip install python-miio", file=sys.stderr)
        sys.exit(1)
    if not AES:
        print("请先安装依赖: pip install pycryptodome", file=sys.stderr)
        sys.exit(1)

    if args.generate_config:
        config_path = Path(args.config)
        config_path.write_text(SAMPLE_CONFIG)
        print(f"已生成配置文件模板: {config_path}")
        print("请填入实际的 IP 和 Token 后重新运行")
        return

    config = BridgeConfig(
        broker=args.broker,
        port=args.port,
        username=args.username,
        password=args.password,
        topic_prefix=args.prefix,
        poll_interval=args.poll_interval,
        config_path=args.config,
    )

    bridge = MiioMqttBridge(config)
    bridge.start()


if __name__ == "__main__":
    main()
