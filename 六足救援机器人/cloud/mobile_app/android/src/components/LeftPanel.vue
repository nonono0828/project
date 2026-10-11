<template>
  <aside class="fixed left-0 top-0 z-[300] flex flex-col gap-2.5 p-2.5 overflow-y-auto" :class="{ 'left-panel--embedded': embedded }" :style="panelStyle">
    <!-- 标题 -->
    <div class="flex items-center gap-1.5 pb-2" style="border-bottom: 1px solid var(--border-color)">
      <img src="/retouch_2026060418314442.png" class="w-5 h-5 object-contain" />
      <span class="text-xs font-semibold" style="color: var(--text-primary)">{{ settingsOnly ? '机器人设置' : '机器人控制' }}</span>
    </div>

    <!-- 窗口启停开关 -->
    <div v-if="!settingsOnly" class="flex flex-col gap-1.5">
      <div class="text-[10px] font-medium" style="color: var(--text-tertiary)">窗口控制</div>
      <div
        v-for="item in windowItems"
        :key="item.key"
        class="flex items-center justify-between py-0.5 px-1 rounded-md hover:bg-black/5 dark:hover:bg-white/5"
      >
        <span class="text-[11px]" style="color: var(--text-secondary)">{{ item.label }}</span>
        <!-- iOS 风格开关 -->
        <button
          class="ios-toggle"
          :class="{ active: item.active }"
          type="button"
          role="switch"
          :aria-checked="item.active"
          :aria-label="`${item.active ? '关闭' : '打开'}${item.label}`"
          @click="$emit('toggleWindow', item.key)"
        >
          <span class="ios-toggle-knob"></span>
        </button>
      </div>
    </div>

    <!-- 布局模式 -->
    <div v-if="!settingsOnly" class="flex flex-col gap-1.5">
      <div class="text-[10px] font-medium" style="color: var(--text-tertiary)">布局模式</div>
      <div class="flex items-center justify-between py-0.5 px-1 rounded-md hover:bg-black/5 dark:hover:bg-white/5">
        <span class="text-[11px]" style="color: var(--text-secondary)">{{ layoutMode === 'split' ? '分屏模式' : '小窗模式' }}</span>
        <button
          class="ios-toggle"
          :class="{ active: layoutMode === 'split' }"
          type="button"
          role="switch"
          :aria-checked="layoutMode === 'split'"
          :aria-label="`切换到${layoutMode === 'split' ? '小窗模式' : '分屏模式'}`"
          @click="$emit('toggleLayout')"
        >
          <span class="ios-toggle-knob"></span>
        </button>
      </div>
    </div>

    <div v-if="!settingsOnly" style="border-bottom: 1px solid var(--border-color)"></div>

    <!-- 实时数据 -->
    <div v-if="!settingsOnly" class="flex flex-col gap-1.5">
      <div class="text-[10px] font-medium" style="color: var(--text-tertiary)">实时数据</div>

      <!-- 温度/湿度/海拔 -->
      <div class="data-card flex justify-between items-center !p-2">
        <span class="text-[11px]" style="color: var(--text-secondary)">温度</span>
        <span class="text-xs font-bold" style="color: var(--text-primary)">
          {{ formatValue(sensorData.temperature, '°C') }}
        </span>
      </div>
      <div class="data-card flex justify-between items-center !p-2">
        <span class="text-[11px]" style="color: var(--text-secondary)">湿度</span>
        <span class="text-xs font-bold" style="color: var(--text-primary)">
          {{ formatValue(sensorData.humidity, '%') }}
        </span>
      </div>
      <div class="data-card flex justify-between items-center !p-2">
        <span class="text-[11px]" style="color: var(--text-secondary)">海拔</span>
        <span class="text-xs font-bold" style="color: var(--text-primary)">
          {{ formatValue(sensorData.altitude, 'm') }}
        </span>
      </div>
      <div class="data-card flex justify-between items-center !p-2">
        <span class="text-[11px]" style="color: var(--text-secondary)">气压</span>
        <span class="text-xs font-bold" style="color: var(--text-primary)">
          {{ formatValue(sensorData.pressure, 'hPa') }}
        </span>
      </div>
      <div class="data-card flex items-center justify-between gap-2 !p-2">
        <div class="text-[11px]" style="color: var(--text-secondary)">可燃气体</div>
        <div class="flex items-center gap-1.5 flex-shrink-0">
          <span class="gas-led" :class="gasLedClass"></span>
          <span class="text-[10px] font-medium" :class="gasTextClass">{{ gasStateText }}</span>
        </div>
      </div>
    </div>

    <div v-if="!settingsOnly" style="border-bottom: 1px solid var(--border-color)"></div>

    <!-- 机器人配置 -->
    <div class="flex flex-col gap-2">
      <div class="text-[10px] font-medium" style="color: var(--text-tertiary)">机器人配置</div>
      <div class="flex flex-col gap-1.5">
        <div class="data-card flex flex-col gap-1.5 !p-2">
          <div class="flex items-center justify-between gap-2">
            <div class="min-w-0">
              <div class="text-[11px]" style="color: var(--text-secondary)">本地配置热点</div>
              <div class="flex items-center gap-1 mt-0.5">
                <span class="ap-state-led" :class="apLedClass"></span>
                <span class="text-[9px] truncate" :class="apTextClass">{{ apStateText }}</span>
              </div>
            </div>
            <button
              class="ios-toggle"
              :class="{ active: apStreamEnabled, disabled: !canToggleAp }"
              type="button"
              role="switch"
              :aria-checked="apStreamEnabled"
              :aria-label="apStreamEnabled ? '关闭本地配置热点' : '开启本地配置热点'"
              :disabled="!canToggleAp"
              @click="toggleApStream"
            >
              <span class="ios-toggle-knob"></span>
            </button>
          </div>
          <a
            v-if="localApStreamAvailable"
            class="local-stream-link"
            :href="LOCAL_AP_STREAM_URL"
            target="_blank"
            rel="noopener noreferrer"
            title="仅连接 ESP-L610-4G 热点时可访问"
          >
            <span>本地视频</span>
            <span>192.168.4.1:8080/live/mjpeg</span>
          </a>
          <div v-else class="local-stream-link local-stream-link-disabled">
            <span>本地视频</span>
            <span>192.168.4.1:8080/live/mjpeg</span>
          </div>
        </div>
        <button
          class="data-card modem-settings-entry flex items-center justify-between gap-2 !p-2"
          type="button"
          @click="showModemSettings = true"
        >
          <div class="text-left min-w-0">
            <div class="text-[11px]" style="color: var(--text-secondary)">4G 设置</div>
            <div class="text-[9px] truncate mt-0.5" style="color: var(--text-tertiary)">
              基站 · 频段 · 延迟
            </div>
          </div>
          <span class="modem-settings-arrow">›</span>
        </button>
        <button
          class="data-card modem-settings-entry flex items-center justify-between gap-2 !p-2"
          type="button"
          @click="showVideoStreamSettings = true"
        >
          <div class="text-left min-w-0">
            <div class="text-[11px]" style="color: var(--text-secondary)">视频流设置</div>
            <div class="text-[9px] truncate mt-0.5" style="color: var(--text-tertiary)">
              清晰度 · 视频帧率
            </div>
          </div>
          <span class="modem-settings-arrow">›</span>
        </button>
        <button
          class="data-card modem-settings-entry flex items-center justify-between gap-2 !p-2"
          type="button"
          @click="showWifiSettings = true"
        >
          <div class="text-left min-w-0">
            <div class="text-[11px]" style="color: var(--text-secondary)">Wi-Fi 设置</div>
            <div class="text-[9px] truncate mt-0.5" style="color: var(--text-tertiary)">
              按需扫描 · 连接 · 云端上行
            </div>
          </div>
          <span class="modem-settings-arrow">›</span>
        </button>
      </div>
    </div>

    <Modem4GDialog v-model="showModemSettings" />
    <VideoStreamSettingsDialog v-model="showVideoStreamSettings" />
    <WifiSettingsDialog v-model="showWifiSettings" />

    <!-- 气体报警弹窗 -->
    <Teleport to="body">
      <div v-if="activeAlarm === 'gas'" class="popup-overlay">
        <div
          class="popup-card popup-gas"
          role="alertdialog"
          aria-modal="true"
          aria-live="assertive"
          aria-labelledby="gas-alarm-title"
        >
          <div class="text-3xl mb-2">⚠️</div>
          <div id="gas-alarm-title" class="text-lg font-bold text-red-500 mb-1">可燃气体报警!</div>
          <button
            ref="alarmConfirmButton"
            class="mt-3 px-4 py-1 rounded-full text-xs font-medium bg-red-500 text-white hover:bg-red-600 transition-colors"
            @click="dismissActiveAlarm"
          >确认已知晓</button>
        </div>
      </div>
    </Teleport>

    <!-- 人体识别弹窗 -->
    <Teleport to="body">
      <div v-if="activeAlarm === 'person'" class="popup-overlay">
        <div
          class="popup-card popup-person"
          role="alertdialog"
          aria-modal="true"
          aria-live="assertive"
          aria-labelledby="person-alarm-title"
        >
          <div class="text-3xl mb-2">🧑</div>
          <div id="person-alarm-title" class="text-lg font-bold text-blue-500 mb-1">识别到疑似幸存者!</div>
          <button
            ref="alarmConfirmButton"
            class="mt-3 px-4 py-1 rounded-full text-xs font-medium bg-blue-500 text-white hover:bg-blue-600 transition-colors"
            @click="dismissActiveAlarm"
          >确认已知晓</button>
        </div>
      </div>
    </Teleport>

    <div style="border-bottom: 1px solid var(--border-color)"></div>

    <!-- 底部状态 -->
    <div class="flex flex-col gap-1.5">
      <div class="flex items-center gap-1.5 text-[10px]" style="color: var(--text-tertiary)">
        <span class="w-1.5 h-1.5 rounded-full" :class="deviceOnline ? 'bg-green-400' : 'bg-red-400'"></span>
        {{ deviceOnline ? '机器人已连接' : '机器人未连接' }}
      </div>
      <div class="theme-mode-row">
        <div class="theme-mode-copy">
          <span class="theme-mode-indicator" :class="{ 'is-dark': isDark }" aria-hidden="true"></span>
          <span>{{ isDark ? '夜间模式' : '日间模式' }}</span>
        </div>
        <button
          class="ios-toggle"
          :class="{ active: isDark }"
          type="button"
          role="switch"
          :aria-checked="isDark"
          :aria-label="isDark ? '切换至日间模式' : '切换至夜间模式'"
          @click="$emit('toggleTheme')"
        >
          <span class="ios-toggle-knob"></span>
        </button>
      </div>
      <div class="account-actions">
        <button class="account-button" type="button" @click="$emit('appHome')">
          <span aria-hidden="true">⌂</span>
          <span>返回功能首页</span>
        </button>
        <button class="account-button account-button--danger" type="button" @click="$emit('logout')">
          <span aria-hidden="true">↪</span>
          <span>退出登录</span>
        </button>
      </div>
    </div>
  </aside>
</template>

<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import { useSensorStore } from '@/composables/useSensorStore'
import { useControlChannel } from '@/composables/useControlChannel'
import Modem4GDialog from '@/components/Modem4GDialog.vue'
import VideoStreamSettingsDialog from '@/components/VideoStreamSettingsDialog.vue'
import WifiSettingsDialog from '@/components/WifiSettingsDialog.vue'
import { useAlarmPopupStore } from '@/composables/useAlarmPopupStore'

defineEmits(['toggleWindow', 'toggleTheme', 'toggleLayout', 'appHome', 'logout'])
const props = defineProps({
  width: { type: Number, default: 190 },
  windowItems: { type: Array, default: () => [] },
  isDark: { type: Boolean, default: false },
  layoutMode: { type: String, default: 'free' },
  embedded: { type: Boolean, default: false },
  settingsOnly: { type: Boolean, default: false },
})

const LOCAL_AP_STREAM_URL = 'http://192.168.4.1:8080/live/mjpeg'

const { connected: wsConnected, online: deviceOnline, sensorData } = useSensorStore()
const {
  connected: controlConnected,
  deviceOnline: apDeviceOnline,
  apStream,
  apRequest,
  apStreamEnabled,
  apStreamBusy,
  requestApStream,
} = useControlChannel()
const localApStreamAvailable = computed(() => (
  controlConnected.value
  && apStream.value.supported === true
  && apStreamEnabled.value
))

function formatValue(value, unit) {
  return value == null ? 'NA' : `${value}${unit}`
}

const showModemSettings = ref(false)
const showVideoStreamSettings = ref(false)
const showWifiSettings = ref(false)
const { activeAlarm, dismissActiveAlarm } = useAlarmPopupStore()
const alarmConfirmButton = ref(null)

watch(activeAlarm, async (alarm) => {
  if (!alarm) return
  await nextTick()
  alarmConfirmButton.value?.focus?.()
})

function guardAlarmOnAppBack(event) {
  if (!activeAlarm.value || event?.defaultPrevented) return
  event.preventDefault()
  alarmConfirmButton.value?.focus?.()
}

onMounted(() => window.addEventListener('robot-app-back', guardAlarmOnAppBack))
onBeforeUnmount(() => window.removeEventListener('robot-app-back', guardAlarmOnAppBack))

const gas = computed(() => sensorData.value?.gas ?? null)
function normalizeGasAlarm(value) {
  const alarm = value && typeof value === 'object' ? value.alarm : value
  if (alarm === true || alarm === 1) return true
  if (alarm === false || alarm === 0) return false
  return null
}

const gasFlag = computed(() => {
  const value = normalizeGasAlarm(gas.value)
  if (value === true || value === 1) return true
  if (value === false || value === 0) return false
  return null
})
const gasAlarm = computed(() => gasFlag.value === true)
const gasStateKnown = computed(() => (
  deviceOnline.value && gasFlag.value !== null
))
const gasStateText = computed(() => {
  if (!gasStateKnown.value) return 'NA'
  return gasAlarm.value ? '报警' : '正常'
})
const gasLedClass = computed(() => {
  if (!gasStateKnown.value) return 'led-offline'
  return gasAlarm.value ? 'led-red' : 'led-green'
})
const gasTextClass = computed(() => {
  if (!gasStateKnown.value) return 'text-gray-400'
  return gasAlarm.value ? 'text-red-500' : 'text-green-500'
})

const canToggleAp = computed(() => (
  controlConnected.value
  && apDeviceOnline.value === true
  && apStream.value.supported === true
  && !apStreamBusy.value
))
const apStateText = computed(() => {
  if (!controlConnected.value) return '机器人未连接'
  if (apDeviceOnline.value === false) return '机器人未连接'
  if (apDeviceOnline.value !== true) return '等待设备状态'
  if (apRequest.value.status === 'rejected') return apRequest.value.error || '请求被拒绝'
  if (apRequest.value.status === 'timeout') return apRequest.value.error || '设备确认超时'
  if (apRequest.value.pending) {
    return apRequest.value.status === 'waiting' ? '等待设备确认' : '请求已排队'
  }
  if (apStream.value.supported === false) return '固件不支持'
  if (apStream.value.supported !== true) return '状态未报告'
  const labels = {
    starting: '正在开启',
    stopping: '正在关闭',
    enabled: '热点已开启',
    disabled: '热点已关闭',
    error: apStream.value.error ? `失败：${apStream.value.error}` : '操作失败',
  }
  return labels[apStream.value.state] ?? '状态未报告'
})
const apLedClass = computed(() => {
  if (apDeviceOnline.value === false) return 'ap-led-red'
  if (!controlConnected.value || apDeviceOnline.value !== true) return 'ap-led-offline'
  if (apRequest.value.status === 'rejected' || apRequest.value.status === 'timeout') return 'ap-led-red'
  if (apRequest.value.pending) return 'ap-led-amber'
  if (apStream.value.state === 'enabled') return 'ap-led-green'
  if (apStreamBusy.value) return 'ap-led-amber'
  if (apStream.value.state === 'error') return 'ap-led-red'
  return 'ap-led-offline'
})
const apTextClass = computed(() => {
  if (
    apDeviceOnline.value === false
    || apStream.value.state === 'error'
    || apRequest.value.status === 'rejected'
    || apRequest.value.status === 'timeout'
  ) return 'text-red-500'
  if (apRequest.value.pending) return 'text-amber-500'
  if (apStream.value.state === 'enabled') return 'text-green-500'
  if (apStreamBusy.value) return 'text-amber-500'
  return 'text-gray-400'
})

function toggleApStream() {
  if (!canToggleAp.value) return
  requestApStream(!apStreamEnabled.value)
}

const panelStyle = computed(() => (props.embedded ? {
  position: 'relative',
  inset: 'auto',
  width: '100%',
  height: 'auto',
  minHeight: '100%',
  background: 'var(--glass-bg)',
  backdropFilter: 'blur(20px) saturate(180%)',
  WebkitBackdropFilter: 'blur(20px) saturate(180%)',
  border: '1px solid var(--glass-border)',
  borderRadius: '12px',
} : {
  width: `${props.width}px`,
  height: 'calc(100vh - 90px)',
  background: 'var(--glass-bg)',
  backdropFilter: 'blur(20px) saturate(180%)',
  WebkitBackdropFilter: 'blur(20px) saturate(180%)',
  borderRight: '1px solid var(--glass-border)',
}))
</script>

<style scoped>
/* iOS 风格开关 */
.ios-toggle {
  position: relative;
  display: inline-block;
  width: 40px;
  height: 22px;
  padding: 0;
  border: 0;
  border-radius: 22px;
  background: #e0e0e0;
  cursor: pointer;
  transition: background 0.2s ease;
  flex-shrink: 0;
}
.ios-toggle.active {
  background: #34C759;
}
.ios-toggle-knob {
  position: absolute;
  top: 2px;
  left: 2px;
  width: 18px;
  height: 18px;
  border-radius: 50%;
  background: #fff;
  box-shadow: 0 1px 3px rgba(0,0,0,0.2);
  transition: transform 0.2s ease;
}
.ios-toggle.active .ios-toggle-knob {
  transform: translateX(18px);
}
.ios-toggle.disabled {
  opacity: 0.45;
  cursor: not-allowed;
}
.dark .ios-toggle { background: #48484a; }
.dark .ios-toggle.active { background: #34C759; }

/* 气体状态 LED */
.gas-led {
  display: inline-block;
  width: 10px; height: 10px;
  border-radius: 50%;
}
.led-green {
  background: #34C759;
  box-shadow: 0 0 6px #34C759;
  animation: led-blink-slow 1.5s ease-in-out infinite;
}
.led-red {
  background: #ff3b30;
  box-shadow: 0 0 8px #ff3b30;
  animation: led-blink-fast 0.3s ease-in-out infinite;
}
.led-offline {
  background: #9ca3af;
  box-shadow: none;
}
.ap-state-led {
  display: inline-block;
  width: 8px;
  height: 8px;
  flex: 0 0 8px;
  border-radius: 50%;
}
.ap-led-green {
  background: #34c759;
  box-shadow: 0 0 6px #34c759;
  animation: led-blink-slow 1.8s ease-in-out infinite;
}
.ap-led-amber {
  background: #f59e0b;
  box-shadow: 0 0 6px #f59e0b;
  animation: led-blink-slow 0.8s ease-in-out infinite;
}
.ap-led-red {
  background: #ff3b30;
  box-shadow: 0 0 6px #ff3b30;
}
.ap-led-offline {
  background: #9ca3af;
  box-shadow: none;
}
.local-stream-link {
  display: flex;
  flex-direction: column;
  gap: 1px;
  width: 100%;
  color: #007aff;
  font-size: 9px;
  line-height: 1.25;
  text-decoration: none;
  overflow-wrap: anywhere;
}
.local-stream-link:hover {
  text-decoration: underline;
}
.local-stream-link-disabled {
  color: var(--text-tertiary);
  cursor: default;
}
.local-stream-link-disabled:hover {
  text-decoration: none;
}
.modem-settings-entry {
  width: 100%;
  border: 1px solid var(--border-color);
  background: transparent;
  cursor: pointer;
}
.modem-settings-entry:hover { border-color: rgba(0, 122, 255, 0.45); }
.modem-settings-arrow { flex: 0 0 auto; font-size: 20px; line-height: 1; color: #007aff; }
.account-actions {
  display: grid;
  gap: 6px;
  margin-top: 3px;
}
.account-button {
  width: 100%;
  min-height: 32px;
  display: flex;
  align-items: center;
  gap: 7px;
  padding: 5px 8px;
  border: 1px solid var(--border-color);
  border-radius: 8px;
  color: var(--text-secondary);
  background: rgba(0, 122, 255, .035);
  font-size: 10px;
  font-weight: 600;
  text-align: left;
  transition: border-color .16s ease, background .16s ease, transform .16s ease;
}
.account-button:hover {
  border-color: rgba(0, 122, 255, .36);
  background: rgba(0, 122, 255, .08);
}
.account-button:active { transform: scale(.98); }
.account-button--danger {
  color: #d94a43;
  background: rgba(255, 59, 48, .035);
}
.account-button--danger:hover {
  border-color: rgba(255, 59, 48, .32);
  background: rgba(255, 59, 48, .08);
}
.theme-mode-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  min-height: 30px;
  padding: 0 8px;
  border: 1px solid rgba(17, 88, 153, .10);
  border-radius: 8px;
  color: #6e7a86;
  background: #f7f9fb;
  font-size: 10px;
  font-weight: 600;
}
.theme-mode-copy { display: flex; align-items: center; gap: 7px; }
.theme-mode-indicator {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: #1d7cc4;
  box-shadow: 0 0 0 3px rgba(29, 124, 196, .10);
}
.theme-mode-indicator.is-dark { background: #596d82; box-shadow: 0 0 0 3px rgba(89, 109, 130, .14); }
.theme-mode-row .ios-toggle { transform: scale(.76); transform-origin: right center; }
.dark .theme-mode-row { border-color: #2d4052; color: #aab9c7; background: #101c28; }
.left-panel--embedded {
  z-index: 1;
  overflow: visible;
  padding: 14px;
  box-shadow: 0 8px 28px rgba(0,0,0,.12);
}
.left-panel--embedded .modem-settings-entry,
.left-panel--embedded button:not(.ios-toggle) {
  min-height: 44px;
}
.left-panel--embedded .ios-toggle {
  width: 44px;
  height: 44px;
  border-radius: 22px;
  background: transparent;
}
.left-panel--embedded .ios-toggle::before {
  content: '';
  position: absolute;
  top: 11px;
  left: 2px;
  width: 40px;
  height: 22px;
  border-radius: 11px;
  background: #e0e0e0;
}
.left-panel--embedded .ios-toggle.active { background: transparent; }
.left-panel--embedded .ios-toggle.active::before { background: #34c759; }
.left-panel--embedded .ios-toggle-knob {
  top: 13px;
  left: 4px;
}
.left-panel--embedded .ios-toggle.active .ios-toggle-knob {
  transform: translateX(18px);
}
.dark .left-panel--embedded .ios-toggle::before { background: #48484a; }
.dark .left-panel--embedded .ios-toggle.active::before { background: #34c759; }
@keyframes led-blink-slow {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.3; }
}
@keyframes led-blink-fast {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.15; }
}

/* 弹窗遮罩 (共用) */
.popup-overlay {
  position: fixed; inset: 0;
  z-index: 20000;
  display: flex; align-items: center; justify-content: center;
  background: rgba(0,0,0,0.5);
  backdrop-filter: blur(4px);
  animation: fade-in 0.2s ease;
}
.popup-card {
  background: var(--glass-bg);
  backdrop-filter: blur(20px);
  border: 1px solid var(--glass-border);
  border-radius: 16px;
  padding: 28px 36px;
  text-align: center;
  animation: pop-in 0.25s ease;
}
.popup-card button { min-height: 44px; }
.popup-gas {
  box-shadow: 0 12px 40px rgba(255,59,48,0.25);
}
.popup-person {
  box-shadow: 0 12px 40px rgba(0,122,255,0.25);
}
@keyframes fade-in {
  from { opacity: 0; }
  to { opacity: 1; }
}
@keyframes pop-in {
  from { transform: scale(0.85); opacity: 0; }
  to { transform: scale(1); opacity: 1; }
}
</style>
