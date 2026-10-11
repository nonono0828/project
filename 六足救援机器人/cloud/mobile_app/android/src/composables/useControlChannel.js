/** Shared control channel. A single WebSocket serves controls and device state. */
import { ref, computed } from 'vue'
import cfg from '@/config/config'
import { getWsClient } from '@/api/websocket'
import {
  acceptRobotControlRequest,
  resolvedRobotModeChange,
  robotControlRequestMatchesDevice,
  validateRobotControl,
} from '@/composables/robotControl'
import { recordRobotModeEvent } from '@/composables/useEventStore'

const connected = ref(false)
const deviceOnline = ref(null)
const lastAck = ref(null)
const apRequest = ref({
  pending: false,
  request_id: null,
  desired_enabled: null,
  status: 'idle',
  error: null,
})
const apStream = ref({
  supported: null,
  state: 'unknown',
  request_id: null,
  error: null,
})
const modemRequest = ref({
  pending: false,
  request_id: null,
  action: null,
  description: null,
  record_event: false,
  status: 'idle',
  error: null,
})
const modem4g = ref({
  supported: null,
  state: 'unknown',
  action: 'query',
  request_id: null,
  error: null,
  operator: null,
  registration: null,
  rssi: null,
  rat: null,
  band_config: '',
  cell_lock: '',
  cells: [],
})
const videoFpsRequest = ref({
  pending: false,
  request_id: null,
  fps: null,
  resolution: null,
  status: 'idle',
  error: null,
})
const videoFps = ref({
  supported: null,
  fps: null,
  resolution: null,
  width: null,
  height: null,
  request_id: null,
  error: null,
})
const wifiRequest = ref({
  pending: false,
  request_id: null,
  action: null,
  description: null,
  record_event: false,
  status: 'idle',
  error: null,
})
const wifiSta = ref({
  supported: null,
  state: 'unknown',
  action: 'query',
  request_id: null,
  error: null,
  feature_enabled: false,
  scanning: false,
  connected: false,
  ssid: '',
  ip: '',
  rssi: null,
  wifi_uplink_selected: false,
  active_uplink: 'none',
  networks: [],
})
const robotControl = ref({
  supported: null,
  body_height: null,
  speed_percent: null,
  terrain: null,
  autonomous: null,
  radar: null,
  radar_ready: null,
  avoidance_enabled: null,
  received_at: null,
})
const robotControlRequest = ref({})

let client = null
let started = false
let requestCounter = 0
let apRequestTimer = null
let modemRequestTimer = null
let videoFpsRequestTimer = null
let wifiRequestTimer = null
const robotControlTimers = new Map()

function clearApRequestTimer() {
  if (!apRequestTimer) return
  clearTimeout(apRequestTimer)
  apRequestTimer = null
}

function setApRequest(value) {
  apRequest.value = {
    pending: value.pending ?? false,
    request_id: value.request_id ?? null,
    desired_enabled: value.desired_enabled ?? null,
    status: value.status ?? 'idle',
    error: value.error ?? null,
  }
}

function resetApRequest() {
  clearApRequestTimer()
  setApRequest({})
}

function failApRequest(status, error) {
  clearApRequestTimer()
  setApRequest({
    pending: false,
    request_id: apRequest.value.request_id,
    desired_enabled: apRequest.value.desired_enabled,
    status,
    error,
  })
}

function unknownApState() {
  apStream.value = {
    supported: null,
    state: 'unknown',
    request_id: null,
    error: null,
  }
}

function clearModemRequestTimer() {
  if (!modemRequestTimer) return
  clearTimeout(modemRequestTimer)
  modemRequestTimer = null
}

function setModemRequest(value) {
  modemRequest.value = {
    pending: value.pending ?? false,
    request_id: value.request_id ?? null,
    action: value.action ?? null,
    description: value.description ?? null,
    record_event: value.record_event ?? false,
    status: value.status ?? 'idle',
    error: value.error ?? null,
  }
}

function failModemRequest(status, error) {
  clearModemRequestTimer()
  setModemRequest({
    ...modemRequest.value,
    pending: false,
    status,
    error,
  })
}

function unknownModemState() {
  modem4g.value = {
    supported: null,
    state: 'unknown',
    action: 'query',
    request_id: null,
    error: null,
    operator: null,
    registration: null,
    rssi: null,
    rat: null,
    band_config: '',
    cell_lock: '',
    cells: [],
  }
}

function clearVideoFpsTimer() {
  if (!videoFpsRequestTimer) return
  clearTimeout(videoFpsRequestTimer)
  videoFpsRequestTimer = null
}

function setVideoFpsRequest(value) {
  videoFpsRequest.value = {
    pending: value.pending ?? false,
    request_id: value.request_id ?? null,
    fps: value.fps ?? null,
    resolution: value.resolution ?? null,
    status: value.status ?? 'idle',
    error: value.error ?? null,
  }
}

function failVideoFpsRequest(status, error) {
  clearVideoFpsTimer()
  setVideoFpsRequest({ ...videoFpsRequest.value, pending: false, status, error })
}

function unknownVideoFpsState() {
  videoFps.value = {
    supported: null,
    fps: null,
    resolution: null,
    width: null,
    height: null,
    request_id: null,
    error: null,
  }
}

function clearWifiRequestTimer() {
  if (!wifiRequestTimer) return
  clearTimeout(wifiRequestTimer)
  wifiRequestTimer = null
}

function setWifiRequest(value) {
  wifiRequest.value = {
    pending: value.pending ?? false,
    request_id: value.request_id ?? null,
    action: value.action ?? null,
    description: value.description ?? null,
    record_event: value.record_event ?? false,
    status: value.status ?? 'idle',
    error: value.error ?? null,
  }
}

function failWifiRequest(status, error) {
  clearWifiRequestTimer()
  setWifiRequest({ ...wifiRequest.value, pending: false, status, error })
}

function unknownWifiState() {
  wifiSta.value = {
    supported: null,
    state: 'unknown',
    action: 'query',
    request_id: null,
    error: null,
    feature_enabled: false,
    scanning: false,
    connected: false,
    ssid: '',
    ip: '',
    rssi: null,
    wifi_uplink_selected: false,
    active_uplink: 'none',
    networks: [],
  }
}

function unknownRobotControlState() {
  robotControl.value = {
    supported: null,
    body_height: null,
    speed_percent: null,
    terrain: null,
    autonomous: null,
    radar: null,
    radar_ready: null,
    avoidance_enabled: null,
    received_at: null,
  }
}

function clearRobotControlTimer(control) {
  const timer = robotControlTimers.get(control)
  if (timer) clearTimeout(timer)
  robotControlTimers.delete(control)
}

function clearRobotControlRequest(control, status = 'idle', error = null) {
  clearRobotControlTimer(control)
  const next = { ...robotControlRequest.value }
  if (status === 'idle') delete next[control]
  else next[control] = { ...next[control], pending: false, status, error }
  robotControlRequest.value = next
}

function clearAllRobotControlRequests() {
  for (const control of [...robotControlTimers.keys()]) clearRobotControlTimer(control)
  robotControlRequest.value = {}
}

function normalizeRobotControlState(value) {
  if (!value || typeof value !== 'object') return null
  const inRange = (candidate, min, max) => (
    Number.isInteger(candidate) && candidate >= min && candidate <= max ? candidate : null
  )
  const boolOrNull = (candidate) => typeof candidate === 'boolean' ? candidate : null
  return {
    supported: boolOrNull(value.supported),
    body_height: inRange(value.body_height, -30, 70),
    speed_percent: inRange(value.speed_percent, 30, 100),
    terrain: boolOrNull(value.terrain),
    autonomous: boolOrNull(value.autonomous),
    radar: boolOrNull(value.radar),
    radar_ready: boolOrNull(value.radar_ready),
    avoidance_enabled: boolOrNull(value.avoidance_enabled),
    received_at: value.received_at ?? null,
  }
}

function normalizeApState(value) {
  if (!value || typeof value !== 'object') return null
  const allowedStates = new Set(['starting', 'stopping', 'enabled', 'disabled', 'error'])
  return {
    supported: typeof value.supported === 'boolean' ? value.supported : null,
    state: allowedStates.has(value.state) ? value.state : 'unknown',
    request_id: value.request_id ?? null,
    error: value.error ?? null,
    received_at: value.received_at ?? null,
  }
}

function normalizeModemState(value) {
  if (!value || typeof value !== 'object') return null
  const allowedStates = new Set([
    'unsupported', 'idle', 'querying', 'applying', 'success', 'error',
  ])
  const cells = Array.isArray(value.cells)
    ? value.cells.filter((cell) => cell && typeof cell === 'object').slice(0, 6)
    : []
  return {
    supported: typeof value.supported === 'boolean' ? value.supported : null,
    state: allowedStates.has(value.state) ? value.state : 'unknown',
    action: value.action ?? 'query',
    request_id: value.request_id ?? null,
    error: value.error ?? null,
    operator: value.operator ?? null,
    registration: Number.isInteger(value.registration) ? value.registration : null,
    rssi: Number.isInteger(value.rssi) ? value.rssi : null,
    rat: value.rat ?? null,
    band_config: value.band_config ?? '',
    cell_lock: value.cell_lock ?? '',
    cells,
    received_at: value.received_at ?? null,
  }
}

function normalizeWifiState(value) {
  if (!value || typeof value !== 'object') return null
  const allowedStates = new Set([
    'unknown', 'unsupported', 'idle', 'working', 'applying', 'success', 'error',
  ])
  const allowedUplinks = new Set(['none', 'l610', 'wifi'])
  const networks = Array.isArray(value.networks)
    ? value.networks
      .filter((network) => network && typeof network === 'object')
      .slice(0, 10)
      .map((network) => ({
        ssid: typeof network.ssid === 'string' ? network.ssid : '',
        rssi: Number.isInteger(network.rssi) ? network.rssi : null,
        channel: Number.isInteger(network.channel) ? network.channel : null,
        security: typeof network.security === 'string' ? network.security : '',
        secured: network.secured !== false,
        supported: network.supported !== false,
      }))
    : []
  return {
    supported: typeof value.supported === 'boolean' ? value.supported : null,
    state: allowedStates.has(value.state) ? value.state : 'unknown',
    action: typeof value.action === 'string' ? value.action : 'query',
    request_id: value.request_id ?? null,
    error: value.error ?? null,
    feature_enabled: value.feature_enabled === true,
    scanning: value.scanning === true,
    connected: value.connected === true,
    ssid: typeof value.ssid === 'string' ? value.ssid : '',
    ip: typeof value.ip === 'string' ? value.ip : '',
    rssi: Number.isInteger(value.rssi) ? value.rssi : null,
    wifi_uplink_selected: value.wifi_uplink_selected === true,
    active_uplink: allowedUplinks.has(value.active_uplink) ? value.active_uplink : 'none',
    networks,
    received_at: value.received_at ?? null,
  }
}

function reconcileRobotControlRequest(control, reportedValue) {
  const request = robotControlRequest.value[control]
  if (!robotControlRequestMatchesDevice(request, reportedValue)) return false
  const modeChange = resolvedRobotModeChange(control, request, reportedValue)
  if (modeChange) recordRobotModeEvent(modeChange)
  clearRobotControlRequest(control, 'success')
  return true
}

function handleMessage(raw) {
  let message
  try {
    message = JSON.parse(raw)
  } catch {
    return
  }

  if (message?.type === 'command_ack') {
    lastAck.value = message
    const ackRequestId = message.requestId ?? message.request_id ?? null
    if (apRequest.value.pending && ackRequestId === apRequest.value.request_id) {
      if (message.accepted === false) {
        failApRequest('rejected', message.message || '服务器拒绝请求')
      } else if (message.accepted === true) {
        setApRequest({ ...apRequest.value, status: 'waiting' })
      }
    }
    if (modemRequest.value.pending && ackRequestId === modemRequest.value.request_id) {
      if (message.accepted === false) {
        failModemRequest('rejected', message.message || '服务器拒绝请求')
      } else if (message.accepted === true) {
        setModemRequest({ ...modemRequest.value, status: 'waiting' })
      }
    }
    if (videoFpsRequest.value.pending && ackRequestId === videoFpsRequest.value.request_id) {
      if (message.accepted === false) {
        failVideoFpsRequest('rejected', message.message || '服务器拒绝请求')
      } else if (message.accepted === true) {
        setVideoFpsRequest({ ...videoFpsRequest.value, status: 'waiting' })
      }
    }
    if (wifiRequest.value.pending && ackRequestId === wifiRequest.value.request_id) {
      if (message.accepted === false) {
        failWifiRequest('rejected', message.message || '服务器拒绝请求')
      } else if (message.accepted === true) {
        setWifiRequest({ ...wifiRequest.value, status: 'waiting' })
      }
    }
    for (const [control, request] of Object.entries(robotControlRequest.value)) {
      if (!request.pending || ackRequestId !== request.request_id) continue
      if (message.accepted === false) clearRobotControlRequest(control, 'rejected', message.message || '命令被拒绝')
      else if (message.accepted === true) {
        const acceptedRequest = acceptRobotControlRequest(request)
        if (!acceptedRequest) continue
        robotControlRequest.value = {
          ...robotControlRequest.value,
          [control]: acceptedRequest,
        }
        // Device state may arrive before the server ACK. Reconcile immediately
        // so both message orders require the same two confirmations.
        reconcileRobotControlRequest(control, robotControl.value[control])
      }
    }
  }
  if (message?.type === 'device_state' && typeof message.online === 'boolean') {
    deviceOnline.value = message.online
    if (message.online === false) resetApRequest()
    if (message.online === false && wifiRequest.value.pending) {
      failWifiRequest('disconnected', '设备已离线')
    }
    if (message.online === false) {
      for (const control of Object.keys(robotControlRequest.value)) clearRobotControlRequest(control)
      unknownRobotControlState()
    }
  }

  const reported = message?.type === 'device_state'
    ? message.ap_stream
    : message?.device_state?.ap_stream
  const normalized = normalizeApState(reported)
  if (normalized) {
    apStream.value = normalized
    const finalState = normalized.state === 'enabled'
      || normalized.state === 'disabled'
      || normalized.state === 'error'
    if (
      normalized.request_id === apRequest.value.request_id
      && finalState
    ) {
      // A final device heartbeat can arrive after the local timeout. Reconcile
      // it by request ID so a stale starting/stopping state cannot lock the UI.
      resetApRequest()
    }
  }

  const modemReported = message?.type === 'device_state'
    ? message.modem_4g
    : message?.device_state?.modem_4g
  const normalizedModem = normalizeModemState(modemReported)
  if (normalizedModem) {
    modem4g.value = normalizedModem
    if (normalizedModem.request_id === modemRequest.value.request_id) {
      if (normalizedModem.state === 'success') {
        clearModemRequestTimer()
        setModemRequest({
          ...modemRequest.value,
          pending: false,
          status: 'success',
          error: null,
        })
      } else if (normalizedModem.state === 'error') {
        failModemRequest('error', normalizedModem.error || '设备未能应用设置')
      }
    }
  }
  const videoReported = message?.type === 'device_state'
    ? message.video_fps
    : message?.device_state?.video_fps
  if (videoReported && typeof videoReported === 'object') {
    const allowedResolutions = new Set(['640x480', '1280x720', '1920x1080'])
    const reportedWidth = Number.isInteger(videoReported.width) ? videoReported.width : null
    const reportedHeight = Number.isInteger(videoReported.height) ? videoReported.height : null
    const inferredResolution = reportedWidth && reportedHeight
      ? `${reportedWidth}x${reportedHeight}`
      : null
    videoFps.value = {
      supported: typeof videoReported.supported === 'boolean' ? videoReported.supported : null,
      fps: Number.isInteger(videoReported.fps) ? videoReported.fps : null,
      resolution: allowedResolutions.has(videoReported.resolution)
        ? videoReported.resolution
        : (allowedResolutions.has(inferredResolution) ? inferredResolution : null),
      width: reportedWidth,
      height: reportedHeight,
      request_id: videoReported.request_id ?? null,
      error: videoReported.error ?? null,
      received_at: videoReported.received_at ?? null,
    }
    if (videoFps.value.request_id === videoFpsRequest.value.request_id) {
      clearVideoFpsTimer()
      if (videoFps.value.error) {
        failVideoFpsRequest('error', videoFps.value.error)
      } else if (
        videoFps.value.fps === videoFpsRequest.value.fps
        && (
          videoFpsRequest.value.resolution == null
          || videoFps.value.resolution === videoFpsRequest.value.resolution
        )
      ) {
        setVideoFpsRequest({ ...videoFpsRequest.value, pending: false, status: 'success', error: null })
      }
    }
  }
  const wifiReported = message?.type === 'device_state'
    ? message.wifi_sta
    : message?.device_state?.wifi_sta
  const normalizedWifi = normalizeWifiState(wifiReported)
  if (normalizedWifi) {
    wifiSta.value = normalizedWifi
    if (normalizedWifi.request_id === wifiRequest.value.request_id) {
      if (normalizedWifi.state === 'success') {
        clearWifiRequestTimer()
        setWifiRequest({
          ...wifiRequest.value,
          pending: false,
          status: 'success',
          error: null,
        })
      } else if (normalizedWifi.state === 'error') {
        failWifiRequest('error', normalizedWifi.error || '设备未能完成 Wi-Fi 操作')
      }
    }
  }

  const robotControlReported = message?.type === 'device_state'
    ? message.robot_control
    : message?.device_state?.robot_control
  const normalizedRobotControl = normalizeRobotControlState(robotControlReported)
  if (normalizedRobotControl) {
    robotControl.value = normalizedRobotControl
    for (const control of Object.keys(robotControlRequest.value)) {
      reconcileRobotControlRequest(control, normalizedRobotControl[control])
    }
  }
}

function connect() {
  if (!cfg.WS_MAIN) return
  client = getWsClient('control', cfg.WS_MAIN, {
    onMessage: handleMessage,
    onOpen() {
      connected.value = true
      deviceOnline.value = null
      resetApRequest()
      unknownApState()
      unknownModemState()
      unknownVideoFpsState()
      unknownWifiState()
      unknownRobotControlState()
      clearAllRobotControlRequests()
    },
    onClose() {
      connected.value = false
      deviceOnline.value = null
      resetApRequest()
      unknownApState()
      if (modemRequest.value.pending) failModemRequest('disconnected', '控制连接已断开')
      unknownModemState()
      if (videoFpsRequest.value.pending) failVideoFpsRequest('disconnected', '控制连接已断开')
      unknownVideoFpsState()
      if (wifiRequest.value.pending) failWifiRequest('disconnected', '控制连接已断开')
      unknownWifiState()
      unknownRobotControlState()
      clearAllRobotControlRequests()
    },
    onError() {
      connected.value = false
      deviceOnline.value = null
      resetApRequest()
      unknownApState()
      if (modemRequest.value.pending) failModemRequest('disconnected', '控制连接已断开')
      unknownModemState()
      if (videoFpsRequest.value.pending) failVideoFpsRequest('disconnected', '控制连接已断开')
      unknownVideoFpsState()
      if (wifiRequest.value.pending) failWifiRequest('disconnected', '控制连接已断开')
      unknownWifiState()
      unknownRobotControlState()
      clearAllRobotControlRequests()
    },
  })
  client.connect()
}

function makeRequestId() {
  requestCounter = (requestCounter + 1) >>> 0
  return `web-${Date.now().toString(36)}-${requestCounter.toString(36)}`
}

function sendCommand(cmd, params = {}) {
  if (!connected.value || !client) return null
  const requestId = makeRequestId()
  const sent = client.send({ type: 'command_request', cmd, params, requestId })
  return sent ? requestId : null
}

function requestRobotControl(control, value) {
  const params = validateRobotControl(control, value)
  if (!params || !connected.value || !client) return null
  const previousValue = robotControl.value[control]
  if (typeof value === 'boolean' && previousValue === value) return null
  const requestId = makeRequestId()
  if (!client.send({ type: 'command_request', cmd: 'robot_control', params, requestId })) return null

  clearRobotControlTimer(control)
  robotControlRequest.value = {
    ...robotControlRequest.value,
    [control]: {
      pending: true,
      request_id: requestId,
      value,
      previous_value: previousValue,
      status: 'queued',
      error: null,
    },
  }
  robotControlTimers.set(control, setTimeout(() => {
    const request = robotControlRequest.value[control]
    if (request?.pending && request.request_id === requestId) {
      clearRobotControlRequest(control, 'timeout', '设备确认超时')
    }
  }, 5000))
  return requestId
}

function requestApStream(enabled) {
  if (!connected.value || !client || apRequest.value.pending) return null
  const requestId = makeRequestId()
  const desiredEnabled = enabled === true
  const sent = client.send({
    type: 'command_request',
    cmd: 'ap_stream',
    params: { enabled: desiredEnabled },
    requestId,
  })
  if (!sent) return null

  clearApRequestTimer()
  setApRequest({
    pending: true,
    request_id: requestId,
    desired_enabled: desiredEnabled,
    status: 'queued',
    error: null,
  })
  apRequestTimer = setTimeout(() => {
    if (apRequest.value.pending && apRequest.value.request_id === requestId) {
      failApRequest('timeout', '设备确认超时')
    }
  }, 7000)
  return requestId
}

function requestModem4g(params, description = '4G 操作', recordEvent = true) {
  if (!connected.value || !client || modemRequest.value.pending) return null
  if (!params || typeof params !== 'object' || typeof params.action !== 'string') return null
  const requestId = makeRequestId()
  const sent = client.send({
    type: 'command_request',
    cmd: 'modem_4g',
    params,
    requestId,
  })
  if (!sent) return null

  clearModemRequestTimer()
  setModemRequest({
    pending: true,
    request_id: requestId,
    action: params.action,
    description,
    record_event: recordEvent,
    status: 'queued',
    error: null,
  })
  modemRequestTimer = setTimeout(() => {
    if (modemRequest.value.pending && modemRequest.value.request_id === requestId) {
      failModemRequest('timeout', '设备确认超时')
    }
  }, 120000)
  return requestId
}

function requestVideoFps(fps, resolution = null) {
  if (!connected.value || !client || videoFpsRequest.value.pending) return null
  if (![5, 8, 15, 20, 30].includes(fps)) return null
  const allowedResolutions = new Set(['640x480', '1280x720', '1920x1080'])
  if (resolution != null && !allowedResolutions.has(resolution)) return null
  const requestId = makeRequestId()
  const params = resolution == null ? { fps } : { fps, resolution }
  const sent = client.send({
    type: 'command_request',
    cmd: 'video_fps',
    params,
    requestId,
  })
  if (!sent) return null
  clearVideoFpsTimer()
  setVideoFpsRequest({
    pending: true,
    request_id: requestId,
    fps,
    resolution,
    status: 'queued',
    error: null,
  })
  videoFpsRequestTimer = setTimeout(() => {
    if (videoFpsRequest.value.pending && videoFpsRequest.value.request_id === requestId) {
      failVideoFpsRequest('timeout', '设备确认超时')
    }
  }, 15000)
  return requestId
}

function requestWifiSta(params, description = 'Wi-Fi 操作', recordEvent = true) {
  if (!connected.value || !client || wifiRequest.value.pending) return null
  if (!params || typeof params !== 'object') return null
  const allowedActions = new Set(['query', 'set_enabled', 'scan', 'connect', 'select_uplink'])
  if (!allowedActions.has(params.action)) return null

  const requestId = makeRequestId()
  const sent = client.send({
    type: 'command_request',
    cmd: 'wifi_sta',
    params,
    requestId,
  })
  if (!sent) return null

  clearWifiRequestTimer()
  setWifiRequest({
    pending: true,
    request_id: requestId,
    action: params.action,
    description,
    record_event: recordEvent,
    status: 'queued',
    error: null,
  })
  const timeoutMs = params.action === 'scan' ? 20000 : params.action === 'connect' ? 25000 : 15000
  wifiRequestTimer = setTimeout(() => {
    if (wifiRequest.value.pending && wifiRequest.value.request_id === requestId) {
      failWifiRequest('timeout', '设备确认超时')
    }
  }, timeoutMs)
  return requestId
}

export function useControlChannel() {
  if (!started) {
    started = true
    connect()
  }

  const apStreamEnabled = computed(() => (
    deviceOnline.value === true && apStream.value.state === 'enabled'
  ))
  const apStreamBusy = computed(() => (
    apRequest.value.pending
    || (
      apRequest.value.status === 'idle'
      && (apStream.value.state === 'starting' || apStream.value.state === 'stopping')
    )
  ))

  return {
    connected,
    deviceOnline,
    lastAck,
    apRequest,
    apStream,
    apStreamEnabled,
    apStreamBusy,
    modem4g,
    modemRequest,
    videoFps,
    videoFpsRequest,
    wifiSta,
    wifiRequest,
    robotControl,
    robotControlRequest,
    sendCommand,
    requestRobotControl,
    requestApStream,
    requestModem4g,
    requestVideoFps,
    requestWifiSta,
  }
}
