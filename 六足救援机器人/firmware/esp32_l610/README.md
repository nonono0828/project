# ESP32-S3 + OV5640 + L610 RNDIS 通信固件 V7.6

本工程包含：

- OV5640 JPEG 云端视频和 SoftAP 本地 MJPEG；
- 响应式“机器人现场配置台”，可在机器人旁直接管理 Wi-Fi、4G 和视频参数；
- 5/8/15/20/30 FPS 动态设置；
- VGA、HD、FHD 分辨率设置；
- L610 4G 状态、频段和小区信息管理；
- 按需启用的 Wi-Fi 扫描、连接和云端链路切换；
- ESP 在线心跳与 STM32 统一传感器/GPS 遥测；
- 云端 PS2 按键 `down/up` 转 UART 控制。

## 配置

源码默认使用以下示例值：

- 云端视频和遥测服务器：`192.0.2.10`（RFC 5737 TEST-NET）；
- AP 密码：`CHANGE_ME_AP_PASSWORD`；
- Web 管理密码：`CHANGE_ME_WEB_PASSWORD`。

构建前在本地工作副本中设置实际服务器和口令。机器人 SoftAP 网关为
`192.168.4.1`。本地入口如下：

- 现场配置台：`http://192.168.4.1:8081/`；
- 本地 MJPEG：`http://192.168.4.1:8080/live/mjpeg`；
- 兼容入口：访问 `http://192.168.4.1:8080/` 会跳转到现场配置台。

配置热点可连接后开始 10 分钟倒计时，普通浏览和状态轮询不会续期。管理员可在现场
配置台按分钟自定义延长时间，也可使用快捷按钮延长 10 分钟。倒计时结束或点击“关闭
热点”后，本地 MJPEG、SoftAP、DHCP 和 NAPT 会关闭，L610 云端视频、遥测和控制链路
不受影响。云端再次开启本地配置热点时会创建新的完整 10 分钟会话。

Wi-Fi 扫描只在用户从配置台明确开启该功能后运行。凭据保存在设备 NVS 中，但不会由
接口返回或写入日志。L610 已取得 IPv4 时不会自动连接历史热点；L610 无网但配置台仍
开启时也保持等待，避免自动连接干扰现场配置。只有在 L610 无网且配置台关闭时，设备
才会使用已验证保存的凭据低频尝试 Wi-Fi 自动兜底。配置台内人工连接 Wi-Fi 后仍需明确
点击“Wi-Fi 上行”才会切换视频、遥测和控制链路；L610 恢复时只撤销自动兜底，不覆盖
人工选择的 Wi-Fi 上行。设备没有保存凭据时不会扫描、猜测或主动连接任何网络。

4G 页面不接受任意 AT 文本，只提供固定的查询、自动选网、频段和小区操作。这些操作
复用固件原有的唯一 AT 队列；即使尚未注册网络，只要 AT 接口已经就绪，仍可查询、
清除小区锁定并恢复完整频段。频段设置收到 `OK` 即报告保存成功，并提示必须断电重启。

现场配置台 REST 接口使用 `/api/local/v1/` 前缀。写操作带随机会话令牌和 `request_id`，
先返回 `202 Accepted`，同一子系统只串行执行一个写操作。

## 构建

安装 ESP-IDF 5.5.1 后执行：

```powershell
.\build_camera_sensor_wifi_manager_v7.ps1 `
  -IdfExport 'C:\path\to\esp-idf-v5.5.1\export.ps1'
```

脚本在独立 staging 目录中构建，输出到被 Git 忽略的
`firmware_binaries\camera_sensor_4g_wifi_v7_6`。

## 烧录

以构建生成的 `flasher_args.json` 为准。当前典型布局：

```text
0x0000  bootloader.bin
0x9000  partition-table.bin
0x20000 usb_rndis_4g_module.bin
```

也可以在 staging 工程中使用：

```powershell
idf.py -p COM6 flash monitor
```

## STM32 UART

UART1 使用 GPIO2/GPIO1、3.3 V TTL、`115200 8N1`、无流控并共地。
完整协议见 [STM32_ESP32_UART_INTEGRATION.md](docs/STM32_ESP32_UART_INTEGRATION.md)。

- `type=0x01`：STM32 到 ESP，统一 `sensor` + `gps` 快照；
- `type=0x02`：ESP 到 STM32，PS2 `button/state`；
- `type=0x03`：保留，不使用。

摄像头固定在机身上，不定义独立摄像头控制。STM32 在 500 ms 内没有收到
续发 `down` 时应释放按键并停车。
