/**
 * device_db.c — 内置设备型号数据库
 *
 * 对齐 Python 版本: device_db.py
 * 提供 device_db_lookup() 和 device_db_print_all()
 * 无外部依赖。
 */

#include "common.h"

/* 型号条目: (前缀, 中文名称, 设备类型) */
typedef struct {
    const char *prefix;
    const char *name;
    const char *type;
} db_entry_t;

static const db_entry_t DEVICE_DATABASE[] = {
    /* ── 智能灯 ── */
    {"zhimi.light.mono1",      "小米台灯 1",        "智能灯"},
    {"zhimi.light.mono2",      "小米台灯 1S",       "智能灯"},
    {"zhimi.light.mono3",      "小米台灯",          "智能灯"},
    {"zhimi.light.miot5",      "米家 LED 灯泡",     "智能灯"},
    {"zhimi.light.miot6",      "米家智能灯",        "智能灯"},
    {"zhimi.light.color1",     "Yeelight 彩光灯",   "智能灯"},
    {"zhimi.light.color2",     "Yeelight 彩光灯 2", "智能灯"},
    {"zhimi.light.color3",     "Yeelight 彩光灯 3", "智能灯"},
    {"zhimi.light.ceiling1",   "Yeelight 吸顶灯",   "智能灯"},
    {"zhimi.light.ceiling2",   "Yeelight 吸顶灯 2", "智能灯"},
    {"zhimi.light.ceiling3",   "Yeelight 吸顶灯 3", "智能灯"},
    {"zhimi.light.ceiling4",   "Yeelight 吸顶灯 Pro","智能灯"},
    {"zhimi.light.bulb1",      "Yeelight 白光灯泡", "智能灯"},
    {"zhimi.light.strip1",     "Yeelight 灯带",     "智能灯"},
    {"zhimi.light.strip2",     "Yeelight 灯带 2",   "智能灯"},
    {"yeelink.light.color1",   "Yeelight 彩光",     "智能灯"},
    {"yeelink.light.color2",   "Yeelight 彩光灯泡", "智能灯"},
    {"yeelink.light.color3",   "Yeelight 彩光灯泡 3","智能灯"},
    {"yeelink.light.color4",   "Yeelight 彩光灯泡 4","智能灯"},
    {"yeelink.light.color5",   "Yeelight 星空灯",   "智能灯"},
    {"yeelink.light.ceiling1", "Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling2", "Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling3", "Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling4", "Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling5", "Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling6", "Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling7", "Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling8", "Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling9", "Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling10","Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling11","Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling12","Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling13","Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling14","Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling15","Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling16","Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling17","Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling18","Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling19","Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.ceiling20","Yeelight 吸顶灯",   "智能灯"},
    {"yeelink.light.strip1",   "Yeelight 灯带",     "智能灯"},
    {"yeelink.light.strip2",   "Yeelight 灯带",     "智能灯"},
    {"yeelink.light.bulb1",    "Yeelight 灯泡",     "智能灯"},
    {"yeelink.light.bulb2",    "Yeelight 灯泡",     "智能灯"},
    {"yeelink.light.lamp1",    "Yeelight 床头灯",   "智能灯"},
    {"yeelink.light.lamp2",    "Yeelight 床头灯",   "智能灯"},
    {"yeelink.light.lamp3",    "Yeelight 床头灯",   "智能灯"},
    {"yeelink.light.lamp4",    "Yeelight 床头灯",   "智能灯"},
    /* ── 净化器 ── */
    {"zhimi.airpurifier.m1",   "小米空气净化器 2",  "净化器"},
    {"zhimi.airpurifier.m2",   "小米空气净化器 2S", "净化器"},
    {"zhimi.airpurifier.ma1",  "小米空气净化器 Pro","净化器"},
    {"zhimi.airpurifier.ma2",  "小米空气净化器 Pro H","净化器"},
    {"zhimi.airpurifier.mb1",  "小米空气净化器 Max","净化器"},
    {"zhimi.airpurifier.mb2",  "小米空气净化器 Max Pro","净化器"},
    {"zhimi.airpurifier.mc1",  "米家空气净化器",     "净化器"},
    {"zhimi.airpurifier.mc2",  "米家空气净化器 Pro H","净化器"},
    {"zhimi.airpurifier.sa1",  "米家空气净化器 3",   "净化器"},
    {"zhimi.airpurifier.sa2",  "米家空气净化器 3H",  "净化器"},
    {"zhimi.airpurifier.sb1",  "米家空气净化器 4",   "净化器"},
    {"zhimi.airpurifier.va1",  "小米空气净化器",     "净化器"},
    {"zhimi.airpurifier.va2",  "小米空气净化器",     "净化器"},
    {"zhimi.airpurifier.v1",   "小米空气净化器 1",   "净化器"},
    {"zhimi.airpurifier.v2",   "小米空气净化器 2",   "净化器"},
    {"zhimi.airpurifier.v3",   "小米空气净化器",     "净化器"},
    {"zhimi.airpurifier.v5",   "小米空气净化器",     "净化器"},
    {"zhimi.airpurifier.v6",   "小米空气净化器",     "净化器"},
    {"zhimi.airpurifier.v7",   "小米空气净化器 Pro","净化器"},
    {"zhimi.airpurifier.cb1",  "米家空气净化器滤芯", "净化器"},
    {"zhimi.airpurifier.ca1",  "米家空气净化器",     "净化器"},
    /* ── 风扇 ── */
    {"zhimi.fan.sa1",          "米家直流变频落地扇",  "风扇"},
    {"zhimi.fan.v2",           "米家直流变频落地扇 2","风扇"},
    {"zhimi.fan.v3",           "米家直流变频落地扇 3","风扇"},
    {"zhimi.fan.za1",          "米家智能落地扇",      "风扇"},
    {"zhimi.fan.za3",          "米家智能落地扇 3",    "风扇"},
    {"zhimi.fan.za4",          "米家智能落地扇",      "风扇"},
    {"zhimi.fan.zb1",          "米家智能塔扇",        "风扇"},
    {"zhimi.fan.zb2",          "米家智能塔扇",        "风扇"},
    {"zhimi.fan.zb3",          "米家智能塔扇",        "风扇"},
    /* ── 插座/开关 ── */
    {"chuangmi.plug.m1",       "小米智能插座",     "插座"},
    {"chuangmi.plug.m2",       "米家智能插座",     "插座"},
    {"chuangmi.plug.m3",       "小米智能插座增强版","插座"},
    {"chuangmi.plug.v1",       "小米智能插座",     "插座"},
    {"chuangmi.plug.v2",       "米家智能插座 2",   "插座"},
    {"chuangmi.plug.v3",       "米家智能插座 3",   "插座"},
    {"chuangmi.plug.sa1",      "米家智能插座 WiFi 版","插座"},
    {"chuangmi.plug.212a01",   "米家智能插座",     "插座"},
    {"chuangmi.plug.hmi205",   "米家智能插座",     "插座"},
    {"chuangmi.switch.v1",     "小米无线开关",     "开关"},
    {"chuangmi.switch.v2",     "小米无线开关",     "开关"},
    {"cuco.plug.cp1",          "米家智能插座",     "插座"},
    {"cuco.plug.cp2",          "米家智能插座",     "插座"},
    {"cuco.plug.cp2m",         "米家智能插座",     "插座"},
    {"cuco.plug.v1",           "米家智能插座",     "插座"},
    {"cuco.plug.v2",           "米家智能插座",     "插座"},
    {"cuco.plug.v3",           "米家智能插座",     "插座"},
    {"cuco.switch.n1",         "米家智能墙壁开关", "开关"},
    {"cuco.switch.n1ac",       "米家智能墙壁开关", "开关"},
    {"cuco.switch.s1",         "米家智能墙壁开关", "开关"},
    {"cuco.switch.s2",         "米家智能墙壁开关", "开关"},
    /* ── 传感器 ── */
    {"lumi.sensor_ht",         "米家温湿度传感器",  "传感器"},
    {"lumi.sensor_motion.aq2", "米家人体传感器",    "传感器"},
    {"lumi.sensor_magnet.aq2", "米家门磁传感器",    "传感器"},
    {"lumi.sensor_wleak.aq1",  "米家水浸传感器",    "传感器"},
    {"lumi.sensor_cube.aqgl01","米家魔方传感器",    "传感器"},
    {"lumi.weather",           "米家温湿度气压传感器","传感器"},
    {"lumi.sensor_smoke",      "米家烟雾传感器",    "传感器"},
    {"lumi.vibration.aq1",     "米家振动传感器",    "传感器"},
    {"lumi.lock.aq1",          "米家门锁",          "门锁"},
    {"lumi.lock.v1",           "米家门锁",          "门锁"},
    {"lumi.lock.v2",           "米家门锁",          "门锁"},
    {"lumi.relay.c2acn01",     "米家继电器",        "开关"},
    {"lumi.plug",              "米家智能插座",      "插座"},
    {"lumi.plug.mmeu01",       "米家智能插座",      "插座"},
    {"lumi.plug.maus01",       "米家智能插座",      "插座"},
    {"lumi.plug.sac01",        "米家智能插座",      "插座"},
    /* ── 网关 ── */
    {"lumi.gateway.mgl03",     "米家多模网关 2",    "网关"},
    {"lumi.gateway.aqcn02",    "米家多功能网关",    "网关"},
    {"lumi.gateway.v3",        "米家网关 3",        "网关"},
    {"lumi.gw.aq1",            "米家多功能网关",    "网关"},
    {"lumi.gateway.ir",        "米家红外遥控",      "网关"},
    {"lumi.remote.b1acn01",    "米家遥控器",        "遥控器"},
    /* ── 摄像头 ── */
    {"chuangmi.camera.v1",     "小米智能摄像机",    "摄像头"},
    {"chuangmi.camera.v2",     "米家智能摄像机",    "摄像头"},
    {"chuangmi.camera.v3",     "米家智能摄像机",    "摄像头"},
    {"chuangmi.camera.ipc009", "米家智能摄像机",    "摄像头"},
    {"chuangmi.camera.ipc019", "米家智能摄像机",    "摄像头"},
    /* ── 扫地机器人 ── */
    {"rokid.robot.vacuum.m1s", "石头扫地机器人",    "扫地机"},
    {"roborock.vacuum.s5",     "石头扫地机器人 S5", "扫地机"},
    {"roborock.vacuum.s6",     "石头扫地机器人 S6", "扫地机"},
    {"roborock.vacuum.t6",     "石头扫地机器人 T6", "扫地机"},
    {"roborock.vacuum.a10",    "石头扫地机器人 A10","扫地机"},
    {"roborock.vacuum.a15",    "石头扫地机器人",    "扫地机"},
    {"xiaomi.vacuum.v1",       "米家扫地机器人 1",  "扫地机"},
    {"xiaomi.vacuum.v2",       "米家扫地机器人 2",  "扫地机"},
    {"xiaomi.vacuum.v3",       "米家扫地机器人 3",  "扫地机"},
    {"mirobo.vacuum.v1",       "米家扫地机器人",    "扫地机"},
    {"dreame.vacuum.p2008",    "追觅扫地机器人",    "扫地机"},
    {"viomi.vacuum.v5",        "云米扫地机器人",    "扫地机"},
    {"viomi.vacuum.v6",        "云米扫地机器人",    "扫地机"},
    {"viomi.vacuum.v7",        "云米扫地机器人",    "扫地机"},
    {"viomi.vacuum.v8",        "云米扫地机器人",    "扫地机"},
    {"viomi.vacuum.v9",        "云米扫地机器人",    "扫地机"},
    {"dreame.vacuum.p2009",    "追觅扫地机器人",    "扫地机"},
    {"dreame.vacuum.p2010",    "追觅扫地机器人",    "扫地机"},
    {"dreame.vacuum.p2028",    "追觅扫地机器人",    "扫地机"},
    /* ── 空调伴侣/取暖/加湿 ── */
    {"lumi.aircondition.acn04","米家空调伴侣 2",    "空调伴侣"},
    {"lumi.aircondition.mcn04","米家空调伴侣",      "空调伴侣"},
    {"zhimi.heater.ma1",       "米家踢脚线电暖器",  "取暖器"},
    {"zhimi.heater.za1",       "米家智能电暖器",    "取暖器"},
    {"zhimi.heater.za2",       "米家智能电暖器 2",  "取暖器"},
    {"zhimi.humidifier.ca1",   "米家加湿器",        "加湿器"},
    {"zhimi.humidifier.cb1",   "米家加湿器 2",      "加湿器"},
    {"zhimi.humidifier.v1",    "米家加湿器",        "加湿器"},
    /* ── 路由器/中继 ── */
    {"xiaomi.router.hd01",     "小米路由器 HD",     "路由器"},
    {"xiaomi.router.r2100",    "小米路由器 AC2100", "路由器"},
    {"xiaomi.repeater.v1",     "小米WiFi放大器",    "中继器"},
    {"xiaomi.repeater.v2",     "小米WiFi放大器 Pro","中继器"},
    {"xiaomi.repeater.v3",     "小米WiFi放大器 2",  "中继器"},
    /* ── 新风机 ── */
    {"zhimi.airfresh.va1",     "智米新风机",        "新风机"},
    {"zhimi.airfresh.va2",     "智米新风机 2",      "新风机"},
    {"zhimi.airfresh.sa1",     "米家新风机",        "新风机"},
    /* ── 厨电 ── */
    {"chunmi.cooker.press2",   "米家电压力锅",      "厨电"},
    {"chunmi.cooker.normal1",  "米家电饭煲",        "厨电"},
    {"chunmi.cooker.nh1",      "米家电饭煲",        "厨电"},
    {"yunmi.kettle.v1",        "米家电水壶",        "厨电"},
    {"yunmi.kettle.v2",        "米家电水壶",        "厨电"},
    {"cuco.fryer.v1",          "米家空气炸锅",      "厨电"},
    {"cuco.fryer.v2",          "米家空气炸锅",      "厨电"},
    {"cuco.breadmaker.v1",     "米家面包机",        "厨电"},
    {"cuco.smart.bin.v1",      "米家智能垃圾桶",    "家电"},
    /* ── 传感器/健康/个护 ── */
    {"cgllc.sensor.monitor.v1","花花草草监测仪",     "传感器"},
    {"cgllc.sensor.monitor.v2","花花草草监测仪",     "传感器"},
    {"xiaomi.weight.scale.v1", "小米体重秤",        "健康"},
    {"xiaomi.weight.scale.v2", "小米体重秤",        "健康"},
    {"soocare.electric.toothbrush.t1","素士电动牙刷","个护"},
    {"zhimi.hairdryer.v1",     "米家负离子吹风机",  "个护"},
    {"zhimi.hairdryer.z1",     "米家负离子吹风机",  "个护"},
    /* ── 净水器 ── */
    {"yunmi.waterpuri.v1",     "云米净水器",        "净水器"},
    {"yunmi.waterpuri.v2",     "云米净水器",        "净水器"},
    {"yunmi.waterpuri.v3",     "云米净水器",        "净水器"},
    /* ── 投影仪/窗帘/门铃 ── */
    {"xiaomi.projector.v1",    "米家投影仪",        "投影仪"},
    {"xiaomi.projector.mji01", "米家投影仪",        "投影仪"},
    {"lumi.curtain.aq1",       "米家智能窗帘",      "窗帘"},
    {"lumi.curtain.v1",        "米家智能窗帘",      "窗帘"},
    {"chuangmi.doorbell.v1",   "小米智能门铃",      "门铃"},
    {"chuangmi.doorbell.v2",   "小米智能门铃 2",    "门铃"},
    /* ── 洗衣机/冰箱/空调/电视 ── */
    {"viomi.washer.v1",        "云米洗衣机",        "洗衣机"},
    {"viomi.washer.v2",        "云米洗衣机",        "洗衣机"},
    {"viomi.fridge.v1",        "云米冰箱",          "冰箱"},
    {"viomi.fridge.v2",        "云米冰箱",          "冰箱"},
    {"viomi.fridge.v3",        "云米冰箱",          "冰箱"},
    {"viomi.aircondition.v1",  "云米空调",          "空调"},
    {"viomi.aircondition.v2",  "云米空调",          "空调"},
    {"viomi.aircondition.v3",  "云米空调",          "空调"},
    {"viomi.aircondition.v4",  "云米空调",          "空调"},
    {"viomi.aircondition.v5",  "云米空调",          "空调"},
    {"xiaomi.tv.v1",           "小米电视",          "电视"},
    {"xiaomi.tv.stick",        "小米电视棒",        "电视"},
    {"xiaomi.tv.box.r1",       "小米电视盒子",      "电视"},
    {"xiaomi.tv.box.r2",       "小米电视盒子",      "电视"},
    {"xiaomi.tv.box.r3",       "小米电视盒子",      "电视"},
};

#define DB_COUNT (sizeof(DEVICE_DATABASE) / sizeof(DEVICE_DATABASE[0]))

int device_db_count(void) {
    return (int)DB_COUNT;
}

/**
 * 品牌反查表（对齐 Python device_db.py brand_map）
 */
typedef struct {
    const char *brand;
    const char *name;
    const char *type;
} brand_entry_t;

static const brand_entry_t BRAND_MAP[] = {
    {"zhimi",   "智米",     "小米生态链"},
    {"yeelink", "Yeelight", "智能灯"},
    {"chuangmi","创米",     "小米生态链"},
    {"cuco",    "MIoT 插座/开关","小米生态链"},
    {"lumi",    "绿米 Aqara","传感器/网关"},
    {"xiaomi",  "小米",     "小米自营"},
    {"cgllc",   "花花草草", "传感器"},
    {"chunmi",  "纯米",     "厨电"},
    {"yunmi",   "云米",     "家电"},
    {"viomi",   "云米",     "家电"},
    {"dreame",  "追觅",     "扫地机"},
    {"roborock","石头",     "扫地机"},
    {"rokid",   "若琪",     "扫地机"},
    {"soocare", "素士",     "个护"},
};

#define BRAND_COUNT (sizeof(BRAND_MAP) / sizeof(BRAND_MAP[0]))

/**
 * device_db_lookup — 根据型号查询中文名称和类型
 * 对齐 Python device_db.py: lookup_device()
 */
void device_db_lookup(const char *model, char *name_out, char *type_out) {
    name_out[0] = '\0';
    type_out[0] = '\0';

    if (!model || !*model || strcmp(model, "unknown") == 0) {
        strcpy(name_out, "未知设备");
        strcpy(type_out, "未知");
        return;
    }

    /* 精确前缀匹配 */
    for (size_t i = 0; i < DB_COUNT; i++) {
        if (strncmp(model, DEVICE_DATABASE[i].prefix, strlen(DEVICE_DATABASE[i].prefix)) == 0) {
            strcpy(name_out, DEVICE_DATABASE[i].name);
            strcpy(type_out, DEVICE_DATABASE[i].type);
            return;
        }
    }

    /* 品牌反查 */
    for (size_t i = 0; i < BRAND_COUNT; i++) {
        size_t blen = strlen(BRAND_MAP[i].brand);
        if (strncmp(model, BRAND_MAP[i].brand, blen) == 0) {
            snprintf(name_out, MAX_DEV_NAME, "%s 设备 (%s)", BRAND_MAP[i].name, model);
            strcpy(type_out, BRAND_MAP[i].type);
            return;
        }
    }

    /* 未找到 */
    strncpy(name_out, model, MAX_DEV_NAME - 1);
    name_out[MAX_DEV_NAME - 1] = '\0';
    strcpy(type_out, "未知");
}

/**
 * device_db_print_all — 打印全部型号数据库
 * 对齐 Python cmd_models()
 */
void device_db_print_all(void) {
    printf("  %s\n", color_bold("miIO 设备型号数据库"));
    printf("  %s\n", color_dim("共 130+ 个型号"));
    printf("\n");

    /* 按类型分组统计 */
    const char *type_order[] = {
        "智能灯","净化器","风扇","插座","开关","传感器","网关",
        "摄像头","扫地机","空调伴侣","取暖器","加湿器","新风机",
        "厨电","家电","洗衣机","冰箱","空调","电视","路由器",
        "中继器","投影仪","窗帘","遥控器","门锁","门铃",
        "净水器","健康","个护","电源","未知", NULL
    };

    /* 先计算每种类型有多少条 */

    for (int t = 0; type_order[t]; t++) {
        int count = 0;
        for (size_t i = 0; i < DB_COUNT; i++) {
            if (strcmp(DEVICE_DATABASE[i].type, type_order[t]) == 0)
                count++;
        }
        if (count == 0) continue;

        printf("  %s (%d 个)\n", color_cyan(type_order[t]), count);
        for (size_t i = 0; i < DB_COUNT; i++) {
            if (strcmp(DEVICE_DATABASE[i].type, type_order[t]) == 0) {
                char padded[64];
                char *p = pad_cjk(DEVICE_DATABASE[i].prefix, 45);
                strncpy(padded, p, sizeof(padded)-1);
                padded[sizeof(padded)-1] = '\0';
                printf("    %s %s\n", padded, DEVICE_DATABASE[i].name);
            }
        }
        printf("\n");
    }
}
