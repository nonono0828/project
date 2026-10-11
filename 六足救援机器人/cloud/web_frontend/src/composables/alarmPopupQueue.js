import { createCooldownGate } from './cooldownGate.js'

export const PERSON_ALARM = 'person'
export const GAS_ALARM = 'gas'

const MAX_PROCESSED_FRAMES = 512
const MAX_RETIRED_BOOTS = 16
const DEFAULT_PERSON_POPUP_COOLDOWN_MS = 30_000

function normalizeBoolean(value) {
  if (value === true || value === 1) return true
  if (value === false || value === 0) return false
  return null
}

function normalizeGasAlarm(value) {
  return normalizeBoolean(value && typeof value === 'object' ? value.alarm : value)
}

function normalizeReceivedAt(value) {
  const receivedAt = Number(value)
  return Number.isFinite(receivedAt) && receivedAt > 0 ? receivedAt : null
}

function normalizeSequence(value) {
  return Number.isInteger(value) && value >= 0 && value <= 0xFFFFFFFF
    ? value
    : null
}

function frameKey(point, alarmType) {
  const sequence = normalizeSequence(point?.seq)
  if (!point || point.boot_id == null || sequence == null) return null
  return `${alarmType}:${point.device_id ?? 'unknown'}:${point.boot_id}:${sequence}`
}

// Telemetry sequence numbers are uint32. This comparison accepts wraparound
// while rejecting equal or older values from the same boot.
function isNewerSequence(sequence, previous) {
  const delta = (sequence - previous) >>> 0
  return delta > 0 && delta < 0x80000000
}

/**
 * Small framework-independent alarm state machine.
 *
 * The active popup is never preempted. Person alarms only take priority when
 * choosing the next popup, including when person and gas arrive together.
 */
export function createAlarmPopupQueue(onChange = () => {}, options = {}) {
  let activeAlarm = null
  const pendingAlarms = []
  let activeBootId = null
  const retiredBootIds = new Set()
  const retiredBootOrder = []
  const processedFrames = new Set()
  const processedFrameOrder = []
  let lastReceivedAt = null
  const signalOrder = {
    [PERSON_ALARM]: { receivedAt: null, sequence: null },
    [GAS_ALARM]: { receivedAt: null, sequence: null },
  }
  let personInputLatched = false
  let gasInputLatched = false
  const personPopupGate = createCooldownGate(
    options.personPopupCooldownMs ?? DEFAULT_PERSON_POPUP_COOLDOWN_MS,
    options.now ?? (() => Date.now()),
  )

  function snapshot() {
    return {
      activeAlarm,
      pendingAlarms: [...pendingAlarms],
    }
  }

  function notify() {
    const current = snapshot()
    onChange(current)
    return current
  }

  function enqueue(type) {
    if (activeAlarm === type || pendingAlarms.includes(type)) return false
    pendingAlarms.push(type)
    return true
  }

  function rememberFrame(key) {
    if (!key) return
    processedFrames.add(key)
    processedFrameOrder.push(key)
    while (processedFrameOrder.length > MAX_PROCESSED_FRAMES) {
      processedFrames.delete(processedFrameOrder.shift())
    }
  }

  function retireBoot(bootId) {
    if (bootId == null || retiredBootIds.has(bootId)) return
    retiredBootIds.add(bootId)
    retiredBootOrder.push(bootId)
    while (retiredBootOrder.length > MAX_RETIRED_BOOTS) {
      retiredBootIds.delete(retiredBootOrder.shift())
    }
  }

  function resetSignalOrder() {
    signalOrder[PERSON_ALARM] = { receivedAt: null, sequence: null }
    signalOrder[GAS_ALARM] = { receivedAt: null, sequence: null }
  }

  function acceptSignal(point, alarmType) {
    const key = frameKey(point, alarmType)
    if (key && processedFrames.has(key)) return false

    const order = signalOrder[alarmType]
    const receivedAt = normalizeReceivedAt(point.received_at)
    if (receivedAt != null && order.receivedAt != null && receivedAt < order.receivedAt) {
      return false
    }

    const sequence = point.boot_id != null ? normalizeSequence(point.seq) : null
    if (sequence != null && order.sequence != null && !isNewerSequence(sequence, order.sequence)) {
      return false
    }

    rememberFrame(key)
    if (receivedAt != null) order.receivedAt = Math.max(order.receivedAt ?? receivedAt, receivedAt)
    if (sequence != null) order.sequence = sequence
    return true
  }

  function showNextAlarm() {
    if (activeAlarm || pendingAlarms.length === 0) return

    const personIndex = pendingAlarms.indexOf(PERSON_ALARM)
    const nextIndex = personIndex >= 0 ? personIndex : 0
    activeAlarm = pendingAlarms.splice(nextIndex, 1)[0]
  }

  function ingest(point) {
    if (!point || point.online !== true) return snapshot()

    const person = normalizeBoolean(point.person_detected)
    const gas = normalizeGasAlarm(point.gas)
    if (person == null && gas == null) return snapshot()

    const receivedAt = normalizeReceivedAt(point.received_at)
    if (point.boot_id != null && point.boot_id !== activeBootId) {
      // Once a newer boot has been observed, a delayed frame from a retired
      // boot must not reset the latches again.
      if (retiredBootIds.has(point.boot_id)) return snapshot()
      if (receivedAt != null && lastReceivedAt != null && receivedAt < lastReceivedAt) {
        return snapshot()
      }
      retireBoot(activeBootId)
      activeBootId = point.boot_id
      personInputLatched = false
      gasInputLatched = false
      resetSignalOrder()
    }

    const acceptPerson = person != null && acceptSignal(point, PERSON_ALARM)
    const acceptGas = gas != null && acceptSignal(point, GAS_ALARM)
    if ((acceptPerson || acceptGas) && receivedAt != null) {
      lastReceivedAt = Math.max(lastReceivedAt ?? receivedAt, receivedAt)
    }

    if (acceptPerson && person === false) personInputLatched = false
    if (acceptGas && gas === false) gasInputLatched = false

    // Queue both rising edges before selecting one so simultaneous alarms are
    // deterministic: person first, gas retained for the next acknowledgement.
    if (acceptPerson && person === true && !personInputLatched) {
      personInputLatched = true
      const alreadyQueued = activeAlarm === PERSON_ALARM || pendingAlarms.includes(PERSON_ALARM)
      if (!alreadyQueued && personPopupGate.tryAccept()) enqueue(PERSON_ALARM)
    }
    if (acceptGas && gas === true && !gasInputLatched) {
      gasInputLatched = true
      enqueue(GAS_ALARM)
    }

    showNextAlarm()
    return notify()
  }

  function dismiss() {
    if (!activeAlarm) return snapshot()
    activeAlarm = null
    showNextAlarm()
    return notify()
  }

  return {
    dismiss,
    ingest,
    snapshot,
  }
}
