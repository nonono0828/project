import cfg from '@/config/config'

const STORAGE_KEY = 'zhixun_auth_session_v1'
const REALTIME_TICKET_SKEW_SECONDS = 5
const AUTH_REQUEST_TIMEOUT_MS = 12000

let realtimeTicketCache = null
let realtimeTicketRequest = null
let realtimeTicketAbortController = null
let sessionGeneration = 0

export class AuthRequestError extends Error {
  constructor(message, { status = 0, code = 'auth_request_failed' } = {}) {
    super(message)
    this.name = 'AuthRequestError'
    this.status = status
    this.code = code
  }
}

function normalizeSession(data) {
  const token = data?.access_token
  const username = data?.username
  const expiresAt = Number(data?.expires_at)
  if (!token || !username || !Number.isFinite(expiresAt)) return null
  return {
    accessToken: token,
    tokenType: data?.token_type || 'Bearer',
    username,
    issuedAt: Number(data?.issued_at) || null,
    expiresAt,
  }
}

function sessionIsFresh(session, skewSeconds = 15) {
  return Boolean(
    session?.accessToken
    && session?.username
    && Number(session?.expiresAt) > Date.now() / 1000 + skewSeconds
  )
}

function resetRealtimeTicketState() {
  sessionGeneration += 1
  realtimeTicketAbortController?.abort()
  realtimeTicketAbortController = null
  realtimeTicketCache = null
  realtimeTicketRequest = null
}

function normalizeRealtimeTicket(data) {
  const ticket = data?.ticket
  const expiresAt = Number(data?.expires_at)
  if (
    typeof ticket !== 'string'
    || !ticket
    || data?.scope !== 'realtime'
    || !Number.isFinite(expiresAt)
  ) {
    return null
  }
  return { ticket, expiresAt }
}

function realtimeTicketIsFresh(value) {
  return Boolean(
    value?.ticket
    && Number(value?.expiresAt) > Date.now() / 1000 + REALTIME_TICKET_SKEW_SECONDS
  )
}

export function readStoredSession() {
  try {
    const session = JSON.parse(localStorage.getItem(STORAGE_KEY) || 'null')
    if (!sessionIsFresh(session)) {
      clearStoredSession()
      return null
    }
    return session
  } catch {
    clearStoredSession()
    return null
  }
}

export function storeSession(session) {
  if (!sessionIsFresh(session, 0)) throw new Error('invalid auth session')
  resetRealtimeTicketState()
  localStorage.setItem(STORAGE_KEY, JSON.stringify(session))
}

export function clearStoredSession() {
  resetRealtimeTicketState()
  localStorage.removeItem(STORAGE_KEY)
  // Remove the obsolete client-only gate used by the previous App build.
  localStorage.removeItem('robot_auth')
}

async function authRequest(path, options = {}) {
  const { signal: callerSignal, ...requestOptions } = options
  const controller = new AbortController()
  let timedOut = false
  const abortFromCaller = () => controller.abort(callerSignal?.reason)
  if (callerSignal?.aborted) {
    abortFromCaller()
  } else {
    callerSignal?.addEventListener('abort', abortFromCaller, { once: true })
  }
  const timeout = setTimeout(() => {
    timedOut = true
    controller.abort()
  }, AUTH_REQUEST_TIMEOUT_MS)

  let response
  try {
    response = await fetch(`${cfg.API_BASE}${path}`, {
      cache: 'no-store',
      ...requestOptions,
      signal: controller.signal,
    })
  } catch (error) {
    if (timedOut) {
      throw new AuthRequestError('登录服务器响应超时，请稍后重试', {
        code: 'auth_server_timeout',
      })
    }
    if (callerSignal?.aborted) throw error
    throw new AuthRequestError('无法连接登录服务器', {
      code: 'auth_server_unreachable',
    })
  } finally {
    clearTimeout(timeout)
    callerSignal?.removeEventListener('abort', abortFromCaller)
  }

  const payload = await response.json().catch(() => ({}))
  if (!response.ok) {
    const code = payload?.message || 'auth_request_failed'
    const messages = {
      auth_not_configured: '云端尚未配置管理员鉴权',
      too_many_login_attempts: '尝试次数过多，请稍后再试',
      invalid_credentials_format: '账号或密码格式不正确',
      'Invalid username or password': '账号或密码错误',
      invalid_or_expired_token: '登录已过期，请重新登录',
    }
    throw new AuthRequestError(messages[code] || `登录服务错误（${response.status}）`, {
      status: response.status,
      code,
    })
  }
  return payload
}

export async function loginWithPassword(username, password) {
  const payload = await authRequest('/auth/login', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ username, password }),
  })
  const session = normalizeSession(payload?.data)
  if (!session) {
    throw new AuthRequestError('登录服务器返回了无效会话', {
      code: 'invalid_auth_response',
    })
  }
  storeSession(session)
  return session
}

export async function validateStoredSession(session = readStoredSession()) {
  if (!sessionIsFresh(session)) {
    clearStoredSession()
    throw new AuthRequestError('登录已过期，请重新登录', {
      status: 401,
      code: 'invalid_or_expired_token',
    })
  }
  const payload = await authRequest('/auth/session', {
    headers: { Authorization: `Bearer ${session.accessToken}` },
  })
  const data = payload?.data
  const validated = {
    ...session,
    username: data?.username || session.username,
    issuedAt: Number(data?.issued_at) || session.issuedAt,
    expiresAt: Number(data?.expires_at) || session.expiresAt,
  }
  storeSession(validated)
  return validated
}

export function getAccessToken() {
  return readStoredSession()?.accessToken || ''
}

export function getAuthHeaders() {
  const token = getAccessToken()
  return token ? { Authorization: `Bearer ${token}` } : {}
}

export function handleAuthResponse(response) {
  if (response?.status === 401) {
    resetRealtimeTicketState()
    window.dispatchEvent(new CustomEvent('robot-auth-expired'))
  }
  return response
}

export async function getRealtimeTicket() {
  if (realtimeTicketIsFresh(realtimeTicketCache)) {
    return realtimeTicketCache.ticket
  }
  if (realtimeTicketRequest) return realtimeTicketRequest

  const token = getAccessToken()
  if (!token) {
    throw new AuthRequestError('登录已过期，请重新登录', {
      status: 401,
      code: 'invalid_or_expired_token',
    })
  }

  const generation = sessionGeneration
  const controller = new AbortController()
  realtimeTicketAbortController = controller
  const request = authRequest('/auth/realtime-ticket', {
    method: 'POST',
    headers: { Authorization: `Bearer ${token}` },
    signal: controller.signal,
  }).then((payload) => {
    const value = normalizeRealtimeTicket(payload?.data)
    if (!value) {
      throw new AuthRequestError('实时连接票据无效', {
        code: 'invalid_realtime_ticket_response',
      })
    }
    if (generation !== sessionGeneration) {
      throw new AuthRequestError('登录会话已经结束', {
        code: 'auth_session_changed',
      })
    }
    realtimeTicketCache = value
    return value.ticket
  }).catch((error) => {
    if (error?.status === 401) {
      resetRealtimeTicketState()
      window.dispatchEvent(new CustomEvent('robot-auth-expired'))
    }
    throw error
  }).finally(() => {
    if (realtimeTicketRequest === request) realtimeTicketRequest = null
    if (realtimeTicketAbortController === controller) {
      realtimeTicketAbortController = null
    }
  })
  realtimeTicketRequest = request
  return request
}

export function withRealtimeTicket(url, ticket) {
  if (!ticket || !url) return url
  try {
    const parsed = new URL(url)
    parsed.searchParams.delete('access_token')
    parsed.searchParams.set('ticket', ticket)
    return parsed.toString()
  } catch {
    return url
  }
}
