/**
 * Shared alarm popup queue.
 *
 * Popup ownership lives outside visual components. Telemetry is ingested by
 * useSensorStore.applyTelemetry(), so alarms do not depend on a particular
 * responsive layout being mounted or on a component-level watcher.
 */
import { ref } from 'vue'
import { createAlarmPopupQueue } from '@/composables/alarmPopupQueue'

const STORE_KEY = Symbol.for('robot-control-panel.alarm-popup-store')

function createStore() {
  const activeAlarm = ref(null)
  const pendingCount = ref(0)
  const queue = createAlarmPopupQueue((snapshot) => {
    activeAlarm.value = snapshot.activeAlarm
    pendingCount.value = snapshot.pendingAlarms.length
  })

  return {
    activeAlarm,
    pendingCount,
    dismissActiveAlarm: () => queue.dismiss(),
    ingestAlarmTelemetry: (point) => queue.ingest(point),
  }
}

// Vite can evaluate a module again during HMR while already-mounted components
// still hold the previous exports. Symbol.for keeps every module generation on
// the same queue and Vue refs, so ingestion and display cannot split apart.
const store = globalThis[STORE_KEY] ?? (globalThis[STORE_KEY] = createStore())

export function ingestAlarmTelemetry(point) {
  return store.ingestAlarmTelemetry(point)
}

export function useAlarmPopupStore() {
  return {
    activeAlarm: store.activeAlarm,
    pendingCount: store.pendingCount,
    dismissActiveAlarm: store.dismissActiveAlarm,
  }
}
