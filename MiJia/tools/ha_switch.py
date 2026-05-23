#!/usr/bin/env python3
"""
ha_switch.py — 通过 Home Assistant REST API 控制开关

用法:
    python3 ha_switch.py <on|off|toggle|status> [entity_id]

默认实体: switch.cuco_cn_945611612_v3_on_p_2_1 (书房智能插座)
"""

import sys
import json
import urllib.request
import urllib.error
import os

DEFAULT_ENTITY = "switch.cuco_cn_945611612_v3_on_p_2_1"

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


def main():
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(f"用法: {sys.argv[0]} <on|off|toggle|status> [entity_id]")
        print(f"\n默认实体: {DEFAULT_ENTITY} (书房智能插座)")
        sys.exit(0)

    action = sys.argv[1]
    entity = sys.argv[2] if len(sys.argv) >= 3 else DEFAULT_ENTITY

    if action not in ("on", "off", "toggle", "status"):
        print(f"未知命令: {action} (支持 on/off/toggle/status)", file=sys.stderr)
        sys.exit(1)

    url, token = load_config()

    if action == "status":
        state = ha_api(url, token, "GET", f"/api/states/{entity}")
        print(f"实体: {entity}")
        print(f"名称: {state['attributes'].get('friendly_name', '')}")
        print(f"状态: {state['state']}")
    else:
        if action == "toggle":
            state = ha_api(url, token, "GET", f"/api/states/{entity}")
            target = "off" if state["state"] == "on" else "on"
        else:
            target = action

        ha_api(url, token, "POST", f"/api/services/switch/turn_{target}",
               {"entity_id": entity})
        print(f"{entity} → {target}")


if __name__ == "__main__":
    main()
