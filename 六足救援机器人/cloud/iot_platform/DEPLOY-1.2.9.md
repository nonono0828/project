# 智巡精灵后端 v1.2.9 部署说明

本部署包不包含 `.auth.env`、`.runtime.env`、`events.json`、`server.log`、
Python 缓存、管理员密码或定位 Token。更新时只覆盖代码文件，不删除现有
`/root/video_server`，因此会保留服务器已有鉴权、基站定位配置和历史事件。

将 `video_server_v1.2.9_20260812.zip` 直接上传到 `/root/`，然后以 root 用户
完整复制执行：

```bash
(
set -e

PKG=/root/video_server_v1.2.9_20260812.zip
APP=/root/video_server
BACKUP="/root/video_server.backup_$(date +%Y%m%d_%H%M%S)"
STAGE=$(mktemp -d /root/video_server_v1.2.9_stage.XXXXXX)
trap 'rm -rf "$STAGE"' EXIT

test -f "$PKG" || { echo "找不到部署包：$PKG"; exit 1; }
test -d "$APP" || { echo "找不到后端目录：$APP"; exit 1; }

cp -a "$APP" "$BACKUP"
unzip -q "$PKG" -d "$STAGE"
test -f "$STAGE/video_server/server.py"
test -f "$STAGE/video_server/VERSION"
grep -qx '1.2.9' "$STAGE/video_server/VERSION"

/bin/cp -af "$STAGE/video_server/." "$APP/"

cd "$APP"
python3.8 -m pip install -r requirements.txt
python3.8 start_server.py --check

PIDS=$(pgrep -f 'python3(\.8)? .*\/root\/video_server\/(start_server|server)\.py' || true)
if [ -n "$PIDS" ]; then
  kill $PIDS
  sleep 2
fi

nohup python3.8 -u /root/video_server/start_server.py \
  > /root/video_server/server.log 2>&1 &

sleep 3
curl -fsS --max-time 5 http://127.0.0.1:8765/api/health |
  python3.8 -m json.tool
python3.8 start_server.py --check
echo "后端 v1.2.9 部署完成，备份目录：$BACKUP"
)
```

部署后再检查：

```bash
cd /root/video_server
cat VERSION
pgrep -af 'python3(\.8)? .*server\.py'
ss -lntup | grep -E ':8765|:9091|:9092|:9093'
tail -n 50 /root/video_server/server.log
```

只有确实需要重置管理员密码时，才执行以下命令；普通版本更新不要执行：

```bash
cd /root/video_server
python3.8 auth.py --generate --username admin \
  --output /root/video_server/.auth.env
chmod 600 /root/video_server/.auth.env
python3.8 start_server.py --check
```

v1.2.9 新增后端独立持久化幸存者/气体报警事件、幸存者视频截图，以及
越野模式和自主避障状态确认后的事件记录。现有控制协议保持兼容。
