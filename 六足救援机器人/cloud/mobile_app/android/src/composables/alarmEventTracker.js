export const SURVIVOR_EVENT = 'survivor'
export const GAS_EVENT = 'gas'

const MAX_PROCESSED_FRAMES = 512
const MAX_RETIRED_BOOTS = 16

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

function frameKey(point, eventType) {
  const sequence = normalizeSequence(point?.seq)
  if (!point || point.boot_id == null || sequence == null) return null
  return `${eventType}:${point.device_id ?? 'unknown'}:${point.boot_id}:${sequence}`
}

function isNewerSequence(sequence, previous) {
  const delta = (sequence - previous) >>> 0
  return delta > 0 && delta < 0x80000000
}

/**
 * Framework-independent telemetry-to-event state machine.
 *
 * Full firmware frames are de-duplicated and ordered by boot/sequence. Older
 * firmware frames without boot_id/seq remain supported through rising-edge
 * latches, so a sustained alarm produces one event and 0 -> 1 produces a new
 * event after the alarm clears.
 */
export function createAlarmEventTracker(onEvent = () => {}) {
  let activeBootId = null
  const retiredBootIds = new Set()
  const retiredBootOrder = []
  const processedFrames = new Set()
  const processedFrameOrder = []
  let lastReceivedAt = null
  const signalOrder = {
    [SURVIVOR_EVENT]: { receivedAt: null, sequence: null },
    [GAS_EVENT]: { receivedAt: null, sequence: null },
  }
  let personLatched = false
  let gasLatched = false

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
    signalOrder[SURVIVOR_EVENT] = { receivedAt: null, sequence: null }
    signalOrder[GAS_EVENT] = { receivedAt: null, sequence: null }
  }

  function acceptSignal(point, eventType) {
    const key = frameKey(point, eventType)
    if (key && processedFrames.has(key)) return false

    const order = signalOrder[eventType]
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

  function ingest(point) {
    if (!point || point.online !== true) return []

    const person = normalizeBoolean(point.person_detected)
    const gas = normalizeGasAlarm(point.gas)
    if (person == null && gas == null) return []

    const receivedAt = normalizeReceivedAt(point.received_at)
    if (point.boot_id != null && point.boot_id !== activeBootId) {
      if (retiredBootIds.has(point.boot_id)) return []
      if (receivedAt != null && lastReceivedAt != null && receivedAt < lastReceivedAt) return []
      retireBoot(activeBootId)
      activeBootId = point.boot_id
      personLatched = false
      gasLatched = false
      resetSignalOrder()
    }

    const acceptPerson = person != null && acceptSignal(point, SURVIVOR_EVENT)
    const acceptGas = gas != null && acceptSignal(point, GAS_EVENT)
    if ((acceptPerson || acceptGas) && receivedAt != null) {
      lastReceivedAt = Math.max(lastReceivedAt ?? receivedAt, receivedAt)
    }

    if (acceptPerson && person === false) personLatched = false
    if (acceptGas && gas === false) gasLatched = false

    const emitted = []
    if (acceptPerson && person === true && !personLatched) {
      personLatched = true
      emitted.push(SURVIVOR_EVENT)
    }
    if (acceptGas && gas === true && !gasLatched) {
      gasLatched = true
      emitted.push(GAS_EVENT)
    }

    for (const type of emitted) onEvent(type, point)
    return emitted
  }

  return { ingest }
}
