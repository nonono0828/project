import assert from 'node:assert/strict'
import {
  appendLocationTrajectory,
  hasValidLocationCoordinates,
  servingCellIdentity,
} from './locationState.js'

assert.equal(hasValidLocationCoordinates({ lat: 23.1, lng: 113.2 }), true)
assert.equal(hasValidLocationCoordinates({ lat: 91, lng: 113.2 }), false)

assert.equal(
  servingCellIdentity([
    { serving: false, mcc: 460, mnc: 0, tac: 9440, cell_id: 1 },
    { serving: true, mcc: 460, mnc: 0, tac: 9440, cell_id: 87064963 },
  ]),
  '460:0:9440:87064963',
)
assert.equal(servingCellIdentity([{ serving: true, cell_id: 1 }]), '')

const first = appendLocationTrajectory([], { lat: 23.1, lng: 113.2, source: 'cell' })
const duplicate = appendLocationTrajectory(first, { lat: 23.1, lng: 113.2, source: 'cell' })
assert.equal(duplicate, first, 'unchanged cell fallback must not create a new map update')
assert.deepEqual(
  appendLocationTrajectory(first, { lat: 23.2, lng: 113.3, source: 'gps' }),
  [[23.1, 113.2], [23.2, 113.3]],
)

console.log('location state: all assertions passed')
