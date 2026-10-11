import { Capacitor, registerPlugin } from '@capacitor/core'

const WifiSettings = registerPlugin('WifiSettings')

export function isNativeWifiSettingsAvailable() {
  return Capacitor.isNativePlatform()
    || typeof window?.zhixunDesktop?.openWifiSettings === 'function'
}

export async function openSystemWifiSettings() {
  if (typeof window?.zhixunDesktop?.openWifiSettings === 'function') {
    return window.zhixunDesktop.openWifiSettings()
  }
  if (!Capacitor.isNativePlatform()) {
    return { opened: false, mode: 'browser' }
  }
  return WifiSettings.open()
}
