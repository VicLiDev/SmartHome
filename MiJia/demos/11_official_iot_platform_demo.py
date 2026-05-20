"""
Demo 11: 小米官方 IoT 开放平台 API (iot.mi.com) 调用

功能:
  - OAuth2 授权流程（authorize_url, access_token, refresh_token）
  - 设备管理 API:
    - 获取设备列表 (GET /v2/home/device_list)
    - 获取设备规格 (GET /v2/home/device/spec)
    - 读取设备属性 (POST /miotspec/prop/get)
    - 设置设备属性 (POST /miotspec/prop/set)
    - 订阅设备事件
  - HMAC-SHA256 请求签名
  - 内置 HTTP 服务器处理 OAuth2 回调
  - 完整的 iot.mi.com 平台注册指南

架构:
  本脚本 ←(OAuth2)→ iot.mi.com ←(API)→ 小米云 ←→ 智能设备

平台注册步骤:
  1. 访问 https://iot.mi.com/new/doc/ 注册开发者账号
  2. 创建应用，获取 app_key 和 app_secret
  3. 配置回调 URI（OAuth2 redirect）
  4. 提交应用审核

API 端点:
  授权: https://account.xiaomi.com/oauth2/authorize
  令牌: https://account.xiaomi.com/oauth2/token
  API:  https://api.io.mi.com

依赖: pip install requests

用法:
  # 启动 OAuth2 授权流程
  python 11_official_iot_platform_demo.py authorize

  # 刷新 access_token
  python 11_official_iot_platform_demo.py refresh

  # 获取设备列表
  python 11_official_iot_platform_demo.py devices

  # 获取设备规格
  python 11_official_iot_platform_demo.py spec <DID>

  # 读取设备属性
  python 11_official_iot_platform_demo.py get <DID> <SIID> <PIID>

  # 设置设备属性
  python 11_official_iot_platform_demo.py set <DID> <SIID> <PIID> <VALUE>

  # 查看平台注册指南
  python 11_official_iot_platform_demo.py guide
"""

import json
import sys
import os
import time
import hashlib
import hmac
import uuid
import threading
import urllib.parse
from http.server import HTTPServer, BaseHTTPRequestHandler
from datetime import datetime
from typing import Optional, Dict, List, Any, Tuple

try:
    import requests
except ImportError:
    print("请先安装依赖: pip install requests")
    sys.exit(1)


# ═══════════════════════════════════════════════════════
#  常量
# ═══════════════════════════════════════════════════════

ACCOUNT_BASE = "https://account.xiaomi.com"
API_BASE = "https://api.io.mi.com"

# OAuth2 作用域
OAUTH_SCOPE = ";".join([
    "1",    # 账号基本信息
    "2",    # 设备列表
    "6",    # 设备控制
])

# 配置文件路径
CONFIG_FILE = os.path.expanduser("~/.config/mijia_iot_config.json")


# ═══════════════════════════════════════════════════════
#  示例配置
# ═══════════════════════════════════════════════════════

SAMPLE_CONFIG = {
    "app_key": "YOUR_APP_KEY",           # 在 iot.mi.com 创建应用后获取
    "app_secret": "YOUR_APP_SECRET",     # 应用密钥
    "redirect_uri": "http://127.0.0.1:18080/callback",  # OAuth2 回调地址
    "access_token": "",                   # 授权后自动保存
    "refresh_token": "",                  # 刷新令牌
    "token_expires_at": 0,                # 令牌过期时间戳
}


# ═══════════════════════════════════════════════════════
#  工具函数
# ═══════════════════════════════════════════════════════

def print_separator(title: str = "", char: str = "━", width: int = 60):
    """打印分隔线"""
    if title:
        padding = width - len(title) - 2
        left = padding // 2
        right = padding - left
        print(f"\n{char * left} {title} {char * right}")
    else:
        print(char * width)


def pretty_json(data: Any, indent: int = 2) -> str:
    """格式化 JSON 输出"""
    return json.dumps(data, indent=indent, ensure_ascii=False)


def load_config(path: str = CONFIG_FILE) -> Dict[str, Any]:
    """加载配置文件

    Args:
        path: 配置文件路径

    Returns:
        配置字典
    """
    if os.path.exists(path):
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    return dict(SAMPLE_CONFIG)


def save_config(config: Dict[str, Any], path: str = CONFIG_FILE):
    """保存配置文件

    Args:
        config: 配置字典
        path: 配置文件路径
    """
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(config, f, indent=2, ensure_ascii=False)
    print(f"配置已保存到: {path}")


# ═══════════════════════════════════════════════════════
#  HMAC-SHA256 请求签名
# ═══════════════════════════════════════════════════════

def sign_request(
    method: str,
    path: str,
    params: Dict[str, str],
    app_secret: str,
    nonce: Optional[str] = None,
) -> str:
    """计算小米 IoT API 请求签名（HMAC-SHA256）

    签名算法:
      1. 将参数按键名排序
      2. 拼接为 key=value&key=value 格式
      3. 在前面拼接 HTTP 方法和路径: GET|/path|key=value&...
      4. 使用 app_secret 作为密钥进行 HMAC-SHA256 计算
      5. 结果转为十六进制字符串

    Args:
        method: HTTP 方法 (GET / POST)
        path: API 路径（如 /v2/home/device_list）
        params: 请求参数字典
        app_secret: 应用密钥
        nonce: 随机字符串（可选，默认自动生成）

    Returns:
        签名字符串
    """
    if nonce is None:
        nonce = str(uuid.uuid4())

    # 确保 nonce 在参数中
    all_params = dict(params)
    all_params["nonce"] = nonce

    # 按键名排序并拼接
    sorted_keys = sorted(all_params.keys())
    param_str = "&".join(f"{k}={all_params[k]}" for k in sorted_keys)

    # 拼接待签名字符串: METHOD|PATH|params
    sign_str = f"{method.upper()}|{path}|{param_str}"

    # HMAC-SHA256
    signature = hmac.new(
        app_secret.encode("utf-8"),
        sign_str.encode("utf-8"),
        hashlib.sha256,
    ).hexdigest()

    return signature, nonce


# ═══════════════════════════════════════════════════════
#  OAuth2 回调 HTTP 服务器
# ═══════════════════════════════════════════════════════

class OAuthCallbackHandler(BaseHTTPRequestHandler):
    """OAuth2 回调处理器

    处理小米 OAuth2 授权回调，提取授权码 (code)，
    然后用授权码换取 access_token 和 refresh_token。
    """

    # 由外部注入的回调
    _callback_result = None
    _callback_event = None

    def log_message(self, format, *args):
        """静默日志"""
        pass

    def do_GET(self):
        """处理 OAuth2 回调 GET 请求"""
        parsed = urllib.parse.urlparse(self.path)

        if parsed.path.rstrip("/") == "/callback":
            params = urllib.parse.parse_qs(parsed.query)

            if "code" in params:
                code = params["code"][0]
                OAuthCallbackHandler._callback_result = {
                    "code": code,
                    "state": params.get("state", [""])[0],
                }
                # 通知主线程
                if OAuthCallbackHandler._callback_event:
                    OAuthCallbackHandler._callback_event.set()

                body = f"""
                <html>
                <head><title>授权成功</title></head>
                <body style="font-family: sans-serif; text-align: center; padding: 50px;">
                    <h1 style="color: #4CAF50;">✓ 授权成功</h1>
                    <p>授权码已获取，正在换取 Token...</p>
                    <p>你可以关闭此页面。</p>
                </body>
                </html>
                """.encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            else:
                error = params.get("error", ["未知错误"])[0]
                error_desc = params.get("error_description", [""])[0]
                body = f"""
                <html>
                <body style="font-family: sans-serif; text-align: center; padding: 50px;">
                    <h1 style="color: #f44336;">✗ 授权失败</h1>
                    <p>错误: {error}</p>
                    <p>{error_desc}</p>
                </body>
                </html>
                """.encode("utf-8")
                self.send_response(400)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
        else:
            self.send_response(404)
            self.end_headers()


# ═══════════════════════════════════════════════════════
#  小米 IoT 开放平台客户端
# ═══════════════════════════════════════════════════════

class XiaomiIotClient:
    """小米官方 IoT 开放平台 API 客户端

    通过 OAuth2 授权获取 access_token，调用小米 IoT API 管理和控制智能设备。

    API 文档: https://iot.mi.com/new/doc/development/platform/api

    使用流程:
      1. 在 iot.mi.com 注册开发者账号并创建应用
      2. 使用 authorize() 启动 OAuth2 授权流程
      3. 授权成功后自动获取 access_token
      4. 使用设备 API 管理和控制设备

    OAuth2 流程:
      1. 用户访问授权 URL（浏览器）
      2. 用户登录小米账号并授权
      3. 小米回调 redirect_uri 并携带 code
      4. 使用 code 换取 access_token
      5. access_token 过期后使用 refresh_token 刷新
    """

    def __init__(
        self,
        app_key: str = "",
        app_secret: str = "",
        redirect_uri: str = "http://127.0.0.1:18080/callback",
        config_path: str = CONFIG_FILE,
    ):
        """初始化 IoT 客户端

        Args:
            app_key: 应用 Key（iot.mi.com 创建应用后获取）
            app_secret: 应用密钥
            redirect_uri: OAuth2 回调 URI
            config_path: 配置文件路径
        """
        config = load_config(config_path)

        self.app_key = app_key or config.get("app_key", "")
        self.app_secret = app_secret or config.get("app_secret", "")
        self.redirect_uri = redirect_uri or config.get("redirect_uri", "")
        self.access_token = config.get("access_token", "")
        self.refresh_token = config.get("refresh_token", "")
        self.token_expires_at = config.get("token_expires_at", 0)
        self.config_path = config_path

        self.session = requests.Session()
        self.session.headers.update({
            "Content-Type": "application/x-www-form-urlencoded",
        })

    def _save(self):
        """保存 token 到配置文件"""
        save_config({
            "app_key": self.app_key,
            "app_secret": self.app_secret,
            "redirect_uri": self.redirect_uri,
            "access_token": self.access_token,
            "refresh_token": self.refresh_token,
            "token_expires_at": self.token_expires_at,
        }, self.config_path)

    def _check_token(self) -> bool:
        """检查 access_token 是否有效（未过期）

        Returns:
            token 是否有效
        """
        if not self.access_token:
            return False
        # 提前 5 分钟判断过期
        if time.time() > self.token_expires_at - 300:
            return False
        return True

    # ── OAuth2 授权 ──

    def get_authorize_url(self, state: str = "") -> str:
        """生成 OAuth2 授权 URL

        用户在浏览器中打开此 URL，登录并授权后，小米会回调 redirect_uri。

        Args:
            state: 防 CSRF 的随机字符串

        Returns:
            完整的授权 URL
        """
        if not state:
            state = str(uuid.uuid4())

        params = {
            "client_id": self.app_key,
            "response_type": "code",
            "redirect_uri": self.redirect_uri,
            "scope": OAUTH_SCOPE,
            "state": state,
        }

        url = f"{ACCOUNT_BASE}/oauth2/authorize"
        query = urllib.parse.urlencode(params)
        return f"{url}?{query}"

    def get_access_token(self, code: str) -> Dict[str, Any]:
        """使用授权码换取 access_token

        OAuth2 标准流程: code → access_token + refresh_token

        Args:
            code: OAuth2 授权码（回调 URL 中的 code 参数）

        Returns:
            包含 token 信息的字典:
              {
                "access_token": "...",
                "refresh_token": "...",
                "expires_in": 2592000,
                "token_expires_at": 1234567890,
              }
        """
        data = {
            "client_id": self.app_key,
            "client_secret": self.app_secret,
            "grant_type": "authorization_code",
            "code": code,
            "redirect_uri": self.redirect_uri,
            "scope": OAUTH_SCOPE,
        }

        resp = self.session.post(
            f"{ACCOUNT_BASE}/oauth2/token",
            data=data,
            timeout=30,
        )
        result = resp.json()

        if "access_token" not in result:
            raise RuntimeError(f"获取 token 失败: {result}")

        self.access_token = result["access_token"]
        self.refresh_token = result["refresh_token"]
        self.token_expires_at = time.time() + int(result.get("expires_in", 2592000))
        self._save()

        result["token_expires_at"] = self.token_expires_at
        result["token_expires_at_readable"] = datetime.fromtimestamp(
            self.token_expires_at
        ).strftime("%Y-%m-%d %H:%M:%S")

        return result

    def refresh_access_token(self) -> Dict[str, Any]:
        """使用 refresh_token 刷新 access_token

        access_token 有效期通常为 30 天，
        过期后可以使用 refresh_token 获取新的 token。

        Returns:
            新的 token 信息字典
        """
        if not self.refresh_token:
            raise RuntimeError("没有 refresh_token，请先执行授权流程")

        data = {
            "client_id": self.app_key,
            "client_secret": self.app_secret,
            "grant_type": "refresh_token",
            "refresh_token": self.refresh_token,
            "redirect_uri": self.redirect_uri,
            "scope": OAUTH_SCOPE,
        }

        resp = self.session.post(
            f"{ACCOUNT_BASE}/oauth2/token",
            data=data,
            timeout=30,
        )
        result = resp.json()

        if "access_token" not in result:
            raise RuntimeError(f"刷新 token 失败: {result}")

        self.access_token = result["access_token"]
        if "refresh_token" in result:
            self.refresh_token = result["refresh_token"]
        self.token_expires_at = time.time() + int(result.get("expires_in", 2592000))
        self._save()

        result["token_expires_at"] = self.token_expires_at
        result["token_expires_at_readable"] = datetime.fromtimestamp(
            self.token_expires_at
        ).strftime("%Y-%m-%d %H:%M:%S")

        return result

    def authorize(self) -> Dict[str, Any]:
        """执行完整的 OAuth2 授权流程

        步骤:
          1. 在本地启动临时 HTTP 服务器（接收回调）
          2. 打开浏览器访问授权 URL
          3. 等待用户授权并回调
          4. 用授权码换取 access_token

        Returns:
            token 信息字典
        """
        if not self.app_key or self.app_key == "YOUR_APP_KEY":
            raise RuntimeError(
                "请先配置 app_key 和 app_secret。\n"
                f"编辑配置文件: {self.config_path}\n"
                "或在 iot.mi.com 创建应用后获取。"
            )

        # 解析回调端口
        parsed = urllib.parse.urlparse(self.redirect_uri)
        callback_host = parsed.hostname or "127.0.0.1"
        callback_port = parsed.port or 18080

        # 启动临时回调服务器
        OAuthCallbackHandler._callback_result = None
        OAuthCallbackHandler._callback_event = threading.Event()

        server = HTTPServer((callback_host, callback_port), OAuthCallbackHandler)
        server_thread = threading.Thread(target=server.handle_request, daemon=True)
        server_thread.start()

        # 生成并打开授权 URL
        auth_url = self.get_authorize_url()
        print(f"\n请在浏览器中打开以下链接进行授权:\n")
        print(f"  {auth_url}\n")
        print("等待用户授权...")

        # 等待回调（最多 5 分钟）
        OAuthCallbackHandler._callback_event.wait(timeout=300)

        if OAuthCallbackHandler._callback_result is None:
            raise RuntimeError("等待授权回调超时（5分钟）")

        code = OAuthCallbackHandler._callback_result["code"]
        print(f"收到授权码: {code[:16]}...")

        # 换取 access_token
        result = self.get_access_token(code)
        print(f"✓ 获取 access_token 成功")
        print(f"  过期时间: {result.get('token_expires_at_readable', 'N/A')}")

        server.server_close()
        return result

    # ── API 签名请求 ──

    def _signed_request(
        self,
        method: str,
        path: str,
        data: Optional[Dict[str, Any]] = None,
        params: Optional[Dict[str, str]] = None,
    ) -> Dict[str, Any]:
        """发送带签名的 API 请求

        所有小米 IoT API 请求都需要签名验证:
          1. 收集请求参数
          2. 添加 access_token、nonce 等公共参数
          3. 计算 HMAC-SHA256 签名
          4. 发送请求到 api.io.mi.com

        Args:
            method: HTTP 方法 (GET / POST)
            path: API 路径
            data: POST 请求的 JSON 数据
            params: 额外的查询参数

        Returns:
            API 响应的 JSON 字典

        Raises:
            RuntimeError: token 无效或请求失败
        """
        if not self._check_token():
            if self.refresh_token:
                print("access_token 已过期，正在刷新...")
                self.refresh_access_token()
            else:
                raise RuntimeError(
                    "access_token 无效或已过期，请先执行授权流程:\n"
                    "  python 11_official_iot_platform_demo.py authorize"
                )

        # 构建公共参数
        timestamp = str(int(time.time()))
        common_params = {
            "data": json.dumps(data) if data else "{}",
        }

        # 合并额外参数
        if params:
            common_params.update(params)

        # 计算签名
        signature, nonce = sign_request(
            method=method,
            path=path,
            params=common_params,
            app_secret=self.app_secret,
            nonce=str(uuid.uuid4()),
        )

        # 构建最终请求参数
        request_params = {
            "client_id": self.app_key,
            "nonce": nonce,
            "sign": signature,
            "ts": timestamp,
            **common_params,
        }

        # 发送请求
        url = f"{API_BASE}{path}"
        headers = {
            "Content-Type": "application/x-www-form-urlencoded",
        }

        if method.upper() == "GET":
            resp = self.session.get(
                url,
                params=request_params,
                headers=headers,
                timeout=30,
            )
        else:
            resp = self.session.post(
                url,
                data=request_params,
                headers=headers,
                timeout=30,
            )

        result = resp.json()

        if result.get("code", 0) != 0:
            error_msg = result.get("message", "未知错误")
            raise RuntimeError(f"API 错误 [{result.get('code')}]: {error_msg}")

        return result

    # ── 设备管理 API ──

    def get_device_list(self, home_id: str = "", page: int = 1) -> Dict[str, Any]:
        """获取设备列表 (GET /v2/home/device_list)

        获取用户账号下的所有智能设备列表。

        Args:
            home_id: 家庭 ID（可选，默认获取所有家庭的设备）
            page: 页码（分页查询）

        Returns:
            设备列表响应:
              {
                "result": {
                  "devices": [
                    {
                      "did": "设备ID",
                      "name": "设备名称",
                      "model": "设备型号",
                      "online": true,
                      "localip": "192.168.1.xxx",
                      ...
                    }
                  ],
                  "total": 10
                }
              }
        """
        params = {}
        if home_id:
            params["home_id"] = home_id
        if page > 1:
            params["page"] = str(page)

        return self._signed_request(
            method="GET",
            path="/v2/home/device_list",
            params=params,
        )

    def get_device_spec(self, did: str) -> Dict[str, Any]:
        """获取设备规格 (GET /v2/home/device/spec)

        获取设备的 MIoT 规格定义，包括服务、属性、事件、动作等。

        Args:
            did: 设备 ID

        Returns:
            设备规格响应:
              {
                "result": {
                  "type": "urn:miot-spec-v2:device:light:0000A001:xxx:1",
                  "services": [
                    {
                      "iid": 1,
                      "type": "...",
                      "properties": [...],
                      "events": [...],
                      "actions": [...]
                    }
                  ]
                }
              }
        """
        params = {"did": did}
        return self._signed_request(
            method="GET",
            path="/v2/home/device/spec",
            params=params,
        )

    def get_properties(
        self,
        did: str,
        properties: List[Dict[str, int]],
    ) -> Dict[str, Any]:
        """读取设备属性 (POST /miotspec/prop/get)

        通过 MIoT Spec 协议读取设备属性值。

        Args:
            did: 设备 ID
            properties: 属性列表，每项格式:
              {"siid": 服务IID, "piid": 属性IID}
              示例: [{"siid": 1, "piid": 1}]

        Returns:
            属性值响应:
              {
                "result": [
                  {"did": "...", "siid": 1, "piid": 1, "value": true},
                  ...
                ]
              }
        """
        return self._signed_request(
            method="POST",
            path="/miotspec/prop/get",
            data={
                "did": did,
                "params": properties,
            },
        )

    def set_properties(
        self,
        did: str,
        properties: List[Dict[str, Any]],
    ) -> Dict[str, Any]:
        """设置设备属性 (POST /miotspec/prop/set)

        通过 MIoT Spec 协议设置设备属性值。

        Args:
            did: 设备 ID
            properties: 属性列表，每项格式:
              {"siid": 服务IID, "piid": 属性IID, "value": 值}
              示例: [{"siid": 1, "piid": 1, "value": true}]

        Returns:
            设置结果响应:
              {
                "result": [
                  {"did": "...", "siid": 1, "piid": 1, "code": 0},
                  ...
                ]
              }

        常见属性 SIID/PIID 示例:
          电源开关:   siid=1, piid=1  (value: true/false)
          亮度:       siid=2, piid=1  (value: 0-100)
          色温:       siid=3, piid=1  (value: 2700-6500)
          当前温度:   siid=2, piid=7  (value: 温度值)
        """
        return self._signed_request(
            method="POST",
            path="/miotspec/prop/set",
            data={
                "did": did,
                "params": properties,
            },
        )

    def get_events(self, did: str) -> Dict[str, Any]:
        """获取设备事件

        查询设备最近上报的事件记录。

        Args:
            did: 设备 ID

        Returns:
            事件列表响应
        """
        return self._signed_request(
            method="GET",
            path="/v2/home/device/events",
            params={"did": did},
        )


# ═══════════════════════════════════════════════════════
#  平台注册指南
# ═══════════════════════════════════════════════════════

def show_registration_guide():
    """展示小米 IoT 开放平台注册和配置指南"""
    print("""
╔══════════════════════════════════════════════════════════════════╗
║        小米 IoT 开放平台注册与配置指南                           ║
║        https://iot.mi.com/new/doc/                              ║
╠══════════════════════════════════════════════════════════════════╣
║                                                                ║
║  第一步: 注册开发者账号                                          ║
║  ─────────────────────                                          ║
║    1. 访问 https://iot.mi.com/new/doc/                          ║
║    2. 使用小米账号登录                                           ║
║    3. 选择"开发者中心" → "成为开发者"                              ║
║    4. 填写开发者信息（个人/企业）                                 ║
║    5. 完成实名认证                                               ║
║                                                                ║
║  第二步: 创建应用                                               ║
║  ─────────────────                                              ║
║    1. 进入"我的应用" → "创建应用"                                ║
║    2. 应用类型选择"智能家居"                                      ║
║    3. 填写应用名称和描述                                         ║
║    4. 设置回调 URI: http://127.0.0.1:18080/callback             ║
║       （如使用内网穿透，填公网地址）                               ║
║    5. 创建成功后获得 app_key 和 app_secret                       ║
║                                                                ║
║  第三步: 配置应用权限                                            ║
║  ─────────────────────                                          ║
║    1. 在应用设置中勾选所需权限:                                   ║
║       ☑ 获取用户设备列表                                         ║
║       ☑ 控制用户设备                                             ║
║       ☑ 读取设备属性                                             ║
║    2. 如需生产环境使用，提交应用审核                               ║
║                                                                ║
║  第四步: 配置本脚本                                             ║
║  ─────────────────────                                          ║
║    方式 A: 编辑配置文件                                          ║
║      ~/.config/mijia_iot_config.json                            ║
║      {                                                          ║
║        "app_key": "你的app_key",                                ║
║        "app_secret": "你的app_secret",                          ║
║        "redirect_uri": "http://127.0.0.1:18080/callback"       ║
║      }                                                          ║
║                                                                ║
║    方式 B: 环境变量                                             ║
║      export MIJIA_APP_KEY=你的app_key                           ║
║      export MIJIA_APP_SECRET=你的app_secret                     ║
║                                                                ║
║  第五步: 运行授权流程                                            ║
║  ───────────────────────                                        ║
║    python 11_official_iot_platform_demo.py authorize             ║
║    → 自动打开浏览器                                              ║
║    → 登录小米账号并授权                                           ║
║    → 自动获取并保存 access_token                                 ║
║                                                                ║
║  注意事项:                                                       ║
║    • access_token 有效期 30 天，过期后用 refresh_token 刷新       ║
║    • refresh_token 长期有效（除非用户主动撤销授权）               ║
║    • 生产和开发环境的 API 配额不同                                ║
║    • 设备控制仅限用户已绑定的设备                                  ║
║    • 应用审核通过后才能正式发布                                    ║
║                                                                ║
║  API 文档:                                                       ║
║    https://iot.mi.com/new/doc/development/platform/api          ║
║    https://iot.mi.com/new/doc/development/platform/miot-spec     ║
║                                                                ║
║  MIoT Spec 属性定义:                                             ║
║    https://iot.mi.com/new/doc/embedded-development/miot-spec    ║
║    每种设备的 siid/piid 不同，需查阅对应设备的 Spec 定义            ║
╚══════════════════════════════════════════════════════════════════╝
""")


def show_api_reference():
    """展示 API 端点参考"""
    print("""
╔══════════════════════════════════════════════════════════════════╗
║        小米 IoT API 端点参考                                     ║
╠══════════════════════════════════════════════════════════════════╣
║                                                                ║
║  基础 URL: https://api.io.mi.com                                ║
║  签名算法: HMAC-SHA256                                          ║
║  认证方式: OAuth2 + access_token                                ║
║                                                                ║
║  ── 设备管理 ──                                                  ║
║  GET  /v2/home/device_list                    获取设备列表       ║
║  GET  /v2/home/device/spec                     获取设备规格       ║
║  GET  /v2/home/device/events                   获取设备事件       ║
║                                                                ║
║  ── MIoT Spec 属性读写 ──                                        ║
║  POST /miotspec/prop/get                       读取设备属性       ║
║  POST /miotspec/prop/set                       设置设备属性       ║
║  POST /miotspec/action/execute                执行设备动作       ║
║                                                                ║
║  ── OAuth2 令牌 ──                                               ║
║  获取授权:  https://account.xiaomi.com/oauth2/authorize          ║
║  获取令牌:  https://account.xiaomi.com/oauth2/token              ║
║  刷新令牌:  同上（grant_type=refresh_token）                      ║
║                                                                ║
║  ── 签名参数（每次请求必须携带）──                                ║
║  client_id   应用 Key                                            ║
║  nonce       随机字符串（UUID）                                   ║
║  sign        HMAC-SHA256 签名                                   ║
║  ts          时间戳（秒级）                                       ║
║  data        JSON 数据（GET 请求为空对象 {}）                     ║
║                                                                ║
║  ── 常见 MIoT Spec 服务 ID (SIID) ──                             ║
║  1  - 电源管理 (电源开关、模式)                                   ║
║  2  - 环境传感器 (温度、湿度)                                     ║
║  3  - 灯光控制 (亮度、色温)                                      ║
║  4  - 风扇控制 (风速、摆头)                                      ║
║  5  - 窗帘控制 (开合百分比)                                      ║
║  6  - 空调控制 (温度、模式)                                      ║
║  7  - 空气净化器 (PM2.5、滤芯)                                   ║
║  8  - 扫地机器人 (清扫、拖地、回充)                               ║
║  9  - 报警器 (报警、亮度)                                        ║
║  10 - 门锁 (锁定、开锁)                                         ║
║  11 - 摄像头 (开关、云台)                                        ║
║                                                                ║
║  注意: 具体 SIID/PIID 因设备型号而异，请查询设备 Spec             ║
╚══════════════════════════════════════════════════════════════════╝
""")


# ═══════════════════════════════════════════════════════
#  CLI 入口
# ═══════════════════════════════════════════════════════

def create_client(args) -> XiaomiIotClient:
    """从命令行参数或配置文件创建客户端实例

    Args:
        args: 命令行参数列表

    Returns:
        XiaomiIotClient 实例
    """
    kwargs = {}
    i = 0
    while i < len(args):
        if args[i] == "--app-key" and i + 1 < len(args):
            kwargs["app_key"] = args[i + 1]
            i += 2
        elif args[i] == "--app-secret" and i + 1 < len(args):
            kwargs["app_secret"] = args[i + 1]
            i += 2
        elif args[i] == "--redirect-uri" and i + 1 < len(args):
            kwargs["redirect_uri"] = args[i + 1]
            i += 2
        else:
            i += 1

    # 环境变量覆盖
    if not kwargs.get("app_key"):
        kwargs["app_key"] = os.environ.get("MIJIA_APP_KEY", "")
    if not kwargs.get("app_secret"):
        kwargs["app_secret"] = os.environ.get("MIJIA_APP_SECRET", "")

    return XiaomiIotClient(**kwargs)


def cmd_authorize(args):
    """执行 OAuth2 授权流程"""
    print_separator("OAuth2 授权流程")
    client = create_client(args)

    try:
        result = client.author()
        print_separator("授权结果")
        print(f"  access_token: {result['access_token'][:16]}...")
        print(f"  refresh_token: {result['refresh_token'][:16]}...")
        print(f"  expires_in: {result.get('expires_in', 'N/A')} 秒")
        print(f"  过期时间: {result.get('token_expires_at_readable', 'N/A')}")
    except Exception as e:
        print(f"授权失败: {e}")


def cmd_refresh(args):
    """刷新 access_token"""
    print_separator("刷新 Access Token")
    client = create_client(args)

    try:
        result = client.refresh_access_token()
        print(f"✓ 刷新成功")
        print(f"  access_token: {result['access_token'][:16]}...")
        print(f"  过期时间: {result.get('token_expires_at_readable', 'N/A')}")
    except Exception as e:
        print(f"刷新失败: {e}")


def cmd_devices(args):
    """获取设备列表"""
    print_separator("设备列表")
    client = create_client(args)

    try:
        result = client.get_device_list()
        devices = result.get("result", {}).get("devices", [])
        total = result.get("result", {}).get("total", len(devices))

        print(f"共 {total} 个设备:\n")
        print(f"{'名称':<20} {'型号':<25} {'状态':<8} {'IP':<18} {'DID'}")
        print("─" * 90)

        for dev in devices:
            name = dev.get("name", "未知")
            model = dev.get("model", "")
            online = "在线" if dev.get("online") else "离线"
            ip = dev.get("localip", "")
            did = dev.get("did", "")
            print(f"{name:<20} {model:<25} {online:<8} {ip:<18} {did}")

    except Exception as e:
        print(f"获取失败: {e}")


def cmd_spec(args):
    """获取设备规格"""
    if len(args) < 1 or args[0].startswith("--"):
        print("用法: python 11_official_iot_platform_demo.py spec <DID>")
        return

    did = args[0]
    print_separator(f"设备规格: {did}")
    client = create_client(args)

    try:
        result = client.get_device_spec(did)
        spec = result.get("result", {})

        print(f"设备类型: {spec.get('type', 'N/A')}\n")

        # 显示服务和属性
        services = spec.get("services", [])
        for svc in services:
            svc_name = svc.get("type", "").split(":")[-1]
            svc_iid = svc.get("iid", 0)
            print(f"  服务 #{svc_iid}: {svc_name}")

            for prop in svc.get("properties", []):
                prop_name = prop.get("type", "").split(":")[-1]
                prop_iid = prop.get("iid", 0)
                fmt = prop.get("format", "")
                access = prop.get("access", [])
                readable = "读" if "read" in access else ""
                writable = "写" if "write" in access else ""
                print(f"    属性 #{prop_iid}: {prop_name} ({fmt}) [{readable}{writable}]")

            for action in svc.get("actions", []):
                action_name = action.get("type", "").split(":")[-1]
                action_iid = action.get("iid", 0)
                print(f"    动作 #{action_iid}: {action_name}")

            for event in svc.get("events", []):
                event_name = event.get("type", "").split(":")[-1]
                event_iid = event.get("iid", 0)
                print(f"    事件 #{event_iid}: {event_name}")
            print()

    except Exception as e:
        print(f"获取失败: {e}")


def cmd_get(args):
    """读取设备属性"""
    # 解析参数
    positional = [a for a in args if not a.startswith("--")]
    if len(positional) < 3:
        print("用法: python 11_official_iot_platform_demo.py get <DID> <SIID> <PIID> [PIID2] ...")
        return

    did = positional[0]
    siid = int(positional[1])
    piids = [int(p) for p in positional[2:]]

    properties = [{"siid": siid, "piid": piid} for piid in piids]

    print_separator(f"读取属性: {did} SIID={siid}")
    client = create_client(args)

    try:
        result = client.get_properties(did, properties)
        print(pretty_json(result))
    except Exception as e:
        print(f"读取失败: {e}")


def cmd_set(args):
    """设置设备属性"""
    positional = [a for a in args if not a.startswith("--")]
    if len(positional) < 4:
        print("用法: python 11_official_iot_platform_demo.py set <DID> <SIID> <PIID> <VALUE>")
        return

    did = positional[0]
    siid = int(positional[1])
    piid = int(positional[2])
    value_str = positional[3]

    # 尝试解析值类型
    if value_str.lower() in ("true", "on"):
        value = True
    elif value_str.lower() in ("false", "off"):
        value = False
    else:
        try:
            value = int(value_str)
        except ValueError:
            try:
                value = float(value_str)
            except ValueError:
                value = value_str

    properties = [{"siid": siid, "piid": piid, "value": value}]

    print_separator(f"设置属性: {did} SIID={siid} PIID={piid} → {value}")
    client = create_client(args)

    try:
        result = client.set_properties(did, properties)
        print(pretty_json(result))
    except Exception as e:
        print(f"设置失败: {e}")


def cmd_guide(args):
    """显示配置指南"""
    which = args[0] if args else "all"
    if which == "api":
        show_api_reference()
    elif which == "register":
        show_registration_guide()
    else:
        show_registration_guide()
        show_api_reference()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(0)

    cmd = sys.argv[1]

    if cmd in ("--help", "-h", "help"):
        print(__doc__)
        sys.exit(0)

    args = sys.argv[2:]

    commands = {
        "authorize": cmd_authorize,
        "refresh": cmd_refresh,
        "devices": cmd_devices,
        "spec": cmd_spec,
        "get": cmd_get,
        "set": cmd_set,
        "guide": cmd_guide,
    }

    if cmd in commands:
        commands[cmd](args)
    else:
        print(f"未知命令: {cmd}")
        print(f"可用命令: {', '.join(commands.keys())}")
