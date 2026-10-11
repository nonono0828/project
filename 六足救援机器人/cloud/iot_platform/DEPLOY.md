# 智巡精灵后端部署说明

不要删除服务器已有的 `/root/video_server/events.json`。该文件保存跨登录同步的事件记录，也用于第三方定位服务不可达时恢复最近一次基站位置。

## 配置文件分工

后端使用两个互相独立的私有配置文件：

- `/root/video_server/.auth.env`：只管理管理员账号、密码哈希和登录签名密钥。
- `/root/video_server/.runtime.env`：管理基站定位供应商、定位 Token 和其他运行参数。

生成或重置管理员密码时，`auth.py --output` 只原子替换三个
`APP_AUTH_*` 登录字段。即使旧版 `.auth.env` 中混有
`CELL_GEOLOCATION_PROVIDER` 或 `UNWIRED_LOCATION_API_TOKEN`，也会保留，
不会被密码重置覆盖。

## 首次配置管理员鉴权

```bash
cd /root/video_server
python3.8 auth.py --generate --username admin \
  --output /root/video_server/.auth.env
```

按提示输入两次管理员密码。程序会直接、安全地更新 `.auth.env`，不要再
使用 `> .auth.env` 重定向。然后确认权限：

```bash
chmod 600 /root/video_server/.auth.env
```

`.auth.env` 不应上传到代码仓库或发送给其他人。

以后重置密码仍执行同一条命令。重置会生成新的登录签名密钥，因此旧登录
令牌立即失效，但不会修改基站定位配置。

## 配置基站定位

首次部署复制模板：

```bash
cd /root/video_server
cp -n runtime.env.example .runtime.env
vi /root/video_server/.runtime.env
chmod 600 /root/video_server/.runtime.env
```

`.runtime.env` 示例：

```text
CELL_GEOLOCATION_PROVIDER=unwired
UNWIRED_LOCATION_API_TOKEN='请填写服务器端基站定位 Token'
```

不要把真实 Token 写入源码、README、命令历史或公开压缩包。

## 检查并启动

新启动器不会执行配置文件中的 Shell 命令，只解析简单的 `KEY=value`，
并且不会输出密码哈希、登录密钥或定位 Token：

```bash
cd /root/video_server
python3.8 start_server.py --check

nohup python3.8 -u /root/video_server/start_server.py \
  > /root/video_server/server.log 2>&1 &
```

启动后检查：

```bash
curl -fsS http://127.0.0.1:8765/api/health
echo
ss -lntup | grep -E ':8765|:9091|:9092|:9093'
tail -n 50 /root/video_server/server.log
```

现有 `nohup env KEY=value ...` 部署仍兼容，进程环境中的非鉴权变量优先于
`.runtime.env`。但三个 `APP_AUTH_*` 登录字段始终以 `.auth.env` 为准，
避免 Shell 中残留的旧值使密码重置失效。例如：

```bash
nohup env CELL_LOCATION_CACHE_SECONDS=3600 \
  python3.8 -u /root/video_server/start_server.py \
  > /root/video_server/server.log 2>&1 &
```

旧的 `python3.8 -u server.py` 入口仍可使用，但它不会自动加载这两个文件，
不建议用于重启或开机启动。

## 上线顺序

新版后端会拒绝匿名业务 API、WebSocket、控制指令和 MJPEG。普通 API 使用 Bearer 登录令牌，WebSocket 与 MJPEG 使用 60 秒、仅限实时连接的短期票据。旧版网页不支持该握手流程，因此应按以下顺序更新：

1. 备份 `/root/video_server` 和前端目录。
2. 上传新版后端和新版前端，但暂不单独启用其中一端。
3. 配置 `.auth.env` 和 `.runtime.env`，执行 `start_server.py --check`。
4. 同一维护窗口内先重启后端，随后立即切换前端。
5. 使用智巡精灵登录，验证控制、遥测、视频和事件。

正式公网使用必须在 Nginx 上配置 HTTPS/WSS。端口 `8765` 建议只允许本机或反向代理访问。
