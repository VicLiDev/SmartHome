# -*- coding: utf-8 -*-
"""
ha.py — Home Assistant API

从 Home Assistant REST API 获取设备列表，按房间分组。
依赖: color.py
"""

import ssl
import urllib.request
import json

from .color import Color


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

    devices = []
    seen = {}  # dkey -> (priority, name, room, dtype) — 只保留最高优先级

    try:
        req = urllib.request.Request(
            f"{ha_url}/api/states",
            headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
        )
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        resp = urllib.request.urlopen(req, context=ctx, timeout=5)
        states = json.loads(resp.read())
    except Exception as e:
        print(Color.dim(f"  [HA] 连接失败: {e}"))
        return []

    ROOM_KW = [
        "休闲阳台", "生活阳台", "入户玄关",
        "客厅", "书房", "厨房", "主卧", "次卧", "卧室",
        "玄关", "阳台", "入户", "卫生间", "走廊",
    ]
    # domain 优先级：只保留这些 domain 的实体
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

        # 去重 key：用"房间 + 设备类型"组合
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
