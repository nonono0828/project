const { readdirSync, readFileSync, statSync } = require('node:fs')
const path = require('node:path')
const {
  FORBIDDEN_VALUES,
  WINDOWS_DIST_DIRECTORY,
  loadProductionOrigins,
} = require('./windows_cloud_config.cjs')

function filesRecursively(directory) {
  return readdirSync(directory).flatMap((name) => {
    const file = path.join(directory, name)
    return statSync(file).isDirectory() ? filesRecursively(file) : [file]
  })
}

const { httpOrigin, wsOrigin } = loadProductionOrigins()
const bundleText = filesRecursively(WINDOWS_DIST_DIRECTORY)
  .filter((file) => /\.(?:html|js|css)$/i.test(file))
  .map((file) => readFileSync(file, 'utf8'))
  .join('\n')

for (const forbidden of FORBIDDEN_VALUES) {
  if (bundleText.toLowerCase().includes(forbidden)) {
    throw new Error(`Windows bundle contains forbidden placeholder: ${forbidden}`)
  }
}
if (!bundleText.includes(httpOrigin) || !bundleText.includes(wsOrigin)) {
  throw new Error('Windows bundle does not contain the configured cloud origins')
}

const generatedConfig = require(path.join(__dirname, '..', 'windows', 'cloud-origin.cjs'))
if (generatedConfig.httpOrigin !== httpOrigin || generatedConfig.wsOrigin !== wsOrigin) {
  throw new Error('Electron and frontend cloud origins are inconsistent')
}

const { CONTENT_SECURITY_POLICY } = require(path.join(
  __dirname,
  '..',
  'windows',
  'security.cjs',
))
if (!CONTENT_SECURITY_POLICY.includes(httpOrigin)
  || !CONTENT_SECURITY_POLICY.includes(wsOrigin)
  || !CONTENT_SECURITY_POLICY.includes('http://192.168.4.1:8080')) {
  throw new Error('Electron CSP is missing a required cloud or local video origin')
}

console.log(`Windows bundle cloud configuration verified for ${new URL(httpOrigin).host}`)
