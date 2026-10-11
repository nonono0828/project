const { httpOrigin: CLOUD_HTTP_ORIGIN, wsOrigin: CLOUD_WS_ORIGIN } = require('./cloud-origin.cjs')

const CONTENT_SECURITY_POLICY = [
  "default-src 'self'",
  "base-uri 'none'",
  "object-src 'none'",
  "form-action 'self'",
  "frame-ancestors 'none'",
  "script-src 'self'",
  "style-src 'self' 'unsafe-inline'",
  "font-src 'self' data:",
  `img-src 'self' data: blob: ${CLOUD_HTTP_ORIGIN} http://192.168.4.1:8080 https://tile.openstreetmap.org https://*.tile.openstreetmap.org https://*.tile.openstreetmap.fr`,
  `connect-src 'self' ${CLOUD_HTTP_ORIGIN} ${CLOUD_WS_ORIGIN} http://192.168.4.1:*`,
  "frame-src http: https:",
].join('; ')

module.exports = { CONTENT_SECURITY_POLICY }
