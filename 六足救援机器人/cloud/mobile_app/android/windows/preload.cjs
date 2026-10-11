const { contextBridge, ipcRenderer } = require('electron')

contextBridge.exposeInMainWorld('zhixunDesktop', Object.freeze({
  platform: 'windows',
  openWifiSettings: () => ipcRenderer.invoke('desktop:open-wifi-settings'),
  getVersion: () => ipcRenderer.invoke('desktop:version'),
}))
