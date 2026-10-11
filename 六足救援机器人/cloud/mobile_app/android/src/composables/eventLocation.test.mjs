import assert from 'node:assert/strict'
import {
  cellEventLocation,
  gpsEventLocation,
  normalizeEventLocation,
} from './eventLocation.js'

assert.deepEqual(
  gpsEventLocation({ online: true, lat: 31.2304, lng: 121.4737 }),
  { lat: 31.2304, lng: 121.4737, source: 'gps' },
)
assert.equal(
  gpsEventLocation({ online: false, lat: 31.2304, lng: 121.4737 }),
  null,
  'offline GPS coordinates must not be recorded',
)
assert.equal(
  normalizeEventLocation({ lat: 91, lng: 121.4737 }),
  null,
  'out-of-range coordinates must be rejected',
)

const responseWithCellFallback = {
  data: {
    selected: { lat: 31.2, lng: 121.4, source: 'cell', accuracy: 600 },
    cell: { lat: 31.2, lng: 121.4, accuracy: 600 },
  },
}
assert.deepEqual(
  cellEventLocation(responseWithCellFallback),
  { lat: 31.2, lng: 121.4, accuracy: 600, source: 'cell' },
)

const responseWithLaterGpsOnly = {
  data: {
    selected: { lat: 30.0, lng: 120.0, source: 'gps' },
    cell: null,
  },
}
assert.equal(
  cellEventLocation(responseWithLaterGpsOnly),
  null,
  'a later GPS fix must not be attached to the survivor event',
)

console.log('event location: all assertions passed')
