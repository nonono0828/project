<template>
  <MacWindow
    :visible="visible"
    title="位置地图 · GPS / 基站定位"
    :x="x" :y="y" :w="w" :h="h"
    :minW="360" :minH="280"
    :dragging="dragState.dragging"
    :draggable="draggable"
    :resizable="resizable"
    :embedded="embedded"
    :show-header="!compact"
    @close="$emit('close')"
    @minimize="$emit('minimize')"
    @dragStart="handleDragStart"
    @resizeStart="(e, edge) => resizeHandlers['on' + edge.toUpperCase()]?.(e)"
  >
    <div class="flex flex-col gap-1.5 h-full min-h-0">
      <div v-if="!compact" class="location-toolbar flex-shrink-0">
        <div class="flex items-center gap-1.5 text-[11px]">
          <span class="inline-block w-1.5 h-1.5 rounded-full" :class="displayPosition ? 'bg-green-400' : 'bg-red-400'"></span>
          <span style="color: var(--text-secondary)">{{ locationStatus }}</span>
        </div>
        <div class="cell-location-control">
          <span>基站定位</span>
          <button
            class="ios-toggle"
            :class="{ active: cellEnabled }"
            type="button"
            role="switch"
            :aria-checked="cellEnabled"
            aria-label="开启或关闭基站定位"
            @click="toggleCellLocation"
          ><span></span></button>
        </div>
      </div>

      <div v-if="!compact" class="location-detail flex-shrink-0">
        <span>{{ sourceText }}</span>
        <span v-if="cellEnabled">{{ validationText }}</span>
        <span>卫星:{{ liveGpsPosition?.satellites ?? 'NA' }}</span>
      </div>

      <div class="map-stage flex-1 min-h-0">
        <div ref="mapContainer" class="map-canvas h-full rounded-lg overflow-hidden"></div>
        <button
          v-if="compact"
          class="compact-location-refresh"
          :class="{ spinning: cellLocating || compactRefreshing }"
          type="button"
          :disabled="cellLocating || compactRefreshing || deviceOnline !== true"
          aria-label="刷新机器人位置"
          title="刷新位置"
          @click.stop="handleCompactRefresh"
        >
          <svg viewBox="0 0 24 24" aria-hidden="true">
            <path d="M20 11a8.1 8.1 0 0 0-14.2-4.1L3.5 9.2M4 4.8v4.4h4.4M4 13a8.1 8.1 0 0 0 14.2 4.1l2.3-2.3M20 19.2v-4.4h-4.4" />
          </svg>
        </button>
      </div>

      <div v-if="!compact" class="location-values flex-shrink-0">
        <span>经度:{{ formatCoordinate(displayPosition?.lng) }}</span>
        <span>纬度:{{ formatCoordinate(displayPosition?.lat) }}</span>
        <span>速度:{{ displayPosition?.speed == null ? 'NA' : `${displayPosition.speed}m/s` }}</span>
        <span v-if="displayPosition?.source === 'cell'">精度半径:{{ formatAccuracy(displayPosition.accuracy) }}</span>
      </div>
    </div>
  </MacWindow>
</template>

<script setup>
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from 'vue'
import L from 'leaflet'
import MacWindow from './MacWindow.vue'
import cfg from '@/config/config'
import { useDraggable } from '@/composables/useDraggable'
import { useResizable } from '@/composables/useResizable'
import { useLocationStore } from '@/composables/useLocationStore'
import { hasValidLocationCoordinates } from '@/composables/locationState'
import markerIcon2x from 'leaflet/dist/images/marker-icon-2x.png'
import markerIcon from 'leaflet/dist/images/marker-icon.png'
import markerShadow from 'leaflet/dist/images/marker-shadow.png'

delete L.Icon.Default.prototype._getIconUrl
L.Icon.Default.mergeOptions({ iconRetinaUrl: markerIcon2x, iconUrl: markerIcon, shadowUrl: markerShadow })

const props = defineProps({
  visible: { type: Boolean, default: true },
  initialX: { type: Number, default: 280 },
  initialY: { type: Number, default: 10 },
  initialW: { type: Number, default: 700 },
  initialH: { type: Number, default: 500 },
  draggable: { type: Boolean, default: true },
  resizable: { type: Boolean, default: true },
  embedded: { type: Boolean, default: false },
  compact: { type: Boolean, default: false },
  active: { type: Boolean, default: true },
})

const emit = defineEmits(['close', 'minimize', 'drag-end', 'resize-end'])

function onDragEnd(pos) { emit('drag-end', pos) }
function onResizeEnd(s) { emit('resize-end', s) }

const { state: dragState, x, y, onPointerDown, setPosition } = useDraggable(props.initialX, props.initialY, { onDragEnd })
function handleDragStart(e) { if (props.draggable) onPointerDown(e) }
const { w, h, resizeHandlers, setSize } = useResizable(360, 280, props.initialW, props.initialH, onResizeEnd, dragState)

function invalidateSize() {
  scheduleMapLayout()
}

function recenterAfterLayout() {
  scheduleMapLayout({ recenter: true })
}

defineExpose({ setPosition, setSize, invalidateSize, recenterAfterLayout, x, y, w, h })

const {
  deviceOnline,
  cellEnabled,
  cellLocating,
  cellLocation,
  cellError,
  validation,
  liveGpsPosition,
  displayPosition,
  trajectory,
  refreshLocation,
  toggleCellLocation,
} = useLocationStore()

const compactRefreshing = ref(false)

const cellErrorMessages = {
  cell_geolocation_unreachable: '定位服务暂时不可达',
  cell_geolocation_not_configured: '定位服务未配置',
  cell_geolocation_quota_exhausted: '定位服务额度不足',
  cell_geolocation_auth_failed: '定位服务认证失败',
  cell_geolocation_no_match: '当前小区暂无定位结果',
  cell_geolocation_lookup_failed: '定位服务查询失败',
  serving_cell_unavailable: '等待服务小区 ID',
  invalid_serving_cell_identity: '服务小区信息不完整',
  device_offline: '机器人未连接',
}

const locationStatus = computed(() => {
  if (liveGpsPosition.value) return 'GPS 定位在线'
  if (displayPosition.value?.source === 'cell') {
    return deviceOnline.value === true ? '基站定位在线' : '上次基站定位'
  }
  return '暂无定位'
})

const sourceText = computed(() => {
  if (liveGpsPosition.value) return cellEnabled.value ? '位置来源：GPS（基站校验）' : '位置来源：GPS'
  if (displayPosition.value?.source === 'cell') return '位置来源：基站定位'
  return '位置来源：NA'
})

const validationText = computed(() => {
  if (cellLocating.value) return '正在获取基站位置…'
  if (cellLocation.value?.stale) {
    const ageSeconds = Number(cellLocation.value.stale_age_seconds)
    const ageText = Number.isFinite(ageSeconds)
      ? `（约 ${Math.max(1, Math.round(ageSeconds / 60))} 分钟前）`
      : ''
    return `定位服务暂不可达，显示上次基站结果${ageText}`
  }
  if (cellError.value) return `基站定位失败：${cellErrorMessages[cellError.value] || cellError.value}`
  if (!cellLocation.value) return '尚无基站定位结果'
  if (!liveGpsPosition.value) return `基站精度半径约 ${formatAccuracy(cellLocation.value.accuracy)}`
  if (!validation.value) return 'GPS 为主，等待基站校验'
  if (typeof validation.value.within_cell_accuracy !== 'boolean') return 'GPS 为主，基站校验结果待确认'
  const distance = Number.isFinite(validation.value.distance_m)
    ? `${Math.round(validation.value.distance_m)}m`
    : 'NA'
  return validation.value.within_cell_accuracy
    ? `GPS 为主 · 基站校验一致（相距 ${distance}）`
    : `GPS 为主 · 与基站结果相差 ${distance}`
})

function formatCoordinate(value) {
  return Number.isFinite(value) ? value.toFixed(6) : 'NA'
}

function formatAccuracy(value) {
  return Number.isFinite(value) ? `${Math.round(value)}m` : 'NA'
}

const mapContainer = ref(null)
let map = null
let marker = null
let pathPolyline = null
let resizeObserver = null
let robotIcon = null
let primaryTiles = null
let fallbackTiles = null
let primaryTileErrors = 0
let primaryTileLoaded = false
let fallbackTilesActive = false
let tileWatchdog = null
let layoutFrame = null
let settledLayoutFrame = null
let lastRenderedLatLng = null

function clearTileWatchdog() {
  if (!tileWatchdog) return
  clearTimeout(tileWatchdog)
  tileWatchdog = null
}

function activateFallbackTiles() {
  if (fallbackTilesActive || !map || !fallbackTiles) return
  fallbackTilesActive = true
  clearTileWatchdog()
  if (primaryTiles && map.hasLayer(primaryTiles)) map.removeLayer(primaryTiles)
  fallbackTiles.addTo(map)
}

function startTileWatchdog() {
  clearTileWatchdog()
  if (primaryTileLoaded || fallbackTilesActive) return
  tileWatchdog = setTimeout(activateFallbackTiles, 6000)
}

function cancelScheduledLayout() {
  if (layoutFrame != null) cancelAnimationFrame(layoutFrame)
  if (settledLayoutFrame != null) cancelAnimationFrame(settledLayoutFrame)
  layoutFrame = null
  settledLayoutFrame = null
}

function scheduleMapLayout({ recenter = false } = {}) {
  cancelScheduledLayout()
  layoutFrame = requestAnimationFrame(() => {
    layoutFrame = null
    settledLayoutFrame = requestAnimationFrame(() => {
      settledLayoutFrame = null
      if (!map || !mapContainer.value) return
      if (mapContainer.value.clientWidth <= 0 || mapContainer.value.clientHeight <= 0) return
      map.invalidateSize({ pan: false, animate: false })
      const point = displayPosition.value
      if (!recenter || !props.active || !props.visible || !hasValidLocationCoordinates(point)) return
      updateMarker(point, false, false)
      map.stop()
      map.setView([point.lat, point.lng], map.getZoom(), { animate: false })
      lastRenderedLatLng = [point.lat, point.lng]
    })
  })
}

onMounted(async () => {
  await nextTick()
  if (!mapContainer.value) return
  map = L.map(mapContainer.value, {
    center: cfg.MAP_CENTER,
    zoom: cfg.MAP_ZOOM,
    zoomControl: true,
    attributionControl: !props.compact,
    dragging: !props.compact,
    doubleClickZoom: !props.compact,
    boxZoom: !props.compact,
    keyboard: !props.compact,
    scrollWheelZoom: !props.compact,
    touchZoom: true,
  })
  const tileOptions = {
    maxZoom: 19,
    attribution: '&copy; OpenStreetMap contributors',
    className: 'standard-map-tiles',
    keepBuffer: props.compact ? 0 : 1,
    updateWhenIdle: props.compact,
  }
  primaryTiles = L.tileLayer(cfg.MAP_TILE_URL, {
    ...tileOptions,
    attribution: '&copy; OpenStreetMap contributors, Tiles style by HOT',
  })
  fallbackTiles = L.tileLayer(cfg.MAP_TILE_FALLBACK_URL, tileOptions)
  primaryTiles.on('loading', startTileWatchdog)
  primaryTiles.on('tileload', () => {
    primaryTileLoaded = true
    primaryTileErrors = 0
    clearTileWatchdog()
  })
  primaryTiles.on('tileerror', () => {
    primaryTileErrors += 1
    if (!primaryTileLoaded || primaryTileErrors >= 3) activateFallbackTiles()
  })
  primaryTiles.addTo(map)
  pathPolyline = L.polyline(trajectory.value, { color: '#007aff', weight: 3, opacity: 0.6 }).addTo(map)
  resizeObserver = new ResizeObserver(() => {
    scheduleMapLayout({ recenter: props.active && props.visible })
  })
  resizeObserver.observe(mapContainer.value)
  if (displayPosition.value) updateMarker(displayPosition.value, false, false)
  scheduleMapLayout({ recenter: true })
})

onUnmounted(() => {
  clearTileWatchdog()
  cancelScheduledLayout()
  resizeObserver?.disconnect()
  resizeObserver = null
  map?.remove()
  map = null
  marker = null
  pathPolyline = null
})

watch(displayPosition, (point) => {
  if (point) updateMarker(point)
  else removeLocation()
})

watch(trajectory, (points) => {
  pathPolyline?.setLatLngs(points)
})

watch(
  () => props.active,
  (active) => {
    if (!active) return
    scheduleMapLayout({ recenter: true })
  },
  { flush: 'post' },
)

watch(
  () => props.visible,
  (visible) => {
    if (!visible) return
    scheduleMapLayout({ recenter: true })
  },
  { flush: 'post' },
)

async function handleCompactRefresh() {
  if (cellLocating.value || compactRefreshing.value || deviceOnline.value === false) return
  compactRefreshing.value = true
  try {
    await refreshLocation({ queryServingCell: true })
    await nextTick()
    scheduleMapLayout({ recenter: true })
  } finally {
    compactRefreshing.value = false
  }
}

function removeLocation() {
  marker?.remove()
  marker = null
  lastRenderedLatLng = null
  pathPolyline?.setLatLngs([])
}

function updateMarker(point, forceCenter = false, allowPan = true) {
  if (!map || !hasValidLocationCoordinates(point)) return
  if (!robotIcon) {
    robotIcon = L.icon({
      iconUrl: '/retouch_2026060418314442.png',
      iconSize: [24, 32],
      iconAnchor: [12, 32],
      popupAnchor: [0, -32],
    })
  }
  if (!marker) {
    marker = L.marker([point.lat, point.lng], { icon: robotIcon }).addTo(map)
    if (!props.compact) marker.bindPopup('').openPopup()
  }

  const latLng = [point.lat, point.lng]
  const coordinatesChanged = !lastRenderedLatLng
    || lastRenderedLatLng[0] !== latLng[0]
    || lastRenderedLatLng[1] !== latLng[1]
  marker.setLatLng(latLng)
  pathPolyline?.setLatLngs(trajectory.value)
  const source = point.source === 'cell' ? '基站定位' : 'GPS'
  if (!props.compact) marker.setPopupContent(`<b>机器人</b> ${point.lat.toFixed(6)}, ${point.lng.toFixed(6)}<br>${source}`)
  if (allowPan && (forceCenter || coordinatesChanged) && props.active && props.visible) {
    map.panTo(latLng, { animate: true, duration: 0.5 })
  }
  lastRenderedLatLng = latLng
}
</script>

<style scoped>
.location-toolbar { min-height: 24px; display: flex; align-items: center; justify-content: space-between; gap: 10px; }
.cell-location-control { display: flex; align-items: center; gap: 7px; color: var(--text-secondary); font-size: 10px; }
.ios-toggle { position: relative; width: 36px; height: 20px; flex: 0 0 36px; padding: 0; border: 0; border-radius: 10px; background: #9ca3af; cursor: pointer; }
.ios-toggle span { position: absolute; top: 2px; left: 2px; width: 16px; height: 16px; border-radius: 50%; background: #fff; box-shadow: 0 1px 3px rgba(0,0,0,.25); transition: transform .2s ease; }
.ios-toggle.active { background: #34c759; }
.ios-toggle.active span { transform: translateX(16px); }
.location-detail, .location-values { display: flex; align-items: center; flex-wrap: wrap; gap: 6px 14px; color: var(--text-tertiary); font-size: 10px; }
.location-detail { min-height: 18px; }
.map-stage { position: relative; min-height: 0; }
.map-canvas { width: 100%; }
.compact-location-refresh {
  position: absolute;
  z-index: 900;
  top: 8px;
  right: 8px;
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  padding: 0;
  border: 1px solid rgba(0, 0, 0, .14);
  border-radius: 12px;
  color: #1d1d1f;
  background: rgba(255, 255, 255, .92);
  box-shadow: 0 2px 8px rgba(0, 0, 0, .18);
  backdrop-filter: blur(8px);
  -webkit-backdrop-filter: blur(8px);
}
.compact-location-refresh:disabled { opacity: .48; }
.compact-location-refresh svg { width: 20px; height: 20px; fill: none; stroke: currentColor; stroke-width: 1.9; stroke-linecap: round; stroke-linejoin: round; }
.compact-location-refresh.spinning svg { animation: location-refresh-spin .9s linear infinite; }
@keyframes location-refresh-spin { to { transform: rotate(360deg); } }
:deep(.standard-map-tiles) { filter: none !important; opacity: 1 !important; }
@media (prefers-color-scheme: dark) {
  .compact-location-refresh { border-color: rgba(255,255,255,.22); color: #f5f5f7; background: rgba(34,34,36,.9); }
}
@media (max-width: 1023px) {
  .location-toolbar { min-height: 44px; }
  .ios-toggle { width: 44px; height: 44px; flex-basis: 44px; border-radius: 22px; background: transparent; }
  .ios-toggle::before { content: ''; position: absolute; top: 12px; left: 4px; width: 36px; height: 20px; border-radius: 10px; background: #9ca3af; }
  .ios-toggle span { top: 14px; left: 6px; }
  .ios-toggle.active { background: transparent; }
  .ios-toggle.active::before { background: #34c759; }
  .ios-toggle.active span { transform: translateX(16px); }
  .location-detail, .location-values { font-size: 11px; }
  :deep(.leaflet-touch .leaflet-bar a) { width: 44px; height: 44px; line-height: 44px; }
}
</style>
