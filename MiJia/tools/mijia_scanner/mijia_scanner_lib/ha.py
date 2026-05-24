# -*- coding: utf-8 -*-
"""
ha.py — Home Assistant API

从 Home Assistant REST API 获取设备列表、查询单实体状态、调用服务控制。
依赖: color.py
"""

import ssl
import urllib.request
import urllib.error
import json

from .color import Color


def _ha_request(ha_url, token, method="GET", path="", data=None):
    """底层 HA API 请求，返回 JSON dict 或 None"""
    if not token:
        return None
    try:
        url = f"{ha_url}{path}" if ha_url.endswith("/") else f"{ha_url}{path}"
        body = json.dumps(data).encode() if data else None
        req = urllib.request.Request(url, data=body, method=method)
        req.add_header("Authorization", f"Bearer {token}")
        req.add_header("Content-Type", "application/json")
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        resp = urllib.request.urlopen(req, context=ctx, timeout=5)
        return json.loads(resp.read())
    except urllib.error.HTTPError as e:
        try:
            err = json.loads(e.read())
            return {"error": err.get("message", str(e))}
        except Exception:
            return {"error": str(e)}
    except Exception as e:
        return {"error": str(e)}


def ha_get_all_devices(ha_url="http://192.168.6.127:8123", token=""):
    """
    从 Home Assistant REST API 获取所有设备，返回按房间分组的设备列表。
    每个物理设备只保留一条（主要功能实体），带 name、room、type。

    Args:
        ha_url: HA 地址（不含尾部斜杠）
        token: HA long-lived access token

    Returns:
        list[dict]: [{name, room, type}, ...]
    """
    if not token:
        return []

    resp = _ha_request(ha_url, token, "GET", "/api/states")
    if resp is None or "error" in resp:
        if resp:
            print(Color.dim(f"  [HA] 连接失败: {resp['error']}"))
        return []

    states = resp if isinstance(resp, list) else []
    devices = []
    seen = {}

    ROOM_KW = [
        "休闲阳台", "生活阳台", "入户玄关",
        "客厅", "书房", "厨房", "主卧", "次卧", "卧室",
        "玄关", "阳台", "入户", "卫生间", "走廊",
    ]
    KEEP_DOMAINS = {"climate", "light", "switch", "camera", "cover", "sensor", "binary_sensor",
                    "button", "number", "event", "text", "fan", "lock", "select", "update"}

    for s in states:
        eid = s["entity_id"]
        domain = eid.split(".")[0]
        if domain not in KEEP_DOMAINS:
            continue

        name = s["attributes"].get("friendly_name", "")

        room = ""
        for kw in ROOM_KW:
            if kw in name:
                room = kw
                break
        if not room:
            continue

        # 提取设备名：去掉房间前缀 + 清理功能描述
        dname = name.replace(room, "", 1).strip(" \u3000·")
        for junk in ["开关状态切换", "功能异常", "电机反向", "全部开关指示灯状态",
                      "防闪烁模式", "故障", "浸没状态", "电池电量", "设备被重置"]:
            dname = dname.replace(junk, "").strip(" \u3000·")
        if "*" in dname:
            continue

        # 设备类型
        dtype = ""
        for kw in ["智能音箱", "智能开关", "黑板插座", "智能家庭面板", "水浸卫士", "移动检测"]:
            if kw in dname:
                dtype = kw
                break
        if not dtype:
            for kw in ["灯带", "窗帘", "布帘", "纱帘", "水阀", "水浸",
                        "灯", "空调", "音箱", "电视", "插座", "开关", "马桶",
                        "电脑", "监控", "面板", "传感器"]:
                if kw in dname:
                    dtype = kw
                    break

        if not dtype:
            continue

        dedup_key = f"{room}|{dtype}"
        if dedup_key in seen:
            continue
        seen[dedup_key] = True

        if dtype and dname:
            devices.append({"name": dname, "room": room, "type": dtype})

    print(Color.dim(f"  [HA] 获取到 {len(devices)} 个设备（{len(set(d['room'] for d in devices))} 个房间）"))
    return devices


def ha_get_entity_state(ha_url, token, entity_id):
    """
    查询单个实体的 state 字段。

    Args:
        ha_url: HA 地址
        token: HA token
        entity_id: 实体 ID (如 "switch.xxx")

    Returns:
        str: 状态值 (如 "on"/"off"/"unavailable")，失败返回 None
    """
    if not token:
        print(Color.dim("  [HA] 未配置 HA_TOKEN"))
        return None

    resp = _ha_request(ha_url, token, "GET", f"/api/states/{entity_id}")
    if resp is None:
        return None
    if "error" in resp:
        print(Color.red(f"  [HA] 错误: {resp['error']}"))
        return None

    return resp.get("state")


def ha_call_service(ha_url, token, domain, service, entity_id, data=None):
    """
    调用 HA 服务。

    Args:
        ha_url: HA 地址
        token: HA token
        domain: 服务域 (如 "switch", "light")
        service: 服务名 (如 "turn_on", "toggle")
        entity_id: 目标实体 ID
        data: 额外参数 dict (可选)

    Returns:
        bool: 成功返回 True
    """
    if not token:
        print(Color.dim("  [HA] 未配置 HA_TOKEN"))
        return False

    payload = {"entity_id": entity_id}
    if data:
        payload.update(data)

    resp = _ha_request(ha_url, token, "POST", f"/api/services/{domain}/{service}", payload)
    if resp is None or "error" in resp:
        if resp:
            print(Color.red(f"  [HA] 错误: {resp['error']}"))
        return False

    return True
