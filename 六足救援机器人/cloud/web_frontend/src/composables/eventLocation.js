export function normalizeEventLocation(point, requireOnline = false) {
  if (!point || (requireOnline && point.online !== true)) return null
  if (!Number.isFinite(point.lat) || !Number.isFinite(point.lng)) return null
  if (point.lat < -90 || point.lat > 90 || point.lng < -180 || point.lng > 180) return null

  const location = { lat: point.lat, lng: point.lng }
  if (typeof point.source === 'string') location.source = point.source
  if (Number.isFinite(point.accuracy)) location.accuracy = point.accuracy
  return location
}

export function gpsEventLocation(point) {
  const location = normalizeEventLocation(point, true)
  return location ? { ...location, source: 'gps' } : null
}

export function cellEventLocation(response) {
  // Use the explicit cell result, not `selected`, because a later GPS fix must
  // not be attached to an earlier survivor-detection frame.
  const location = normalizeEventLocation(response?.data?.cell)
  return location ? { ...location, source: 'cell' } : null
}
