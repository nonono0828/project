"""Load private backend configuration files and replace this process with server.py.

Designed for:

    nohup python3.8 -u start_server.py > server.log 2>&1 &

The parser accepts simple shell-style KEY=value or export KEY=value lines but
does not execute shell code. Existing process environment variables take
precedence, preserving compatibility with ``nohup env KEY=value ...``.
"""

import argparse
import os
import re
import shlex
import sys

from auth import AuthManager


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_AUTH_ENV = os.path.join(BASE_DIR, ".auth.env")
DEFAULT_RUNTIME_ENV = os.path.join(BASE_DIR, ".runtime.env")
DEFAULT_SERVER = os.path.join(BASE_DIR, "server.py")
AUTH_PREFIX = "APP_AUTH_"
REQUIRED_AUTH_KEYS = (
    "APP_AUTH_USERNAME",
    "APP_AUTH_PASSWORD_HASH",
    "APP_AUTH_SECRET",
)
PLACEHOLDER_CREDENTIALS = {
    "replace-with-your-server-side-token",
    "your-server-side-token",
    "your-server-side-key",
    "请填写服务器端基站定位 Token",
    "请填写服务器端基站定位Token",
}
_ASSIGNMENT_RE = re.compile(
    r"^\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*=(.*)$"
)


class ConfigurationError(RuntimeError):
    pass


def parse_env_file(path, required):
    if not os.path.exists(path):
        if required:
            raise ConfigurationError("缺少配置文件：" + path)
        return {}
    if not os.path.isfile(path) or not os.access(path, os.R_OK):
        raise ConfigurationError("配置文件不可读取：" + path)

    values = {}
    with open(path, "r", encoding="utf-8-sig") as handle:
        for line_number, original in enumerate(handle, 1):
            stripped = original.strip()
            if not stripped or stripped.startswith("#"):
                continue
            match = _ASSIGNMENT_RE.match(original.rstrip("\r\n"))
            if not match:
                raise ConfigurationError(
                    "{} 第 {} 行不是 KEY=value 配置".format(
                        path, line_number
                    )
                )
            key, raw_value = match.groups()
            try:
                parts = shlex.split(raw_value, comments=True, posix=True)
            except ValueError as exc:
                raise ConfigurationError(
                    "{} 第 {} 行的引号格式不正确".format(path, line_number)
                ) from exc
            if len(parts) > 1:
                raise ConfigurationError(
                    "{} 第 {} 行的值必须使用引号包住空格".format(
                        path, line_number
                    )
                )
            values[key] = parts[0] if parts else ""
    return values


def build_environment(auth_env, runtime_env, process_env=None):
    auth_values = parse_env_file(auth_env, required=True)
    runtime_values = parse_env_file(runtime_env, required=False)
    misplaced = sorted(
        key for key in runtime_values if key.startswith(AUTH_PREFIX)
    )
    if misplaced:
        raise ConfigurationError(
            "运行配置文件不能包含管理员鉴权字段："
            + ", ".join(misplaced)
        )

    merged = dict(auth_values)
    merged.update(runtime_values)
    inherited = dict(os.environ if process_env is None else process_env)
    for key, value in inherited.items():
        # The authentication file is authoritative. This prevents stale
        # APP_AUTH_* values left in an interactive shell from undoing a
        # password reset. Non-auth variables still support `nohup env ...`.
        if not key.startswith(AUTH_PREFIX):
            merged[key] = value

    missing = [key for key in REQUIRED_AUTH_KEYS if not merged.get(key)]
    if missing:
        raise ConfigurationError(
            "管理员鉴权配置不完整，缺少：" + ", ".join(missing)
        )

    manager = AuthManager(
        merged["APP_AUTH_USERNAME"],
        merged["APP_AUTH_PASSWORD_HASH"],
        merged["APP_AUTH_SECRET"],
    )
    if not manager.configured:
        raise ConfigurationError(
            "管理员鉴权配置无效，请重新运行 auth.py --generate --output"
        )

    provider = merged.get("CELL_GEOLOCATION_PROVIDER", "").strip().lower()
    unwired_token = merged.get("UNWIRED_LOCATION_API_TOKEN", "").strip()
    google_key = (
        merged.get("GOOGLE_GEOLOCATION_API_KEY", "").strip()
        or merged.get("CELL_GEOLOCATION_API_KEY", "").strip()
    )
    if provider == "unwired" and (
        not unwired_token or unwired_token in PLACEHOLDER_CREDENTIALS
    ):
        raise ConfigurationError(
            "已选择 unwired 基站定位，但未配置 "
            "UNWIRED_LOCATION_API_TOKEN"
        )
    if provider == "google" and (
        not google_key or google_key in PLACEHOLDER_CREDENTIALS
    ):
        raise ConfigurationError(
            "已选择 google 基站定位，但未配置 Google API Key"
        )
    if provider and provider not in ("unwired", "google"):
        raise ConfigurationError(
            "不支持的 CELL_GEOLOCATION_PROVIDER：" + provider
        )
    return merged


def configuration_summary(environment):
    provider = environment.get(
        "CELL_GEOLOCATION_PROVIDER", ""
    ).strip().lower()
    if not provider:
        provider = "未启用"
    return (
        "配置检查通过\n"
        "管理员账号：{}\n"
        "基站定位：{}\n"
        "鉴权凭据和定位 Token 均未输出"
    ).format(environment["APP_AUTH_USERNAME"], provider)


def main():
    parser = argparse.ArgumentParser(
        description="Load 智巡精灵 backend configuration and start server.py"
    )
    parser.add_argument("--auth-env", default=DEFAULT_AUTH_ENV)
    parser.add_argument("--runtime-env", default=DEFAULT_RUNTIME_ENV)
    parser.add_argument("--server", default=DEFAULT_SERVER)
    parser.add_argument(
        "--check",
        action="store_true",
        help="validate configuration without starting the server",
    )
    args = parser.parse_args()

    try:
        environment = build_environment(
            os.path.abspath(os.path.expanduser(args.auth_env)),
            os.path.abspath(os.path.expanduser(args.runtime_env)),
        )
    except ConfigurationError as exc:
        parser.exit(2, "配置错误：" + str(exc) + "\n")

    if args.check:
        print(configuration_summary(environment))
        return

    server = os.path.abspath(os.path.expanduser(args.server))
    if not os.path.isfile(server):
        parser.exit(2, "配置错误：找不到 server.py：" + server + "\n")
    os.chdir(os.path.dirname(server))
    os.execve(
        sys.executable,
        [sys.executable, "-u", server],
        environment,
    )


if __name__ == "__main__":
    main()
