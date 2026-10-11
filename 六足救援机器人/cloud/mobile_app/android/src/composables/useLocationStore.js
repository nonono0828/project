/**
 * Shared location source for every map in the cloud dashboard.
 * GPS remains primary; when enabled, cell geolocation is a shared fallback.
 */
import { computed, ref, watch } from 'vue'
import { fetchLocationLatest } from '@/api'
import { useControlChannel } from '@/composables/useControlChannel'
import { useGpsStore } from '@/composables/useGpsStore'
import {
  appendLocationTrajectory,
  hasValidLocationCoordinates,
  servingCellIdentity,
} from '@/composables/locationState'

const cellEnabled = ref(false)
const cellLocating = ref(false)
const cellLocation = ref(null)
const cellError = ref('')
const validation = ref(null)
const cellDeviceOnline = ref(null)
const trajectory = ref([])

let started = false
let paused = false
let cellRefreshTimer = null
let cellRefreshGeneration = 0
let lastCellQueryAt = 0
let lastManualCellQueryAt = 0
let gpsOnline = null
let gpsPosition = null
let deviceOnline = null
let modem4g = null
let modemRequest = null
let requestModem4g = null
let liveGpsPosition = null
let displayPosition = null
let servingCellKey = null

function clearCellRefreshTimer() {
  if (!cellRefreshTimer) return
  clearInterval(cellRefreshTimer)
  cellRefreshTimer = null
}

function clearCellState({ clearTrail = false } = {}) {
  cellLocation.value = null
  validation.value = null
  cellError.value = ''
  cellDeviceOnline.value = null
  if (clearTrail) trajectory.value = []
}

function hasServingCell() {
  return Array.isArray(modem4g?.value?.cells)
    && modem4g.value.cells.some((cell) => cell?.serving === true && cell?.cell_id != null)
}

function requestServingCell({ manual = false } = {}) {
  const now = Date.now()
  const lastQueryAt = Math.max(lastCellQueryAt, lastManualCellQueryAt)
  if (modemRequest?.value?.pending || now - lastQueryAt < 30_000) return false

  const requestId = requestModem4g?.(
    { action: 'query' },
    manual ? '刷新基站定位服务小区' : '获取基站定位服务小区',
    false,
  )
  if (!requestId) return false

  lastCellQueryAt = now
  if (manual) lastManualCellQueryAt = now
  return true
}

function ensureServingCell() {
  if (hasServingCell()) return true
  requestServingCell()
  cellError.value = modemRequest?.value?.pending
    ? (modemRequest.value.action === 'query' ? '正在获取服务小区信息' : '等待 4G 管理操作完成')
    : '等待服务小区 ID'
  return false
}

function startCellRefreshTimer() {
  clearCellRefreshTimer()
  if (paused) return
  cellRefreshTimer = setInterval(() => { void refreshCellLocation() }, 30_000)
}

async function refreshCellLocation() {
  // Preserve a recent marker while the control socket is reconnecting, but do
  // not issue HTTP or modem work until the robot is explicitly online.
  if (paused || !cellEnabled.value || deviceOnline?.value !== true || cellLocating.value) return false
  if (!ensureServingCell()) return false

  const generation = cellRefreshGeneration
  cellLocating.value = true
  try {
    const response = await fetchLocationLatest(true)
    if (paused || generation !== cellRefreshGeneration || !cellEnabled.value || deviceOnline?.value !== true) {
      return false
    }
    const payload = response?.data || {}
    cellLocation.value = hasValidLocationCoordinates(payload.cell) ? payload.cell : null
    validation.value = payload.validation && typeof payload.validation === 'object'
      ? payload.validation
      : null
    cellError.value = payload.cell_error || ''
    cellDeviceOnline.value = typeof payload.online === 'boolean' ? payload.online : null
    return true
  } catch (error) {
    if (generation === cellRefreshGeneration && cellEnabled.value && deviceOnline?.value === true) {
      cellLocation.value = null
      validation.value = null
      cellError.value = error?.message || '请求失败'
      cellDeviceOnline.value = null
    }
    return false
  } finally {
    const retryCurrentGeneration = generation !== cellRefreshGeneration
      && !paused
      && cellEnabled.value
      && deviceOnline?.value === true
    cellLocating.value = false
    if (retryCurrentGeneration) queueMicrotask(() => { void refreshCellLocation() })
  }
}

function toggleCellLocation() {
  cellEnabled.value = !cellEnabled.value
  cellRefreshGeneration += 1
  clearCellRefreshTimer()
  if (cellEnabled.value) {
    void refreshCellLocation()
    startCellRefreshTimer()
  } else {
    clearCellState({ clearTrail: !liveGpsPosition?.value })
  }
}

async function refreshLocation({ queryServingCell = false } = {}) {
  if (!cellEnabled.value) return displayPosition?.value || null

  // The compact-map button can request the already-approved fixed modem
  // query once per cooldown. It never accepts arbitrary AT commands and the
  // subsequent HTTP lookup still uses the server's normal cell cache.
  if (!paused && queryServingCell && deviceOnline?.value === true) requestServingCell({ manual: true })
  await refreshCellLocation()
  return displayPosition?.value || null
}

function pauseLocationStore() {
  paused = true
  clearCellRefreshTimer()
  cellRefreshGeneration += 1
  cellLocating.value = false
}

function resumeLocationStore() {
  paused = false
  if (cellEnabled.value && deviceOnline?.value === true) {
    void refreshCellLocation()
    startCellRefreshTimer()
  }
}

function resetLocationStore() {
  paused = true
  clearCellRefreshTimer()
  cellRefreshGeneration += 1
  cellLocating.value = false
  cellEnabled.value = false
  lastCellQueryAt = 0
  lastManualCellQueryAt = 0
  clearCellState({ clearTrail: true })
}

function ensureStarted() {
  if (started) return
  started = true

  const gps = useGpsStore()
  const control = useControlChannel()
  gpsOnline = gps.online
  gpsPosition = gps.position
  deviceOnline = control.deviceOnline
  modem4g = control.modem4g
  modemRequest = control.modemRequest
  requestModem4g = control.requestModem4g

  liveGpsPosition = computed(() => {
    const point = gpsPosition.value
    if (!gpsOnline.value || !hasValidLocationCoordinates(point)) return null
    return { ...point, source: 'gps', accuracy: null }
  })
  displayPosition = computed(() => {
    if (liveGpsPosition.value) return liveGpsPosition.value
    if (
      cellEnabled.value
      && deviceOnline.value !== false
      && cellDeviceOnline.value !== false
      && hasValidLocationCoordinates(cellLocation.value)
    ) {
      return {
        ...cellLocation.value,
        source: 'cell',
        speed: null,
        satellites: null,
      }
    }
    return null
  })
  servingCellKey = computed(() => servingCellIdentity(modem4g.value?.cells))

  watch(deviceOnline, (online) => {
    if (online === false) {
      cellRefreshGeneration += 1
      clearCellState({ clearTrail: true })
      return
    }
    if (!paused && cellEnabled.value && online === true) void refreshCellLocation()
  })
  watch(servingCellKey, (current, previous) => {
    if (paused || !current || current === previous || !cellEnabled.value || deviceOnline.value !== true) return
    // The response is keyed to the serving cell observed at request start.
    // Invalidate an older lookup so its finally block queues exactly one
    // replacement request for the newly reported service cell.
    cellRefreshGeneration += 1
    if (cellLocating.value) return
    void refreshCellLocation()
  })
  watch(displayPosition, (point) => {
    if (!hasValidLocationCoordinates(point)) {
      trajectory.value = []
      return
    }
    trajectory.value = appendLocationTrajectory(trajectory.value, point)
  })
}

if (typeof window !== 'undefined') {
  window.addEventListener('robot-app-cloud-pause', pauseLocationStore)
  window.addEventListener('robot-app-cloud-resume', resumeLocationStore)
  window.addEventListener('robot-auth-expired', resetLocationStore)
  window.addEventListener('robot-location-reset', resetLocationStore)
}

export function useLocationStore() {
  ensureStarted()
  return {
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
  }
}
