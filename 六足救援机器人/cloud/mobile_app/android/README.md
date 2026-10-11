# 智巡精灵 Android App

当前源码版本为 **1.2.10**，共享 Vue 3 业务代码同时用于 Capacitor Android App 和 Electron Windows 客户端。Android 应用 ID 为 `cn.hajimi.spider001`。

## 主要功能

- 管理员登录与登录状态校验，云端业务接口使用 Bearer 令牌，实时连接使用短期票据。
- 云端控制平台与现场配置台双入口。
- 视频、位置、传感器、事件记录、4G/Wi-Fi 设置与前后左右运动控制。
- 支持机身高度、无级调速、越野模式和自主避障控制，并在设备确认后写入事件记录。
- 控制页视频区域叠加透明高对比环境数据，不遮挡主要画面。
- 控制页集成紧凑地图和最近事件面板；地图保留必要缩放，事件面板为只读展示。
- 幸存者识别与气体超限按优先级弹窗并持久记录；幸存者事件保存最新有效视频截图。
- 页面退出、切入后台、连接断开或控制超时时立即释放运动指令。
- 现场配置模式只允许访问机器人本地地址，可调用 Android 系统 Wi-Fi 设置并连接机器人热点。
- 黑底银白 ZX 启动图标与应用内品牌标识。

云端网页控制台位于相邻的 `../前端/`，两者是独立工程，不应相互覆盖。

## 配置

仓库不包含真实服务器地址、Token、密码、签名证书或本机 SDK 路径。首次构建前复制示例配置：

```powershell
Copy-Item .env.production.example .env.production
```

然后在本机的 `.env.production` 中填写部署地址。该文件已被 `.gitignore` 排除。

机器人热点现场配置默认使用：

```text
http://192.168.4.1:8081/
```

## 前端验证

```powershell
npm ci
npm run test:unit
npm run build
```

## Android 调试构建

配置 `JAVA_HOME`、`ANDROID_HOME` 和 `ANDROID_SDK_ROOT` 后执行：

```powershell
npm run android:sync
Set-Location android
.\gradlew.bat :app:assembleDebug
```

不要执行 `cap add android` 或直接重建 Android 平台目录，否则可能覆盖原生 Wi-Fi 插件、返回键处理和运动安全逻辑。

## Windows 客户端构建

```powershell
Set-Location windows
npm ci
npm run build:portable
```

Windows 构建产物位于 `windows/release*/`，不提交到源码仓库。

## 发布约束

- 不提交 `node_modules`、`dist`、Gradle 构建目录、APK/AAB 或日志。
- 不提交 `.env.production`、本地开发配置、签名材料与 `local.properties`。
- 正式发布应使用 HTTPS/WSS 和独立保管的正式签名证书。
