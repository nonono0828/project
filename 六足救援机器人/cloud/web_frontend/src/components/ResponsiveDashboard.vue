<template>
  <div class="responsive-dashboard">
    <header class="responsive-header">
      <button v-if="!isPhone" class="header-action" type="button" aria-label="打开导航" @click="navDrawerOpen = true">☰</button>
      <div class="brand-block">
        <img src="/retouch_2026060418314442.png" alt="" />
        <div>
          <strong>机器人控制</strong>
          <span>{{ activeLabel }}</span>
        </div>
      </div>
      <div class="device-state" :class="deviceOnline ? 'is-online' : 'is-offline'">
        <i></i>
        <span>{{ deviceOnline ? '机器人已连接' : '机器人未连接' }}</span>
      </div>
      <button v-if="!isPhone" class="header-action" type="button" aria-label="打开事件记录" @click="eventDrawerOpen = true">🔔</button>
    </header>

    <main class="responsive-main">
      <section v-if="activeTab === 'control'" class="responsive-pane control-pane" aria-label="操控">
        <div class="video-shell">
          <VideoWindow embedded :visible="true" :draggable="false" :resizable="false" :show-title="false" />
        </div>
        <ControlPanel ref="controlRef" embedded mobile />
      </section>

      <section v-show="activeTab === 'location'" class="responsive-pane location-pane" aria-label="位置">
        <MapWindow ref="mapRef" embedded :visible="true" :draggable="false" :resizable="false" />
      </section>

      <section v-show="activeTab === 'data'" class="responsive-pane data-pane" aria-label="实时数据">
        <div class="mobile-status-grid">
          <div class="mobile-status-card">
            <span>设备状态</span>
            <strong>{{ deviceOnline ? '在线' : '离线' }}</strong>
          </div>
          <div class="mobile-status-card">
            <span>可燃气体</span>
            <strong :class="gasAlarm ? 'alarm-text' : ''">{{ gasStateText }}</strong>
          </div>
        </div>
        <div class="sensor-shell">
          <SensorWindow embedded :visible="true" :draggable="false" :resizable="false" />
        </div>
      </section>

      <section v-if="isPhone" v-show="activeTab === 'events'" class="responsive-pane events-pane" aria-label="事件记录">
        <RightEventPanel embedded />
      </section>

      <section v-show="activeTab === 'settings'" class="responsive-pane settings-pane" aria-label="设置">
        <LeftPanel
          embedded
          settings-only
          :is-dark="isDark"
          @toggle-theme="$emit('toggleTheme')"
        />
      </section>
    </main>

    <nav v-if="isPhone" class="bottom-navigation" aria-label="主导航">
      <button
        v-for="item in navigation"
        :key="item.key"
        type="button"
        :class="{ active: activeTab === item.key }"
        :aria-current="activeTab === item.key ? 'page' : undefined"
        @click="selectTab(item.key)"
      >
        <span aria-hidden="true">{{ item.icon }}</span>
        <small>{{ item.label }}</small>
      </button>
    </nav>

    <Teleport to="body">
      <div v-if="!isPhone && navDrawerOpen" class="tablet-drawer-overlay" @click.self="navDrawerOpen = false">
        <aside class="tablet-nav-drawer">
          <header>
            <strong>功能导航</strong>
            <button type="button" aria-label="关闭导航" @click="navDrawerOpen = false">×</button>
          </header>
          <button
            v-for="item in navigation"
            :key="item.key"
            type="button"
            :class="{ active: activeTab === item.key && item.key !== 'events' }"
            @click="selectTabletItem(item.key)"
          >
            <span>{{ item.icon }}</span>{{ item.label }}
          </button>
        </aside>
      </div>

      <div v-if="!isPhone && eventDrawerOpen" class="tablet-event-overlay" @click.self="eventDrawerOpen = false">
        <aside class="tablet-event-drawer">
          <button class="drawer-close" type="button" aria-label="关闭事件记录" @click="eventDrawerOpen = false">×</button>
          <RightEventPanel embedded />
        </aside>
      </div>
    </Teleport>
  </div>
</template>

<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import VideoWindow from '@/components/VideoWindow.vue'
import MapWindow from '@/components/MapWindow.vue'
import SensorWindow from '@/components/SensorWindow.vue'
import RightEventPanel from '@/components/RightEventPanel.vue'
import LeftPanel from '@/components/LeftPanel.vue'
import ControlPanel from '@/components/ControlPanel.vue'
import { useSensorStore } from '@/composables/useSensorStore'

defineEmits(['toggleTheme'])
defineProps({ isDark: { type: Boolean, default: false } })

const navigation = [
  { key: 'control', label: '操控', icon: '🎮' },
  { key: 'location', label: '位置', icon: '⌖' },
  { key: 'data', label: '数据', icon: '◫' },
  { key: 'events', label: '事件', icon: '🔔' },
  { key: 'settings', label: '设置', icon: '⚙' },
]

const activeTab = ref('control')
const navDrawerOpen = ref(false)
const eventDrawerOpen = ref(false)
const viewportWidth = ref(window.innerWidth)
const controlRef = ref(null)
const mapRef = ref(null)
const isPhone = computed(() => viewportWidth.value < 768)
const activeLabel = computed(() => navigation.find((item) => item.key === activeTab.value)?.label || '操控')
const { connected: wsConnected, online: deviceOnline, sensorData } = useSensorStore()

function normalizeGasAlarm(value) {
  const alarm = value && typeof value === 'object' ? value.alarm : value
  if (alarm === true || alarm === 1) return true
  if (alarm === false || alarm === 0) return false
  return null
}

const gasAlarmValue = computed(() => normalizeGasAlarm(sensorData.value?.gas))
const gasAlarm = computed(() => gasAlarmValue.value === true)
const gasStateText = computed(() => {
  if (!deviceOnline.value || gasAlarmValue.value === null) return 'NA'
  return gasAlarm.value ? '报警' : '正常'
})

function onResize() {
  viewportWidth.value = window.innerWidth
}

function selectTab(key) {
  activeTab.value = key
}

function selectTabletItem(key) {
  navDrawerOpen.value = false
  if (key === 'events') {
    eventDrawerOpen.value = true
    return
  }
  activeTab.value = key
}

watch(activeTab, async (tab, previous) => {
  if (previous === 'control' && tab !== 'control') controlRef.value?.releaseAllButtons?.()
  if (tab === 'location') {
    await nextTick()
    mapRef.value?.invalidateSize?.()
  }
})

watch(isPhone, async (phone) => {
  navDrawerOpen.value = false
  if (!phone && activeTab.value === 'events') {
    activeTab.value = 'control'
    eventDrawerOpen.value = true
  }
  if (activeTab.value === 'location') {
    await nextTick()
    mapRef.value?.invalidateSize?.()
  }
})

onMounted(() => window.addEventListener('resize', onResize))
onBeforeUnmount(() => {
  controlRef.value?.releaseAllButtons?.()
  window.removeEventListener('resize', onResize)
})
</script>

<style scoped>
.responsive-dashboard {
  width: 100%;
  height: 100vh;
  height: 100dvh;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  color: var(--text-primary);
  background: var(--bg-primary);
}
.responsive-header {
  position: relative;
  z-index: 20;
  min-height: calc(58px + env(safe-area-inset-top));
  display: flex;
  align-items: center;
  gap: 10px;
  padding: env(safe-area-inset-top) max(12px, env(safe-area-inset-right)) 0 max(12px, env(safe-area-inset-left));
  border-bottom: 1px solid var(--border-color);
  background: var(--glass-bg);
  backdrop-filter: blur(20px) saturate(180%);
  -webkit-backdrop-filter: blur(20px) saturate(180%);
}
.brand-block { min-width: 0; flex: 1; display: flex; align-items: center; gap: 8px; }
.brand-block img { width: 28px; height: 32px; object-fit: contain; }
.brand-block div { min-width: 0; display: flex; flex-direction: column; }
.brand-block strong { font-size: 14px; line-height: 1.2; }
.brand-block span { margin-top: 2px; font-size: 10px; color: var(--text-tertiary); }
.device-state { display: flex; align-items: center; gap: 5px; font-size: 10px; color: var(--text-secondary); }
.device-state i { width: 8px; height: 8px; border-radius: 50%; background: #ff3b30; }
.device-state.is-online i { background: #34c759; box-shadow: 0 0 7px rgba(52,199,89,.7); }
.header-action {
  width: 44px;
  height: 44px;
  flex: 0 0 44px;
  border: 0;
  border-radius: 10px;
  color: var(--text-primary);
  background: rgba(127,127,127,.10);
  font-size: 20px;
}
.responsive-main { flex: 1; min-height: 0; overflow: hidden; }
.responsive-pane {
  width: 100%;
  height: 100%;
  min-height: 0;
  padding: 10px;
}
.control-pane, .data-pane, .settings-pane { overflow-y: auto; overscroll-behavior: contain; }
.control-pane { display: grid; align-content: start; gap: 10px; }
.video-shell { width: 100%; aspect-ratio: 16 / 9; min-height: 220px; }
.video-shell :deep(.glass-window-body) { padding: 0; }
.location-pane { padding: 8px; }
.events-pane { padding: 8px; overflow: hidden; }
.settings-pane { max-width: 760px; margin: 0 auto; }
.mobile-status-grid { display: grid; grid-template-columns: repeat(2, minmax(0,1fr)); gap: 8px; margin-bottom: 8px; }
.mobile-status-card { min-height: 60px; display: flex; flex-direction: column; justify-content: center; padding: 10px 12px; border: 1px solid var(--border-color); border-radius: 10px; background: var(--glass-bg); }
.mobile-status-card span { font-size: 10px; color: var(--text-tertiary); }
.mobile-status-card strong { margin-top: 3px; font-size: 14px; }
.mobile-status-card .alarm-text { color: #ff3b30; }
.sensor-shell { height: max(480px, calc(100% - 68px)); }
.bottom-navigation {
  position: relative;
  z-index: 30;
  min-height: calc(62px + env(safe-area-inset-bottom));
  display: grid;
  grid-template-columns: repeat(5, 1fr);
  padding: 4px max(4px, env(safe-area-inset-right)) env(safe-area-inset-bottom) max(4px, env(safe-area-inset-left));
  border-top: 1px solid var(--border-color);
  background: var(--control-bg);
  backdrop-filter: blur(24px) saturate(180%);
  -webkit-backdrop-filter: blur(24px) saturate(180%);
}
.bottom-navigation button { min-width: 0; min-height: 54px; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 2px; border: 0; border-radius: 9px; color: var(--text-tertiary); background: transparent; }
.bottom-navigation button span { font-size: 19px; line-height: 1; }
.bottom-navigation button small { font-size: 10px; }
.bottom-navigation button.active { color: #007aff; background: rgba(0,122,255,.10); }

@media (max-width: 420px) {
  .device-state span { display: none; }
  .responsive-pane { padding: 7px; }
  .video-shell { min-height: 190px; }
}

@media (min-width: 768px) {
  .responsive-header { padding-left: 14px; padding-right: 14px; }
  .responsive-pane { padding: 14px; }
  .control-pane { grid-template-columns: minmax(0, 1fr) minmax(340px, .8fr); align-items: start; max-width: 1000px; margin: 0 auto; }
  .video-shell { min-height: 260px; }
  .data-pane, .location-pane { max-width: 1000px; margin: 0 auto; }
}
</style>

<style>
.tablet-drawer-overlay,
.tablet-event-overlay { position: fixed; inset: 0; z-index: 10010; background: rgba(0,0,0,.42); backdrop-filter: blur(3px); }
.tablet-nav-drawer { width: min(310px, 82vw); height: 100dvh; display: flex; flex-direction: column; gap: 6px; padding: max(14px, env(safe-area-inset-top)) 12px max(14px, env(safe-area-inset-bottom)); border-right: 1px solid var(--glass-border); background: var(--glass-bg); box-shadow: 20px 0 60px rgba(0,0,0,.24); }
.tablet-nav-drawer header { min-height: 50px; display: flex; align-items: center; justify-content: space-between; padding: 0 8px; }
.tablet-nav-drawer header button,
.tablet-nav-drawer > button { min-height: 48px; border: 0; border-radius: 10px; color: var(--text-primary); background: transparent; font-size: 14px; }
.tablet-nav-drawer header button { width: 48px; font-size: 26px; }
.tablet-nav-drawer > button { display: flex; align-items: center; gap: 12px; padding: 0 14px; text-align: left; }
.tablet-nav-drawer > button.active { color: #007aff; background: rgba(0,122,255,.12); }
.tablet-event-overlay { display: flex; justify-content: flex-end; }
.tablet-event-drawer { position: relative; width: min(400px, 90vw); height: 100dvh; padding: max(8px, env(safe-area-inset-top)) 8px max(8px, env(safe-area-inset-bottom)); background: var(--bg-primary); box-shadow: -20px 0 60px rgba(0,0,0,.24); }
.tablet-event-drawer .drawer-close { position: absolute; top: max(13px, env(safe-area-inset-top)); right: 58px; z-index: 5; width: 44px; height: 44px; border: 0; border-radius: 9px; color: var(--text-primary); background: rgba(127,127,127,.12); font-size: 24px; }
</style>
