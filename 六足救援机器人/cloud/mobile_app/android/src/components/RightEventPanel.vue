<template>
  <aside class="event-panel" :class="{ 'event-panel--embedded': embedded, 'event-panel--compact': compact, 'event-panel--readonly': readonly }" :style="panelStyle">
    <header class="event-header">
      <div class="flex items-center gap-1.5 min-w-0">
        <el-icon class="text-blue-500"><Bell /></el-icon>
        <span class="text-xs font-semibold" style="color: var(--text-primary)">事件记录</span>
        <span class="event-count">{{ events.length }}</span>
      </div>
      <el-tooltip v-if="!readonly" content="清空事件" placement="left">
        <button
          class="icon-button"
          type="button"
          :disabled="events.length === 0"
          aria-label="清空事件"
          @click="confirmClear"
        >
          <el-icon><Delete /></el-icon>
        </button>
      </el-tooltip>
    </header>

    <div v-if="eventsToDisplay.length === 0" class="event-empty">
      <el-icon :size="24"><Bell /></el-icon>
      <span>暂无事件</span>
    </div>

    <div v-else class="event-list">
      <article
        v-for="event in eventsToDisplay"
        :key="event.id"
        class="event-item"
        :class="[event.type === 'location' ? 'event-position' : `event-${event.type}`, { 'event-failed': operationEventTypes.includes(event.type) && !event.success }]"
      >
        <div class="event-title-row">
          <div class="flex items-center gap-1.5 min-w-0">
            <el-icon :class="eventIconClass(event)">
              <UserFilled v-if="event.type === 'survivor'" />
              <VideoCamera v-else-if="event.type === 'video'" />
              <LocationFilled v-else-if="event.type === 'location'" />
              <Connection v-else-if="['modem', 'wifi', 'robot_control'].includes(event.type)" />
              <WarningFilled v-else />
            </el-icon>
            <strong>{{ eventTitle(event) }}</strong>
          </div>
          <time :datetime="new Date(event.timestamp).toISOString()">{{ formatTime(event.timestamp) }}</time>
        </div>

        <p v-if="event.type === 'gas'" class="event-message">
          当前可燃气体阈值超限，可能有危险
        </p>

        <p v-else-if="event.type === 'location'" class="event-message">
          {{ event.message || '机器人定时位置记录' }}
        </p>

        <template v-else-if="operationEventTypes.includes(event.type)">
          <p class="event-operation">{{ event.operation }}</p>
          <p class="event-message">{{ event.message }}</p>
        </template>

        <p v-if="!operationEventTypes.includes(event.type) || event.location" class="event-location">
          <el-icon><LocationFilled /></el-icon>
          <span>{{ formatLocation(event.location) }}</span>
        </p>

        <button
          v-if="event.type === 'survivor' && event.snapshotStatus === 'ready' && !readonly"
          class="snapshot-button"
          type="button"
          aria-label="查看幸存者截图大图"
          @click="selectedEvent = event"
        >
          <img :src="event.snapshotUrl" alt="疑似幸存者事件截图" />
          <span class="snapshot-zoom"><el-icon><ZoomIn /></el-icon></span>
        </button>
        <div v-else-if="event.type === 'survivor' && event.snapshotStatus === 'ready' && readonly" class="snapshot-button snapshot-button--readonly" aria-hidden="true">
          <img :src="event.snapshotUrl" alt="疑似幸存者事件截图" />
        </div>
        <div v-else-if="event.type === 'survivor'" class="snapshot-placeholder">
          {{ event.snapshotStatus === 'loading' ? '正在保存当前视频帧…' : '当前视频帧不可用' }}
        </div>
      </article>
    </div>
  </aside>

  <Teleport to="body">
    <div v-if="!readonly && selectedEvent?.snapshotUrl" class="image-overlay" @click="selectedEvent = null">
      <div class="image-dialog" role="dialog" aria-modal="true" aria-label="幸存者事件截图" @click.stop>
        <button class="image-close" type="button" aria-label="关闭大图" @click="selectedEvent = null">
          <el-icon><Close /></el-icon>
        </button>
        <img :src="selectedEvent.snapshotUrl" alt="疑似幸存者事件截图大图" />
        <div class="image-caption">
          <span>{{ formatTime(selectedEvent.timestamp) }}</span>
          <span>{{ formatLocation(selectedEvent.location) }}</span>
        </div>
      </div>
    </div>
  </Teleport>
</template>

<script setup>
import { computed, onMounted, onUnmounted, ref } from 'vue'
import { ElMessageBox } from 'element-plus'
import 'element-plus/theme-chalk/base.css'
import 'element-plus/theme-chalk/el-overlay.css'
import 'element-plus/theme-chalk/el-button.css'
import 'element-plus/theme-chalk/el-icon.css'
import 'element-plus/theme-chalk/el-message-box.css'
import {
  Bell,
  Close,
  Connection,
  Delete,
  LocationFilled,
  UserFilled,
  VideoCamera,
  WarningFilled,
  ZoomIn,
} from '@element-plus/icons-vue'
import { useEventStore } from '@/composables/useEventStore'

const props = defineProps({
  width: { type: Number, default: 280 },
  embedded: { type: Boolean, default: false },
  compact: { type: Boolean, default: false },
  readonly: { type: Boolean, default: false },
  maxItems: { type: Number, default: 0 },
})

const { events, clearEvents } = useEventStore()
const operationEventTypes = ['modem', 'wifi', 'video', 'robot_control']
const selectedEvent = ref(null)
const eventsToDisplay = computed(() => {
  if (!props.maxItems || props.maxItems < 1) return events.value
  return events.value.slice(0, props.maxItems)
})

const panelStyle = computed(() => (props.embedded ? {
  position: 'relative',
  inset: 'auto',
  width: '100%',
  height: '100%',
} : { width: `${props.width}px` }))

const timeFormatter = new Intl.DateTimeFormat('zh-CN', {
  year: 'numeric',
  month: '2-digit',
  day: '2-digit',
  hour: '2-digit',
  minute: '2-digit',
  second: '2-digit',
  hour12: false,
})

function eventTitle(event) {
  if (event.type === 'survivor') return '发现疑似幸存者'
  if (event.type === 'gas') return '可燃气体超限'
  if (event.type === 'location') return '机器人位置记录'
  if (event.type === 'wifi') return event.success ? 'Wi-Fi 操作已生效' : 'Wi-Fi 操作未生效'
  if (event.type === 'video') return event.success ? '视频流设置已生效' : '视频流设置未生效'
  if (event.type === 'robot_control') return event.message || '机器人模式已更新'
  return event.success ? '4G 操作已生效' : '4G 操作未生效'
}

function eventIconClass(event) {
  if (event.type === 'survivor') return 'text-blue-500'
  if (event.type === 'location') return 'text-green-500'
  if (operationEventTypes.includes(event.type) && event.success) return 'text-green-500'
  return 'text-red-500'
}

function formatTime(timestamp) {
  return Number.isFinite(timestamp) ? timeFormatter.format(new Date(timestamp)) : '时间 NA'
}

function formatLocation(location) {
  if (!location || !Number.isFinite(location.lat) || !Number.isFinite(location.lng)) {
    return '经纬度：NA'
  }
  return `经度 ${location.lng.toFixed(6)} · 纬度 ${location.lat.toFixed(6)}`
}

async function confirmClear() {
  if (!events.value.length) return
  try {
    await ElMessageBox.confirm(
      '清空后无法恢复，确认清空全部事件记录？',
      '清空事件',
      {
        confirmButtonText: '清空',
        cancelButtonText: '取消',
        type: 'warning',
        confirmButtonClass: 'el-button--danger',
        modalClass: 'event-clear-confirm-overlay',
      },
    )
    selectedEvent.value = null
    await clearEvents()
  } catch {
    // Cancel keeps the journal unchanged.
  }
}

function onKeydown(event) {
  if (event.key === 'Escape') selectedEvent.value = null
}

onMounted(() => window.addEventListener('keydown', onKeydown))
onUnmounted(() => window.removeEventListener('keydown', onKeydown))
</script>

<style scoped>
.event-panel {
  position: fixed;
  top: 0;
  right: 0;
  z-index: 300;
  height: calc(100vh - 90px);
  display: flex;
  flex-direction: column;
  overflow: hidden;
  background: var(--glass-bg);
  backdrop-filter: blur(20px) saturate(180%);
  -webkit-backdrop-filter: blur(20px) saturate(180%);
  border-left: 1px solid var(--glass-border);
}
.event-panel--embedded {
  z-index: 1;
  min-height: 0;
  border: 1px solid var(--glass-border);
  border-radius: 12px;
  box-shadow: 0 8px 28px rgba(0,0,0,.12);
}
.event-panel--embedded .event-header { min-height: 54px; }
.event-panel--embedded .icon-button {
  width: 44px;
  height: 44px;
  flex-basis: 44px;
}
.event-panel--compact .event-header { min-height: 34px; padding: 6px 8px; }
.event-panel--compact .event-header .el-icon { font-size: 12px; }
.event-panel--compact .event-header .text-xs { font-size: 10px; }
.event-panel--compact .event-count { min-width: 16px; height: 15px; padding: 0 4px; font-size: 8px; }
.event-panel--compact .event-list { overflow: hidden; }
.event-panel--compact .event-item { padding: 7px 8px; }
.event-panel--compact .event-title-row { gap: 3px; font-size: 9px; }
.event-panel--compact .event-title-row time { max-width: 70px; font-size: 8px; }
.event-panel--compact .event-message,
.event-panel--compact .event-operation,
.event-panel--compact .event-location { margin-top: 3px; font-size: 8px; line-height: 1.35; }
.event-panel--readonly .snapshot-button { cursor: default; }
.snapshot-button--readonly { pointer-events: none; }
.event-header {
  min-height: 47px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 8px;
  padding: 8px 10px;
  border-bottom: 1px solid var(--border-color);
}
.event-count {
  min-width: 20px;
  height: 18px;
  padding: 0 6px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border-radius: 9px;
  font-size: 10px;
  color: var(--text-secondary);
  background: rgba(0, 0, 0, 0.06);
}
.dark .event-count { background: rgba(255, 255, 255, 0.08); }
.icon-button {
  width: 30px;
  height: 30px;
  flex: 0 0 30px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border: 0;
  border-radius: 6px;
  color: var(--text-secondary);
  background: transparent;
  cursor: pointer;
}
.icon-button:hover:not(:disabled) { color: #ff3b30; background: rgba(255, 59, 48, 0.1); }
.icon-button:disabled { opacity: 0.35; cursor: default; }
.event-empty {
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 8px;
  font-size: 11px;
  color: var(--text-tertiary);
}
.event-list {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
}
.event-item {
  position: relative;
  padding: 10px;
  border-bottom: 1px solid var(--border-color);
}
.event-item::before {
  content: '';
  position: absolute;
  top: 10px;
  bottom: 10px;
  left: 0;
  width: 3px;
}
.event-survivor::before { background: #007aff; }
.event-gas::before { background: #ff3b30; }
.event-modem::before { background: #34c759; }
.event-wifi::before { background: #34c759; }
.event-video::before { background: #34c759; }
.event-robot_control::before { background: #34c759; }
.event-position::before { background: #007aff; }
.event-modem.event-failed::before,
.event-wifi.event-failed::before,
.event-video.event-failed::before { background: #ff3b30; }
.event-robot_control.event-failed::before { background: #ff3b30; }
.event-title-row {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 8px;
  font-size: 11px;
  color: var(--text-primary);
}
.event-title-row strong { line-height: 1.4; }
.event-title-row time {
  flex: 0 0 auto;
  max-width: 104px;
  font-size: 9px;
  line-height: 1.35;
  text-align: right;
  color: var(--text-tertiary);
}
.event-message,
.event-operation,
.event-location {
  margin-top: 5px;
  font-size: 10px;
  line-height: 1.45;
  color: var(--text-secondary);
}
.event-operation {
  font-weight: 600;
  color: var(--text-primary);
}
.event-location {
  display: flex;
  align-items: flex-start;
  gap: 4px;
}
.event-location .el-icon { flex: 0 0 auto; margin-top: 1px; }
.snapshot-button {
  position: relative;
  width: 100%;
  aspect-ratio: 4 / 3;
  margin-top: 8px;
  overflow: hidden;
  border: 1px solid var(--border-color);
  border-radius: 6px;
  background: #000;
  cursor: zoom-in;
}
.snapshot-button img {
  width: 100%;
  height: 100%;
  display: block;
  object-fit: contain;
}
.snapshot-zoom {
  position: absolute;
  right: 6px;
  bottom: 6px;
  width: 26px;
  height: 26px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border-radius: 50%;
  color: #fff;
  background: rgba(0, 0, 0, 0.58);
}
.snapshot-placeholder {
  width: 100%;
  aspect-ratio: 4 / 3;
  margin-top: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  border: 1px dashed var(--border-color);
  border-radius: 6px;
  font-size: 10px;
  color: var(--text-tertiary);
  background: rgba(0, 0, 0, 0.03);
}
.image-overlay {
  position: fixed;
  inset: 0;
  z-index: 10030;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 36px;
  background: rgba(0, 0, 0, 0.72);
  backdrop-filter: blur(4px);
}
:global(.event-clear-confirm-overlay) {
  z-index: 10030 !important;
}
.image-dialog {
  position: relative;
  max-width: min(1000px, 86vw);
  max-height: 86vh;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  border: 1px solid rgba(255, 255, 255, 0.18);
  border-radius: 8px;
  background: #101010;
  box-shadow: 0 20px 70px rgba(0, 0, 0, 0.45);
}
.image-dialog img {
  max-width: 100%;
  max-height: calc(86vh - 44px);
  display: block;
  object-fit: contain;
}
.image-close {
  position: absolute;
  top: 8px;
  right: 8px;
  z-index: 1;
  width: 32px;
  height: 32px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  border: 0;
  border-radius: 50%;
  color: #fff;
  background: rgba(0, 0, 0, 0.62);
  cursor: pointer;
}
.image-caption {
  min-height: 44px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  padding: 8px 12px;
  font-size: 10px;
  color: rgba(255, 255, 255, 0.72);
}
@media (max-width: 700px) {
  .event-title-row {
    flex-direction: column;
    gap: 2px;
  }
  .event-title-row time {
    max-width: 100%;
    text-align: left;
  }
  .event-item { padding: 8px; }
  .event-panel--embedded .event-item { padding: 12px; }
  .event-panel--embedded .snapshot-button { max-height: 56dvh; }
  .image-overlay { padding: max(12px, env(safe-area-inset-top)) 10px max(12px, env(safe-area-inset-bottom)); }
  .image-dialog { max-width: 100%; max-height: 94dvh; }
  .image-dialog img { max-height: calc(94dvh - 70px); }
  .image-close { width: 44px; height: 44px; }
  .image-caption {
    align-items: flex-start;
    flex-direction: column;
    gap: 2px;
  }
}
</style>
