"""
Demo 3: python-miio 库使用

python-miio 是最成熟的 Python 米家设备控制库。
支持 200+ 设备型号，纯本地 UDP 通信。

GitHub: https://github.com/rytilahti/python-miio

依赖: pip install python-miio

用法:
  # 扫描设备
  python 03_python_miio_demo.py scan

  # 查询设备信息
  python 03_python_miio_demo.py info <IP> <TOKEN>

  # 控制设备
  python 03_python_miio_demo.py control <IP> <TOKEN> <TYPE> <ACTION> [ARGS...]

  # 从云端获取 token
  python 03_python_miio_demo.py cloud-token <USERNAME> <PASSWORD>

  # 交互模式
  python 03_python_miio_demo.py interactive <IP> <TOKEN>

设备类型:
  vacuum      — 扫地机器人
  airpurifier — 空气净化器
  plug        — 智能插座
  light       — 智能灯泡
  fan         — 智能风扇
  humidifier  — 加湿器
  ir          — 红外遥控
  generic     — 通用设备
"""

import asyncio
import json
import sys


# ═══════════════════════════════════════════════════════
#  设备发现
# ═══════════════════════════════════════════════════════

async def scan_devices(timeout: int = 5):
    """发现局域网内的 miIO 设备（mDNS）"""
    from miio import MiotDevice, DeviceException

    print("━" * 60)
    print("  扫描 miIO 设备 (mDNS)...")
    print("━" * 60)

    try:
        from miio.discovery import Discovery

        devices = Discovery.discover_mdns(timeout=timeout)

        if not devices:
            print("未发现设备。请检查:")
            print("  • 设备在同一局域网")
            print("  • 路由器关闭了 AP 隔离")
            return

        print(f"\n发现 {len(devices)} 个设备:\n")
        print(f"{'IP':<18} {'型号':<30} {'ID'}")
        print("─" * 70)
        for identifier, dev in devices.items():
            model = getattr(dev, "model", "unknown")
            did = getattr(dev, "device_id", "")
            ip = getattr(dev, "ip", identifier)
            print(f"{ip:<18} {str(model):<30} {did}")

    except ImportError as e:
        print(f"mDNS 发现不可用: {e}")
        print("请安装依赖: pip install zeroconf")


# ═══════════════════════════════════════════════════════
#  设备信息查询
# ═══════════════════════════════════════════════════════

async def get_device_info(ip: str, token: str):
    """查询设备详细信息"""
    from miio import Device

    print("━" * 60)
    print(f"  设备信息: {ip}")
    print("━" * 60)

    dev = Device(ip, token)
    try:
        info = await dev.info()
        print(f"\n型号:       {info.model}")
        print(f"固件版本:   {info.firmware_version}")
        print(f"硬件版本:   {info.hardware_version}")
        print(f"MAC 地址:   {info.mac_address}")
        print(f"设备 ID:    {info.identifier}")

        # 尝试获取额外属性
        try:
            props = await dev.get_properties(["power"], max_properties=1)
            if props:
                print(f"电源状态:   {props[0]}")
        except Exception:
            pass

    except Exception as e:
        print(f"查询失败: {e}")
        print("可能原因: Token 错误、设备离线、或设备使用 miOT 协议")


# ═══════════════════════════════════════════════════════
#  设备控制（按类型）
# ═══════════════════════════════════════════════════════

async def control_device(ip: str, token: str, dev_type: str, action: str, args: list):
    """控制指定类型的设备"""

    controller = get_controller(ip, token, dev_type)
    if not controller:
        print(f"不支持的设备类型: {dev_type}")
        print("可用类型: vacuum, airpurifier, plug, light, fan, humidifier, ir, generic")
        return

    print("━" * 60)
    print(f"  {dev_type}: {action}")
    print(f"  目标: {ip}")
    print("━" * 60)

    try:
        result = await controller(action, args)
        if result is not None:
            if isinstance(result, dict):
                print(json.dumps(result, indent=2, ensure_ascii=False, default=str))
            else:
                print(f"结果: {result}")
        else:
            print("执行成功")
    except Exception as e:
        print(f"执行失败: {e}")


def get_controller(ip: str, token: str, dev_type: str):
    """根据设备类型返回控制函数"""

    async def control_vacuum(action, args):
        from miio import Vacuum
        vac = Vacuum(ip, token)

        if action == "status":
            s = await vac.status()
            return {
                "battery": s.battery,
                "state": s.state,
                "clean_area": s.clean_area,
                "clean_time": s.clean_time,
            }
        elif action == "start":
            return await vac.start()
        elif action == "pause":
            return await vac.pause()
        elif action == "home":
            return await vac.home()
        elif action == "stop":
            return await vac.stop()
        elif action == "spot":
            return await vac.spot()
        elif action == "fan":
            speed = args[0] if args else "auto"
            return await vac.fan_speed(speed)
        elif action == "find":
            return await vac.find()
        else:
            print(f"未知动作: {action}")
            print("可用: status, start, pause, home, stop, spot, fan, find")

    async def control_airpurifier(action, args):
        from miio import AirPurifier
        ap = AirPurifier(ip, token)

        if action == "status":
            s = await ap.status()
            return {
                "power": s.power,
                "aqi": s.aqi,
                "humidity": s.humidity,
                "temperature": s.temperature,
                "mode": s.mode,
                "favorite_level": s.favorite_level,
                "filter_life": s.filter_hours_remaining,
            }
        elif action == "on":
            return await ap.on()
        elif action == "off":
            return await ap.off()
        elif action == "level":
            level = int(args[0]) if args else 1
            return await ap.favorite_level(level)
        else:
            print(f"未知动作: {action}")
            print("可用: status, on, off, level")

    async def control_plug(action, args):
        from miio import ChuangmiPlug
        plug = ChuangmiPlug(ip, token)

        if action == "status":
            s = await plug.status()
            return {
                "power": s.power,
                "voltage": s.voltage,
                "current": s.current,
                "power_consumed": s.power_consumed,
            }
        elif action == "on":
            return await plug.on()
        elif action == "off":
            return await plug.off()
        elif action == "usb_on":
            return await plug.usb_on()
        elif action == "usb_off":
            return await plug.usb_off()
        else:
            print(f"未知动作: {action}")
            print("可用: status, on, off, usb_on, usb_off")

    async def control_light(action, args):
        from miio.yeelink.light import Yeelight
        light = Yeelight(ip, token)

        if action == "status":
            s = await light.status()
            return {
                "power": s.is_on,
                "brightness": s.brightness,
                "color_temp": s.color_temp,
                "mode": s.mode,
            }
        elif action == "on":
            return await light.on()
        elif action == "off":
            return await light.off()
        elif action == "brightness":
            val = int(args[0]) if args else 80
            return await light.set_brightness(val)
        elif action == "color_temp":
            val = int(args[0]) if args else 4000
            return await light.set_color_temp(val)
        elif action == "rgb":
            r, g, b = int(args[0]), int(args[1]), int(args[2])
            return await light.set_rgb(r, g, b)
        else:
            print(f"未知动作: {action}")
            print("可用: status, on, off, brightness, color_temp, rgb")

    async def control_fan(action, args):
        from miio import Fan
        fan = Fan(ip, token)

        if action == "status":
            s = await fan.status()
            return {
                "power": s.power,
                "speed": s.speed,
                "mode": s.mode,
                "angle": s.angle,
                "oscillate": s.oscillate,
            }
        elif action == "on":
            return await fan.on()
        elif action == "off":
            return await fan.off()
        elif action == "speed":
            val = int(args[0]) if args else 1
            return await fan.set_speed(val)
        elif action == "oscillate":
            return await fan.set_oscillate(True)
        else:
            print(f"未知动作: {action}")
            print("可用: status, on, off, speed, oscillate")

    async def control_ir(action, args):
        from miio.chuangmi_ir import ChuangmiIrV2
        ir = ChuangmiIrV2(ip, token)

        if action == "learn":
            print("请对准遥控器按下按键...")
            learned = await ir.learn()
            print(f"学到的红外码: {learned}")
            return {"ir_code": learned}
        elif action == "play":
            code = args[0] if args else ""
            return await ir.play(code)
        elif action == "read":
            return await ir.read()
        else:
            print(f"未知动作: {action}")
            print("可用: learn, play, read")

    async def control_generic(action, args):
        from miio import Device
        dev = Device(ip, token)

        if action == "status":
            return await dev.status()
        elif action == "on":
            return await dev.on()
        elif action == "off":
            return await dev.off()
        elif action == "raw":
            method = args[0] if args else "get_prop"
            params = json.loads(args[1]) if len(args) > 1 else []
            return await dev.send(method, params)
        else:
            print(f"未知动作: {action}")
            print("可用: status, on, off, raw")

    controllers = {
        "vacuum": control_vacuum,
        "airpurifier": control_airpurifier,
        "purifier": control_airpurifier,
        "plug": control_plug,
        "light": control_light,
        "fan": control_fan,
        "ir": control_ir,
        "generic": control_generic,
    }

    return controllers.get(dev_type)


# ═══════════════════════════════════════════════════════
#  云端 Token 获取
# ═══════════════════════════════════════════════════════

async def get_cloud_tokens(username: str, password: str):
    """从小米云端获取所有设备 token"""
    try:
        from micloud import MiCloud
    except ImportError:
        print("请安装 micloud: pip install micloud")
        return

    print("━" * 60)
    print("  从云端获取设备 Token")
    print("━" * 60)

    try:
        cloud = MiCloud(username, password)
        await cloud.login()

        devices = await cloud.get_devices()
        print(f"\n共 {len(devices)} 个设备:\n")
        print(f"{'名称':<25} {'型号':<30} {'Token'}")
        print("─" * 90)

        for d in devices:
            name = d.get("name", "?")
            model = d.get("model", "?")
            token = d.get("token", "N/A")
            ip = d.get("localip", "?")
            print(f"{name:<25} {model:<30} {token}")

    except Exception as e:
        print(f"获取失败: {e}")
        print("可能原因: 账号密码错误、需要验证码、或接口变更")


# ═══════════════════════════════════════════════════════
#  交互模式
# ═══════════════════════════════════════════════════════

async def interactive_mode(ip: str, token: str):
    """交互式控制设备"""
    from miio import Device

    dev = Device(ip, token)
    print("━" * 60)
    print(f"  交互模式: {ip}")
    print("━" * 60)
    print("命令: status | on | off | send <method> <params_json> | quit")

    try:
        while True:
            try:
                cmd = input("\n> ").strip()
            except EOFError:
                break

            if cmd in ("quit", "exit", "q"):
                break
            elif cmd == "status":
                result = await dev.status()
                print(json.dumps(result.__dict__, indent=2, ensure_ascii=False, default=str))
            elif cmd == "on":
                await dev.on()
                print("已开启")
            elif cmd == "off":
                await dev.off()
                print("已关闭")
            elif cmd.startswith("send "):
                parts = cmd.split(None, 2)
                method = parts[1] if len(parts) > 1 else ""
                params = json.loads(parts[2]) if len(parts) > 2 else []
                result = await dev.send(method, params)
                print(json.dumps(result, indent=2, ensure_ascii=False))
            elif cmd:
                print("未知命令。输入 quit 退出。")

    except KeyboardInterrupt:
        print("\n已退出")


# ═══════════════════════════════════════════════════════
#  CLI
# ═══════════════════════════════════════════════════════

async def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(0)

    cmd = sys.argv[1]

    if cmd in ("--help", "-h", "help"):
        print(__doc__)
        sys.exit(0)

    args = sys.argv[2:]

    if cmd == "scan":
        await scan_devices()

    elif cmd == "info":
        if len(args) < 2:
            print("用法: python 03_python_miio_demo.py info <IP> <TOKEN>")
            return
        await get_device_info(args[0], args[1])

    elif cmd == "control":
        if len(args) < 4:
            print("用法: python 03_python_miio_demo.py control <IP> <TOKEN> <TYPE> <ACTION> [ARGS...]")
            return
        await control_device(args[0], args[1], args[2], args[3], args[4:])

    elif cmd == "cloud-token":
        if len(args) < 2:
            print("用法: python 03_python_miio_demo.py cloud-token <USERNAME> <PASSWORD>")
            return
        await get_cloud_tokens(args[0], args[1])

    elif cmd == "interactive":
        if len(args) < 2:
            print("用法: python 03_python_miio_demo.py interactive <IP> <TOKEN>")
            return
        await interactive_mode(args[0], args[1])

    else:
        print(f"未知命令: {cmd}")


if __name__ == "__main__":
    asyncio.run(main())
