import test from 'node:test'
import assert from 'node:assert/strict'
import {
  createAlarmEventTracker,
  GAS_EVENT,
  SURVIVOR_EVENT,
} from './alarmEventTracker.js'

function legacyPoint(overrides = {}) {
  return {
    online: true,
    device_id: 'esp32s3-01',
    person_detected: 0,
    gas: { alarm: false, concentration: 0 },
    ...overrides,
  }
}

test('legacy person telemetry records one event per rising edge without boot_id or seq', () => {
  const recorded = []
  const tracker = createAlarmEventTracker((type) => recorded.push(type))

  tracker.ingest(legacyPoint({ person_detected: 1 }))
  tracker.ingest(legacyPoint({ person_detected: 1 }))
  assert.deepEqual(recorded, [SURVIVOR_EVENT])

  tracker.ingest(legacyPoint({ person_detected: 0 }))
  tracker.ingest(legacyPoint({ person_detected: 1 }))
  assert.deepEqual(recorded, [SURVIVOR_EVENT, SURVIVOR_EVENT])
})

test('legacy gas telemetry records one event per rising edge without boot_id or seq', () => {
  const recorded = []
  const tracker = createAlarmEventTracker((type) => recorded.push(type))

  tracker.ingest(legacyPoint({ gas: { alarm: true, concentration: 9 } }))
  tracker.ingest(legacyPoint({ gas: { alarm: true, concentration: 10 } }))
  assert.deepEqual(recorded, [GAS_EVENT])

  tracker.ingest(legacyPoint({ gas: { alarm: false, concentration: 1 } }))
  tracker.ingest(legacyPoint({ gas: { alarm: true, concentration: 11 } }))
  assert.deepEqual(recorded, [GAS_EVENT, GAS_EVENT])
})

test('a complete telemetry frame is de-duplicated', () => {
  const recorded = []
  const tracker = createAlarmEventTracker((type) => recorded.push(type))
  const point = legacyPoint({
    boot_id: 'boot-a',
    seq: 7,
    received_at: 100,
    person_detected: 1,
    gas: { alarm: true },
  })

  tracker.ingest(point)
  tracker.ingest(point)
  assert.deepEqual(recorded, [SURVIVOR_EVENT, GAS_EVENT])
})

test('partial copies of the same frame can independently provide person and gas alarms', () => {
  const recorded = []
  const tracker = createAlarmEventTracker((type) => recorded.push(type))
  const metadata = {
    online: true,
    device_id: 'esp32s3-01',
    boot_id: 'boot-partial',
    seq: 8,
    received_at: 101,
  }

  tracker.ingest({ ...metadata, person_detected: null, gas: { alarm: true } })
  tracker.ingest({ ...metadata, person_detected: true, gas: null })
  assert.deepEqual(recorded, [GAS_EVENT, SURVIVOR_EVENT])
})

test('an out-of-order false frame cannot unlock a newer alarm', () => {
  const recorded = []
  const tracker = createAlarmEventTracker((type) => recorded.push(type))

  tracker.ingest(legacyPoint({ boot_id: 'boot-order', seq: 11, received_at: 111, person_detected: true }))
  tracker.ingest(legacyPoint({ boot_id: 'boot-order', seq: 10, received_at: 110, person_detected: false }))
  tracker.ingest(legacyPoint({ boot_id: 'boot-order', seq: 12, received_at: 112, person_detected: true }))
  assert.deepEqual(recorded, [SURVIVOR_EVENT])
})

test('a cleared survivor signal records the next rising edge immediately', () => {
  const recorded = []
  const tracker = createAlarmEventTracker((type) => recorded.push(type))

  tracker.ingest(legacyPoint({ person_detected: 1 }))
  tracker.ingest(legacyPoint({ person_detected: 0 }))
  tracker.ingest(legacyPoint({ person_detected: 1 }))
  assert.deepEqual(recorded, [SURVIVOR_EVENT, SURVIVOR_EVENT])
})
