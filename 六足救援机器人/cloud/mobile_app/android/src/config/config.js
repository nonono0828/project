/**
 * 机器人远程控制面板 - 统一接口配置
 *
 * 切换环境：修改 ENV 为 'production' 或 'development'
 * 打包部署时使用 production，本地调试使用 development
 */

const ENV = 'production'

function trimTrailingSlash(value) {
  return String(value || '').replace(/\/+$/, '')
}

const CLOUD_HTTP_ORIGIN = trimTrailingSlash(
  import.meta.env.VITE_CLOUD_HTTP_ORIGIN
)
const CLOUD_WS_ORIGIN = trimTrailingSlash(
  import.meta.env.VITE_CLOUD_WS_ORIGIN
  || CLOUD_HTTP_ORIGIN.replace(/^http/i, 'ws')
)

if (!CLOUD_HTTP_ORIGIN || !CLOUD_WS_ORIGIN) {
  throw new Error('Cloud endpoint configuration is missing')
}

// ==================== 正式服务端接口 ====================
const PRODUCTION = {
  // WebSocket 主控地址
  WS_MAIN: `${CLOUD_WS_ORIGIN}/ws/control`,

  // WebSocket 传感器数据地址
  WS_SENSOR: `${CLOUD_WS_ORIGIN}/ws/sensor`,

  // WebSocket GPS 数据地址
  WS_GPS: `${CLOUD_WS_ORIGIN}/ws/gps`,

  // 视频流 MJPEG
  MJPEG_URL: `${CLOUD_HTTP_ORIGIN}/live/mjpeg`,

  // HTTP API 基础地址
  API_BASE: `${CLOUD_HTTP_ORIGIN}/api`,
  WIFI_MANAGER_BASE: 'http://192.168.4.1:8081/api/wifi',

  // 地图瓦片服务
  // HOT 已在当前 4G 环境实测可达；标准 OSM 保留为超时后的备用。
  MAP_TILE_URL: 'https://{s}.tile.openstreetmap.fr/hot/{z}/{x}/{y}.png',
  MAP_TILE_FALLBACK_URL: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
  MAP_CENTER: [39.9042, 116.4074],
  MAP_ZOOM: 15,
  SENSOR_INTERVAL: 1000,
  GPS_INTERVAL: 1000,
  TELEMETRY_STALE_MS: 6500,
}

// ==================== 本地调试接口 ====================
const DEVELOPMENT = {
  WS_MAIN: `${CLOUD_WS_ORIGIN}/ws/control`,
  WS_SENSOR: `${CLOUD_WS_ORIGIN}/ws/sensor`,
  WS_GPS: `${CLOUD_WS_ORIGIN}/ws/gps`,
  RTSP_URL: '',
  // BW21 实时视频流 (MJPEG)
  MJPEG_URL: `${CLOUD_HTTP_ORIGIN}/live/mjpeg`,
  // WebSocket 视频流（备用）
  WS_VIDEO: `${CLOUD_WS_ORIGIN}/ws/video`,
  API_BASE: `${CLOUD_HTTP_ORIGIN}/api`,
  WIFI_MANAGER_BASE: 'http://192.168.4.1:8081/api/wifi',
  MAP_TILE_URL: 'https://{s}.tile.openstreetmap.fr/hot/{z}/{x}/{y}.png',
  MAP_TILE_FALLBACK_URL: 'https://tile.openstreetmap.org/{z}/{x}/{y}.png',
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
