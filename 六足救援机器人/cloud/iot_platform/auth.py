"""Small, dependency-free authentication helper for the control panel.

Credentials and signing material are provided through environment variables.
The password itself is never stored by the server; only a PBKDF2-SHA256 hash
is compared. Access tokens are short-lived HMAC-signed claims.
"""

import argparse
import base64
import getpass
import hashlib
import hmac
import json
import os
import re
import secrets
import tempfile
import time


PASSWORD_SCHEME = "pbkdf2_sha256"
DEFAULT_ITERATIONS = 390_000
DEFAULT_TOKEN_TTL_SECONDS = 12 * 60 * 60
REALTIME_TICKET_TTL_SECONDS = 60
REALTIME_TICKET_SCOPE = "realtime"
MIN_SECRET_BYTES = 32
MANAGED_AUTH_ENV_KEYS = (
    "APP_AUTH_USERNAME",
    "APP_AUTH_PASSWORD_HASH",
    "APP_AUTH_SECRET",
)
_ENV_ASSIGNMENT_RE = re.compile(
    r"^\s*(?:export\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*="
)


def _b64encode(value):
    return base64.urlsafe_b64encode(value).rstrip(b"=").decode("ascii")


def _b64decode(value):
    padding = "=" * (-len(value) % 4)
    return base64.urlsafe_b64decode((value + padding).encode("ascii"))


def hash_password(password, iterations=DEFAULT_ITERATIONS, salt=None):
    if not isinstance(password, str) or not password:
        raise ValueError("password must not be empty")
    if len(password) > 256:
        raise ValueError("password is too long")
    if not isinstance(iterations, int) or iterations < 100_000:
        raise ValueError("iterations is too small")
    salt = salt or secrets.token_bytes(18)
    digest = hashlib.pbkdf2_hmac(
        "sha256", password.encode("utf-8"), salt, iterations
    )
    return "$".join((
        PASSWORD_SCHEME,
        str(iterations),
        _b64encode(salt),
        _b64encode(digest),
    ))


def verify_password(password, encoded):
    try:
        scheme, iterations_text, salt_text, digest_text = encoded.split("$", 3)
        if scheme != PASSWORD_SCHEME:
            return False
        iterations = int(iterations_text)
        if iterations < 100_000 or iterations > 2_000_000:
            return False
        salt = _b64decode(salt_text)
        expected = _b64decode(digest_text)
        actual = hashlib.pbkdf2_hmac(
            "sha256", password.encode("utf-8"), salt, iterations
        )
        return hmac.compare_digest(actual, expected)
    except (AttributeError, TypeError, ValueError):
        return False


def _read_ttl():
    try:
        value = int(os.getenv(
            "APP_AUTH_TOKEN_TTL_SECONDS", DEFAULT_TOKEN_TTL_SECONDS
        ))
    except ValueError:
        value = DEFAULT_TOKEN_TTL_SECONDS
    return max(15 * 60, min(value, 7 * 24 * 60 * 60))


class AuthManager:
    def __init__(self, username, password_hash, secret, ttl_seconds=None):
        self.username = username.strip() if isinstance(username, str) else ""
        self.password_hash = password_hash.strip() if isinstance(
            password_hash, str
        ) else ""
        self.secret = secret.encode("utf-8") if isinstance(secret, str) else b""
        self.ttl_seconds = ttl_seconds or _read_ttl()

    @classmethod
    def from_env(cls):
        return cls(
            os.getenv("APP_AUTH_USERNAME", "admin"),
            os.getenv("APP_AUTH_PASSWORD_HASH", ""),
            os.getenv("APP_AUTH_SECRET", ""),
        )

    @property
    def configured(self):
        return (
            bool(self.username)
            and self.password_hash.startswith(PASSWORD_SCHEME + "$")
            and len(self.secret) >= MIN_SECRET_BYTES
        )

    def authenticate(self, username, password, now=None):
        if not self.configured:
            return None
        username_ok = hmac.compare_digest(
            str(username).encode("utf-8"),
            self.username.encode("utf-8"),
        )
        password_ok = verify_password(str(password), self.password_hash)
        if not (username_ok and password_ok):
            return None
        return self.issue_token(now=now)

    def issue_token(self, now=None):
        if not self.configured:
            raise RuntimeError("authentication is not configured")
        issued_at = int(time.time() if now is None else now)
        claims = {
            "sub": self.username,
            "iat": issued_at,
            "exp": issued_at + self.ttl_seconds,
            "nonce": secrets.token_hex(8),
        }
        token = self._sign_claims("v1", claims)
        return {
            "access_token": token,
            "token_type": "Bearer",
            "username": self.username,
            "issued_at": claims["iat"],
            "expires_at": claims["exp"],
        }

    def issue_realtime_ticket(self, now=None):
        """Issue a URL-safe credential limited to realtime handshakes."""
        if not self.configured:
            raise RuntimeError("authentication is not configured")
        issued_at = int(time.time() if now is None else now)
        claims = {
            "sub": self.username,
            "iat": issued_at,
            "exp": issued_at + REALTIME_TICKET_TTL_SECONDS,
            "scope": REALTIME_TICKET_SCOPE,
            "nonce": secrets.token_hex(8),
        }
        return {
            "ticket": self._sign_claims("rt1", claims),
            "token_type": "RealtimeTicket",
            "scope": REALTIME_TICKET_SCOPE,
            "issued_at": claims["iat"],
            "expires_at": claims["exp"],
        }

    def _sign_claims(self, version, claims):
        payload = _b64encode(json.dumps(
            claims, separators=(",", ":"), ensure_ascii=False
        ).encode("utf-8"))
        signed = f"{version}.{payload}".encode("ascii")
        signature = _b64encode(hmac.new(
            self.secret, signed, hashlib.sha256
        ).digest())
        return f"{version}.{payload}.{signature}"

    def _verify_signed_claims(self, token, expected_version, now=None):
        if not self.configured or not isinstance(token, str):
            return None
        try:
            version, payload, signature = token.split(".", 2)
            if version != expected_version or len(token) > 2048:
                return None
            signed = f"{version}.{payload}".encode("ascii")
            expected = _b64encode(hmac.new(
                self.secret, signed, hashlib.sha256
            ).digest())
            if not hmac.compare_digest(signature, expected):
                return None
            claims = json.loads(_b64decode(payload).decode("utf-8"))
            current = int(time.time() if now is None else now)
            if claims.get("sub") != self.username:
                return None
            if not isinstance(claims.get("iat"), int):
                return None
            if not isinstance(claims.get("exp"), int):
                return None
            if claims["iat"] > current + 60 or claims["exp"] <= current:
                return None
            return claims
        except (
            UnicodeDecodeError, ValueError, TypeError, KeyError,
            json.JSONDecodeError,
        ):
            return None

    def verify_token(self, token, now=None):
        return self._verify_signed_claims(token, "v1", now=now)

    def verify_realtime_ticket(self, ticket, now=None):
        claims = self._verify_signed_claims(ticket, "rt1", now=now)
        if claims is None:
            return None
        if claims.get("scope") != REALTIME_TICKET_SCOPE:
            return None
        lifetime = claims["exp"] - claims["iat"]
        if lifetime <= 0 or lifetime > REALTIME_TICKET_TTL_SECONDS:
            return None
        return claims


class LoginRateLimiter:
    """In-memory per-client limiter; successful login clears prior failures."""

    def __init__(self, max_attempts=5, window_seconds=300):
        self.max_attempts = max_attempts
        self.window_seconds = window_seconds
        self.failures = {}

    def _recent(self, client, now):
        cutoff = now - self.window_seconds
        recent = [
            value for value in self.failures.get(client, [])
            if value >= cutoff
        ]
        if recent:
            self.failures[client] = recent
        else:
            self.failures.pop(client, None)
        return recent

    def blocked(self, client, now=None):
        current = time.monotonic() if now is None else now
        return len(self._recent(client, current)) >= self.max_attempts

    def record_failure(self, client, now=None):
        current = time.monotonic() if now is None else now
        recent = self._recent(client, current)
        recent.append(current)
        self.failures[client] = recent

    def clear(self, client):
        self.failures.pop(client, None)


def _shell_quote(value):
    value = str(value)
    if "\x00" in value or "\r" in value or "\n" in value:
        raise ValueError("environment values must be single-line text")
    return "'" + value.replace("'", "'\"'\"'") + "'"


def _auth_env_lines(values):
    return [
        f"{key}={_shell_quote(values[key])}"
        for key in MANAGED_AUTH_ENV_KEYS
    ]


def write_auth_env(path, values):
    """Atomically replace only managed authentication keys in an env file.

    Non-authentication entries are preserved deliberately. This makes password
    rotation safe even when an older deployment stored cell-geolocation
    settings in the same file.
    """
    missing = [key for key in MANAGED_AUTH_ENV_KEYS if not values.get(key)]
    if missing:
        raise ValueError(
            "missing authentication values: " + ", ".join(missing)
        )

    target = os.path.abspath(os.path.expanduser(os.fspath(path)))
    parent = os.path.dirname(target) or os.curdir
    if not os.path.isdir(parent):
        raise FileNotFoundError(
            "authentication env directory does not exist: " + parent
        )

    existing_lines = []
    if os.path.exists(target):
        with open(target, "r", encoding="utf-8") as handle:
            existing_lines = handle.read().splitlines()

    preserved = []
    for line in existing_lines:
        match = _ENV_ASSIGNMENT_RE.match(line)
        if match and match.group(1) in MANAGED_AUTH_ENV_KEYS:
            continue
        preserved.append(line)

    while preserved and not preserved[-1].strip():
        preserved.pop()
    if preserved:
        preserved.append("")
    preserved.extend(_auth_env_lines(values))
    content = "\n".join(preserved) + "\n"

    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            newline="\n",
            prefix="." + os.path.basename(target) + ".",
            suffix=".tmp",
            dir=parent,
            delete=False,
        ) as handle:
            temporary = handle.name
            handle.write(content)
            handle.flush()
            os.fsync(handle.fileno())
        os.chmod(temporary, 0o600)
        os.replace(temporary, target)
        temporary = None
        os.chmod(target, 0o600)
    finally:
        if temporary and os.path.exists(temporary):
            os.unlink(temporary)


def _generate_config(username):
    first = getpass.getpass("请输入新的管理员密码: ")
    second = getpass.getpass("请再次输入管理员密码: ")
    if first != second:
        raise SystemExit("两次输入的密码不一致")
    if len(first) < 10:
        raise SystemExit("管理员密码至少需要 10 个字符")
    return {
        "APP_AUTH_USERNAME": username,
        "APP_AUTH_PASSWORD_HASH": hash_password(first),
        "APP_AUTH_SECRET": secrets.token_urlsafe(48),
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate 智巡精灵 authentication environment values"
    )
    parser.add_argument("--generate", action="store_true")
    parser.add_argument("--username", default="admin")
    parser.add_argument(
        "--output",
        required=True,
        help=(
            "atomically update this authentication env file; only "
            "APP_AUTH_USERNAME, APP_AUTH_PASSWORD_HASH and APP_AUTH_SECRET "
            "are replaced"
        ),
    )
    args = parser.parse_args()
    if not args.generate:
        parser.error("use --generate")
    values = _generate_config(args.username.strip() or "admin")
    write_auth_env(args.output, values)
    print(
        "管理员鉴权配置已安全更新，只替换 APP_AUTH_* 登录字段："
        + os.path.abspath(os.path.expanduser(args.output))
    )
