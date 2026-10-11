import { createApp } from 'vue'
import App from './App.vue'
import { closeAllWs, reconnectAllWs } from '@/api/websocket'
import 'leaflet/dist/leaflet.css'
import './styles/index.css'
import './styles/light-industrial.css'

// Restore the industrial appearance chosen by the administrator. The
// dashboard remains the single owner of the interactive toggle state.
const savedTheme = localStorage.getItem('theme')
const startDark = savedTheme === 'dark'
document.documentElement.classList.toggle('dark', startDark)
document.documentElement.style.colorScheme = startDark ? 'dark' : 'light'

function resumeNetworkChannels() {
  if (window.__ROBOT_CLOUD_ACTIVE !== true) return
  reconnectAllWs()
}

window.addEventListener('online', resumeNetworkChannels)
window.addEventListener('robot-app-resume', resumeNetworkChannels)
window.addEventListener('robot-app-cloud-resume', resumeNetworkChannels)
window.addEventListener('robot-app-cloud-pause', closeAllWs)

const app = createApp(App)
app.mount('#app')
