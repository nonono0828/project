const { app, BrowserWindow, ipcMain, Menu, net, protocol, session, shell } = require('electron')
const { existsSync, statSync } = require('node:fs')
const path = require('node:path')
const { pathToFileURL } = require('node:url')
const { CONTENT_SECURITY_POLICY } = require('./security.cjs')

const APP_SCHEME = 'zhixun'
const APP_HOST = 'app'
let mainWindow = null

protocol.registerSchemesAsPrivileged([
  {
    scheme: APP_SCHEME,
    privileges: {
      standard: true,
      secure: false,
      supportFetchAPI: true,
      stream: true,
    },
  },
])

function appAssetsDirectory() {
  return app.isPackaged
    ? path.join(process.resourcesPath, 'app')
    : path.resolve(__dirname, 'staging-v1.2.12')
}

function resolveAppAsset(requestUrl) {
  const url = new URL(requestUrl)
  if (url.host !== APP_HOST) return null

  const requestedPath = decodeURIComponent(url.pathname || '/')
  const relativePath = requestedPath === '/' ? 'index.html' : requestedPath.replace(/^\/+/, '')
  const root = appAssetsDirectory()
  const resolved = path.resolve(root, relativePath)
  const relative = path.relative(root, resolved)
  if (relative.startsWith('..') || path.isAbsolute(relative)) return null

  if (existsSync(resolved) && statSync(resolved).isFile()) return resolved
  if (!path.extname(relativePath)) return path.join(root, 'index.html')
  return null
}

async function registerAppProtocol() {
  await protocol.handle(APP_SCHEME, (request) => {
    let assetPath = null
    try {
      assetPath = resolveAppAsset(request.url)
    } catch {
      return new Response('Bad request', { status: 400 })
    }
    if (!assetPath) return new Response('Not found', { status: 404 })
    return net.fetch(pathToFileURL(assetPath).toString()).then((response) => {
      if (path.basename(assetPath).toLowerCase() !== 'index.html') return response
      const headers = new Headers(response.headers)
      headers.set('Content-Security-Policy', CONTENT_SECURITY_POLICY)
      return new Response(response.body, {
        status: response.status,
        statusText: response.statusText,
        headers,
      })
    })
  })
}

function isTrustedMainFrame(event) {
  if (!mainWindow || event.sender !== mainWindow.webContents) return false
  if (!event.senderFrame || event.senderFrame !== mainWindow.webContents.mainFrame) return false
  try {
    const url = new URL(event.senderFrame.url)
    return url.protocol === `${APP_SCHEME}:` && url.host === APP_HOST
  } catch {
    return false
  }
}

function createMainWindow() {
  mainWindow = new BrowserWindow({
    title: '智巡精灵',
    width: 1440,
    height: 900,
    minWidth: 1024,
    minHeight: 680,
    show: false,
    autoHideMenuBar: true,
    backgroundColor: '#f5f5f7',
    icon: path.join(__dirname, 'build', 'icon.ico'),
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true,
      webSecurity: true,
    },
  })

  mainWindow.webContents.setWindowOpenHandler(() => ({ action: 'deny' }))
  mainWindow.webContents.on('will-navigate', (event, url) => {
    if (url.startsWith(`${APP_SCHEME}://${APP_HOST}/`)) return
    event.preventDefault()
  })
  mainWindow.once('ready-to-show', () => mainWindow?.show())
  mainWindow.on('closed', () => { mainWindow = null })
  mainWindow.loadURL(`${APP_SCHEME}://${APP_HOST}/`)
}

ipcMain.handle('desktop:open-wifi-settings', async (event) => {
  if (!isTrustedMainFrame(event)) throw new Error('untrusted_desktop_request')
  try {
    await shell.openExternal('ms-settings:network-wifi')
    return { opened: true, mode: 'windows' }
  } catch (error) {
    return {
      opened: false,
      mode: 'windows',
      error: error instanceof Error ? error.message : 'unable_to_open_wifi_settings',
    }
  }
})

ipcMain.handle('desktop:version', (event) => {
  if (!isTrustedMainFrame(event)) throw new Error('untrusted_desktop_request')
  return app.getVersion()
})

const hasSingleInstanceLock = app.requestSingleInstanceLock()
if (!hasSingleInstanceLock) {
  app.quit()
} else {
  app.on('second-instance', () => {
    if (!mainWindow) return
    if (mainWindow.isMinimized()) mainWindow.restore()
    mainWindow.show()
    mainWindow.focus()
  })

  app.whenReady().then(async () => {
    app.setAppUserModelId('cn.hajimi.zhixun.desktop')
    Menu.setApplicationMenu(null)
    session.defaultSession.setPermissionRequestHandler((_webContents, _permission, callback) => {
      callback(false)
    })
    session.defaultSession.setPermissionCheckHandler(() => false)
    session.defaultSession.setDevicePermissionHandler(() => false)
    await registerAppProtocol()
    createMainWindow()
  })
}

app.on('window-all-closed', () => app.quit())
