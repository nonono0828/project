/**
 * 机器人远程控制面板 - 统一接口配置
 *
 * 切换环境：修改 ENV 为 'production' 或 'development'
 * 打包部署时使用 production，本地调试使用 development
 */

const ENV = 'production'; // Private deployment build

// ==================== 正式服务端接口 ====================
const PRODUCTION = {
  // WebSocket 主控地址
  WS_MAIN: 'ws://example.com:8765/ws/control',

  // WebSocket 传感器数据地址
  WS_SENSOR: 'ws://example.com:8765/ws/sensor',

  // WebSocket GPS 数据地址
  WS_GPS: 'ws://example.com:8765/ws/gps',

  // 视频流 MJPEG
  MJPEG_URL: 'http://example.com:8765/live/mjpeg',

  // HTTP API 基础地址
  API_BASE: 'http://example.com:8765/api',
  WIFI_MANAGER_BASE: 'http://192.168.4.1:8081/api/wifi',

  // 地图瓦片服务
  MAP_TILE_URL: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
  MAP_CENTER: [39.9042, 116.4074],
  MAP_ZOOM: 15,
  SENSOR_INTERVAL: 1000,
  GPS_INTERVAL: 1000,
  TELEMETRY_STALE_MS: 6500,
}

// ==================== 本地调试接口 ====================
const DEVELOPMENT = {
  WS_MAIN: 'ws://example.com:8765/ws/control',
  WS_SENSOR: 'ws://example.com:8765/ws/sensor',
  WS_GPS: 'ws://example.com:8765/ws/gps',
  RTSP_URL: '',
  // BW21 实时视频流 (MJPEG)
  MJPEG_URL: 'http://example.com:8765/live/mjpeg',
  // WebSocket 视频流（备用）
  WS_VIDEO: 'ws://example.com:8765/ws/video',
  API_BASE: 'http://example.com:8765/api',
  WIFI_MANAGER_BASE: 'http://192.168.4.1:8081/api/wifi',
  MAP_TILE_URL: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
  MAP_CENTER: [39.9042, 116.4074],
  MAP_ZOOM: 15,
  SENSOR_INTERVAL: 2000,
  GPS_INTERVAL: 2000,
  TELEMETRY_STALE_MS: 6500,
}

// ==================== 键盘到 PS2 按键映射 ====================
const KEY_MAP = {
  ArrowUp: 'PAD_UP',
  ArrowRight: 'PAD_RIGHT',
  ArrowDown: 'PAD_DOWN',
  ArrowLeft: 'PAD_LEFT',
  w: 'PAD_UP',
  W: 'PAD_UP',
  d: 'PAD_RIGHT',
  D: 'PAD_RIGHT',
  s: 'PAD_DOWN',
  S: 'PAD_DOWN',
  a: 'PAD_LEFT',
  A: 'PAD_LEFT',
}

// ==================== 云端四向控制布局 ====================
const PS2_BUTTONS = {
  dpad: [
    { key: 'PAD_UP', button: 'PAD_UP', label: '▲', class: 'dpad-up' },
    { key: 'PAD_LEFT', button: 'PAD_LEFT', label: '◀', class: 'dpad-left' },
    { key: 'PAD_RIGHT', button: 'PAD_RIGHT', label: '▶', class: 'dpad-right' },
    { key: 'PAD_DOWN', button: 'PAD_DOWN', label: '▼', class: 'dpad-down' },
  ],
}

const cfg = ENV === 'production' ? PRODUCTION : DEVELOPMENT

export default {
  ...cfg,
  ENV,
  KEY_MAP,
  PS2_BUTTONS,
}
