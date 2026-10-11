# 智巡精灵 Windows 客户端

该桌面端复用 Android App 的 Vue 业务代码以及现有云端鉴权协议，后端无需修改。

- 登录：`/api/auth/login`
- 普通 API：Bearer Token
- WebSocket/MJPEG：短期实时票据
- 系统 Wi-Fi：调用 Windows 设置页
- 现场配置：不读取或校验 Windows Wi-Fi 状态，输入 HTTP/HTTPS 地址后直接尝试打开

桌面容器启用上下文隔离、沙箱和 Web 安全，不向页面开放 Node.js。

构建：

```powershell
cd communication\安卓APP\windows
npm ci
npm run build:portable
```

输出位于 `communication/安卓APP/windows/release-v1.2.12-final/`。生产构建必须提供被 Git 忽略的 `.env.production`；占位地址会使构建直接失败。
