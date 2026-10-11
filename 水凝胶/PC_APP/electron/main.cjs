const path = require('node:path');
const { app, BrowserWindow, ipcMain } = require('electron');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');

let mainWindow = null;
let activePort = null;
let activePath = null;

function sendToRenderer(channel, payload) {
  if (mainWindow && !mainWindow.isDestroyed()) {
    mainWindow.webContents.send(channel, payload);
  }
}

function serialStatus(state, message = '', portPath = activePath) {
  sendToRenderer('serial:status', { state, message, path: portPath });
}

async function closeSerial() {
  const port = activePort;
  activePort = null;
  activePath = null;
  if (port && port.isOpen) {
    await new Promise((resolve) => port.close(() => resolve()));
  }
  serialStatus('disconnected', '串口已断开', null);
}

async function openSerial(portPath) {
  if (!portPath || typeof portPath !== 'string') {
    throw new Error('无效的串口路径');
  }
  if (activePort && activePath === portPath && activePort.isOpen) {
    return { ok: true, path: activePath };
  }
  if (activePort) {
    await closeSerial();
  }

  const port = new SerialPort({ path: portPath, baudRate: 115200, autoOpen: false });
  activePort = port;
  activePath = portPath;
  serialStatus('connecting', '正在连接', portPath);

  await new Promise((resolve, reject) => {
    port.open((error) => {
      if (error) reject(error);
      else resolve();
    });
  });

  const parser = port.pipe(new ReadlineParser({ delimiter: '\n' }));
  parser.on('data', (line) => {
    const cleanLine = String(line).replace(/\r/g, '').trim();
    if (cleanLine) sendToRenderer('serial:line', cleanLine);
  });
  port.on('error', (error) => serialStatus('error', error.message, portPath));
  port.on('close', () => {
    if (activePort === port) {
      activePort = null;
      activePath = null;
    }
    serialStatus('disconnected', '设备已断开', portPath);
  });
  serialStatus('connected', '串口已连接', portPath);
  return { ok: true, path: portPath };
}

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1360,
    height: 860,
    minWidth: 960,
    minHeight: 640,
    backgroundColor: '#e9edf0',
    autoHideMenuBar: true,
    show: false,
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: true
    }
  });

  if (process.env.VITE_DEV_SERVER_URL) {
    mainWindow.loadURL(process.env.VITE_DEV_SERVER_URL);
  } else {
    mainWindow.loadFile(path.join(__dirname, '..', 'dist', 'index.html'));
  }
  mainWindow.once('ready-to-show', () => mainWindow.show());
}

app.whenReady().then(() => {
  ipcMain.handle('serial:list', async () => SerialPort.list());
  ipcMain.handle('serial:connect', async (_event, portPath) => openSerial(portPath));
  ipcMain.handle('serial:disconnect', async () => {
    await closeSerial();
    return { ok: true };
  });
  ipcMain.handle('window:toggle-fullscreen', () => {
    if (!mainWindow) return false;
    const next = !mainWindow.isFullScreen();
    mainWindow.setFullScreen(next);
    return next;
  });

  createWindow();
  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});

app.on('before-quit', () => {
  if (activePort && activePort.isOpen) activePort.close();
});
