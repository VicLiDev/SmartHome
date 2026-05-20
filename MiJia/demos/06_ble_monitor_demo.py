"""
Demo 6: BLE 蓝牙传感器被动监听

功能:
  - 被动扫描小米 BLE 传感器的广播数据
  - 解析 MiBeacon 协议（温湿度传感器、门磁等）
  - 无需蓝牙连接，无需网关
  - 数据输出到终端或 MQTT

支持的设备:
  - LYWSD03MMC / MHO-C401 (温湿度传感器)
  - MCCGQ02LM (蓝牙门磁)
  - RTCGQ02LM (蓝牙人体传感器)
  - 刷了 pvvx/MiThermometer 固件的传感器（ATC 格式）

依赖: pip install bleak

注意:
  - 需要 Linux + 蓝牙适配器
  - 需要 root 权限或蓝牙权限
  - 部分传感器需要刷自定义固件才能获取完整数据

用法:
  # 扫描 BLE 设备
  sudo python 06_ble_monitor_demo.py scan

  # 监听传感器数据
  sudo python 06_ble_monitor_demo.py monitor

  # 监听并推送到 MQTT
  sudo python 06_ble_monitor_demo.py monitor --mqtt localhost

  # 指定监听时长
  sudo python 06_ble_monitor_demo.py monitor --duration 60
"""

import asyncio
import json
import struct
import time
import sys
import logging
from dataclasses import dataclass
from typing import Optional

try:
    from bleak import BleakScanner
except ImportError:
    BleakScanner = None

try:
    import paho.mqtt.client as mqtt
    HAS_MQTT = True
except ImportError:
    HAS_MQTT = False


logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("ble_monitor")


# ═══════════════════════════════════════════════════════
#  MiBeacon 协议解析
# ═══════════════════════════════════════════════════════

# 小米 BLE Service UUID
MI_BLE_SERVICE = "0000fe95-0000-1000-8000-00805f9b34fb"

# 设备类型标识
MI_DEVICE_TYPES = {
    0x0121: "温湿度传感器 (LYWSD03MMC)",
    0x0576: "门磁 (MCCGQ02LM)",
    0x0471: "人体传感器 (RTCGQ02LM)",
    0x03dd: "温湿度传感器 (MHO-C401)",
    0x0f47: "体重秤",
    0x0a3c: "空调伴侣",
}

# MiBeacon Frame Control 标志位
FRAME_CTRL_ENCRYPTED = 0x01
FRAME_CTRL_MAC_INCLUDED = 0x02
FRAME_CTRL_CAPABILITY_INCLUDED = 0x04
FRAME_CTRL_OBJECT_INCLUDED = 0x08
FRAME_CTRL_IS_BINDING = 0x10
FRAME_CTRL_HAS_SUBFRAME = 0x40
FRAME_CTRL_HAS_NEW_FORMAT = 0x80


@dataclass
class MiBeaconData:
    """解析后的 MiBeacon 数据"""
    mac: str = ""
    device_type: str = "unknown"
    frame_type: str = "unknown"
    temperature: Optional[float] = None
    humidity: Optional[float] = None
    battery: Optional[int] = None
    rssi: int = 0
    contact: Optional[bool] = None   # 门磁状态
    motion: Optional[bool] = None    # 人体感应
    lux: Optional[int] = None       # 光照
    raw: dict = None


def parse_mibeacon(service_data: bytes, rssi: int) -> Optional[MiBeaconData]:
    """
    解析 MiBeacon 广播数据

    MiBeacon 格式:
      Frame Control (1B) | Device Type (1B) | Frame Length (1B)
      | [MAC (6B)] | [Capability (1B)] | [Data (变长)]
    """
    if len(service_data) < 5:
        return None

    frame_ctrl = service_data[0]
    device_id = service_data[1]
    frame_len = service_data[2]

    result = MiBeaconData(rssi=rssi, raw={})
    result.device_type = MI_DEVICE_TYPES.get(device_id, f"unknown (0x{device_id:04x})")

    offset = 3

    # MAC 地址
    if frame_ctrl & FRAME_CTRL_MAC_INCLUDED:
        if offset + 6 > len(service_data):
            return None
        mac_bytes = service_data[offset:offset + 6]
        result.mac = ":".join(f"{b:02X}" for b in reversed(mac_bytes))
        offset += 6

    # Capability
    if frame_ctrl & FRAME_CTRL_CAPABILITY_INCLUDED:
        if offset + 1 > len(service_data):
            return None
        capability = service_data[offset]
        offset += 1
        # Bit 4-7: battery
        if capability & 0x80:
            battery = (capability & 0x0F) * 10 + 20
            result.battery = min(battery, 100)

    # Data
    data_start = offset
    data = service_data[data_start:]

    # 尝试解析数据字段
    result.raw["device_type_hex"] = f"0x{device_id:04x}"
    result.raw["frame_ctrl"] = f"0x{frame_ctrl:02X}"
    result.raw["data_hex"] = data.hex()

    if frame_ctrl & FRAME_CTRL_ENCRYPTED:
        result.frame_type = "encrypted"
        return result  # 加密数据无法解析

    result.frame_type = "plain"

    # 解析 TLV (Type-Length-Value) 格式的数据
    pos = 0
    while pos + 1 < len(data):
        tlv_type = data[pos]
        tlv_len = data[pos + 1]
        pos += 2

        if pos + tlv_len > len(data):
            break

        tlv_value = data[pos:pos + tlv_len]
        pos += tlv_len

        if tlv_type == 0x0D and tlv_len == 1:
            # 温度 (整数，需除以 10)
            raw_temp = struct.unpack(">h", b'\x00' + bytes([tlv_value[0]]))[0]
            # 实际格式可能不同，根据设备类型判断
            pass
        elif tlv_type == 0x0A and tlv_len == 1:
            # 温度 (另一种编码)
            raw_temp = tlv_value[0]
            if raw_temp > 127:
                raw_temp -= 256
            result.temperature = raw_temp / 10.0
        elif tlv_type == 0x06 and tlv_len == 1:
            # 湿度
            result.humidity = tlv_value[0]
        elif tlv_type == 0x04 and tlv_len >= 4:
            # 温湿度（4字节格式）
            temp_raw = struct.unpack(">h", tlv_value[0:2])[0]
            hum_raw = struct.unpack(">H", tlv_value[2:4])[0]
            result.temperature = temp_raw / 10.0
            result.humidity = hum_raw / 10.0
        elif tlv_type == 0x10 and tlv_len == 1:
            # 电池
            result.battery = tlv_value[0]
        elif tlv_type == 0x0F and tlv_len == 1:
            # 门磁/接触状态
            result.contact = bool(tlv_value[0])
        elif tlv_type == 0x0E and tlv_len == 1:
            # 人体感应
            result.motion = bool(tlv_value[0])
        elif tlv_type == 0x09 and tlv_len == 1:
            # 光照
            result.lux = tlv_value[0]

    return result


# ═══════════════════════════════════════════════════════
#  ATC 格式解析（pvvx 自定义固件）
# ═══════════════════════════════════════════════════════

# ATC 格式的 Service Data UUID
ATC_UUID = "0000181a-0000-1000-8000-00805f9b34fb"


def parse_atc_format(service_data: bytes, rssi: int) -> Optional[MiBeaconData]:
    """
    解析 ATC 格式（pvvx/MiThermometer 自定义固件）

    格式:
      MAC (6B, reversed) | [Optional: Device Info (1B)]
      | Temperature (2B, signed) | Humidity (1B) | Battery mV (2B)
      | Battery % (1B) | Frame Counter (1B)
    """
    if len(service_data) < 10:
        return None

    result = MiBeaconData(rssi=rssi)
    result.frame_type = "atc"
    result.device_type = "ATC (pvvx 固件)"

    offset = 0

    # MAC 地址
    if len(service_data) >= 6:
        mac_bytes = service_data[0:6]
        result.mac = ":".join(f"{b:02X}" for b in reversed(mac_bytes))
        offset = 6

    # 可选的设备信息字节
    if len(service_data) >= 14:
        offset = 7  # MAC + device info

    # 温度 (2 bytes, signed big-endian, 单位 0.01°C)
    if offset + 2 <= len(service_data):
        temp_raw = struct.unpack(">h", service_data[offset:offset + 2])[0]
        result.temperature = temp_raw / 100.0
        offset += 2

    # 湿度 (1 byte, %)
    if offset + 1 <= len(service_data):
        result.humidity = service_data[offset]
        offset += 1

    # 电池电压 mV (2 bytes)
    if offset + 2 <= len(service_data):
        voltage_mv = struct.unpack(">H", service_data[offset:offset + 2])[0]
        result.raw["voltage_mv"] = voltage_mv
        offset += 2

    # 电池百分比 (1 byte)
    if offset + 1 <= len(service_data):
        result.battery = service_data[offset]
        offset += 1

    return result


# ═══════════════════════════════════════════════════════
#  BLE 扫描和监听
# ═══════════════════════════════════════════════════════

async def scan_ble_devices(duration: float = 10.0):
    """扫描 BLE 设备"""
    log.info(f"扫描 BLE 设备 ({duration}秒)...")
    print("─" * 70)

    scanner = BleakScanner()

    devices = await scanner.discover(timeout=duration)

    # 过滤小米设备
    xiaomi_devices = []
    for d in devices:
        name = d.name or ""
        if ("Mi" in name or "MJ" in name or "LYWSD" in name or
            "MHO" in name or "ATC" in name or
            any(MI_BLE_SERVICE in str(sd.uuid) for sd in d.metadata.get("service_data", {}).values() if hasattr(sd, 'uuid'))):
            xiaomi_devices.append(d)

    if not xiaomi_devices:
        # 列出所有发现的设备供参考
        log.info(f"共发现 {len(devices)} 个 BLE 设备，未识别到小米设备")
        for d in sorted(devices, key=lambda x: x.rssi, reverse=True)[:10]:
            print(f"  {d.address:20} RSSI={d.rssi:>3}  {d.name or '(无名)'}")
        return

    print(f"\n发现 {len(xiaomi_devices)} 个小米 BLE 设备:\n")
    print(f"{'MAC':<20} {'RSSI':<6} {'名称':<25} {'厂商数据'}")
    print("─" * 70)

    for d in sorted(xiaomi_devices, key=lambda x: x.rssi, reverse=True):
        sd_info = ""
        for uuid_str, sd_bytes in d.metadata.get("service_data", {}).items():
            sd_info += f"{uuid_str.split('-')[0][-4:]}:{sd_bytes.hex()[:16]} "

        print(f"{d.address:<20} {d.rssi:<6} {d.name or '(无名)':<25} {sd_info.strip()}")


async def monitor_ble(duration: float = 0, mqtt_config: dict = None):
    """持续监听 BLE 传感器数据"""
    from bleak import BleakScanner

    log.info("开始监听 BLE 传感器...")
    if duration > 0:
        log.info(f"监听时长: {duration}秒")
    print("─" * 70)
    print(f"{'时间':<10} {'MAC':<20} {'类型':<25} {'温度':>6} {'湿度':>6} {'电池':>4} {'RSSI':>4}")
    print("─" * 70)

    mqtt_client = None
    if mqtt_config and HAS_MQTT:
        mqtt_client = mqtt.Client()
        mqtt_client.connect(mqtt_config["broker"], mqtt_config.get("port", 1883))
        mqtt_client.loop_start()
        log.info(f"已连接 MQTT: {mqtt_config['broker']}")

    seen_devices = {}
    detection_cb = None

    def on_detection(device, advertisement_data):
        """检测回调"""
        now = time.strftime("%H:%M:%S")

        # 尝试解析 MiBeacon 格式
        result = None
        for uuid_str, sd_bytes in advertisement_data.service_data.items():
            uuid_lower = str(uuid_str).lower()

            if MI_BLE_SERVICE in uuid_lower:
                result = parse_mibeacon(sd_bytes, advertisement_data.rssi)
            elif ATC_UUID in uuid_lower:
                result = parse_atc_format(sd_bytes, advertisement_data.rssi)

        if result is None:
            # 尝试从所有 service_data 中查找
            for uuid_str, sd_bytes in advertisement_data.service_data.items():
                if len(sd_bytes) >= 5 and sd_bytes[0] in range(0x00, 0xFF):
                    result = parse_mibeacon(sd_bytes, advertisement_data.rssi)
                    if result:
                        break

        if result is None:
            return

        # 构建显示行
        mac = result.mac or device.address
        dtype = result.device_type[:25]
        temp = f"{result.temperature:.1f}°" if result.temperature is not None else "  N/A"
        hum = f"{result.humidity:.0f}%" if result.humidity is not None else "  N/A"
        bat = f"{result.battery}%" if result.battery is not None else "  N/A"
        rssi = result.rssi

        extra = ""
        if result.contact is not None:
            extra = f" 接触:{'开' if result.contact else '关'}"
        if result.motion is not None:
            extra = f" 运动:{'有' if result.motion else '无'}"

        print(f"{now:<10} {mac:<20} {dtype:<25} {temp:>6} {hum:>6} {bat:>4} {rssi:>4}{extra}")

        # 推送到 MQTT
        if mqtt_client and result.mac:
            topic = f"xiaomi/ble/{result.mac.replace(':', '')}"
            payload = {
                "mac": result.mac,
                "device_type": result.device_type,
                "rssi": result.rssi,
                "timestamp": time.time(),
            }
            if result.temperature is not None:
                payload["temperature"] = result.temperature
            if result.humidity is not None:
                payload["humidity"] = result.humidity
            if result.battery is not None:
                payload["battery"] = result.battery
            if result.contact is not None:
                payload["contact"] = result.contact
            if result.motion is not None:
                payload["motion"] = result.motion

            mqtt_client.publish(topic, json.dumps(payload), retain=True)

    scanner = BleakScanner(detection_callback=on_detection)

    try:
        await scanner.start()
        if duration > 0:
            await asyncio.sleep(duration)
            await scanner.stop()
        else:
            while True:
                await asyncio.sleep(1)
    except KeyboardInterrupt:
        log.info("停止监听")
    finally:
        await scanner.stop()
        if mqtt_client:
            mqtt_client.loop_stop()
            mqtt_client.disconnect()


# ═══════════════════════════════════════════════════════
#  CLI
# ═══════════════════════════════════════════════════════

def main():
    import argparse

    parser = argparse.ArgumentParser(
        description="BLE 小米传感器监听",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
注意: 需要 root 权限运行 (sudo)。

示例:
  # 扫描 BLE 设备
  sudo python 06_ble_monitor_demo.py scan

  # 持续监听
  sudo python 06_ble_monitor_demo.py monitor

  # 监听 60 秒
  sudo python 06_ble_monitor_demo.py monitor --duration 60

  # 推送到 MQTT
  sudo python 06_ble_monitor_demo.py monitor --mqtt localhost --mqtt-port 1883
        """
    )

    sub = parser.add_subparsers(dest="command")

    p_scan = sub.add_parser("scan", help="扫描 BLE 设备")
    p_scan.add_argument("--duration", type=float, default=10, help="扫描时长（秒）")

    p_mon = sub.add_parser("monitor", help="监听传感器数据")
    p_mon.add_argument("--duration", type=float, default=0, help="监听时长（0=持续）")
    p_mon.add_argument("--mqtt", help="MQTT Broker 地址")
    p_mon.add_argument("--mqtt-port", type=int, default=1883, help="MQTT 端口")

    args = parser.parse_args()

    if not BleakScanner:
        print("请先安装依赖: pip install bleak", file=sys.stderr)
        sys.exit(1)

    if args.command == "scan":
        asyncio.run(scan_ble_devices(args.duration))
    elif args.command == "monitor":
        mqtt_config = None
        if args.mqtt:
            mqtt_config = {"broker": args.mqtt, "port": args.mqtt_port}
            if not HAS_MQTT:
                log.error("需要安装 paho-mqtt: pip install paho-mqtt")
                return
        asyncio.run(monitor_ble(args.duration, mqtt_config))
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
