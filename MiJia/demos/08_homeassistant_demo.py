"""
Demo 8: Home Assistant REST API 交互 — 通过 REST API 管理智能家居

功能:
  - 获取 Home Assistant 运行状态
  - 列出所有实体（实体状态概览）
  - 查询指定实体状态
  - 调用服务（控制设备、执行动作）
  - 列出所有自动化规则
  - 查询实体历史数据
  - 展示推荐的 configuration.yaml 配置片段

架构:
  本脚本 ←(HTTP REST)→ Home Assistant ←(各种集成)→ 智能设备

Home Assistant REST API 端点:
  GET  /api/                              — 获取 HA 状态信息
  GET  /api/states                        — 列出所有实体状态
  GET  /api/states/{entity_id}            — 获取指定实体状态
  POST /api/services/{domain}/{service}   — 调用服务
  GET  /api/history/period/{timestamp}    — 获取历史数据
  GET  /api/config                        — 获取 HA 配置信息
  GET  /api/services                      — 列出所有已注册服务

前置条件:
  1. 已安装并运行 Home Assistant: https://www.home-assistant.io/
  2. 已创建长期访问令牌 (Long-Lived Access Token)
     路径: 个人资料页面 → 滚动到底部 → 长期访问令牌

依赖: pip install requests

用法:
  # 获取 HA 状态
  python 08_homeassistant_demo.py --host 192.168.1.100 --token YOUR_TOKEN status

  # 列出所有实体
  python 08_homeassistant_demo.py --host 192.168.1.100 --token YOUR_TOKEN entities

  # 查询指定实体状态
  python 08_homeassistant_demo.py --host 192.168.1.100 --token YOUR_TOKEN get climate.living_room_ac

  # 调用服务 — 打开客厅灯
  python 08_homeassistant_demo.py --host 192.168.1.100 --token YOUR_TOKEN call light turn_on '{"entity_id":"light.living_room"}'

  # 调用服务 — 设置空调温度
  python 08_homeassistant_demo.py --host 192.168.1.100 --token YOUR_TOKEN call climate set_temperature '{"entity_id":"climate.living_room_ac","temperature":26}'

  # 列出所有自动化
  python 08_homeassistant_demo.py --host 192.168.1.100 --token YOUR_TOKEN automations

  # 查询实体历史（最近 2 小时）
  python 08_homeassistant_demo.py --host 192.168.1.100 --token YOUR_TOKEN history sensor.living_room_temperature 2

  # 查看推荐配置
  python 08_homeassistant_demo.py config

  # 指定端口
  python 08_homeassistant_demo.py --host 192.168.1.100 --port 8123 --token YOUR_TOKEN status
"""

import json
import sys
import time
import logging
from datetime import datetime, timedelta
from typing import Optional, Any, Dict, List

try:
    import requests
except ImportError:
    print("请先安装依赖: pip install requests")
    sys.exit(1)


# ═══════════════════════════════════════════════════════
#  日志配置
# ═══════════════════════════════════════════════════════

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
log = logging.getLogger("ha_demo")


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
    "host": "localhost",         # Home Assistant 主机地址
    "port": 8123,                # Home Assistant 端口
    "token": "",                 # 长期访问令牌（Long-Lived Access Token）
    "timeout": 10,               # 请求超时（秒）
    "use_ssl": False,            # 是否使用 HTTPS
    # 常用服务调用示例
    "service_examples": {
        # 灯光控制
        "light_on": {
            "domain": "light",
            "service": "turn_on",
            "data": {"entity_id": "light.living_room"},
        },
        "light_off": {
            "domain": "light",
            "service": "turn_off",
            "data": {"entity_id": "light.living_room"},
        },
        "light_set_brightness": {
            "domain": "light",
            "service": "turn_on",
            "data": {"entity_id": "light.living_room", "brightness_pct": 50},
        },
        # 空调控制
        "ac_on": {
            "domain": "climate",
            "service": "turn_on",
            "data": {"entity_id": "climate.living_room_ac"},
        },
        "ac_set_temp": {
            "domain": "climate",
            "service": "set_temperature",
            "data": {"entity_id": "climate.living_room_ac", "temperature": 26},
        },
        "ac_set_hvac_mode": {
            "domain": "climate",
            "service": "set_hvac_mode",
            "data": {"entity_id": "climate.living_room_ac", "hvac_mode": "cool"},
        },
        # 扫地机器人
        "vacuum_start": {
            "domain": "vacuum",
            "service": "start",
            "data": {"entity_id": "vacuum.roborock"},
        },
        "vacuum_pause": {
            "domain": "vacuum",
            "service": "pause",
            "data": {"entity_id": "vacuum.roborock"},
        },
        "vacuum_return_to_base": {
            "domain": "vacuum",
            "service": "return_to_base",
            "data": {"entity_id": "vacuum.roborock"},
        },
        # 场景控制
        "scene_activate": {
            "domain": "scene",
            "service": "turn_on",
            "data": {"entity_id": "scene.movie_night"},
        },
        # 脚本控制
        "script_run": {
            "domain": "script",
            "service": "turn_on",
            "data": {"entity_id": "script.good_morning"},
        },
        # 通知
        "notify_mobile": {
            "domain": "notify",
            "service": "mobile_app_phone",
            "data": {"title": "提示", "message": "这是一条来自 Home Assistant 的通知"},
        },
    },
}


# ═══════════════════════════════════════════════════════
#  Home Assistant REST API 客户端
# ═══════════════════════════════════════════════════════

class HomeAssistantClient:
    """Home Assistant REST API 客户端

    通过 HTTP REST API 与 Home Assistant 交互。
    使用长期访问令牌 (Long-Lived Access Token) 进行认证。

    认证方式:
      在 HTTP 请求头中添加:
        Authorization: Bearer <长期访问令牌>

    获取令牌:
      1. 打开 Home Assistant Web 界面
      2. 点击左下角用户头像 → 个人资料
      3. 滚动到页面底部 → 创建令牌
    """

    def __init__(
        self,
        host: str = "localhost",
        port: int = 8123,
        token: str = "",
        timeout: int = 10,
        use_ssl: bool = False,
    ):
        """初始化 Home Assistant 客户端

        Args:
            host: Home Assistant 主机地址
            port: Home Assistant 端口（默认 8123）
            token: 长期访问令牌
            timeout: 请求超时时间（秒）
            use_ssl: 是否使用 HTTPS
        """
        self.host = host
        self.port = port
        self.token = token
        self.timeout = timeout
        self.use_ssl = use_ssl

        # 构建 base URL
        scheme = "https" if use_ssl else "http"
        self.base_url = f"{scheme}://{host}:{port}/api"

        # 请求会话
        self.session = requests.Session()
        self.session.headers.update({
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        })

    def _url(self, path: str = "") -> str:
        """构建完整的 API URL

        Args:
            path: API 路径（如 'states' 或 'states/light.living_room'）

        Returns:
            完整的 URL 字符串
        """
        url = self.base_url
        if path:
            url = f"{self.base_url}/{path}"
        return url

    def _request(
        self,
        method: str = "GET",
        path: str = "",
        data: Optional[dict] = None,
        params: Optional[dict] = None,
    ) -> Optional[dict]:
        """发送 HTTP 请求到 Home Assistant API

        Args:
            method: HTTP 方法 (GET/POST)
            path: API 路径
            data: POST 请求的 JSON 数据
            params: URL 查询参数

        Returns:
            响应的 JSON 字典，出错返回 None
        """
        url = self._url(path)
        log.debug(f"{method} {url}")

        try:
            if method.upper() == "GET":
                resp = self.session.get(url, params=params, timeout=self.timeout)
            elif method.upper() == "POST":
                resp = self.session.post(url, json=data, timeout=self.timeout)
            else:
                log.error(f"不支持的 HTTP 方法: {method}")
                return None

            # 检查状态码
            if resp.status_code == 200:
                try:
                    return resp.json()
                except json.JSONDecodeError:
                    # 某些端点可能返回空内容或非 JSON
                    return {"status_code": resp.status_code, "text": resp.text}

            elif resp.status_code == 401:
                log.error("认证失败: 令牌无效或已过期")
                print("  [错误] 401 Unauthorized — 请检查访问令牌是否正确")
                print("  获取令牌: HA 界面 → 个人资料 → 滚动到底部 → 长期访问令牌")
                return None

            elif resp.status_code == 404:
                log.error(f"资源未找到: {path}")
                print(f"  [错误] 404 Not Found — 请求的资源不存在: {path}")
                return None

            else:
                log.error(f"请求失败: HTTP {resp.status_code}")
                try:
                    error_msg = resp.json().get("message", resp.text)
                except Exception:
                    error_msg = resp.text
                print(f"  [错误] HTTP {resp.status_code}: {error_msg}")
                return None

        except requests.exceptions.ConnectionError:
            log.error(f"连接失败: 无法连接到 {self.host}:{self.port}")
            print(f"  [错误] 无法连接到 Home Assistant ({self.host}:{self.port})")
            print("  请确认:")
            print("    1. Home Assistant 正在运行")
            print("    2. 主机地址和端口正确")
            print(f"    3. 防火墙允许访问端口 {self.port}")
            return None

        except requests.exceptions.Timeout:
            log.error(f"请求超时: {method} {path}")
            print(f"  [错误] 请求超时（{self.timeout}秒）")
            print("  Home Assistant 可能负载较高，请稍后重试")
            return None

        except requests.exceptions.RequestException as e:
            log.error(f"请求异常: {e}")
            print(f"  [错误] 请求异常: {e}")
            return None

    # ═══════════════════════════════════════════════════
    #  业务功能
    # ═══════════════════════════════════════════════════

    def get_status(self) -> Optional[dict]:
        """获取 Home Assistant 运行状态

        调用 GET /api/ 获取 HA 基本信息，包括版本、位置等。

        Returns:
            HA 状态字典，包含:
              - message: 状态消息
              - version: HA 版本号
        """
        print_separator("Home Assistant 运行状态")

        data = self._request("GET", "")
        if data is None:
            return None

        print(f"\n  状态: {data.get('message', '未知')}")
        print(f"  版本: {data.get('version', '未知')}")

        # 获取配置信息以展示更多详情
        config = self._request("GET", "config")
        if config:
            location = config.get("location_name", "未知")
            lat = config.get("latitude", "未知")
            lon = config.get("longitude", "未知")
            tz = config.get("time_zone", "未知")
            unit = config.get("unit_system", {}).get("length", "未知")
            currency = config.get("currency", "未知")

            print(f"  位置: {location}")
            print(f"  坐标: ({lat}, {lon})")
            print(f"  时区: {tz}")
            print(f"  单位: {unit}")
            print(f"  货币: {currency}")

        return data

    def list_entities(self, domain_filter: Optional[str] = None) -> List[dict]:
        """列出所有实体

        调用 GET /api/states 获取所有实体的当前状态。
        可按 domain 过滤（如 'light', 'sensor', 'climate'）。

        Args:
            domain_filter: 实体域过滤器（可选），如 'sensor'

        Returns:
            实体状态列表
        """
        print_separator("列出所有实体")

        data = self._request("GET", "states")
        if data is None:
            return []

        # 按域过滤
        if domain_filter:
            entities = [e for e in data if e.get("entity_id", "").startswith(f"{domain_filter}.")]
        else:
            entities = data

        # 按 domain 分组统计
        domain_counts: Dict[str, int] = {}
        for entity in entities:
            entity_id = entity.get("entity_id", "")
            domain = entity_id.split(".")[0] if "." in entity_id else "unknown"
            domain_counts[domain] = domain_counts.get(domain, 0) + 1

        print(f"\n  共 {len(entities)} 个实体\n")

        print("  ┌─ 实体域统计 ──────────────────────────────")
        for domain, count in sorted(domain_counts.items(), key=lambda x: -x[1]):
            print(f"  │  {domain:20s}: {count:4d} 个")
        print("  └──────────────────────────────────────────\n")

        # 打印每个实体的简要信息
        for i, entity in enumerate(entities[:100]):  # 最多显示 100 个
            entity_id = entity.get("entity_id", "未知")
            state = entity.get("state", "未知")
            attributes = entity.get("attributes", {})
            friendly_name = attributes.get("friendly_name", entity_id)
            last_changed = entity.get("last_changed", "未知")

            # 状态图标
            if state in ("on", "home", "active", "locked"):
                icon = "●"
            elif state in ("off", "not_home", "unlocked", "idle"):
                icon = "○"
            elif state in ("unavailable", "unknown"):
                icon = "✕"
            else:
                icon = "◆"

            # 格式化时间
            if last_changed != "未知":
                try:
                    dt = datetime.fromisoformat(last_changed.replace("Z", "+00:00"))
                    time_str = dt.strftime("%H:%M:%S")
                except (ValueError, AttributeError):
                    time_str = last_changed[:8] if len(last_changed) >= 8 else last_changed
            else:
                time_str = "—"

            # 对于 sensor 类型，显示单位
            unit = attributes.get("unit_of_measurement", "")
            if unit:
                state_display = f"{state} {unit}"
            else:
                state_display = str(state)

            print(f"  [{i + 1:3d}] {icon} {entity_id}")
            print(f"         名称: {friendly_name}")
            print(f"         状态: {state_display:<20s}  最后变更: {time_str}")

        if len(entities) > 100:
            print(f"\n  ... 还有 {len(entities) - 100} 个实体未显示")
            print(f"  使用 domain 过滤查看特定类型，如: entities --domain sensor")

        return entities

    def get_entity(self, entity_id: str) -> Optional[dict]:
        """获取指定实体状态

        调用 GET /api/states/{entity_id} 获取单个实体的完整状态。

        Args:
            entity_id: 实体 ID（如 'light.living_room'）

        Returns:
            实体状态字典
        """
        print_separator(f"实体状态: {entity_id}")

        data = self._request("GET", f"states/{entity_id}")
        if data is None:
            return None

        # 显示实体信息
        state = data.get("state", "未知")
        attributes = data.get("attributes", {})
        friendly_name = attributes.get("friendly_name", entity_id)
        last_changed = data.get("last_changed", "未知")
        last_updated = data.get("last_updated", "未知")
        context = data.get("context", {})

        print(f"\n  实体 ID:   {entity_id}")
        print(f"  名称:     {friendly_name}")
        print(f"  状态:     {state}")
        print(f"  最后变更: {last_changed}")
        print(f"  最后更新: {last_updated}")

        # 显示属性
        if attributes:
            print(f"\n  ┌─ 属性 ────────────────────────────────────")
            for key, value in attributes.items():
                if key == "friendly_name":
                    continue  # 已单独显示
                if isinstance(value, (dict, list)):
                    value_str = pretty_json(value, indent=4)
                    print(f"  │  {key}: {value_str}")
                else:
                    print(f"  │  {key}: {value}")
            print("  └──────────────────────────────────────────")

        return data

    def call_service(
        self,
        domain: str,
        service: str,
        entity_id: Optional[str] = None,
        service_data: Optional[dict] = None,
    ) -> bool:
        """调用 Home Assistant 服务

        调用 POST /api/services/{domain}/{service} 执行服务调用。

        Home Assistant 通过「服务」来控制设备和执行动作。
        服务由 domain 和 service 两部分组成，例如:
          - light.turn_on    — 打开灯
          - climate.set_temperature — 设置空调温度
          - script.turn_on   — 执行脚本

        Args:
            domain: 服务域（如 'light', 'climate', 'script'）
            service: 服务名（如 'turn_on', 'set_temperature'）
            entity_id: 目标实体 ID（可选）
            service_data: 额外的服务数据（可选）

        Returns:
            是否调用成功
        """
        print_separator(f"调用服务: {domain}.{service}")

        # 构建服务数据
        data = service_data or {}
        if entity_id:
            data["entity_id"] = entity_id

        print(f"\n  域:     {domain}")
        print(f"  服务:   {service}")
        if entity_id:
            print(f"  实体:   {entity_id}")
        if data:
            print(f"  数据:   {pretty_json(data)}")

        result = self._request("POST", f"services/{domain}/{service}", data=data)
        if result is None:
            return False

        print(f"\n  [成功] 服务调用已执行")
        return True

    def list_automations(self) -> List[dict]:
        """列出所有自动化规则

        从 GET /api/states 中过滤以 'automation.' 开头的实体。
        每个自动化实体包含其触发条件、动作等配置信息。

        Returns:
            自动化实体列表
        """
        print_separator("列出所有自动化")

        # 获取所有状态，然后过滤 automation 域
        data = self._request("GET", "states")
        if data is None:
            return []

        automations = [
            e for e in data
            if e.get("entity_id", "").startswith("automation.")
        ]

        print(f"\n  共 {len(automations)} 条自动化规则\n")

        for i, auto in enumerate(automations):
            entity_id = auto.get("entity_id", "未知")
            state = auto.get("state", "未知")
            attributes = auto.get("attributes", {})
            friendly_name = attributes.get("friendly_name", entity_id)

            # 获取自动化 ID（去掉 automation. 前缀）
            auto_id = entity_id.replace("automation.", "")

            # 状态标记
            status_icon = "●" if state == "on" else "○"
            status_text = "已启用" if state == "on" else "已禁用"

            print(f"  [{i + 1:2d}] {status_icon} {friendly_name}")
            print(f"       ID:   {auto_id}")
            print(f"       状态: {status_text}")
            print(f"       实体: {entity_id}")

            # 显示最后触发时间
            last_triggered = attributes.get("last_triggered")
            if last_triggered:
                try:
                    dt = datetime.fromisoformat(last_triggered.replace("Z", "+00:00"))
                    print(f"       上次触发: {dt.strftime('%Y-%m-%d %H:%M:%S')}")
                except (ValueError, AttributeError):
                    print(f"       上次触发: {last_triggered}")

            # 显示触发器信息
            triggers = attributes.get("trigger", [])
            if isinstance(triggers, list) and triggers:
                trigger_types = [t.get("platform", "未知") for t in triggers]
                print(f"       触发器: {', '.join(trigger_types)}")

            # 显示动作数量
            actions = attributes.get("action", [])
            if isinstance(actions, list):
                print(f"       动作数: {len(actions)}")

            print()

        if not automations:
            print("  暂无自动化规则")
            print("  可在 HA 界面的「自动化」页面创建，或在 configuration.yaml 中定义")

        return automations

    def get_history(
        self,
        entity_id: str,
        hours: int = 1,
    ) -> Optional[List[dict]]:
        """获取实体历史数据

        调用 GET /api/history/period/{timestamp} 获取指定时间范围内的状态变更记录。

        注意: 需要在 HA 配置中启用 recorder 集成（默认已启用）。

        Args:
            entity_id: 实体 ID
            hours: 查询最近多少小时的数据（默认 1 小时）

        Returns:
            历史数据列表
        """
        print_separator(f"实体历史: {entity_id}")

        # 计算起始时间
        end_time = datetime.utcnow()
        start_time = end_time - timedelta(hours=hours)

        # 格式化时间戳（ISO 8601）
        start_ts = start_time.strftime("%Y-%m-%dT%H:%M:%S")
        end_ts = end_time.strftime("%Y-%m-%dT%H:%M:%S")

        print(f"\n  实体:       {entity_id}")
        print(f"  时间范围:   最近 {hours} 小时")
        print(f"  起始时间:   {start_ts} UTC")
        print(f"  结束时间:   {end_ts} UTC")

        params = {
            "filter_entity_id": entity_id,
            "end_time": f"{end_ts}Z",
        }

        data = self._request("GET", f"history/period/{start_ts}", params=params)
        if data is None:
            return None

        # data 是一个列表的列表，每个子列表对应一个实体
        total_changes = 0
        for entity_history in data:
            if isinstance(entity_history, list):
                total_changes += len(entity_history)

        if total_changes == 0:
            print(f"\n  在指定时间范围内没有找到历史记录")
            print("  注意: 需要 recorder 集成正常运行")
            return []

        print(f"\n  共 {total_changes} 条状态变更记录\n")
        print("  ┌─ 时间 ──────────── 状态 ─────────────────────────")

        for entity_history in data:
            if not isinstance(entity_history, list):
                continue
            for record in entity_history:
                state = record.get("state", "未知")
                last_changed = record.get("last_changed", "未知")
                attributes = record.get("attributes", {})

                # 格式化时间
                try:
                    dt = datetime.fromisoformat(last_changed.replace("Z", "+00:00"))
                    time_str = dt.strftime("%Y-%m-%d %H:%M:%S")
                except (ValueError, AttributeError):
                    time_str = last_changed[:19]

                # 对于带单位的状态
                unit = attributes.get("unit_of_measurement", "")
                if unit:
                    state_display = f"{state} {unit}"
                else:
                    state_display = str(state)

                print(f"  │  {time_str}  {state_display}")

        print("  └────────────────────────────────────────────────")

        return data

    def show_config(self) -> None:
        """展示推荐的 Home Assistant configuration.yaml 配置片段

        包含:
          1. Xiaomi Miot Auto 集成配置
          2. REST Sensor 自定义网关集成
          3. 示例自动化: 温度触发空调
          4. 示例自动化: 人体感应触发灯光
        """
        print_separator("推荐 configuration.yaml 配置")

        config_yaml = """
# ═══════════════════════════════════════════════════════════════
#  Xiaomi Miot Auto 集成配置
# ═══════════════════════════════════════════════════════════════
# HACS 安装: https://github.com/al-one/hass-xiaomi-miot
# 支持大量小米设备: 空调、灯、插座、扫地机器人、传感器等

# 方式一: 使用小米账号登录（推荐）
# 自动发现账号下的所有设备，无需逐个配置
xiaomi_miot_auto:
  # 小米账号登录
  accounts:
    - username: !secret xiaomi_username    # 小米账号（手机号/邮箱）
      password: !secret xiaomi_password    # 小米密码
      servers: ["cn"]                      # 服务器区域: cn(大陆), sg(新加坡), etc.
  # 全局配置
  update_interval: 30           # 状态更新间隔（秒）
  include_devices:              # 只包含指定设备（可选）
    - "zhimi-aircondition-mc2"  # 设备型号
  exclude_devices:              # 排除指定设备（可选）
    - "lumi.sensor_ht.v1"

# 方式二: 使用 token 直接连接局域网设备
# 适合不需要云端的纯本地控制场景
xiaomi_miot_auto:
  # 使用设备 token 直接连接（局域网）
  integration_type: local      # 本地集成模式
  local_discovery: true        # 自动发现局域网设备
  devices:
    - host: 192.168.1.xxx       # 设备 IP
      token: xxxxxxxxxxxxxxxx   # 设备 Token
      model: zhimi-aircondition-mc2  # 设备型号（可选，自动检测）
    - host: 192.168.1.yyy
      token: yyyyyyyyyyyyyyyy
      model: yeelink-light-color2


# ═══════════════════════════════════════════════════════════════
#  REST Sensor — 自定义网关集成
# ═══════════════════════════════════════════════════════════════
# 从自定义 HTTP API 获取数据（如自建网关、第三方服务）

rest:
  # 温湿度传感器
  - resource: http://192.168.1.100:8080/api/sensors/temperature
    method: GET
    name: "室内温度"
    device_class: temperature
    unit_of_measurement: "°C"
    value_template: "{{ value_json.temperature | round(1) }}"
    scan_interval: 30

  - resource: http://192.168.1.100:8080/api/sensors/humidity
    method: GET
    name: "室内湿度"
    device_class: humidity
    unit_of_measurement: "%"
    value_template: "{{ value_json.humidity | round(1) }}"
    scan_interval: 30

  # 空气质量（PM2.5）
  - resource: http://192.168.1.100:8080/api/sensors/air_quality
    method: GET
    name: "PM2.5"
    device_class: pm25
    unit_of_measurement: "μg/m³"
    value_template: "{{ value_json.pm25 }}"
    scan_interval: 60

  # 电量统计
  - resource: http://192.168.1.100:8080/api/power/total
    method: GET
    name: "今日用电量"
    device_class: energy
    unit_of_measurement: "kWh"
    value_template: "{{ value_json.today_kwh | round(2) }}"
    scan_interval: 300


# ═══════════════════════════════════════════════════════════════
#  示例自动化
# ═══════════════════════════════════════════════════════════════

automation:

  # 自动化 1: 温度高于阈值自动开空调
  - id: "auto_ac_on_high_temp"
    alias: "高温自动开空调"
    description: "室内温度超过 28°C 时自动开启空调制冷模式"
    mode: single
    trigger:
      - platform: numeric_state
        entity_id: sensor.indoor_temperature
        above: 28
        for:
          minutes: 5          # 持续 5 分钟才触发，避免短暂波动
    condition:
      # 条件: 空调当前处于关闭状态
      - condition: state
        entity_id: climate.living_room_ac
        state: "off"
      # 条件: 有人在客厅
      - condition: state
        entity_id: binary_sensor.living_room_occupancy
        state: "on"
    action:
      - service: climate.turn_on
        target:
          entity_id: climate.living_room_ac
      - service: climate.set_temperature
        target:
          entity_id: climate.living_room_ac
        data:
          temperature: 26
          hvac_mode: cool
      - service: notify.mobile_app_phone
        data:
          title: "🏠 智能家居"
          message: "室内温度超过 28°C，已自动开启空调制冷至 26°C"

  # 自动化 2: 温度恢复正常自动关空调
  - id: "auto_ac_off_normal_temp"
    alias: "温度正常自动关空调"
    description: "室内温度降到 24°C 以下时自动关闭空调"
    mode: single
    trigger:
      - platform: numeric_state
        entity_id: sensor.indoor_temperature
        below: 24
        for:
          minutes: 10
    action:
      - service: climate.turn_off
        target:
          entity_id: climate.living_room_ac

  # 自动化 3: 人体感应触发灯光
  - id: "auto_light_occupancy"
    alias: "人来灯亮，人走灯灭"
    description: "检测到有人自动开灯，无人 5 分钟后关灯"
    mode: restart
    trigger:
      - platform: state
        entity_id: binary_sensor.living_room_occupancy
        to: "on"
        id: "person_arrived"
      - platform: state
        entity_id: binary_sensor.living_room_occupancy
        to: "off"
        for:
          minutes: 5
        id: "person_left"
    action:
      - choose:
          # 人来了 → 开灯
          - conditions:
              - condition: trigger
                id: "person_arrived"
            sequence:
              - service: light.turn_on
                target:
                  entity_id: light.living_room
                data:
                  brightness_pct: 80
                  transition: 1
          # 人走了 → 关灯
          - conditions:
              - condition: trigger
                id: "person_left"
            sequence:
              - service: light.turn_off
                target:
                  entity_id: light.living_room
                data:
                  transition: 5

  # 自动化 4: 睡前场景
  - id: "auto_bedtime_scene"
    alias: "睡前模式"
    description: "晚上 22:30 自动执行睡前场景"
    mode: single
    trigger:
      - platform: time
        at: "22:30:00"
    condition:
      # 只在工作日执行
      - condition: time
        weekday:
          - mon
          - tue
          - wed
          - thu
          - fri
    action:
      - service: light.turn_off
        target:
          entity_id: light.living_room
      - service: light.turn_on
        target:
          entity_id: light.bedroom
        data:
          brightness_pct: 20
          color_temp: 450
      - service: climate.set_temperature
        target:
          entity_id: climate.bedroom_ac
        data:
          temperature: 25
          hvac_mode: cool
      - service: lock.lock
        target:
          entity_id: lock.front_door

  # 自动化 5: 空气质量差通知
  - id: "auto_air_quality_alert"
    alias: "空气质量警告"
    description: "PM2.5 超过 100 时发送通知并开启空气净化器"
    mode: single
    trigger:
      - platform: numeric_state
        entity_id: sensor.pm25
        above: 100
    action:
      - service: fan.turn_on
        target:
          entity_id: fan.air_purifier
        data:
          percentage: 100
      - service: notify.mobile_app_phone
        data:
          title: "⚠️ 空气质量警告"
          message: "PM2.5 超过 100 μg/m³，已自动开启空气净化器"


# ═══════════════════════════════════════════════════════════════
#  secrets.yaml 模板（敏感信息不要直接写在 YAML 中）
# ═══════════════════════════════════════════════════════════════
# 创建 configuration 目录下的 secrets.yaml:

# xiaomi_username: "your_phone_number"
# xiaomi_password: "your_xiaomi_password"
# ha_token: "your_long_lived_access_token"

# 使用时用 !secret 引用:
#   password: !secret xiaomi_password
"""
        print(config_yaml)

        print_separator("配置说明", char="-")
        print("""
  1. Xiaomi Miot Auto 集成:
     - 通过 HACS (Home Assistant Community Store) 安装
     - 仓库地址: https://github.com/al-one/hass-xiaomi-miot
     - 安装后在 HACS → 集成 中搜索 "Xiaomi Miot Auto"
     - 建议使用账号登录方式，自动发现设备

  2. REST Sensor:
     - 用于从自定义 HTTP API 获取数据
     - 需要配合自建网关或第三方服务使用
     - 注意设置合理的 scan_interval 避免请求过于频繁

  3. 自动化配置:
     - 可写在 configuration.yaml 中（如上所示）
     - 也可在 HA 界面的「自动化」页面通过 UI 创建
     - UI 创建的自动化存储在 automations.yaml 中
     - 建议简单规则用 UI 创建，复杂规则用 YAML 编写

  4. Secrets 管理:
     - 敏感信息（密码、Token）存储在 secrets.yaml
     - 使用 !secret 引用，如: password: !secret xiaomi_password
     - secrets.yaml 应加入 .gitignore
        """)


# ═══════════════════════════════════════════════════════
#  CLI 参数解析与主入口
# ═══════════════════════════════════════════════════════

def parse_args():
    """解析命令行参数"""
    import argparse

    parser = argparse.ArgumentParser(
        description="Home Assistant REST API 交互演示",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  # 获取 HA 状态
  python 08_homeassistant_demo.py --host 192.168.1.100 --token YOUR_TOKEN status

  # 列出所有实体
  python 08_homeassistant_demo.py --host 192.168.1.100 --token YOUR_TOKEN entities

  # 按域过滤实体
  python 08_homeassistant_demo.py --host 192.168.1.100 --token YOUR_TOKEN entities --domain sensor

  # 查询实体状态
  python 08_homeassistant_demo.py --host 192.168.1.100 --token YOUR_TOKEN get climate.living_room_ac

  # 调用服务 — 开灯
  python 08_homeassistant_demo.py --host 192.168.1.100 --token YOUR_TOKEN call light turn_on light.living_room

  # 调用服务 — 设置温度（带额外参数）
  python 08_homeassistant_demo.py --host 192.168.1.100 --token YOUR_TOKEN call climate set_temperature --data '{"temperature":26}'

  # 调用服务 — 完整格式（entity_id 包含在 data 中）
  python 08_homeassistant_demo.py --host 192.168.1.100 --token YOUR_TOKEN call light turn_on --data '{"entity_id":"light.living_room","brightness_pct":50}'

  # 列出自动化
  python 08_homeassistant_demo.py --host 192.168.1.100 --token YOUR_TOKEN automations

  # 查询历史（最近 4 小时）
  python 08_homeassistant_demo.py --host 192.168.1.100 --token YOUR_TOKEN history sensor.indoor_temperature 4

  # 查看推荐配置
  python 08_homeassistant_demo.py config
        """,
    )

    # 全局参数
    parser.add_argument("--host", default=SAMPLE_CONFIG["host"], help="Home Assistant 主机地址（默认: localhost）")
    parser.add_argument("--port", type=int, default=SAMPLE_CONFIG["port"], help=f"Home Assistant 端口（默认: {SAMPLE_CONFIG['port']}）")
    parser.add_argument("--token", default="", help="长期访问令牌")
    parser.add_argument("--timeout", type=int, default=SAMPLE_CONFIG["timeout"], help=f"请求超时秒数（默认: {SAMPLE_CONFIG['timeout']}）")
    parser.add_argument("--ssl", action="store_true", help="使用 HTTPS")

    # 子命令
    subparsers = parser.add_subparsers(dest="command", help="命令")

    # status 命令
    subparsers.add_parser("status", help="获取 Home Assistant 运行状态")

    # entities 命令
    entities_parser = subparsers.add_parser("entities", help="列出所有实体")
    entities_parser.add_argument("--domain", default=None, help="按域过滤（如 sensor, light, climate）")

    # get 命令
    get_parser = subparsers.add_parser("get", help="获取实体状态")
    get_parser.add_argument("entity_id", help="实体 ID（如 climate.living_room_ac）")

    # call 命令
    call_parser = subparsers.add_parser("call", help="调用服务")
    call_parser.add_argument("domain", help="服务域（如 light, climate, script）")
    call_parser.add_argument("service", help="服务名（如 turn_on, set_temperature）")
    call_parser.add_argument("entity_id", nargs="?", default=None, help="目标实体 ID（可选）")
    call_parser.add_argument("--data", default=None, help="服务数据 JSON 字符串")

    # automations 命令
    subparsers.add_parser("automations", help="列出所有自动化规则")

    # history 命令
    history_parser = subparsers.add_parser("history", help="查询实体历史数据")
    history_parser.add_argument("entity_id", help="实体 ID")
    history_parser.add_argument("hours", nargs="?", type=int, default=1, help="查询最近多少小时（默认: 1）")

    # config 命令
    subparsers.add_parser("config", help="显示推荐的 configuration.yaml 配置")

    args = parser.parse_args()

    # 如果没有指定命令，显示帮助
    if not args.command:
        parser.print_help()
        sys.exit(0)

    return args


def main():
    """主入口函数"""
    args = parse_args()

    # 从环境变量读取 token（如果未通过参数指定）
    import os
    token = args.token or os.environ.get("HA_TOKEN", SAMPLE_CONFIG.get("token", ""))

    if not token and args.command != "config":
        print("警告: 未设置访问令牌，API 调用可能会失败")
        print("  设置方式: --token YOUR_TOKEN 或环境变量 HA_TOKEN")
        print()

    # 创建客户端
    client = HomeAssistantClient(
        host=args.host,
        port=args.port,
        token=token,
        timeout=args.timeout,
        use_ssl=args.ssl,
    )

    print_separator("Home Assistant REST API 交互演示", char="─")
    print(f"\n  连接: {client.base_url}")

    try:
        if args.command == "status":
            client.get_status()

        elif args.command == "entities":
            client.list_entities(domain_filter=args.domain)

        elif args.command == "get":
            result = client.get_entity(args.entity_id)
            if result is None:
                sys.exit(1)

        elif args.command == "call":
            # 解析服务数据
            service_data = None
            if args.data:
                try:
                    service_data = json.loads(args.data)
                except json.JSONDecodeError as e:
                    print(f"  [错误] JSON 解析失败: {e}")
                    print(f"  原始数据: {args.data}")
                    sys.exit(1)

            success = client.call_service(
                domain=args.domain,
                service=args.service,
                entity_id=args.entity_id,
                service_data=service_data,
            )
            if not success:
                sys.exit(1)

        elif args.command == "automations":
            client.list_automations()

        elif args.command == "history":
            result = client.get_history(args.entity_id, hours=args.hours)
            if result is None:
                sys.exit(1)

        elif args.command == "config":
            client.show_config()

        print()  # 结尾空行

    except KeyboardInterrupt:
        print("\n\n操作已取消")
    except Exception as e:
        log.error(f"操作出错: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
