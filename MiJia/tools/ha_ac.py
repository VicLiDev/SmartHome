#!/usr/bin/env python3
"""
ha_ac.py — 通过 Home Assistant REST API 控制书房空调

用法:
    python3 ha_ac.py <命令> [参数] [entity_id]

命令:
    status              查询当前状态（默认）
    on                  开机
    off                 关机
    toggle              开关切换
    temp <温度>         设置温度 (16~32)
    mode <模式>         设置模式 (cool/dry/fan_only/heat/off 或 制冷/制热/除湿/送风/关)
    fan <风速>          设置风速 (1~5/auto 或 一档~五档/自动)

默认实体: climate.daikin_cn_x_10357_2544043221_ipbox_601803328177_indoor_10427c64_k5 (书房空调)
"""

import sys
import json
import urllib.request
import urllib.error
import os

DEFAULT_ENTITY = "climate.daikin_cn_x_10357_2544043221_ipbox_601803328177_indoor_10427c64_k5"

MODE_ALIASES = {
    "制冷": "cool", "制热": "heat", "除湿": "dry", "送风": "fan_only", "关": "off",
}

FAN_ALIASES = {
    "1": "一档", "2": "二档", "3": "三档", "4": "四档", "5": "五档",
    "auto": "自动",
}

CONFIG_PATHS = [
    "./config.ini",
    "../config.ini",
    "./mijia_scanner_c/config.ini",
    os.path.expanduser("~/config.ini"),
]


def load_config() -> tuple[str, str]:
    url = token = ""
    for p in CONFIG_PATHS:
        if os.path.isfile(p):
            with open(p) as f:
                for line in f:
                    line = line.strip()
                    if line.startswith("HA_URL="):
                        url = line.split("=", 1)[1]
                    elif line.startswith("HA_TOKEN="):
                        token = line.split("=", 1)[1]
            if url and token:
                return url, token
    print("错误: 找不到 config.ini (需要 HA_URL 和 HA_TOKEN)", file=sys.stderr)
    sys.exit(1)


def ha_api(url: str, token: str, method: str, path: str, data: dict | None = None) -> dict:
    full_url = f"{url}{path}"
    body = json.dumps(data).encode() if data else None
    req = urllib.request.Request(full_url, data=body, method=method)
    req.add_header("Authorization", f"Bearer {token}")
    req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return json.loads(resp.read())
    except urllib.error.HTTPError as e:
        print(f"HTTP {e.code}: {e.read().decode()}", file=sys.stderr)
        sys.exit(1)


def print_status(state: dict, entity: str):
    attrs = state.get("attributes", {})
    name = attrs.get('friendly_name', entity)
    print(f"名称: {name}")
    print(f"状态: {state['state']}")
    print(f"温度: {attrs.get('temperature', '?')}°C")
    print(f"模式: {attrs.get('hvac_mode', '?')}")
    print(f"风速: {attrs.get('fan_mode', '?')}")
    if "current_temperature" in attrs:
        print(f"室温: {attrs['current_temperature']}°C")


def list_climates(url: str, token: str) -> list[dict]:
    """获取所有 climate 实体列表"""
    all_states = ha_api(url, token, "GET", "/api/states")
    climates = [s for s in all_states if s["entity_id"].startswith("climate.")]
    climates.sort(key=lambda s: s["attributes"].get("friendly_name", ""))
    return climates


def print_list(climates: list[dict]):
    """打印 climate 实体列表"""
    if not climates:
        print("未找到 climate 实体")
        return
    for i, s in enumerate(climates):
        attrs = s["attributes"]
        name = attrs.get("friendly_name", "")
        state = "off" if s["state"] == "off" else "on"
        print(f"  [{i + 1}] {name}")
        print(f"      {s['entity_id']}  {state}")


def select_entity(climates: list[dict]) -> str:
    """交互式选择实体"""
    print_list(climates)
    print()
    try:
        choice = int(input("选择实体编号: "))
        if 1 <= choice <= len(climates):
            return climates[choice - 1]["entity_id"]
    except (ValueError, EOFError):
        pass
    print("无效选择", file=sys.stderr)
    sys.exit(1)


def main():
    usage = f"""用法: {sys.argv[0]} <命令> [参数] [entity_id]

命令:
  status              查询当前状态（默认）
  on                  开机
  off                 关机
  toggle              开关切换
  temp <温度>         设置温度 (16~32)
  mode <模式>         设置模式 (cool/dry/fan_only/heat/off)
                     中文别名: 制冷/制热/除湿/送风/关
  fan <风速>          设置风速 (1~5/auto 或 一档~五档/自动)
  list                列出所有空调实体

entity_id 留空使用默认实体，传 - 交互式选择，或传数字序号

默认实体: {DEFAULT_ENTITY} (书房空调)"""

    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(usage)
        sys.exit(0)

    cmd = sys.argv[1]
    valid_cmds = ("status", "on", "off", "toggle", "temp", "mode", "fan", "list")
    if cmd not in valid_cmds:
        print(f"未知命令: {cmd} (支持: {'/'.join(valid_cmds)})", file=sys.stderr)
        sys.exit(1)

    # 解析可选 entity_id 参数
    if cmd in ("temp", "mode", "fan"):
        if len(sys.argv) < 3:
            print(f"错误: {cmd} 命令需要一个参数", file=sys.stderr)
            sys.exit(1)
        arg = sys.argv[2]
        entity = sys.argv[3] if len(sys.argv) >= 4 else DEFAULT_ENTITY
    elif cmd == "list":
        entity = None
    else:
        entity = sys.argv[2] if len(sys.argv) >= 3 else DEFAULT_ENTITY

    url, token = load_config()

    # list 命令
    if cmd == "list":
        climates = list_climates(url, token)
        print_list(climates)
        return

    # 交互式选择或数字序号选择
    if entity == "-" or (entity.isdigit() and len(entity) <= 2):
        climates = list_climates(url, token)
        if entity == "-":
            entity = select_entity(climates)
        else:
            idx = int(entity)
            if idx < 1 or idx > len(climates):
                print(f"无效序号: {idx} (范围 1~{len(climates)})", file=sys.stderr)
                sys.exit(1)
            entity = climates[idx - 1]["entity_id"]

    # 获取 friendly_name (后续命令复用)
    state_info = ha_api(url, token, "GET", f"/api/states/{entity}")
    name = state_info["attributes"].get("friendly_name", entity)

    if cmd == "status":
        print_status(state_info, entity)

    elif cmd == "on":
        ha_api(url, token, "POST", "/api/services/climate/turn_on", {"entity_id": entity})
        print(f"{name} → 开机")

    elif cmd == "off":
        ha_api(url, token, "POST", "/api/services/climate/turn_off", {"entity_id": entity})
        print(f"{name} → 关机")

    elif cmd == "toggle":
        if state_info["state"] == "off":
            ha_api(url, token, "POST", "/api/services/climate/turn_on", {"entity_id": entity})
            print(f"{name} → 开机")
        else:
            ha_api(url, token, "POST", "/api/services/climate/turn_off", {"entity_id": entity})
            print(f"{name} → 关机")

    elif cmd == "temp":
        temp = int(arg)
        if temp < 16 or temp > 32:
            print("错误: 温度范围为 16~32°C", file=sys.stderr)
            sys.exit(1)
        ha_api(url, token, "POST", "/api/services/climate/set_temperature",
               {"entity_id": entity, "temperature": temp})
        print(f"{name} → {temp}°C")

    elif cmd == "mode":
        mode = MODE_ALIASES.get(arg, arg)
        valid_modes = ("cool", "dry", "fan_only", "heat", "off")
        if mode not in valid_modes:
            print(f"错误: 不支持的模式 '{arg}' (支持: {'/'.join(valid_modes)})", file=sys.stderr)
            sys.exit(1)
        if mode == "off":
            ha_api(url, token, "POST", "/api/services/climate/turn_off", {"entity_id": entity})
        else:
            ha_api(url, token, "POST", "/api/services/climate/set_hvac_mode",
                   {"entity_id": entity, "hvac_mode": mode})
        print(f"{name} → {mode}")

    elif cmd == "fan":
        fan = FAN_ALIASES.get(arg, arg)
        ha_api(url, token, "POST", "/api/services/climate/set_fan_mode",
               {"entity_id": entity, "fan_mode": fan})
        print(f"{name} → 风速 {fan}")


if __name__ == "__main__":
    main()
