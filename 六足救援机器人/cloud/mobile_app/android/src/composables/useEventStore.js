/** Durable event journal shared by every page and browser session. */
import { effectScope, ref, watch } from 'vue'
import {
  deleteEvents,
  fetchEvents,
  fetchLocationLatest,
  fetchTelemetryLatest,
  fetchVideoSnapshot,
  saveEvent,
} from '@/api'
import { useSensorStore } from '@/composables/useSensorStore'
import { useGpsStore } from '@/composables/useGpsStore'
import { createAlarmEventTracker } from '@/composables/alarmEventTracker'
import {
  cellEventLocation,
  gpsEventLocation,
  normalizeEventLocation,
} from '@/composables/eventLocation'

const MAX_EVENTS = 200
const SYNC_INTERVAL_MS = 5000

const events = ref([])
const pendingEvents = new Map()
const flushingEvents = new Set()
const canonicalEventIds = new Map()

let started = false
let syncTimer = null
let syncRunning = false
let eventCounter = 0
let eventScope = null

function makeEventId() {
  eventCounter = (eventCounter + 1) >>> 0
  return `event-${Date.now().toString(36)}-${eventCounter.toString(36)}`
}

function resolveEventId(id) {
  let current = id
  const seen = new Set()
  while (canonicalEventIds.has(current) && !seen.has(current)) {
    seen.add(current)
    current = canonicalEventIds.get(current)
  }
  return current
}

function sameFrame(sensor, gps) {
  return sensor && gps
    && sensor.device_id === gps.device_id
    && sensor.boot_id === gps.boot_id
    && sensor.seq === gps.seq
}

function validLocation(gps) {
  return gpsEventLocation(gps)
}

function revokeSnapshot(event) {
  if (event?.snapshotUrl?.startsWith('blob:')) URL.revokeObjectURL(event.snapshotUrl)
}

function trimEvents() {
  if (events.value.length <= MAX_EVENTS) return
  const removed = events.value.splice(MAX_EVENTS)
  removed.forEach(revokeSnapshot)
}

function serializableEvent(event) {
  const { snapshotUrl, ...payload } = event
  return payload
}

function hydrateEvent(raw, local = null) {
  const snapshotData = typeof raw?.snapshotData === 'string' ? raw.snapshotData : null
  return {
    ...raw,
    location: normalizeEventLocation(raw?.location),
    snapshotData,
    snapshotUrl: snapshotData || local?.snapshotUrl || null,
    snapshotStatus: snapshotData ? 'ready' : (raw?.snapshotStatus || 'none'),
  }
}

function mergeRemoteEvents(remoteEvents) {
  if (!Array.isArray(remoteEvents)) return
  const localById = new Map(events.value.map((event) => [event.id, event]))
  const remoteIds = new Set()
  const merged = remoteEvents.map((raw) => {
    remoteIds.add(raw.id)
    return hydrateEvent(raw, localById.get(raw.id))
  })
  for (const [id, pending] of pendingEvents) {
    if (!remoteIds.has(id)) merged.push(hydrateEvent(pending, localById.get(id)))
  }
  merged.sort((left, right) => (right.timestamp || 0) - (left.timestamp || 0))
  const nextIds = new Set(merged.map((event) => event.id))
  events.value.forEach((event) => {
    if (!nextIds.has(event.id)) revokeSnapshot(event)
  })
  events.value = merged.slice(0, MAX_EVENTS)
}

function mergeSavedEvent(saved, previousId) {
  const resolvedPreviousId = resolveEventId(previousId)
  const previous = events.value.find(
    (event) => event.id === previousId || event.id === resolvedPreviousId || event.id === saved.id,
  )
  const hydrated = hydrateEvent(saved, previous)
  canonicalEventIds.set(previousId, saved.id)
  canonicalEventIds.set(resolvedPreviousId, saved.id)
  events.value = events.value.filter(
    (event) => event.id !== previousId && event.id !== resolvedPreviousId && event.id !== saved.id,
  )
  events.value.unshift(hydrated)
  events.value.sort((left, right) => (right.timestamp || 0) - (left.timestamp || 0))
  trimEvents()
}

async function flushPendingEvent(id) {
  if (window.__ROBOT_CLOUD_ACTIVE === false) return
  if (flushingEvents.has(id)) return
  flushingEvents.add(id)
  try {
    while (pendingEvents.has(id) && window.__ROBOT_CLOUD_ACTIVE !== false) {
      const pending = pendingEvents.get(id)
      const response = await saveEvent(pending)
      const saved = response?.data
      if (pendingEvents.get(id) === pending) pendingEvents.delete(id)
      if (saved?.id) mergeSavedEvent(saved, id)
    }
  } catch {
    // Keep it pending; the next synchronization cycle retries the write.
  } finally {
    flushingEvents.delete(id)
  }
}

function persistEvent(event) {
  const payload = serializableEvent(event)
  pendingEvents.set(event.id, payload)
  void flushPendingEvent(event.id)
}

async function syncEvents() {
  if (window.__ROBOT_CLOUD_ACTIVE === false) return
  if (syncRunning) return
  syncRunning = true
  try {
    const response = await fetchEvents(MAX_EVENTS)
    mergeRemoteEvents(response?.data?.events)
    for (const id of [...pendingEvents.keys()]) {
      if (window.__ROBOT_CLOUD_ACTIVE === false) break
      await flushPendingEvent(id)
    }
  } catch {
    // Existing local records remain usable while the server is unavailable.
  } finally {
    syncRunning = false
  }
}

function updateEvent(id, patch, persist = false) {
  const resolvedId = resolveEventId(id)
  const target = events.value.find((event) => event.id === id || event.id === resolvedId)
  if (!target) return false
  Object.assign(target, patch)
  if (persist) persistEvent(target)
  return true
}

async function resolveEventLocation(id, type, sensorPoint, currentGps) {
  const current = sameFrame(sensorPoint, currentGps) ? validLocation(currentGps) : null
  if (current) {
    updateEvent(id, { location: current }, true)
    return
  }

  try {
    const response = await fetchTelemetryLatest()
    const latestSensor = response?.data?.sensor
    const latestGps = response?.data?.gps
    if (sameFrame(sensorPoint, latestSensor) && sameFrame(sensorPoint, latestGps)) {
      const location = validLocation(latestGps)
      if (location) updateEvent(id, { location }, true)
    }
  } catch {
    // A missing exact GPS sample is recorded as NA; stale coordinates are never used.
  }

  if (type !== 'survivor') return
  try {
    const response = await fetchLocationLatest(true)
    const location = cellEventLocation(response)
    if (location) updateEvent(id, { location }, true)
  } catch {
    // Keep the survivor event even when cell geolocation is unavailable.
  }
}

function blobToDataUrl(blob) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader()
    reader.onload = () => resolve(typeof reader.result === 'string' ? reader.result : null)
    reader.onerror = () => reject(reader.error)
    reader.readAsDataURL(blob)
  })
}

async function attachSnapshot(id) {
  try {
    const snapshot = await fetchVideoSnapshot()
    const snapshotUrl = URL.createObjectURL(snapshot.blob)
    const snapshotData = await blobToDataUrl(snapshot.blob)
    if (!updateEvent(id, {
      snapshotUrl,
      snapshotData,
      snapshotStatus: 'ready',
      frameId: snapshot.frameId,
      frameSequence: snapshot.frameSequence,
    }, true)) {
      URL.revokeObjectURL(snapshotUrl)
    }
  } catch {
    updateEvent(id, { snapshotStatus: 'error' }, true)
  }
}

function addEvent(type, point, currentGps) {
  const timestamp = Number.isFinite(point.received_at)
    ? Math.round(point.received_at * 1000)
    : Date.now()
  const event = {
    id: makeEventId(),
    type,
    timestamp,
    deviceId: point.device_id ?? null,
    bootId: point.boot_id ?? null,
    sequence: point.seq ?? null,
    dedupeKey: telemetryDedupeKey(point, type),
    location: sameFrame(point, currentGps) ? validLocation(currentGps) : null,
    gas: type === 'gas' ? {
      concentration: point.gas?.concentration ?? null,
      unit: point.gas?.unit ?? null,
    } : null,
    snapshotData: null,
    snapshotUrl: null,
    snapshotStatus: type === 'survivor' ? 'loading' : 'none',
    frameId: null,
    frameSequence: null,
  }

  events.value.unshift(event)
  trimEvents()
  persistEvent(event)

  if (!event.location) void resolveEventLocation(event.id, type, point, currentGps)
  if (type === 'survivor') void attachSnapshot(event.id)
}

function telemetryDedupeKey(point, type) {
  if (point.boot_id != null && Number.isInteger(point.seq)) {
    return `telemetry:${point.device_id ?? 'unknown'}:${point.boot_id}:${point.seq}:${type}`
  }
  const receivedAt = Number(point.received_at)
  if (Number.isFinite(receivedAt) && receivedAt > 0) {
    return `telemetry:${point.device_id ?? 'unknown'}:${point.boot_id ?? 'unknown'}:received:${Math.round(receivedAt * 1000)}:${type}`
  }
  return null
}

function start() {
  if (started) return
  started = true
  const sensorStore = useSensorStore()
  const gpsStore = useGpsStore()
  const tracker = createAlarmEventTracker(
    (type, point) => {
      if (window.__ROBOT_CLOUD_ACTIVE === true) {
        addEvent(type, point, gpsStore.data.value)
      }
    },
  )
  // A detached effect scope keeps alarm journaling alive when a responsive
  // event panel is unmounted, the page tree changes, or the user logs in again.
  eventScope = effectScope(true)
  eventScope.run(() => {
    watch(sensorStore.data, (point) => tracker.ingest(point), { immediate: true })
  })
  resumeEventSync()
}

function pauseEventSync() {
  if (!syncTimer) return
  window.clearInterval(syncTimer)
  syncTimer = null
}

function resumeEventSync() {
  if (!started || window.__ROBOT_CLOUD_ACTIVE === false || syncTimer) return
  void syncEvents()
  syncTimer = window.setInterval(syncEvents, SYNC_INTERVAL_MS)
}

async function clearEvents() {
  events.value.forEach(revokeSnapshot)
  events.value = []
  pendingEvents.clear()
  flushingEvents.clear()
  canonicalEventIds.clear()
  try {
    await deleteEvents()
  } catch {
    void syncEvents()
    throw new Error('事件记录清空失败')
  }
}

function recordOperationEvent(type, {
  operation,
  success,
  message,
  location = null,
  dedupeKey = null,
}) {
  const event = {
    id: makeEventId(),
    type,
    timestamp: Date.now(),
    success: success === true,
    operation,
    message,
    location: normalizeEventLocation(location),
    gas: null,
    dedupeKey,
    snapshotData: null,
    snapshotUrl: null,
    snapshotStatus: 'none',
    frameId: null,
    frameSequence: null,
  }
  events.value.unshift(event)
  trimEvents()
  persistEvent(event)
  return event.id
}

function recordModemEvent({ operation, success, message, location = null }) {
  return recordOperationEvent('modem', {
    operation: operation || '4G 操作',
    success,
    message: message || (success ? '设置已生效' : '设置未生效'),
    location,
  })
}

function recordWifiEvent({ operation, success, message }) {
  return recordOperationEvent('wifi', {
    operation: operation || 'Wi-Fi 操作',
    success,
    message: message || (success ? '设置已生效' : '设置未生效'),
  })
}

function recordVideoEvent({ operation, success, message }) {
  return recordOperationEvent('video', {
    operation: operation || '视频流设置',
    success,
    message: message || (success ? '设置已生效' : '设置未生效'),
  })
}

export function recordRobotModeEvent({ control, enabled, requestId }) {
  const labels = {
    terrain: '越野模式',
    autonomous: '自主避障',
  }
  const label = labels[control]
  if (!label || typeof enabled !== 'boolean' || !requestId) return null
  const message = `${label}已${enabled ? '开启' : '关闭'}`
  return recordOperationEvent('robot_control', {
    operation: label,
    success: true,
    message,
    dedupeKey: `robot_control:${requestId}`,
  })
}

export function useEventStore() {
  start()
  return {
    events,
    clearEvents,
    recordModemEvent,
    recordWifiEvent,
    recordVideoEvent,
    recordRobotModeEvent,
    syncEvents,
  }
}

window.addEventListener('robot-app-cloud-pause', pauseEventSync)
window.addEventListener('robot-app-cloud-resume', resumeEventSync)
