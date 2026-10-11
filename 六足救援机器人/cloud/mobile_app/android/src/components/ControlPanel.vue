<template>
  <div class="control-bar" :class="{ embedded, mobile }" :style="barStyle" aria-label="机器人方向控制器">
    <div class="dpad-grid">
      <button
        v-for="btn in buttons.dpad"
        :key="btn.key"
        class="ps2-btn dpad-btn"
        :class="[btn.class, { active: activeKeys.has(btn.key) }]"
        :aria-label="btn.button || '方向键中心'"
        :disabled="!controlReady"
        :title="controlStatusHint"
        @pointerdown.prevent="onBtnPress(btn)"
        @pointerup.prevent="onBtnRelease(btn.key)"
        @pointerleave.prevent="onBtnRelease(btn.key)"
        @pointercancel.prevent="onBtnRelease(btn.key)"
      >
        {{ btn.label }}
      </button>
      <span class="dpad-center" aria-hidden="true"></span>
    </div>

    <div class="control-adjustments" :class="{ pending: hasRobotControlPending }">
      <label class="range-control height-control" :class="{ pending: isPending('body_height') }">
        <span class="range-caption"><span>机身高度</span><b>{{ confirmedHeightLabel }}</b></span>
        <input
          v-model.number="bodyHeightInput"
          type="range"
          min="-30"
          max="70"
          step="1"
          :disabled="!robotControlReady"
          aria-label="机身高度"
          @input="queueSlider('body_height', bodyHeightInput)"
          @change="sendSliderFinal('body_height', bodyHeightInput)"
        >
      </label>
      <label class="range-control speed-control" :class="{ pending: isPending('speed_percent') }">
        <span class="range-caption"><span>调速</span><b>{{ confirmedSpeedLabel }}</b></span>
        <input
          v-model.number="speedInput"
          type="range"
          min="30"
          max="100"
          step="1"
          :disabled="!robotControlReady"
          aria-label="调速"
          @input="queueSlider('speed_percent', speedInput)"
          @change="sendSliderFinal('speed_percent', speedInput)"
        >
      </label>
    </div>

    <div class="switch-list" aria-label="机器人模式">
      <div class="switch-setting" :class="{ pending: isPending('terrain') }">
        <span class="switch-label">越野模式</span>
        <button class="robot-switch" :class="{ enabled: robotControl.terrain === true }" :disabled="!robotControlReady" role="switch" :aria-checked="robotControl.terrain === true" aria-label="越野模式" @click="setToggle('terrain')">
          <i aria-hidden="true"></i>
        </button>
      </div>
      <div class="switch-setting" :class="{ pending: isPending('autonomous') }">
        <span class="switch-label">自主避障</span>
        <button class="robot-switch" :class="{ enabled: robotControl.autonomous === true }" :disabled="!robotControlReady" role="switch" :aria-checked="robotControl.autonomous === true" aria-label="自主避障" @click="setToggle('autonomous')">
          <i aria-hidden="true"></i>
        </button>
      </div>
    </div>

    <div class="center-controls flex flex-col items-center gap-0.5 mx-1">
      <div
        class="control-device-state flex items-center gap-1 text-[9px]"
        :class="controlStatusTone"
        role="status"
        aria-live="polite"
      >
        <span class="control-status-dot" aria-hidden="true"></span>
        {{ controlStatusText }}
      </div>
    </div>
  </div>
</template>

<script setup>
import { reactive, computed, onMounted, onUnmounted, watch, ref } from 'vue'
import cfg from '@/config/config'
import { useControlChannel } from '@/composables/useControlChannel'
import { useSensorStore } from '@/composables/useSensorStore'

const props = defineProps({
  leftInset: { type: Number, default: 0 },
  rightInset: { type: Number, default: 0 },
  embedded: { type: Boolean, default: false },
  mobile: { type: Boolean, default: false },
})

const barStyle = computed(() => (props.embedded ? {} : ({
  paddingLeft: `${props.leftInset + 14}px`,
  paddingRight: `${props.rightInset + 14}px`,
})))

const {
  sendCommand,
  requestRobotControl,
  robotControl,
  robotControlRequest,
  connected: wsConnected,
} = useControlChannel()
const { online: deviceOnline } = useSensorStore()
const controlReady = computed(() => wsConnected.value === true && deviceOnline.value === true)
const controlStatusText = computed(() => {
  if (!wsConnected.value) return '控制链路未连接'
  if (!deviceOnline.value) return '机器人未连接'
  return '机器人已连接，可控制'
})
const controlStatusHint = computed(() => (
  controlReady.value ? '按住发送运动指令，松开停止' : `${controlStatusText.value}，运动按键已禁用`
))
const controlStatusTone = computed(() => {
  if (controlReady.value) return 'is-ready'
  return wsConnected.value ? 'is-offline' : 'is-disconnected'
})

const bodyHeightInput = ref(0)
const speedInput = ref(100)
const sliderTimers = new Map()
const sliderDrafts = reactive({
  body_height: null,
  speed_percent: null,
})
const robotControlReady = computed(() => (
  controlReady.value && robotControl.value.supported === true
))
const confirmedHeightLabel = computed(() => (
  Number.isInteger(robotControl.value.body_height) ? `${robotControl.value.body_height} mm` : '--'
))
const confirmedSpeedLabel = computed(() => (
  Number.isInteger(robotControl.value.speed_percent) ? `${robotControl.value.speed_percent}%` : '--'
))
const hasRobotControlPending = computed(() => (
  Object.values(robotControlRequest.value).some(request => request?.pending)
))

function isPending(control) {
  return robotControlRequest.value[control]?.pending === true
}

function requestSlider(control, value) {
  if (!robotControlReady.value) return
  requestRobotControl(control, Number(value))
}

function queueSlider(control, value) {
  sliderDrafts[control] = Number(value)
  const existing = sliderTimers.get(control)
  if (existing) clearTimeout(existing)
  sliderTimers.set(control, setTimeout(() => {
    sliderTimers.delete(control)
    requestSlider(control, sliderDrafts[control])
  }, 150))
}

function sendSliderFinal(control, value) {
  sliderDrafts[control] = Number(value)
  const existing = sliderTimers.get(control)
  if (existing) clearTimeout(existing)
  sliderTimers.delete(control)
  requestSlider(control, value)
}

function setToggle(control) {
  if (!robotControlReady.value || isPending(control)) return
  if (control === 'autonomous' && robotControl.value[control] !== true) releaseAllButtons()
  requestRobotControl(control, robotControl.value[control] !== true)
}

watch(robotControl, (state) => {
  if (Number.isInteger(state.body_height)
    && (sliderDrafts.body_height === null || state.body_height === sliderDrafts.body_height)) {
    bodyHeightInput.value = state.body_height
    sliderDrafts.body_height = null
  }
  if (Number.isInteger(state.speed_percent)
    && (sliderDrafts.speed_percent === null || state.speed_percent === sliderDrafts.speed_percent)) {
    speedInput.value = state.speed_percent
    sliderDrafts.speed_percent = null
  }
}, { deep: true, immediate: true })

watch(robotControlRequest, (requests) => {
  for (const [control, request] of Object.entries(requests)) {
    if (request?.pending || !['rejected', 'timeout'].includes(request?.status)) continue
    if (control === 'body_height' && Number.isInteger(robotControl.value.body_height)) {
      bodyHeightInput.value = robotControl.value.body_height
      sliderDrafts.body_height = null
    }
    if (control === 'speed_percent' && Number.isInteger(robotControl.value.speed_percent)) {
      speedInput.value = robotControl.value.speed_percent
      sliderDrafts.speed_percent = null
    }
  }
}, { deep: true })

const buttons = cfg.PS2_BUTTONS
const activeKeys = reactive(new Set())
const repeatTimers = new Map()
const flatButtons = Object.values(buttons).flat()
const buttonByName = new Map(
  flatButtons.filter(btn => btn.button).map(btn => [btn.button, btn]),
)
const oppositeDirection = {
  PAD_UP: 'PAD_DOWN',
  PAD_DOWN: 'PAD_UP',
  PAD_LEFT: 'PAD_RIGHT',
  PAD_RIGHT: 'PAD_LEFT',
}

function sendButtonState(button, state) {
  sendCommand('ps2_button', { button, state })
}

function onBtnPress(btn) {
  if (!controlReady.value || !btn.button || activeKeys.has(btn.key)) return

  const opposite = buttonByName.get(oppositeDirection[btn.button])
  if (opposite && activeKeys.has(opposite.key)) {
    onBtnRelease(opposite.key)
  }

  activeKeys.add(btn.key)
  sendButtonState(btn.button, 'down')

  const timer = setInterval(() => {
    sendButtonState(btn.button, 'down')
  }, 200)
  repeatTimers.set(btn.key, timer)
}

function onBtnRelease(key) {
  const timer = repeatTimers.get(key)
  if (timer) clearInterval(timer)
  repeatTimers.delete(key)

  if (!activeKeys.has(key)) return
  activeKeys.delete(key)

  const btn = buttonByName.get(key)
  if (btn) sendButtonState(btn.button, 'up')
}

const keyToBtnKey = {}
function keyboardKeyId(key) {
  return key.length === 1 ? key.toLowerCase() : key
}

function onKeyDown(e) {
  if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return
  if (e.repeat) return
  if (!controlReady.value) return
  const keyId = keyboardKeyId(e.key)
  const button = cfg.KEY_MAP[keyId]
  if (!button) return
  e.preventDefault()
  const btn = buttonByName.get(button)
  if (!btn) return
  keyToBtnKey[keyId] = btn.key
  onBtnPress(btn)
}

function onKeyUp(e) {
  const keyId = keyboardKeyId(e.key)
  const btnKey = keyToBtnKey[keyId]
  if (!btnKey) return
  e.preventDefault()
  onBtnRelease(btnKey)
  delete keyToBtnKey[keyId]
}

function releaseAllButtons() {
  for (const key of [...activeKeys]) onBtnRelease(key)
  for (const timer of repeatTimers.values()) clearInterval(timer)
  repeatTimers.clear()
  for (const key of Object.keys(keyToBtnKey)) delete keyToBtnKey[key]
}

defineExpose({ releaseAllButtons })

function onVisibilityChange() {
  if (document.visibilityState === 'hidden') releaseAllButtons()
}

watch([wsConnected, deviceOnline], ([controlConnected, robotOnline]) => {
  if (!controlConnected || !robotOnline) releaseAllButtons()
})

onMounted(() => {
  window.addEventListener('keydown', onKeyDown)
  window.addEventListener('keyup', onKeyUp)
  window.addEventListener('blur', releaseAllButtons)
  window.addEventListener('pagehide', releaseAllButtons)
  window.addEventListener('beforeunload', releaseAllButtons)
  window.addEventListener('orientationchange', releaseAllButtons)
  window.addEventListener('robot-app-release-controls', releaseAllButtons)
  document.addEventListener('pointercancel', releaseAllButtons, true)
  document.addEventListener('visibilitychange', onVisibilityChange)
})
onUnmounted(() => {
  releaseAllButtons()
  for (const timer of sliderTimers.values()) clearTimeout(timer)
  sliderTimers.clear()
  window.removeEventListener('keydown', onKeyDown)
  window.removeEventListener('keyup', onKeyUp)
  window.removeEventListener('blur', releaseAllButtons)
  window.removeEventListener('pagehide', releaseAllButtons)
  window.removeEventListener('beforeunload', releaseAllButtons)
  window.removeEventListener('orientationchange', releaseAllButtons)
  window.removeEventListener('robot-app-release-controls', releaseAllButtons)
  document.removeEventListener('pointercancel', releaseAllButtons, true)
  document.removeEventListener('visibilitychange', onVisibilityChange)
})
</script>

<style scoped>
.control-bar {
  position: fixed;
  bottom: 0; left: 0; right: 0;
  z-index: 500;
  height: 90px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  padding: 6px 14px;
  background: var(--control-bg);
  backdrop-filter: blur(24px) saturate(180%);
  -webkit-backdrop-filter: blur(24px) saturate(180%);
  border-top: 1px solid var(--glass-border);
  box-shadow: 0 -4px 24px rgba(0,0,0,0.08);
}

.control-bar.embedded {
  position: relative;
  inset: auto;
  z-index: 1;
  width: 100%;
  height: auto;
  min-height: 90px;
  border: 1px solid var(--glass-border);
  border-radius: 12px;
  box-shadow: 0 8px 28px rgba(0,0,0,.12);
}

.dpad-grid {
  display: grid;
  grid-template-columns: 24px 24px 24px;
  grid-template-rows: 24px 24px 24px;
  gap: 1px;
  grid-template-areas: '. up .' 'left center right' '. down .';
}
.dpad-up { grid-area: up; }
.dpad-left { grid-area: left; }
.dpad-center { grid-area: center; background: transparent!important; border: none!important; cursor: default; }
.dpad-right { grid-area: right; }
.dpad-down { grid-area: down; }
.dpad-btn {
  width: 24px; height: 24px;
  font-size: 10px;
  border-radius: 6px;
}
.dpad-btn:disabled {
  cursor: not-allowed;
  opacity: .38;
  color: var(--text-tertiary);
  border-color: var(--border-color);
  transform: none;
}
.dpad-btn:disabled:hover,
.dpad-btn:disabled:active {
  background: rgba(0,0,0,.06);
  border-color: var(--border-color);
  color: var(--text-tertiary);
  transform: none;
}
.range-control {
  display: flex;
  flex-direction: column;
  align-items: stretch;
  gap: 4px;
  width: 94px;
  min-width: 0;
  color: var(--text-secondary);
  font-size: 11px;
  line-height: 1;
}
.range-caption {
  display: grid;
  grid-template-columns: minmax(0, 1fr) max-content;
  align-items: center;
  gap: 4px;
  width: 100%;
  white-space: nowrap;
}
.range-caption > span { min-width: 0; }
.range-control b { color: var(--text-primary); font-weight: 650; }
.range-control input { accent-color: #34c759; cursor: pointer; }
.range-control input:disabled { cursor: not-allowed; opacity: .42; }
.range-control input {
  width: 100%;
  min-width: 0;
  max-width: 100%;
  height: 18px;
  margin: 0;
}
.control-adjustments {
  display: grid;
  grid-template-columns: repeat(2, 94px);
  align-items: center;
  gap: 6px;
}
.switch-list {
  display: grid;
  grid-template-columns: repeat(2, 82px);
  align-items: center;
  gap: 6px;
}
.switch-setting {
  width: 82px;
  min-width: 0;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 4px;
  color: var(--text-secondary);
  font-size: 9px;
  line-height: 1;
}
.switch-label { flex: 0 0 auto; white-space: nowrap; }
.robot-switch {
  position: relative;
  width: 32px;
  min-width: 32px;
  height: 18px;
  flex: 0 0 32px;
  padding: 0;
  border: 0;
  border-radius: 9px;
  background: #d1d1d6;
  cursor: pointer;
  transition: background .2s ease;
}
.robot-switch i {
  position: absolute;
  top: 2px;
  left: 2px;
  width: 14px;
  height: 14px;
  border-radius: 50%;
  background: #fff;
  box-shadow: 0 1px 3px rgba(0,0,0,.25);
  transition: transform .2s ease;
}
.robot-switch.enabled { background: #34c759; }
.robot-switch.enabled i { transform: translateX(14px); }
.robot-switch:disabled { cursor: not-allowed; opacity: .46; }
.switch-setting.pending, .range-control.pending { opacity: .66; }
.dark .robot-switch { background: #48484a; }
.dark .robot-switch.enabled { background: #34c759; }
.control-device-state {
  max-width: 80px;
  line-height: 1.25;
  text-align: center;
  color: var(--text-tertiary);
}
.center-controls { width: 80px; min-width: 0; flex: 0 0 80px; margin: 0 !important; }
.control-status-dot {
  width: 6px;
  height: 6px;
  flex: 0 0 6px;
  border-radius: 50%;
  background: #ff3b30;
}
.control-device-state.is-ready { color: #248a3d; }
.control-device-state.is-ready .control-status-dot {
  background: #34c759;
  box-shadow: 0 0 0 3px rgba(52,199,89,.14);
}
.control-device-state.is-offline { color: #9a6700; }
.control-device-state.is-offline .control-status-dot { background: #ff9f0a; }
.control-device-state.is-disconnected { color: #c9342d; }
.dark .control-device-state.is-ready { color: #57d474; }
.dark .control-device-state.is-offline { color: #ffb340; }
.dark .control-device-state.is-disconnected { color: #ff6961; }

.control-bar.mobile {
  min-height: 188px;
  display: grid;
  overflow: hidden;
  grid-template-columns: 136px minmax(0, 1fr);
  grid-template-areas:
    'dpad adjustments'
    'dpad switches'
    'status status';
  align-items: center;
  justify-content: stretch;
  column-gap: 10px;
  row-gap: 7px;
  padding: 12px 10px calc(12px + env(safe-area-inset-bottom));
  touch-action: none;
}
.control-bar.mobile .dpad-grid {
  grid-area: dpad;
  grid-template-columns: repeat(3, 44px);
  grid-template-rows: repeat(3, 44px);
  gap: 2px;
}
.control-bar.mobile .dpad-btn {
  width: 44px;
  height: 44px;
  font-size: 16px;
  border-radius: 10px;
}
.control-bar.mobile .center-controls {
  grid-area: status;
  align-self: center;
  justify-content: center;
  justify-self: stretch;
  width: 100%;
  min-width: 0;
  margin: 0;
}
.control-bar.mobile .control-adjustments {
  grid-area: adjustments;
  min-width: 0;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 7px;
}
.control-bar.mobile .range-control { width: auto; font-size: 11px; }
.control-bar.mobile .range-control input { height: 20px; }
.control-bar.mobile .switch-list {
  grid-area: switches;
  grid-template-columns: minmax(0, 1fr);
  grid-template-rows: repeat(2, 20px);
  width: 100%;
  min-width: 0;
  align-content: center;
  gap: 3px;
}
.control-bar.mobile .switch-setting {
  width: 100%;
  min-width: 0;
  display: grid;
  grid-template-columns: max-content 32px;
  justify-content: center;
  column-gap: 8px;
  padding: 0;
  font-size: 10px;
}
.control-bar.mobile .control-device-state {
  max-width: none;
  margin-top: 0;
  line-height: 1.25;
  text-align: center;
}
@media (max-width: 600px) {
  .control-bar {
    gap: 3px;
    padding: 4px 6px !important;
  }
  .control-bar.mobile {
    grid-template-columns: 130px minmax(0, 1fr);
    gap: 5px;
    padding: 10px 5px calc(10px + env(safe-area-inset-bottom)) !important;
  }
  .control-bar.mobile .dpad-grid {
    grid-template-columns: repeat(3, 42px);
    grid-template-rows: repeat(3, 42px);
  }
  .control-bar.mobile .dpad-btn { width: 42px; height: 42px; }
  .control-bar.mobile .switch-list { grid-template-columns: minmax(0, 1fr); gap: 3px; }
  .control-bar.mobile .switch-setting { width: 100%; column-gap: 8px; font-size: 10px; }
}
</style>
