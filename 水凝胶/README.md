# 水凝胶机械手

来自 `E:\hand_project\水凝胶` 的项目文件。

- `GESTURE_3X3_final/`：STM32 固件和 Keil 工程，保留已有的 HEX/BIN 烧录文件。
- `PC_APP/`：Electron/Vite 电脑端源码、依赖锁文件和测试。
- `robotic_hand.glb`、`robotic_hand2.glb`：机械手模型；电脑端使用的模型同时保留在 `PC_APP/public/models/`。
- `说明书.md`、`技术文档.docx`、`水凝胶密码锁方案.docx`：原始接线说明及技术文档。

## 从源码运行电脑端

安装兼容 Vite 7 的 Node.js 版本后，在 `PC_APP` 目录执行：

```powershell
npm ci
npm run dev
```

生成 Windows 便携程序：

```powershell
npm run dist:win
```

上述命令来自原项目的 `package.json`。本次导入核对了文件复制完整性，未重新构建固件或电脑端应用。

原始 `说明书.md` 提到的 `应用/Hydrogel Hand.exe` 是本机打包程序。本仓库保留对应源码，未包含该打包目录、依赖目录和重复构建目录。
