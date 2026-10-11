<template>
  <MacWindow
    :visible="visible"
    :title="'远程监控'"
    :x="x" :y="y" :w="w" :h="h"
    :minW="360" :minH="280"
    :dragging="dragState.dragging"
    :draggable="draggable"
    :resizable="resizable"
    :embedded="embedded"
    :show-header="showTitle"
    @close="$emit('close')"
    @minimize="$emit('minimize')"
    @dragStart="handleDragStart"
    @resizeStart="(e, edge) => resizeHandlers['on' + edge.toUpperCase()]?.(e)"
  >
    <div class="flex flex-col gap-2 h-full min-h-0">
      <div class="video-surface flex-1 min-h-0 rounded-lg overflow-hidden relative">
        <img
          v-if="mjpegUrl"
          :key="streamKey"
          :src="mjpegUrl"
          class="video-frame absolute inset-0 w-full h-full object-contain"
          alt="蜘蛛001实时视频画面"
          @load="handleMjpegLoaded"
          @error="handleMjpegError"
        />

        <div class="stream-status-badge" :class="streamStatusTone" role="status" aria-live="polite">
          <span aria-hidden="true"></span>
          {{ streamStatusText }}
        </div>

        <div v-if="showBlockingOverlay" class="stream-overlay">
          <div v-if="streamState === 'loading' && deviceOnline" class="stream-spinner" aria-hidden="true"></div>
          <div v-else class="stream-state-icon" aria-hidden="true">{{ streamStateIcon }}</div>
          <strong>{{ streamStatusText }}</strong>
          <p>{{ streamStatusDetail }}</p>
          <button
            v-if="canRetry"
            type="button"
            class="stream-retry"
            :disabled="streamState === 'loading'"
            @click="retryMjpeg"
          >
            {{ streamState === 'loading' ? '正在连接…' : '重新连接视频' }}
          </button>
        </div>
      </div>
    </div>
  </MacWindow>
</template>

<script setup>
import { computed, onBeforeUnmount, ref, watch } from 'vue'
import MacWindow from './MacWindow.vue'
import cfg from '@/config/config'
import { getRealtimeTicket, withRealtimeTicket } from '@/api/auth'
import { useDraggable } from '@/composables/useDraggable'
import { useResizable } from '@/composables/useResizable'
import { useSensorStore } from '@/composables/useSensorStore'

const props = defineProps({
  visible: { type: Boolean, default: true },
  initialX: { type: Number, default: 280 },
  initialY: { type: Number, default: 10 },
  initialW: { type: Number, default: 700 },
  initialH: { type: Number, default: 500 },
  draggable: { type: Boolean, default: true },
  resizable: { type: Boolean, default: true },
  embedded: { type: Boolean, default: false },
  showTitle: { type: Boolean, default: true },
})

const emit = defineEmits(['close', 'minimize', 'drag-end', 'resize-end'])

function onDragEnd(pos) { emit('drag-end', pos) }
function onResizeEnd(s) { emit('resize-end', s) }

const { state: dragState, x, y, onPointerDown, setPosition } = useDraggable(props.initialX, props.initialY, { onDragEnd })
function handleDragStart(e) { if (props.draggable) onPointerDown(e) }
const { size, w, h, resizeHandlers, setSize } = useResizable(360, 280, props.initialW, props.initialH, onResizeEnd, dragState)

defineExpose({ setPosition, setSize, x, y, w, h })

const { online: deviceOnline } = useSensorStore()
const mjpegUrl = ref('')
const streamKey = ref(0)
const streamState = ref('idle')
const streamErrorMessage = ref('')
const lastConnectedAt = ref(null)
let loadGeneration = 0
let retryTimer = null

const streamStatusText = computed(() => {
  if (!cfg.MJPEG_URL) return '视频地址未配置'
  if (!deviceOnline.value) return '机器人未连接'
  if (streamState.value === 'live') return '实时画面'
  if (streamState.value === 'error') return '视频流已中断'
  return '正在连接视频'
})

const streamStatusDetail = computed(() => {
  if (!cfg.MJPEG_URL) return '请先配置云端 MJPEG 视频地址。'
  if (!deviceOnline.value) {
    return lastConnectedAt.value
      ? `等待机器人恢复连接，上次画面时间 ${lastConnectedAt.value}。`
      : '等待机器人恢复连接后再重试视频。'
  }
  if (streamState.value === 'error') {
    return streamErrorMessage.value || '未收到视频画面，请检查机器人上行链路。'
  }
  return '正在建立安全视频连接，请稍候。'
})

const streamStatusTone = computed(() => {
  if (deviceOnline.value && streamState.value === 'live') return 'is-live'
  if (!deviceOnline.value) return 'is-offline'
  if (streamState.value === 'error' || !cfg.MJPEG_URL) return 'is-error'
  return 'is-loading'
})

const showBlockingOverlay = computed(() => (
  !deviceOnline.value || streamState.value !== 'live'
))
const canRetry = computed(() => Boolean(cfg.MJPEG_URL) && streamState.value !== 'loading')
const streamStateIcon = computed(() => {
  if (!cfg.MJPEG_URL) return '⚙'
  if (!deviceOnline.value) return '⌁'
  return '!'
})

function clearRetryTimer() {
  if (!retryTimer) return
  clearTimeout(retryTimer)
  retryTimer = null
}

function scheduleMjpegRetry(generation) {
  if (!props.visible || generation !== loadGeneration || retryTimer) return
  retryTimer = setTimeout(() => {
    retryTimer = null
    void loadMjpeg()
  }, 3000)
}

async function loadMjpeg() {
  clearRetryTimer()
  const generation = ++loadGeneration
  mjpegUrl.value = ''
  streamErrorMessage.value = ''
  if (!props.visible) {
    streamState.value = 'idle'
    return
  }
  if (!cfg.MJPEG_URL) {
    streamState.value = 'error'
    return
  }
  streamState.value = 'loading'
  try {
    const ticket = await getRealtimeTicket()
    if (!props.visible || generation !== loadGeneration) return
    streamKey.value += 1
    mjpegUrl.value = withRealtimeTicket(cfg.MJPEG_URL, ticket)
  } catch {
    if (!props.visible || generation !== loadGeneration) return
    streamState.value = 'error'
    streamErrorMessage.value = '无法获取视频连接凭据，请稍后重试。'
    scheduleMjpegRetry(generation)
  }
}

function retryMjpeg() {
  void loadMjpeg()
}

function handleMjpegLoaded() {
  clearRetryTimer()
  streamState.value = 'live'
  streamErrorMessage.value = ''
  lastConnectedAt.value = new Date().toLocaleTimeString('zh-CN', {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit',
  })
}

function handleMjpegError() {
  const generation = ++loadGeneration
  mjpegUrl.value = ''
  streamState.value = 'error'
  streamErrorMessage.value = '视频连接中断，系统将在 3 秒后自动重试。'
  scheduleMjpegRetry(generation)
}

watch(() => props.visible, (visible) => {
  if (visible) {
    void loadMjpeg()
  } else {
    loadGeneration += 1
    clearRetryTimer()
    mjpegUrl.value = ''
    streamState.value = 'idle'
  }
}, { immediate: true })

watch(deviceOnline, (online, wasOnline) => {
  if (online && wasOnline === false && props.visible && streamState.value !== 'live') {
    void loadMjpeg()
  }
})

onBeforeUnmount(() => {
  loadGeneration += 1
  clearRetryTimer()
  mjpegUrl.value = ''
  streamState.value = 'idle'
})
</script>

<style scoped>
.video-surface {
  background: #000;
  isolation: isolate;
}
.video-frame {
  z-index: 1;
  background: #000;
}
.stream-status-badge {
  position: absolute;
  z-index: 3;
  top: 10px;
  left: 10px;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  min-height: 28px;
  padding: 5px 9px;
  border: 1px solid rgba(255,255,255,.18);
  border-radius: 999px;
  color: #fff;
  background: rgba(16,16,18,.72);
  box-shadow: 0 3px 12px rgba(0,0,0,.24);
  backdrop-filter: blur(10px);
  -webkit-backdrop-filter: blur(10px);
  font-size: 12px;
  line-height: 1;
}
.stream-status-badge > span {
  width: 7px;
  height: 7px;
  flex: 0 0 7px;
  border-radius: 50%;
  background: #ff9f0a;
}
.stream-status-badge.is-live > span {
  background: #34c759;
  box-shadow: 0 0 0 3px rgba(52,199,89,.18);
}
.stream-status-badge.is-error > span,
.stream-status-badge.is-offline > span {
  background: #ff453a;
}
.stream-overlay {
  position: absolute;
  z-index: 2;
  inset: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 9px;
  padding: 24px;
  color: #f5f5f7;
  text-align: center;
  background: radial-gradient(circle at center, rgba(40,43,49,.88), rgba(7,8,10,.96));
}
.stream-overlay strong {
  font-size: 17px;
  line-height: 1.25;
}
.stream-overlay p {
  max-width: 340px;
  margin: 0;
  color: rgba(245,245,247,.68);
  font-size: 12px;
  line-height: 1.5;
}
.stream-state-icon {
  display: grid;
  width: 38px;
  height: 38px;
  place-items: center;
  border: 1px solid rgba(255,255,255,.16);
  border-radius: 50%;
  color: #ffb340;
  background: rgba(255,255,255,.06);
  font-size: 20px;
}
.stream-spinner {
  width: 34px;
  height: 34px;
  border: 3px solid rgba(255,255,255,.18);
  border-top-color: #0a84ff;
  border-radius: 50%;
  animation: stream-spin .85s linear infinite;
}
.stream-retry {
  min-width: 132px;
  min-height: 40px;
  margin-top: 3px;
  padding: 8px 16px;
  border: 1px solid rgba(255,255,255,.16);
  border-radius: 999px;
  color: #fff;
  background: #0a84ff;
  font-size: 13px;
  font-weight: 650;
  cursor: pointer;
}
.stream-retry:hover { background: #1677dd; }
.stream-retry:focus-visible {
  outline: 3px solid rgba(100,181,255,.62);
  outline-offset: 2px;
}
.stream-retry:disabled {
  opacity: .55;
  cursor: wait;
}
@keyframes stream-spin {
  to { transform: rotate(360deg); }
}
@media (prefers-reduced-motion: reduce) {
  .stream-spinner { animation-duration: 1.8s; }
}
@media (max-width: 600px) {
  .stream-status-badge { top: 8px; left: 8px; }
  .stream-overlay { padding: 18px; }
  .stream-retry { min-height: 44px; }
}
</style>
