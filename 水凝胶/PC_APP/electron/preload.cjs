const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('handBridge', {
  listPorts: () => ipcRenderer.invoke('serial:list'),
  connect: (portPath) => ipcRenderer.invoke('serial:connect', portPath),
  disconnect: () => ipcRenderer.invoke('serial:disconnect'),
  toggleFullscreen: () => ipcRenderer.invoke('window:toggle-fullscreen'),
  onSerialLine: (callback) => {
    const listener = (_event, line) => callback(line);
    ipcRenderer.on('serial:line', listener);
    return () => ipcRenderer.removeListener('serial:line', listener);
  },
  onSerialStatus: (callback) => {
    const listener = (_event, status) => callback(status);
    ipcRenderer.on('serial:status', listener);
    return () => ipcRenderer.removeListener('serial:status', listener);
  }
});
