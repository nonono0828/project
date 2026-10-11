const { readFileSync } = require('node:fs')
const path = require('node:path')

const FRONTEND_ROOT = path.resolve(__dirname, '..')
const ENV_FILE = path.join(FRONTEND_ROOT, '.env.production')
const WINDOWS_DIST_DIRECTORY = path.join(FRONTEND_ROOT, 'windows', 'staging-v1.2.12')
const FORBIDDEN_VALUES = [
  'robot.example.com',
  '192.0.2.10',
  'replace-me',
  'change_me',
  'example.com',
]

function parseEnvFile() {
  let source
  try {
    source = readFileSync(ENV_FILE, 'utf8')
  } catch {
    throw new Error(`Missing production configuration: ${ENV_FILE}`)
  }

  const values = {}
  for (const rawLine of source.split(/\r?\n/)) {
    const line = rawLine.trim()
    if (!line || line.startsWith('#')) continue
    const separator = line.indexOf('=')
    if (separator < 1) continue
    const key = line.slice(0, separator).trim()
    const value = line.slice(separator + 1).trim().replace(/^(['"])(.*)\1$/, '$2')
    values[key] = value
  }
  return values
}

function validateOrigin(name, value, protocols) {
  if (!value) throw new Error(`${name} is required in .env.production`)
  const lower = value.toLowerCase()
  if (FORBIDDEN_VALUES.some((item) => lower.includes(item))) {
    throw new Error(`${name} contains a placeholder value`)
  }

  let parsed
  try {
    parsed = new URL(value)
  } catch {
    throw new Error(`${name} must be a valid origin URL`)
  }
  if (!protocols.includes(parsed.protocol)) {
    throw new Error(`${name} must use ${protocols.join(' or ')}`)
  }
  if (parsed.username || parsed.password || parsed.pathname !== '/' || parsed.search || parsed.hash) {
    throw new Error(`${name} must contain only scheme, host and optional port`)
  }
  return parsed.origin.replace(/\/$/, '')
}

function loadProductionOrigins() {
  const env = parseEnvFile()
  const httpOrigin = validateOrigin(
    'VITE_CLOUD_HTTP_ORIGIN',
    env.VITE_CLOUD_HTTP_ORIGIN,
    ['http:', 'https:'],
  )
  const wsOrigin = validateOrigin(
    'VITE_CLOUD_WS_ORIGIN',
    env.VITE_CLOUD_WS_ORIGIN,
    ['ws:', 'wss:'],
  )

  const httpUrl = new URL(httpOrigin)
  const wsUrl = new URL(wsOrigin)
  if (httpUrl.host !== wsUrl.host) {
    throw new Error('Cloud HTTP and WebSocket origins must use the same host and port')
  }
  if ((httpUrl.protocol === 'https:') !== (wsUrl.protocol === 'wss:')) {
    throw new Error('Cloud HTTP and WebSocket security modes do not match')
  }
  return { httpOrigin, wsOrigin }
}

module.exports = {
  FORBIDDEN_VALUES,
  FRONTEND_ROOT,
  WINDOWS_DIST_DIRECTORY,
  loadProductionOrigins,
}
