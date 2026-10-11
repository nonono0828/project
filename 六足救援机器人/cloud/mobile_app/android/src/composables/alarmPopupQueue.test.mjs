import assert from 'node:assert/strict'
import { createAlarmPopupQueue } from './alarmPopupQueue.js'

function createQueueWithoutPersonCooldown() {
  return createAlarmPopupQueue(undefined, { personPopupCooldownMs: 0 })
}

function point({
  person = 0,
  gas = 0,
  boot = 'boot-a',
  sequence,
  receivedAt,
  device = 'robot-a',
} = {}) {
  const telemetry = {
    online: true,
    device_id: device,
    boot_id: boot,
    person_detected: person,
    gas: { alarm: gas },
  }
  if (sequence !== undefined) telemetry.seq = sequence
  if (receivedAt !== undefined) telemetry.received_at = receivedAt
  return telemetry
}

const simultaneous = createAlarmPopupQueue()
assert.deepEqual(
  simultaneous.ingest(point({ person: 1, gas: 1 })),
  { activeAlarm: 'person', pendingAlarms: ['gas'] },
  'person must win when both alarms arrive together',
)
assert.deepEqual(
  simultaneous.ingest(point({ person: 1, gas: 1 })),
  { activeAlarm: 'person', pendingAlarms: ['gas'] },
  'a high input must not duplicate or overwrite the active popup',
)
assert.deepEqual(
  simultaneous.dismiss(),
  { activeAlarm: 'gas', pendingAlarms: [] },
  'dismissing person must reveal the queued gas popup',
)

const noPreemption = createAlarmPopupQueue()
noPreemption.ingest(point({ gas: 1 }))
assert.deepEqual(
  noPreemption.ingest(point({ person: 1, gas: 1 })),
  { activeAlarm: 'gas', pendingAlarms: ['person'] },
  'an already visible gas popup must not be overwritten by person',
)
assert.deepEqual(
  noPreemption.dismiss(),
  { activeAlarm: 'person', pendingAlarms: [] },
  'queued person must be shown after the current popup is acknowledged',
)

const risingEdge = createQueueWithoutPersonCooldown()
risingEdge.ingest(point({ person: 1 }))
risingEdge.dismiss()
assert.equal(risingEdge.ingest(point({ person: 1 })).activeAlarm, null)
risingEdge.ingest(point({ person: 0 }))
assert.equal(risingEdge.ingest(point({ person: 1 })).activeAlarm, 'person')

const outOfOrder = createQueueWithoutPersonCooldown()
outOfOrder.ingest(point({ person: 1, sequence: 11, receivedAt: 101 }))
outOfOrder.dismiss()
outOfOrder.ingest(point({ person: 0, sequence: 10, receivedAt: 100 }))
assert.equal(
  outOfOrder.ingest(point({ person: 1, sequence: 12, receivedAt: 102 })).activeAlarm,
  null,
  'an older false frame must not clear the person latch',
)
outOfOrder.ingest(point({ person: 0, sequence: 13, receivedAt: 103 }))
assert.equal(
  outOfOrder.ingest(point({ person: 1, sequence: 14, receivedAt: 104 })).activeAlarm,
  'person',
  'a newer false frame must still allow the next rising edge',
)

const duplicateSources = createAlarmPopupQueue()
const duplicateHigh = point({ gas: 1, sequence: 20, receivedAt: 200 })
duplicateSources.ingest(duplicateHigh)
duplicateSources.dismiss()
assert.equal(
  duplicateSources.ingest({ ...duplicateHigh, gas: { alarm: 1 } }).activeAlarm,
  null,
  'the same frame arriving through HTTP and WebSocket must only be processed once',
)

const sequenceFallback = createAlarmPopupQueue()
sequenceFallback.ingest(point({ gas: 1, sequence: 31 }))
sequenceFallback.dismiss()
sequenceFallback.ingest(point({ gas: 0, sequence: 30 }))
assert.equal(
  sequenceFallback.ingest(point({ gas: 1, sequence: 32 })).activeAlarm,
  null,
  'sequence ordering must reject an old false frame when received_at is absent',
)

const receivedAtFallback = createAlarmPopupQueue()
receivedAtFallback.ingest(point({ gas: 1, receivedAt: 401 }))
receivedAtFallback.dismiss()
receivedAtFallback.ingest(point({ gas: 0, receivedAt: 400 }))
assert.equal(
  receivedAtFallback.ingest(point({ gas: 1, receivedAt: 402 })).activeAlarm,
  null,
  'received_at ordering must reject an old false frame when seq is absent',
)

const missingMetadata = createQueueWithoutPersonCooldown()
missingMetadata.ingest({ online: true, person_detected: null, gas: null })
assert.equal(
  missingMetadata.ingest({ online: true, person_detected: true }).activeAlarm,
  'person',
  'missing frame metadata must not block a normal rising edge',
)
missingMetadata.dismiss()
missingMetadata.ingest({ online: true, person_detected: false })
assert.equal(
  missingMetadata.ingest({ online: true, person_detected: true }).activeAlarm,
  'person',
  'telemetry without boot, sequence, or received_at must retain legacy edge behavior',
)

const partialFrame = createAlarmPopupQueue()
partialFrame.ingest(point({ person: null, gas: 0, sequence: 50, receivedAt: 500 }))
assert.equal(
  partialFrame.ingest(point({ person: 1, gas: null, sequence: 50, receivedAt: 500 })).activeAlarm,
  'person',
  'a partial duplicate must not mark an absent alarm field as already processed',
)

const reboot = createQueueWithoutPersonCooldown()
reboot.ingest(point({ person: 1, boot: 'boot-a', sequence: 100, receivedAt: 300 }))
reboot.dismiss()
reboot.ingest(point({ person: 0, boot: 'old-boot', sequence: 900, receivedAt: 299 }))
assert.equal(
  reboot.ingest(point({ person: 1, boot: 'boot-b', sequence: 1, receivedAt: 301 })).activeAlarm,
  'person',
  'a stale unknown boot must be ignored and a newer boot must reset ordering and latches',
)
reboot.dismiss()
reboot.ingest(point({ person: 0, boot: 'boot-a', sequence: 101, receivedAt: 299 }))
assert.equal(
  reboot.ingest(point({ person: 1, boot: 'boot-b', sequence: 2, receivedAt: 302 })).activeAlarm,
  null,
  'a delayed frame from a retired boot must not reset the active boot latch',
)

let popupNow = 1_000_000
const popupCooldown = createAlarmPopupQueue(undefined, { now: () => popupNow })
popupCooldown.ingest(point({ person: 1, sequence: 200, receivedAt: 500 }))
popupCooldown.dismiss()
popupCooldown.ingest(point({ person: 0, sequence: 201, receivedAt: 501 }))
popupNow += 29_999
assert.equal(
  popupCooldown.ingest(point({ person: 1, sequence: 202, receivedAt: 502 })).activeAlarm,
  null,
  'repeated survivor detections within 30 seconds must not open another popup',
)
popupCooldown.ingest(point({ person: 0, sequence: 203, receivedAt: 503 }))
popupNow += 1
assert.equal(
  popupCooldown.ingest(point({ person: 1, sequence: 204, receivedAt: 504 })).activeAlarm,
  'person',
  'a survivor detection at the 30-second boundary may open a new popup',
)

console.log('alarm popup queue: all assertions passed')
