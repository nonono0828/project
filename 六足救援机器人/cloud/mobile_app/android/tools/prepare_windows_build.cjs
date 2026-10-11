const { rmSync, writeFileSync } = require('node:fs')
const path = require('node:path')
const {
  FRONTEND_ROOT,
  WINDOWS_DIST_DIRECTORY,
  loadProductionOrigins,
} = require('./windows_cloud_config.cjs')

const { httpOrigin, wsOrigin } = loadProductionOrigins()
const output = path.join(FRONTEND_ROOT, 'windows', 'cloud-origin.cjs')
const source = [
  '// Generated from the git-ignored .env.production file. Do not edit or commit.',
  `module.exports = Object.freeze(${JSON.stringify({ httpOrigin, wsOrigin }, null, 2)})`,
  '',
].join('\n')

rmSync(WINDOWS_DIST_DIRECTORY, { recursive: true, force: true })
writeFileSync(output, source, 'utf8')
console.log(`Windows cloud configuration prepared for ${new URL(httpOrigin).host}`)
