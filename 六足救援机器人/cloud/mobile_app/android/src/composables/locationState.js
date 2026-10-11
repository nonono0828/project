export function hasValidLocationCoordinates(point) {
  return point
    && Number.isFinite(point.lat)
    && Number.isFinite(point.lng)
    && point.lat >= -90 && point.lat <= 90
    && point.lng >= -180 && point.lng <= 180
}

export function servingCellIdentity(cells) {
  const serving = Array.isArray(cells)
    ? cells.find((cell) => cell?.serving === true && cell?.cell_id != null)
    : null
  if (!serving) return ''

  const parts = [serving.mcc, serving.mnc, serving.tac, serving.cell_id]
  if (parts.some((part) => part == null || part === '')) return ''
  return parts.join(':')
}

export function appendLocationTrajectory(points, point, maxPoints = 100) {
  const trail = Array.isArray(points) ? points : []
  if (!hasValidLocationCoordinates(point)) return trail

  const nextPoint = [point.lat, point.lng]
  const previous = trail.at(-1)
  const duplicateCellFallback = point.source === 'cell'
    && previous
    && previous[0] === nextPoint[0]
    && previous[1] === nextPoint[1]
  if (duplicateCellFallback) return trail

  const boundedMax = Number.isInteger(maxPoints) && maxPoints > 0 ? maxPoints : 100
  return [...trail, nextPoint].slice(-boundedMax)
}
