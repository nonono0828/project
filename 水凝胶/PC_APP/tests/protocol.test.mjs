import test from 'node:test';
import assert from 'node:assert/strict';
import { parseDeviceLine } from '../src/protocol.js';
import { POSES } from '../src/gestures.js';

test('parses device identity', () => {
  assert.deepEqual(parseDeviceLine('DEVICE,GESTURE3X3,1\r\n'), {
    type: 'device', name: 'GESTURE3X3', version: '1', raw: 'DEVICE,GESTURE3X3,1'
  });
});

test('parses key down and sanitizes pressure', () => {
  assert.deepEqual(parseDeviceLine('KEY,4,DOWN,1250'), {
    type: 'key', key: 4, action: 'DOWN', pressure: 1250, raw: 'KEY,4,DOWN,1250'
  });
  assert.equal(parseDeviceLine('KEY,10,DOWN,2'), null);
  assert.equal(parseDeviceLine('KEY,1,HOLD,2'), null);
});

test('parses heartbeat and rejects malformed values', () => {
  assert.deepEqual(parseDeviceLine('HEARTBEAT,0,84'), {
    type: 'heartbeat', key: 0, pressure: 84, raw: 'HEARTBEAT,0,84'
  });
  assert.equal(parseDeviceLine('HEARTBEAT,NOPE,84'), null);
  assert.equal(parseDeviceLine(''), null);
});

test('defines nine distinct, complete hand poses', () => {
  assert.deepEqual(Object.keys(POSES), ['1', '2', '3', '4', '5', '6', '7', '8', '9']);
  const signatures = Object.values(POSES).map((pose) => JSON.stringify([
    pose.index, pose.middle, pose.ring, pose.pinky, pose.thumb, pose.thumbSpread
  ]));
  assert.equal(new Set(signatures).size, 9);
  for (const pose of Object.values(POSES)) {
    for (const fingerName of ['index', 'middle', 'ring', 'pinky', 'thumb']) {
      assert.equal(pose[fingerName].length, 3);
    }
  }
});
