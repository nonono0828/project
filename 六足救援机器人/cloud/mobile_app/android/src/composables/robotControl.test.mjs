import test from 'node:test'
import assert from 'node:assert/strict'
import {
  acceptRobotControlRequest,
  resolvedRobotModeChange,
  robotControlRequestMatchesDevice,
  validateRobotControl,
} from './robotControl.js'

test('robot control only accepts the documented absolute ranges', () => {
  assert.deepEqual(validateRobotControl('body_height', -30), { control: 'body_height', value: -30 })
  assert.deepEqual(validateRobotControl('body_height', 70), { control: 'body_height', value: 70 })
  assert.equal(validateRobotControl('body_height', -31), null)
  assert.equal(validateRobotControl('speed_percent', 29), null)
  assert.deepEqual(validateRobotControl('speed_percent', 100), { control: 'speed_percent', value: 100 })
})

test('robot control toggles require explicit booleans', () => {
  assert.deepEqual(validateRobotControl('terrain', true), { control: 'terrain', value: true })
  assert.deepEqual(validateRobotControl('autonomous', false), { control: 'autonomous', value: false })
  assert.equal(validateRobotControl('radar', true), null)
  assert.equal(validateRobotControl('terrain', 1), null)
  assert.equal(validateRobotControl('unknown', true), null)
})

test('a robot control request completes only after ACK acceptance and matching device state', () => {
  const request = {
    pending: true,
    status: 'waiting',
    request_id: 'mode-1',
    value: true,
    previous_value: false,
  }
  assert.equal(robotControlRequestMatchesDevice(request, true), true)
  assert.equal(robotControlRequestMatchesDevice({ ...request, status: 'queued' }, true), false)
  assert.equal(robotControlRequestMatchesDevice({ ...request, pending: false }, true), false)
  assert.equal(robotControlRequestMatchesDevice(request, false), false)
})

test('mode events require a real terrain or autonomous state transition', () => {
  const request = {
    pending: true,
    status: 'waiting',
    request_id: 'mode-2',
    value: true,
    previous_value: false,
  }
  assert.deepEqual(resolvedRobotModeChange('terrain', request, true), {
    control: 'terrain',
    enabled: true,
    requestId: 'mode-2',
  })
  assert.equal(resolvedRobotModeChange('autonomous', { ...request, status: 'queued' }, true), null)
  assert.equal(resolvedRobotModeChange('autonomous', { ...request, previous_value: true }, true), null)
  assert.equal(resolvedRobotModeChange('autonomous', request, false), null)
  assert.equal(resolvedRobotModeChange('radar', request, true), null)
  assert.equal(resolvedRobotModeChange('terrain', { ...request, pending: false }, true), null)
})

test('device state before ACK is reconciled when the ACK is accepted', () => {
  const queued = {
    pending: true,
    status: 'queued',
    request_id: 'state-first',
    value: true,
    previous_value: false,
  }
  const reportedBeforeAck = true
  assert.equal(resolvedRobotModeChange('terrain', queued, reportedBeforeAck), null)

  const accepted = acceptRobotControlRequest(queued)
  assert.deepEqual(resolvedRobotModeChange('terrain', accepted, reportedBeforeAck), {
    control: 'terrain',
    enabled: true,
    requestId: 'state-first',
  })
})

test('ACK before device state waits for the matching state and records once', () => {
  const accepted = acceptRobotControlRequest({
    pending: true,
    status: 'queued',
    request_id: 'ack-first',
    value: false,
    previous_value: true,
  })
  assert.equal(resolvedRobotModeChange('autonomous', accepted, true), null)
  assert.deepEqual(resolvedRobotModeChange('autonomous', accepted, false), {
    control: 'autonomous',
    enabled: false,
    requestId: 'ack-first',
  })
  assert.equal(resolvedRobotModeChange('autonomous', { ...accepted, pending: false }, false), null)
})
