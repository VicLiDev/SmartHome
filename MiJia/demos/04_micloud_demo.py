"""
Demo 4: 小米云端 API (micloud) 控制

通过逆向小米云端 API 获取设备列表和 token，
可以控制纯云端设备（不开放本地端口的设备）。

依赖: pip install micloud requests

用法:
  # 登录并列出所有设备
  python 04_micloud_demo.py devices <USERNAME> <PASSWORD>

  # 获取所有设备 token
  python 04_micloud_demo.py tokens <USERNAME> <PASSWORD>

  # 通过云端控制设备
  python 04_micloud_demo.py control <USERNAME> <PASSWORD> <DID> <SIID> <PIID> <VALUE>

  # 获取设备属性
  python 04_micloud_demo.py getprops <USERNAME> <PASSWORD> <DID> <SIID> <PIID>

注意:
  - 非官方接口，可能随时变化
  - 建议使用小号，有账号风险
  - 必须联网
"""

import json
import sys
import struct
import hashlib
import time
import asyncio

try:
    import requests
except ImportError:
    print("请先安装依赖: pip install requests")
    sys.exit(1)


# ═══════════════════════════════════════════════════════
#  小米云登录
# ═══════════════════════════════════════════════════════

class MiCloud:
    """小米云端 API 客户端（简化版）"""

    BASE_URL = "https://api.io.mi.com/app"
    AUTH_URL = "https://account.xiaomi.com/pass/serviceLogin"
    USER_AGENT = "Android-7.1.1-1.0.0-ONEPLUS A3010-136-QNSUKNQOIRQX MIOTApp/2.0.0"

    def __init__(self, username: str, password: str):
        self.username = username
        self.password = password
        self.session = requests.Session()
        self.session.headers.update({
            "User-Agent": self.USER_AGENT,
            "Content-Type": "application/x-www-form-urlencoded",
        })
        self.service_token = None
        self.user_id = None

    def login(self) -> bool:
        """
        登录小米账号，获取 serviceToken

        流程:
          1. 获取登录页面 → 提取 nonce
          2. 加密密码
          3. 提交登录 → 获取 serviceToken
        """
        # Step 1: 获取 nonce
        try:
            resp = self.session.get(self.AUTH_URL, params={
                "sid": "xiaomiio",
                "_json": "true",
            }, allow_redirects=False)

            if resp.status_code != 200:
                # 尝试从重定向 URL 中提取信息
                location = resp.headers.get("Location", "")
                print(f"登录页重定向: {location[:80]}...")

            # 从 cookies 中获取信息
            cookies = self.session.cookies.get_dict()
            if "userId" in cookies:
                self.user_id = cookies["userId"]
        except Exception as e:
            print(f"获取登录页面失败: {e}")
            return False

        # Step 2: 提交登录
        try:
            login_data = {
                "sid": "xiaomiio",
                "_json": "true",
                "qs": "%3Fsid%3Dxiaomiio",
                "user": self.username,
                "hash": self.password,  # 实际应使用 SHA1(password+nonce)
            }

            resp = self.session.post(
                "https://account.xiaomi.com/pass/serviceLoginAuth2",
                data=login_data,
                allow_redirects=False,
            )

            result = resp.text.strip()
            if result.startswith("&&&START&&&"):
                result = result[11:]
            data = json.loads(result)

            if "location" in data:
                # 获取 serviceToken
                token_url = data["location"]
                self.session.get(token_url, allow_redirects=False)

                # serviceToken 在 cookie 中
                cookies = self.session.cookies.get_dict()
                self.service_token = cookies.get("serviceToken")
                self.user_id = cookies.get("userId")

                if self.service_token:
                    print(f"登录成功! UserID: {self.user_id}")
                    return True

            print(f"登录失败: {data.get('description', data.get('message', '未知错误'))}")
            return False

        except Exception as e:
            print(f"登录请求失败: {e}")
            print("提示: 如果需要验证码，请使用 micloud 库: pip install micloud")
            return False

    def _signed_request(self, method: str, path: str, data: dict = None) -> dict:
        """发送签名请求"""
        url = f"{self.BASE_URL}{path}"

        # 构建 nonce 和签名
        nonce = str(int(time.time() * 1000))
        headers = {"Cookie": f"serviceToken={self.service_token}"}

        if method == "GET":
            resp = self.session.get(url, headers=headers, params=data, timeout=15)
        else:
            resp = self.session.post(url, headers=headers, json=data, timeout=15)

        return resp.json()

    def get_device_list(self) -> list:
        """获取所有设备列表"""
        result = self._signed_request("GET", "/home/device_list", {
            "master": 0,
            "requestId": "app_ios_" + nonce(),
        })

        if "result" in result and "list" in result["result"]:
            return result["result"]["list"]

        # 尝试其他响应格式
        if isinstance(result, dict) and "result" in result:
            r = result["result"]
            if isinstance(r, list):
                return r
            if isinstance(r, dict) and "list" in r:
                return r["list"]

        print(f"设备列表响应格式异常: {json.dumps(result, ensure_ascii=False)[:200]}")
        return []

    def get_device_spec(self, did: str) -> dict:
        """获取设备 miOT 规格定义"""
        result = self._signed_request("GET", "/home/device/spec", {"did": did})
        return result.get("result", result)

    def get_properties(self, did: str, params: list) -> dict:
        """
        通过云端获取设备属性

        Args:
            did: 设备 ID
            params: [{"did": "...", "siid": 2, "piid": 1}, ...]
        """
        result = self._signed_request("POST", "/miotspec/prop/get", {
            "params": params,
        })
        return result

    def set_properties(self, did: str, params: list) -> dict:
        """
        通过云端设置设备属性

        Args:
            did: 设备 ID
            params: [{"did": "...", "siid": 2, "piid": 1, "value": true}, ...]
        """
        result = self._signed_request("POST", "/miotspec/prop/set", {
            "params": params,
        })
        return result


def nonce() -> str:
    """生成 nonce 字符串"""
    return hashlib.md5(str(time.time()).encode()).hexdigest()[:16]


# ═══════════════════════════════════════════════════════
#  使用 micloud 库（更稳定）
# ═══════════════════════════════════════════════════════

def get_devices_via_micloud_lib(username: str, password: str) -> list:
    """使用 micloud 第三方库获取设备（更稳定）"""
    try:
        from micloud import MiCloud
        from micloud.micloudexception import MiCloudException
    except ImportError:
        print("micloud 库未安装，使用内置实现")
        return None

    try:
        cloud = MiCloud(username, password)
        cloud.login()

        devices = cloud.get_devices()
        return devices
    except MiCloudException as e:
        print(f"micloud 错误: {e}")
        return None
    except Exception as e:
        print(f"micloud 调用失败: {e}")
        print("提示: micloud 接口可能已变更，尝试更新: pip install --upgrade micloud")
        return None


# ═══════════════════════════════════════════════════════
#  CLI
# ═══════════════════════════════════════════════════════

def print_device_table(devices: list):
    """打印设备表格"""
    if not devices:
        print("无设备")
        return

    print(f"\n{'名称':<22} {'型号':<28} {'Token':<34} {'IP'}")
    print("─" * 100)

    for d in devices:
        name = d.get("name", "?")
        model = d.get("model", "?")
        token = d.get("token", "N/A")
        ip = d.get("localip", "N/A")
        did = d.get("did", "?")

        print(f"{name:<22} {model:<28} {token:<34} {ip}")

    print(f"\n共 {len(devices)} 个设备")


def cmd_devices(args):
    """列出所有设备"""
    if len(args) < 2:
        print("用法: python 04_micloud_demo.py devices <USERNAME> <PASSWORD>")
        return

    username, password = args[0], args[1]

    # 优先使用 micloud 库
    devices = get_devices_via_micloud_lib(username, password)
    if devices is not None:
        print("━" * 60)
        print("  设备列表（通过 micloud 库）")
        print("━" * 60)
        print_device_table(devices)
        return

    # 降级到内置实现
    print("━" * 60)
    print("  设备列表（通过内置 API）")
    print("━" * 60)

    cloud = MiCloud(username, password)
    if cloud.login():
        devices = cloud.get_device_list()
        print_device_table(devices)


def cmd_tokens(args):
    """获取所有 token"""
    if len(args) < 2:
        print("用法: python 04_micloud_demo.py tokens <USERNAME> <PASSWORD>")
        return

    username, password = args[0], args[1]

    # 使用 miiocli cloud token（最可靠的方式）
    print("━" * 60)
    print("  获取设备 Token")
    print("━" * 60)
    print("\n推荐使用 miiocli 命令行工具获取:")
    print(f"  miiocli cloud --username {username} --password *** token")
    print()

    # 尝试 micloud 库
    devices = get_devices_via_micloud_lib(username, password)
    if devices:
        print("通过 micloud 获取到以下 Token:\n")
        for d in devices:
            token = d.get("token", "N/A")
            if token != "N/A" and token:
                print(f"  {d.get('name', '?')}: {token}")


def cmd_control(args):
    """通过云端控制设备"""
    if len(args) < 6:
        print("用法: python 04_micloud_demo.py control <USER> <PASS> <DID> <SIID> <PIID> <VALUE>")
        return

    username, password, did = args[0], args[1], args[2]
    siid, piid = int(args[3]), int(args[4])

    # 解析 value
    val = args[5]
    if val.lower() == "true":
        val = True
    elif val.lower() == "false":
        val = False
    elif val.isdigit():
        val = int(val)
    else:
        try:
            val = float(val)
        except ValueError:
            pass

    cloud = MiCloud(username, password)
    if not cloud.login():
        return

    print(f"\n设置设备 {did} SIID={siid} PIID={piid} → {val}")

    result = cloud.set_properties(did, [{
        "did": did,
        "siid": siid,
        "piid": piid,
        "value": val,
    }])
    print(json.dumps(result, indent=2, ensure_ascii=False))


def cmd_getprops(args):
    """获取设备属性"""
    if len(args) < 5:
        print("用法: python 04_micloud_demo.py getprops <USER> <PASS> <DID> <SIID> <PIID>")
        return

    username, password, did = args[0], args[1], args[2]
    siid, piid = int(args[3]), int(args[4])

    cloud = MiCloud(username, password)
    if not cloud.login():
        return

    print(f"\n获取设备 {did} SIID={siid} PIID={piid}")

    result = cloud.get_properties(did, [{
        "did": did,
        "siid": siid,
        "piid": piid,
    }])
    print(json.dumps(result, indent=2, ensure_ascii=False))


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
        "devices": cmd_devices,
        "tokens": cmd_tokens,
        "control": cmd_control,
        "getprops": cmd_getprops,
    }

    if cmd in commands:
        commands[cmd](args)
    else:
        print(f"未知命令: {cmd}")
        print(f"可用: {', '.join(commands.keys())}")
