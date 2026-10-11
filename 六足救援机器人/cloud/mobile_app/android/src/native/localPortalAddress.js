export const LOCAL_PORTAL_HOST = '192.168.4.1'
export const DEFAULT_LOCAL_PORTAL_ADDRESS = `http://${LOCAL_PORTAL_HOST}:8081/`

export function normalizeLocalPortalAddress(raw) {
  const value = String(raw || '').trim()
  if (!value) throw new Error('请输入机器人配置地址')
  const candidate = /^[a-z][a-z0-9+.-]*:\/\//i.test(value)
    ? value
    : `http://${value}`

  let parsed
  try {
    parsed = new URL(candidate)
  } catch {
    throw new Error('配置地址格式不正确')
  }
  if (parsed.protocol !== 'http:') {
    throw new Error('当前设备配置地址仅支持 HTTP')
  }
  if (parsed.username || parsed.password) {
    throw new Error('配置地址不能包含用户名或密码')
  }
  if (parsed.hostname.toLowerCase() !== LOCAL_PORTAL_HOST) {
    throw new Error(`配置地址主机必须是 ${LOCAL_PORTAL_HOST}`)
  }
  parsed.hash = ''
  return parsed.toString()
}

export function normalizeDesktopPortalAddress(raw) {
  const value = String(raw || '').trim()
  if (!value) throw new Error('请输入机器人配置地址')
  const candidate = /^[a-z][a-z0-9+.-]*:\/\//i.test(value)
    ? value
    : `http://${value}`

  let parsed
  try {
    parsed = new URL(candidate)
  } catch {
    throw new Error('配置地址格式不正确')
  }
  if (!['http:', 'https:'].includes(parsed.protocol)) {
    throw new Error('配置地址仅支持 HTTP 或 HTTPS')
  }
  if (parsed.username || parsed.password) {
    throw new Error('配置地址不能包含用户名或密码')
  }
  if (!parsed.hostname) {
    throw new Error('配置地址格式不正确')
  }
  parsed.hash = ''
  return parsed.toString()
}
