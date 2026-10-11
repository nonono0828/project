<template>
  <div class="control-bar" :class="{ embedded, mobile }" :style="barStyle" aria-label="机器人方向控制器">
    <div class="dpad-grid">
      <button
        v-for="btn in buttons.dpad"
        :key="btn.key"
        class="ps2-btn dpad-btn"
        :class="[btn.class, { active: activeKeys.has(btn.key) }]"
        :aria-label="btn.button || '方向键中心'"
        @pointerdown.prevent="onBtnPress(btn)"
        @pointerup.prevent="onBtnRelease(btn.key)"
        @pointerleave.prevent="onBtnRelease(btn.key)"
        @pointercancel.prevent="onBtnRelease(btn.key)"
      >
        {{ btn.label }}
      </button>
      <span class="dpad-center" aria-hidden="true"></span>
    </div>

    <div class="center-controls flex flex-col items-center gap-0.5 mx-1">
      <div class="control-device-state flex items-center gap-1 text-[9px]" style="color: var(--text-tertiary)">
        <span class="w-1.5 h-1.5 rounded-full" :class="deviceOnline ? 'bg-green-400' : 'bg-red-400'"></span>
        {{ deviceOnline ? '机器人已连接' : '机器人未连接' }}
      </div>
    </div>
  </div>
</template>

<script setup>
import { reactive, computed, onMounted, onUnmounted, watch } from 'vue'
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

const { sendCommand, connected: wsConnected } = useControlChannel()
const { online: deviceOnline } = useSensorStore()

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
  if (!btn.button || activeKeys.has(btn.key)) return

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

watch(wsConnected, (connected) => {
  if (!connected) releaseAllButtons()
})

onMounted(() => {
  window.addEventListener('keydown', onKeyDown)
  window.addEventListener('keyup', onKeyUp)
  window.addEventListener('blur', releaseAllButtons)
  window.addEventListener('pagehide', releaseAllButtons)
  window.addEventListener('beforeunload', releaseAllButtons)
  window.addEventListener('orientationchange', releaseAllButtons)
  document.addEventListener('pointercancel', releaseAllButtons, true)
  document.addEventListener('visibilitychange', onVisibilityChange)
})
onUnmounted(() => {
  releaseAllButtons()
  window.removeEventListener('keydown', onKeyDown)
  window.removeEventListener('keyup', onKeyUp)
  window.removeEventListener('blur', releaseAllButtons)
  window.removeEventListener('pagehide', releaseAllButtons)
  window.removeEventListener('beforeunload', releaseAllButtons)
  window.removeEventListener('orientationchange', releaseAllButtons)
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

.control-bar.mobile {
  min-height: 164px;
  justify-content: space-around;
  gap: 8px;
  padding: 12px 10px calc(12px + env(safe-area-inset-bottom));
  touch-action: none;
}
.control-bar.mobile .dpad-grid {
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
  align-self: stretch;
  justify-content: center;
  min-width: 66px;
  margin: 0;
}
.control-bar.mobile .control-device-state {
  max-width: 74px;
  margin-top: 5px;
  line-height: 1.25;
  text-align: center;
}
@media (max-width: 600px) {
  .control-bar {
    gap: 3px;
    padding: 4px 6px !important;
  }
  .control-bar.mobile {
    gap: 5px;
    padding: 10px 5px calc(10px + env(safe-area-inset-bottom)) !important;
  }
}
</style>
