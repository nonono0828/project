/**
 * WebSocket 统一管理
 * 支持多路连接（控制、传感器、GPS），自动重连
 */
import { getRealtimeTicket, withRealtimeTicket } from '@/api/auth'

const EXPONENTIAL_RECONNECT_ATTEMPTS = 10
const RECONNECT_BASE_MS = 1000
const STEADY_RECONNECT_MS = 30000

class WsClient {
  constructor(url, { onMessage, onOpen, onClose, onError } = {}) {
    this.url = url
    this.ws = null
    this.reconnectCount = 0
    this.reconnectTimer = null
    this.manualClose = false
    this.connectionAttempt = 0
    this.pendingAttempt = null
    this.handlers = { onMessage, onOpen, onClose, onError }
  }

  connect() {
    if (this.ws && (this.ws.readyState === WebSocket.OPEN || this.ws.readyState === WebSocket.CONNECTING)) {
      return
    }
    if (this.pendingAttempt !== null) return
    this.manualClose = false
    const attempt = ++this.connectionAttempt
    this.pendingAttempt = attempt
    getRealtimeTicket().then((ticket) => {
      if (this.manualClose || this.pendingAttempt !== attempt) return
      this.pendingAttempt = null

      let socket
      try {
        socket = new WebSocket(withRealtimeTicket(this.url, ticket))
        this.ws = socket
      } catch (e) {
        console.error(`[WS] 连接失败: ${this.url}`, e)
        this.handlers.onError?.(e)
        this._scheduleReconnect()
        return
      }

      socket.onopen = (e) => {
        if (attempt !== this.connectionAttempt) return
        this.reconnectCount = 0
        console.log(`[WS] 已连接: ${this.url}`)
        this.handlers.onOpen?.(e)
      }

      socket.onmessage = (e) => {
        if (attempt !== this.connectionAttempt) return
        this.handlers.onMessage?.(e.data, e)
      }

      socket.onclose = (e) => {
        if (attempt !== this.connectionAttempt) return
        if (this.ws === socket) this.ws = null
        console.log(`[WS] 已断开: ${this.url}`, e.code)
        this.handlers.onClose?.(e)
        if (!this.manualClose) this._scheduleReconnect()
      }

      socket.onerror = (e) => {
        if (attempt !== this.connectionAttempt) return
        console.error(`[WS] 错误: ${this.url}`, e)
        this.handlers.onError?.(e)
      }
    }).catch((e) => {
      if (this.pendingAttempt !== attempt) return
      this.pendingAttempt = null
      console.error(`[WS] 获取实时连接票据失败: ${this.url}`, e)
      this.handlers.onError?.(e)
      if (!this.manualClose) this._scheduleReconnect()
    })
  }

  send(data) {
    if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
      console.warn('[WS] 未连接，无法发送')
      return false
    }
    const payload = typeof data === 'string' ? data : JSON.stringify(data)
    this.ws.send(payload)
    return true
  }

  close() {
    this.manualClose = true
    this.connectionAttempt += 1
    this.pendingAttempt = null
    this._clearReconnect()
    if (this.ws) {
      this.ws.close()
      this.ws = null
    }
  }

  resume() {
    this.manualClose = false
    this.reconnectCount = 0
    this._clearReconnect()
    this.connect()
  }

  _scheduleReconnect() {
    if (this.manualClose || this.reconnectTimer) return
    const delay = this.reconnectCount < EXPONENTIAL_RECONNECT_ATTEMPTS
      ? Math.min(RECONNECT_BASE_MS * 2 ** this.reconnectCount, STEADY_RECONNECT_MS)
      : STEADY_RECONNECT_MS
    this.reconnectCount++
    console.log(`[WS] ${delay / 1000}s 后第 ${this.reconnectCount} 次重连: ${this.url}`)
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null
      this.connect()
    }, delay)
  }

  _clearReconnect() {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer)
      this.reconnectTimer = null
    }
  }

  get readyState() {
    return this.ws ? this.ws.readyState : WebSocket.CLOSED
  }
}

// ==================== 全局 WS 实例工厂 ====================
const clients = {}

export function getWsClient(key, url, handlers) {
  // 关闭旧连接
  if (clients[key]) {
    clients[key].close()
  }
  const client = new WsClient(url, handlers)
  clients[key] = client
  return client
}

export function closeAllWs() {
  Object.values(clients).forEach((c) => c.close())
}

export function reconnectAllWs() {
  Object.values(clients).forEach((client) => client.resume())
}

export default WsClient
