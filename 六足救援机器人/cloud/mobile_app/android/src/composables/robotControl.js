export const ROBOT_CONTROL_LIMITS = Object.freeze({
  body_height: Object.freeze({ min: -30, max: 70, defaultValue: 0 }),
  speed_percent: Object.freeze({ min: 30, max: 100, defaultValue: 100 }),
})

export const ROBOT_CONTROL_TOGGLES = new Set(['terrain', 'autonomous'])

export function validateRobotControl(control, value) {
  const range = ROBOT_CONTROL_LIMITS[control]
  if (range) {
    if (!Number.isInteger(value) || value < range.min || value > range.max) return null
    return { control, value }
  }
  if (ROBOT_CONTROL_TOGGLES.has(control) && typeof value === 'boolean') {
    return { control, value }
  }
  return null
}

export function robotControlRequestMatchesDevice(request, reportedValue) {
  return request?.pending === true
    && request.status === 'waiting'
    && reportedValue === request.value
}

export function acceptRobotControlRequest(request) {
  if (request?.pending !== true) return null
  return { ...request, status: 'waiting' }
}

export function resolvedRobotModeChange(control, request, reportedValue) {
  if (!ROBOT_CONTROL_TOGGLES.has(control)) return null
  if (!robotControlRequestMatchesDevice(request, reportedValue)) return null
  if (typeof request.previous_value !== 'boolean' || request.previous_value === request.value) return null
  return {
    control,
    enabled: request.value,
    requestId: request.request_id,
  }
}
