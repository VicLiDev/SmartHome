# -*- coding: utf-8 -*-
"""
device_db.py — 内置设备型号数据库

提供 DEVICE_DATABASE 和 lookup_device 函数。
根据设备型号前缀查询中文名称和设备类型。
本模块无外部依赖。
"""

# ═══════════════════════════════════════════════════════════
# 内置设备型号数据库（50+ 常见小米设备）
# 格式：(型号前缀, 中文名称, 设备类型)
# ═══════════════════════════════════════════════════════════

DEVICE_DATABASE = [
    # ── 智能灯 ──
    ("zhimi.light.mono1",      "小米台灯 1",             "智能灯"),
    ("zhimi.light.mono2",      "小米台灯 1S",            "智能灯"),
    ("zhimi.light.mono3",      "小米台灯",               "智能灯"),
    ("zhimi.light.miot5",      "米家 LED 灯泡",          "智能灯"),
    ("zhimi.light.miot6",      "米家智能灯",             "智能灯"),
    ("zhimi.light.color1",     "Yeelight 彩光灯",        "智能灯"),
    ("zhimi.light.color2",     "Yeelight 彩光灯 2",      "智能灯"),
    ("zhimi.light.color3",     "Yeelight 彩光灯 3",      "智能灯"),
    ("zhimi.light.ceiling1",   "Yeelight 吸顶灯",        "智能灯"),
    ("zhimi.light.ceiling2",   "Yeelight 吸顶灯 2",      "智能灯"),
    ("zhimi.light.ceiling3",   "Yeelight 吸顶灯 3",      "智能灯"),
    ("zhimi.light.ceiling4",   "Yeelight 吸顶灯 Pro",    "智能灯"),
    ("zhimi.light.bulb1",      "Yeelight 白光灯泡",      "智能灯"),
    ("zhimi.light.strip1",     "Yeelight 灯带",          "智能灯"),
    ("zhimi.light.strip2",     "Yeelight 灯带 2",        "智能灯"),
    ("yeelink.light.color1",   "Yeelight 彩光",          "智能灯"),
    ("yeelink.light.color2",   "Yeelight 彩光灯泡",      "智能灯"),
    ("yeelink.light.color3",   "Yeelight 彩光灯泡 3",    "智能灯"),
    ("yeelink.light.color4",   "Yeelight 彩光灯泡 4",    "智能灯"),
    ("yeelink.light.color5",   "Yeelight 星空灯",        "智能灯"),
    ("yeelink.light.ceiling1", "Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling2", "Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling3", "Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling4", "Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling5", "Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling6", "Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling7", "Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling8", "Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling9", "Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling10","Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling11","Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling12","Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling13","Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling14","Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling15","Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling16","Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling17","Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling18","Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling19","Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.ceiling20","Yeelight 吸顶灯",        "智能灯"),
    ("yeelink.light.strip1",   "Yeelight 灯带",          "智能灯"),
    ("yeelink.light.strip2",   "Yeelight 灯带",          "智能灯"),
    ("yeelink.light.bulb1",    "Yeelight 灯泡",          "智能灯"),
    ("yeelink.light.bulb2",    "Yeelight 灯泡",          "智能灯"),
    ("yeelink.light.lamp1",    "Yeelight 床头灯",        "智能灯"),
    ("yeelink.light.lamp2",    "Yeelight 床头灯",        "智能灯"),
    ("yeelink.light.lamp3",    "Yeelight 床头灯",        "智能灯"),
    ("yeelink.light.lamp4",    "Yeelight 床头灯",        "智能灯"),
    # ── 净化器 ──
    ("zhimi.airpurifier.m1",   "小米空气净化器 2",       "净化器"),
    ("zhimi.airpurifier.m2",   "小米空气净化器 2S",      "净化器"),
    ("zhimi.airpurifier.ma1",  "小米空气净化器 Pro",     "净化器"),
    ("zhimi.airpurifier.ma2",  "小米空气净化器 Pro H",   "净化器"),
    ("zhimi.airpurifier.mb1",  "小米空气净化器 Max",     "净化器"),
    ("zhimi.airpurifier.mb2",  "小米空气净化器 Max Pro", "净化器"),
    ("zhimi.airpurifier.mc1",  "米家空气净化器",          "净化器"),
    ("zhimi.airpurifier.mc2",  "米家空气净化器 Pro H",   "净化器"),
    ("zhimi.airpurifier.sa1",  "米家空气净化器 3",       "净化器"),
    ("zhimi.airpurifier.sa2",  "米家空气净化器 3H",      "净化器"),
    ("zhimi.airpurifier.sb1",  "米家空气净化器 4",       "净化器"),
    ("zhimi.airpurifier.va1",  "小米空气净化器",          "净化器"),
    ("zhimi.airpurifier.va2",  "小米空气净化器",          "净化器"),
    ("zhimi.airpurifier.v1",   "小米空气净化器 1",       "净化器"),
    ("zhimi.airpurifier.v2",   "小米空气净化器 2",       "净化器"),
    ("zhimi.airpurifier.v3",   "小米空气净化器",          "净化器"),
    ("zhimi.airpurifier.v5",   "小米空气净化器",          "净化器"),
    ("zhimi.airpurifier.v6",   "小米空气净化器",          "净化器"),
    ("zhimi.airpurifier.v7",   "小米空气净化器 Pro",     "净化器"),
    ("zhimi.airpurifier.cb1",  "米家空气净化器滤芯",      "净化器"),
    ("zhimi.airpurifier.ca1",  "米家空气净化器",          "净化器"),
    # ── 风扇 ──
    ("zhimi.fan.sa1",          "米家直流变频落地扇",      "风扇"),
    ("zhimi.fan.v2",           "米家直流变频落地扇 2",    "风扇"),
    ("zhimi.fan.v3",           "米家直流变频落地扇 3",    "风扇"),
    ("zhimi.fan.za1",          "米家智能落地扇",          "风扇"),
    ("zhimi.fan.za3",          "米家智能落地扇 3",        "风扇"),
    ("zhimi.fan.za4",          "米家智能落地扇",          "风扇"),
    ("zhimi.fan.zb1",          "米家智能塔扇",            "风扇"),
    ("zhimi.fan.zb2",          "米家智能塔扇",            "风扇"),
    ("zhimi.fan.zb3",          "米家智能塔扇",            "风扇"),
    # ── 插座/开关 ──
    ("chuangmi.plug.m1",       "小米智能插座",            "插座"),
    ("chuangmi.plug.m2",       "米家智能插座",            "插座"),
    ("chuangmi.plug.m3",       "小米智能插座增强版",      "插座"),
    ("chuangmi.plug.v1",       "小米智能插座",            "插座"),
    ("chuangmi.plug.v2",       "米家智能插座 2",          "插座"),
    ("chuangmi.plug.v3",       "米家智能插座 3",          "插座"),
    ("chuangmi.plug.sa1",      "米家智能插座 WiFi 版",    "插座"),
    ("chuangmi.plug.212a01",   "米家智能插座",            "插座"),
    ("chuangmi.plug.hmi205",   "米家智能插座",            "插座"),
    ("chuangmi.switch.v1",     "小米无线开关",            "开关"),
    ("chuangmi.switch.v2",     "小米无线开关",            "开关"),
    ("cuco.plug.cp1",          "米家智能插座",            "插座"),
    ("cuco.plug.cp2",          "米家智能插座",            "插座"),
    ("cuco.plug.cp2m",         "米家智能插座",            "插座"),
    ("cuco.plug.v1",           "米家智能插座",            "插座"),
    ("cuco.plug.v2",           "米家智能插座",            "插座"),
    ("cuco.plug.v3",           "米家智能插座",            "插座"),
    ("cuco.switch.n1",         "米家智能墙壁开关",        "开关"),
    ("cuco.switch.n1ac",       "米家智能墙壁开关",        "开关"),
    ("cuco.switch.s1",         "米家智能墙壁开关",        "开关"),
    ("cuco.switch.s2",         "米家智能墙壁开关",        "开关"),
    # ── 传感器 ──
    ("lumi.sensor_ht",         "米家温湿度传感器",        "传感器"),
    ("lumi.sensor_motion.aq2", "米家人体传感器",          "传感器"),
    ("lumi.sensor_magnet.aq2", "米家门磁传感器",          "传感器"),
    ("lumi.sensor_wleak.aq1",  "米家水浸传感器",          "传感器"),
    ("lumi.sensor_cube.aqgl01","米家魔方传感器",          "传感器"),
    ("lumi.weather",           "米家温湿度气压传感器",     "传感器"),
    ("lumi.sensor_smoke",      "米家烟雾传感器",          "传感器"),
    ("lumi.vibration.aq1",     "米家振动传感器",          "传感器"),
    ("lumi.lock.aq1",          "米家门锁",                "门锁"),
    ("lumi.lock.v1",           "米家门锁",                "门锁"),
    ("lumi.lock.v2",           "米家门锁",                "门锁"),
    ("lumi.relay.c2acn01",     "米家继电器",              "开关"),
    ("lumi.plug",              "米家智能插座",            "插座"),
    ("lumi.plug.mmeu01",       "米家智能插座",            "插座"),
    ("lumi.plug.maus01",       "米家智能插座",            "插座"),
    ("lumi.plug.sac01",        "米家智能插座",            "插座"),
    # ── 网关 ──
    ("lumi.gateway.mgl03",     "米家多模网关 2",         "网关"),
    ("lumi.gateway.aqcn02",    "米家多功能网关",          "网关"),
    ("lumi.gateway.v3",        "米家网关 3",             "网关"),
    ("lumi.gw.aq1",            "米家多功能网关",          "网关"),
    ("lumi.gateway.ir",        "米家红外遥控",            "网关"),
    ("lumi.remote.b1acn01",    "米家遥控器",              "遥控器"),
    # ── 摄像头 ──
    ("chuangmi.camera.v1",     "小米智能摄像机",          "摄像头"),
    ("chuangmi.camera.v2",     "米家智能摄像机",          "摄像头"),
    ("chuangmi.camera.v3",     "米家智能摄像机",          "摄像头"),
    ("chuangmi.camera.ipc009", "米家智能摄像机",          "摄像头"),
    ("chuangmi.camera.ipc019", "米家智能摄像机",          "摄像头"),
    # ── 扫地机器人 ──
    ("rokid.robot.vacuum.m1s", "石头扫地机器人",          "扫地机"),
    ("roborock.vacuum.s5",     "石头扫地机器人 S5",       "扫地机"),
    ("roborock.vacuum.s6",     "石头扫地机器人 S6",       "扫地机"),
    ("roborock.vacuum.t6",     "石头扫地机器人 T6",       "扫地机"),
    ("roborock.vacuum.a10",    "石头扫地机器人 A10",      "扫地机"),
    ("roborock.vacuum.a15",    "石头扫地机器人",          "扫地机"),
    ("xiaomi.vacuum.v1",       "米家扫地机器人 1",        "扫地机"),
    ("xiaomi.vacuum.v2",       "米家扫地机器人 2",        "扫地机"),
    ("xiaomi.vacuum.v3",       "米家扫地机器人 3",        "扫地机"),
    ("mirobo.vacuum.v1",       "米家扫地机器人",          "扫地机"),
    # ── 蒸汽拖地机 / 其他 ──
    ("dreame.vacuum.p2008",    "追觅扫地机器人",          "扫地机"),
    ("viomi.vacuum.v5",        "云米扫地机器人",          "扫地机"),
    ("viomi.vacuum.v6",        "云米扫地机器人",          "扫地机"),
    ("viomi.vacuum.v7",        "云米扫地机器人",          "扫地机"),
    ("viomi.vacuum.v8",        "云米扫地机器人",          "扫地机"),
    ("viomi.vacuum.v9",        "云米扫地机器人",          "扫地机"),
    # ── 空调伴侣 ──
    ("lumi.aircondition.acn04","米家空调伴侣 2",          "空调伴侣"),
    ("lumi.aircondition.mcn04","米家空调伴侣",            "空调伴侣"),
    ("zhimi.heater.ma1",       "米家踢脚线电暖器",        "取暖器"),
    ("zhimi.heater.za1",       "米家智能电暖器",          "取暖器"),
    ("zhimi.heater.za2",       "米家智能电暖器 2",        "取暖器"),
    ("zhimi.humidifier.ca1",   "米家加湿器",              "加湿器"),
    ("zhimi.humidifier.cb1",   "米家加湿器 2",            "加湿器"),
    ("zhimi.humidifier.v1",    "米家加湿器",              "加湿器"),
    # ── 路由器 / 中继 ──
    ("xiaomi.router.hd01",     "小米路由器 HD",           "路由器"),
    ("xiaomi.router.r2100",    "小米路由器 AC2100",       "路由器"),
    ("xiaomi.repeater.v1",     "小米WiFi放大器",          "中继器"),
    ("xiaomi.repeater.v2",     "小米WiFi放大器 Pro",      "中继器"),
    ("xiaomi.repeater.v3",     "小米WiFi放大器 2",        "中继器"),
    # ── 新风机 ──
    ("zhimi.airfresh.va1",     "智米新风机",              "新风机"),
    ("zhimi.airfresh.va2",     "智米新风机 2",            "新风机"),
    ("zhimi.airfresh.sa1",     "米家新风机",              "新风机"),
    # ── 蒸饭煲 / 厨电 ──
    ("chunmi.cooker.press2",   "米家电压力锅",            "厨电"),
    ("chunmi.cooker.normal1",  "米家电饭煲",              "厨电"),
    ("chunmi.cooker.nh1",      "米家电饭煲",              "厨电"),
    # ── 花花草草 / 植物 ──
    ("cgllc.sensor.monitor.v1","花花草草监测仪",          "传感器"),
    ("cgllc.sensor.monitor.v2","花花草草监测仪",          "传感器"),
    # ── 电水壶 / 饮水机 ──
    ("yunmi.kettle.v1",        "米家电水壶",              "厨电"),
    ("yunmi.kettle.v2",        "米家电水壶",              "厨电"),
    # ── 投影仪 ──
    ("xiaomi.projector.v1",    "米家投影仪",              "投影仪"),
    ("xiaomi.projector.mji01", "米家投影仪",              "投影仪"),
    # ── 电动窗帘 ──
    ("lumi.curtain.aq1",       "米家智能窗帘",            "窗帘"),
    ("lumi.curtain.v1",        "米家智能窗帘",            "窗帘"),
    # ── 电动牙刷 ──
    ("soocare.electric.toothbrush.t1", "素士电动牙刷",   "个护"),
    # ── 水质检测 / 净水器 ──
    ("yunmi.waterpuri.v1",     "云米净水器",              "净水器"),
    ("yunmi.waterpuri.v2",     "云米净水器",              "净水器"),
    ("yunmi.waterpuri.v3",     "云米净水器",              "净水器"),
    # ── 空气炸锅 / 烤箱 ──
    ("cuco.fryer.v1",          "米家空气炸锅",            "厨电"),
    ("cuco.fryer.v2",          "米家空气炸锅",            "厨电"),
    # ── 面包机 ──
    ("cuco.breadmaker.v1",     "米家面包机",              "厨电"),
    # ── 垃圾桶 ──
    ("cuco.smart.bin.v1",      "米家智能垃圾桶",          "家电"),
    # ── 体重秤 ──
    ("xiaomi.weight.scale.v1", "小米体重秤",              "健康"),
    ("xiaomi.weight.scale.v2", "小米体重秤",              "健康"),
    # ── 电吹风 ──
    ("zhimi.hairdryer.v1",     "米家负离子吹风机",        "个护"),
    ("zhimi.hairdryer.z1",     "米家负离子吹风机",        "个护"),
    # ── 洗衣机 / 冰箱 / 空调 ──
    ("viomi.washer.v1",        "云米洗衣机",              "洗衣机"),
    ("viomi.washer.v2",        "云米洗衣机",              "洗衣机"),
    ("viomi.fridge.v1",        "云米冰箱",                "冰箱"),
    ("viomi.fridge.v2",        "云米冰箱",                "冰箱"),
    ("viomi.fridge.v3",        "云米冰箱",                "冰箱"),
    ("viomi.aircondition.v1",  "云米空调",                "空调"),
    ("viomi.aircondition.v2",  "云米空调",                "空调"),
    ("viomi.aircondition.v3",  "云米空调",                "空调"),
    ("viomi.aircondition.v4",  "云米空调",                "空调"),
    ("viomi.aircondition.v5",  "云米空调",                "空调"),
    # ── 电视 ──
    ("xiaomi.tv.v1",           "小米电视",                "电视"),
    ("xiaomi.tv.stick",        "小米电视棒",              "电视"),
    ("xiaomi.tv.box.r1",       "小米电视盒子",            "电视"),
    ("xiaomi.tv.box.r2",       "小米电视盒子",            "电视"),
    ("xiaomi.tv.box.r3",       "小米电视盒子",            "电视"),
    # ── 扫地机补充 ──
    ("dreame.vacuum.p2009",    "追觅扫地机器人",          "扫地机"),
    ("dreame.vacuum.p2010",    "追觅扫地机器人",          "扫地机"),
    ("dreame.vacuum.p2028",    "追觅扫地机器人",          "扫地机"),
    # ── 智能门铃 ──
    ("chuangmi.doorbell.v1",   "小米智能门铃",            "门铃"),
    ("chuangmi.doorbell.v2",   "小米智能门铃 2",          "门铃"),
]


def lookup_device(model):
    """根据型号查询中文名称和类型，返回 (名称, 类型)"""
    if not model or model == "unknown":
        return ("未知设备", "未知")
    for prefix, name, dtype in DEVICE_DATABASE:
        if model.startswith(prefix) or model == prefix:
            return (name, dtype)
    # 品牌反查
    brand_map = {
        "zhimi":   ("智米", "小米生态链"),
        "yeelink": ("Yeelight", "智能灯"),
        "chuangmi":("创米", "小米生态链"),
        "cuco":    ("MIoT 插座/开关", "小米生态链"),
        "lumi":    ("绿米 Aqara", "传感器/网关"),
        "xiaomi":  ("小米", "小米自营"),
        "cgllc":   ("花花草草", "传感器"),
        "chunmi":  ("纯米", "厨电"),
        "yunmi":   ("云米", "家电"),
        "viomi":   ("云米", "家电"),
        "dreame":  ("追觅", "扫地机"),
        "roborock":("石头", "扫地机"),
        "rokid":   ("若琪", "扫地机"),
        "soocare": ("素士", "个护"),
    }
    for brand, (name, dtype) in brand_map.items():
        if model.startswith(brand):
            return (name + " 设备 (" + model + ")", dtype)
    return (model, "未知")
