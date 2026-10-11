<template>
  <div class="app-root">
    <main v-if="stage === 'auth-loading'" class="auth-loading" aria-live="polite">
      <div class="loading-grid" aria-hidden="true"></div>
      <section class="loading-card">
        <img class="loading-brand-logo" :src="zxBrandLogo" alt="" aria-hidden="true" />
        <p>SMART INSPECTION</p>
        <h1>智巡精灵</h1>
        <div class="loading-line"><i></i></div>
        <small>正在验证登录状态</small>
      </section>
    </main>

    <Login
      v-else-if="stage === 'login'"
      :loading="loginLoading"
      :error="loginError"
      :initial-username="lastUsername"
      @submit="handleLogin"
    />

    <AppLauncher
      v-else-if="stage === 'launcher'"
      :username="authSession?.username || lastUsername"
      :offline-session="offlineSession"
      :expires-at="authSession?.expiresAt || null"
      :refreshing="sessionRefreshing"
      :refresh-error="sessionRefreshError"
      @cloud="openCloudPlatform"
      @local="openLocalSetup"
      @refresh-session="refreshSession"
      @logout="requestLogout"
    />

    <Dashboard
      v-else-if="stage === 'dashboard'"
      @app-home="returnToLauncher"
      @logout="requestLogout"
    />

    <LocalConnectionSetup
      v-else-if="stage === 'local-setup'"
      @back="returnToLauncher"
      @logout="requestLogout"
      @open="openLocalPortal"
    />

    <LocalPortal
      v-else-if="stage === 'local-portal'"
      :url="portalUrl"
      @back="stage = 'local-setup'"
      @logout="requestLogout"
    />

    <Teleport to="body">
      <div
        v-if="logoutConfirmVisible"
        class="logout-confirm-overlay"
        role="presentation"
        @click.self="logoutConfirmVisible = false"
      >
        <section
          class="logout-confirm-card"
          role="alertdialog"
          aria-modal="true"
          aria-labelledby="logout-confirm-title"
          aria-describedby="logout-confirm-description"
        >
          <div class="logout-confirm-icon" aria-hidden="true">↪</div>
          <h2 id="logout-confirm-title">退出登录？</h2>
          <p id="logout-confirm-description">退出后会断开云端实时连接，并需要重新验证管理员身份。</p>
          <div>
            <button ref="logoutCancelButton" type="button" @click="logoutConfirmVisible = false">取消</button>
            <button class="is-danger" type="button" @click="confirmLogout">退出登录</button>
          </div>
        </section>
      </div>
    </Teleport>
  </div>
</template>

<script setup>
import { defineAsyncComponent, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import zxBrandLogo from '@/assets/zx-suture-mark.svg'
import AppLauncher from '@/components/AppLauncher.vue'
import Login from '@/views/Login.vue'
import {
  AuthRequestError,
  clearStoredSession,
  loginWithPassword,
  readStoredSession,
  validateStoredSession,
} from '@/api/auth'
import { closeAllWs } from '@/api/websocket'

const Dashboard = defineAsyncComponent(() => import('@/views/Dashboard.vue'))
const LocalConnectionSetup = defineAsyncComponent(
  () => import('@/components/LocalConnectionSetup.vue')
)
const LocalPortal = defineAsyncComponent(() => import('@/components/LocalPortal.vue'))

const LAST_USERNAME_KEY = 'zhixun_last_username'
const stage = ref('auth-loading')
const authSession = ref(null)
const offlineSession = ref(false)
const portalUrl = ref('')
const loginLoading = ref(false)
const loginError = ref('')
const logoutConfirmVisible = ref(false)
const logoutCancelButton = ref(null)
const sessionRefreshing = ref(false)
const sessionRefreshError = ref('')
const lastUsername = ref(localStorage.getItem(LAST_USERNAME_KEY) || 'admin')
let lastSessionValidationAt = 0
let sessionExpiryTimer = 0

function releaseRobotControls() {
  window.dispatchEvent(new CustomEvent('robot-app-release-controls'))
}

function activateStage(nextStage) {
  releaseRobotControls()
  stage.value = nextStage
}

async function bootstrapAuthentication() {
  const stored = readStoredSession()
  if (!stored) {
    stage.value = 'login'
    return
  }

  lastUsername.value = stored.username
  try {
    authSession.value = await validateStoredSession(stored)
    offlineSession.value = false
    lastSessionValidationAt = Date.now()
    stage.value = 'launcher'
  } catch (error) {
    // A still-fresh, previously verified session may open only the local setup
    // launcher while the cloud is temporarily unreachable. Opening cloud control
    // still requires a reachable network and its own live channels.
    if (
      error instanceof AuthRequestError
      && ['auth_server_unreachable', 'auth_server_timeout'].includes(error.code)
      && Number(stored.expiresAt) > Date.now() / 1000
    ) {
      authSession.value = stored
      offlineSession.value = true
      stage.value = 'launcher'
      return
    }
    clearStoredSession()
    authSession.value = null
    stage.value = 'login'
  }
}

async function handleLogin(credentials) {
  loginLoading.value = true
  loginError.value = ''
  try {
    const session = await loginWithPassword(credentials.username, credentials.password)
    authSession.value = session
    offlineSession.value = false
    lastSessionValidationAt = Date.now()
    sessionRefreshError.value = ''
    lastUsername.value = session.username
    localStorage.setItem(LAST_USERNAME_KEY, session.username)
    activateStage('launcher')
  } catch (error) {
    loginError.value = error?.message || '登录失败，请稍后重试'
  } finally {
    loginLoading.value = false
  }
}

async function refreshSession({ openCloud = false } = {}) {
  if (!authSession.value || sessionRefreshing.value) return false
  sessionRefreshing.value = true
  sessionRefreshError.value = ''
  try {
    authSession.value = await validateStoredSession(authSession.value)
    offlineSession.value = false
    lastSessionValidationAt = Date.now()
    if (openCloud) activateStage('dashboard')
    return true
  } catch (error) {
    if (
      error instanceof AuthRequestError
      && ['auth_server_unreachable', 'auth_server_timeout'].includes(error.code)
      && Number(authSession.value?.expiresAt) > Date.now() / 1000
    ) {
      offlineSession.value = true
      sessionRefreshError.value = '云端暂时不可达，可继续使用现场配置台'
      return false
    }
    handleAuthExpired()
    return false
  } finally {
    sessionRefreshing.value = false
  }
}

async function openCloudPlatform() {
  if (!authSession.value || sessionRefreshing.value) return
  if (offlineSession.value) {
    await refreshSession({ openCloud: true })
    return
  }
  offlineSession.value = false
  activateStage('dashboard')
}

function openLocalSetup() {
  if (!authSession.value) return
  activateStage('local-setup')
}

function openLocalPortal(url) {
  if (!authSession.value) return
  portalUrl.value = url
  activateStage('local-portal')
}

function returnToLauncher() {
  if (!authSession.value) {
    activateStage('login')
    return
  }
  activateStage('launcher')
  closeAllWs()
}

function requestLogout() {
  logoutConfirmVisible.value = true
  nextTick(() => logoutCancelButton.value?.focus?.())
}

function confirmLogout() {
  logoutConfirmVisible.value = false
  releaseRobotControls()
  closeAllWs()
  window.dispatchEvent(new CustomEvent('robot-location-reset'))
  clearStoredSession()
  authSession.value = null
  offlineSession.value = false
  sessionRefreshError.value = ''
  lastSessionValidationAt = 0
  portalUrl.value = ''
  loginError.value = ''
  stage.value = 'login'
}

function handleAuthExpired() {
  if (!authSession.value) return
  releaseRobotControls()
  closeAllWs()
  window.dispatchEvent(new CustomEvent('robot-location-reset'))
  clearStoredSession()
  authSession.value = null
  offlineSession.value = false
  sessionRefreshError.value = ''
  lastSessionValidationAt = 0
  portalUrl.value = ''
  logoutConfirmVisible.value = false
  loginError.value = '登录已过期，请重新登录'
  stage.value = 'login'
}

function handleNativeBack(event) {
  if (logoutConfirmVisible.value) {
    event?.preventDefault?.()
    logoutConfirmVisible.value = false
    return
  }
  Promise.resolve().then(() => {
    if (event?.defaultPrevented) return
    if (stage.value === 'dashboard' || stage.value === 'local-setup') {
      returnToLauncher()
    } else if (stage.value === 'local-portal') {
      activateStage('local-setup')
    }
  })
}

function handleAppResume() {
  if (!authSession.value) return
  const validationAge = Date.now() - lastSessionValidationAt
  if (validationAge >= 5 * 60 * 1000) {
    refreshSession()
  }
}

function scheduleSessionExpiry(expiresAt) {
  window.clearTimeout(sessionExpiryTimer)
  if (!expiresAt) return
  const delay = Math.max(0, Number(expiresAt) * 1000 - Date.now())
  sessionExpiryTimer = window.setTimeout(handleAuthExpired, Math.min(delay, 2_147_000_000))
}

watch(stage, (value, previous) => {
  const cloudActive = value === 'dashboard'
  window.__ROBOT_APP_SCREEN = value
  window.__ROBOT_CLOUD_ACTIVE = cloudActive
  if (cloudActive && previous !== 'dashboard') {
    window.dispatchEvent(new CustomEvent('robot-app-cloud-resume'))
  } else if (!cloudActive && previous === 'dashboard') {
    window.dispatchEvent(new CustomEvent('robot-app-cloud-pause'))
  }
}, { immediate: true })

watch(
  () => authSession.value?.expiresAt,
  (expiresAt) => scheduleSessionExpiry(expiresAt),
  { immediate: true }
)

onMounted(() => {
  window.addEventListener('robot-app-back', handleNativeBack)
  window.addEventListener('robot-app-resume', handleAppResume)
  window.addEventListener('robot-auth-expired', handleAuthExpired)
  bootstrapAuthentication()
})

onBeforeUnmount(() => {
  window.clearTimeout(sessionExpiryTimer)
  releaseRobotControls()
  closeAllWs()
  window.removeEventListener('robot-app-back', handleNativeBack)
  window.removeEventListener('robot-app-resume', handleAppResume)
  window.removeEventListener('robot-auth-expired', handleAuthExpired)
})
</script>

<style scoped>
.app-root {
  width: 100%;
  min-height: 100vh;
  min-height: 100dvh;
}
.auth-loading {
  position: fixed;
  inset: 0;
  display: grid;
  place-items: center;
  overflow: hidden;
  color: #f5f9ff;
  background:
    radial-gradient(circle at 82% 12%, rgba(45, 137, 255, .24), transparent 35%),
    radial-gradient(circle at 15% 90%, rgba(14, 190, 159, .12), transparent 30%),
    linear-gradient(150deg, #081421, #06101d 58%, #09192a);
}
.loading-grid {
  position: absolute;
  inset: 0;
  opacity: .1;
  background-image:
    linear-gradient(rgba(255,255,255,.08) 1px, transparent 1px),
    linear-gradient(90deg, rgba(255,255,255,.08) 1px, transparent 1px);
  background-size: 30px 30px;
}
.loading-card {
  position: relative;
  z-index: 1;
  width: min(82vw, 290px);
  padding: 34px 28px 30px;
  text-align: center;
  border: 1px solid rgba(132, 184, 235, .17);
  border-radius: 24px;
  background: rgba(10, 28, 47, .74);
  box-shadow: 0 24px 80px rgba(0,0,0,.34);
  backdrop-filter: blur(18px);
}
.loading-brand-logo {
  display: block;
  width: 64px;
  height: 64px;
  margin: 0 auto 16px;
  border-radius: 18px;
  object-fit: cover;
  box-shadow: 0 14px 40px rgba(0, 0, 0, .42);
}
.loading-mark {
  position: relative;
  width: 58px;
  height: 58px;
  margin: 0 auto 16px;
  border: 1px solid rgba(91, 174, 255, .45);
  border-radius: 18px;
  background: rgba(44, 137, 236, .12);
}
.loading-mark i {
  position: absolute;
  left: 20px;
  top: 15px;
  width: 18px;
  height: 27px;
  border: 2px solid #77bdff;
  border-radius: 10px 10px 7px 7px;
}
.loading-mark span {
  position: absolute;
  width: 14px;
  height: 2px;
  border-radius: 2px;
  background: #77bdff;
}
.loading-mark span:nth-child(1) { left: 8px; top: 19px; transform: rotate(28deg); }
.loading-mark span:nth-child(2) { right: 8px; top: 19px; transform: rotate(-28deg); }
.loading-mark span:nth-child(3) { left: 8px; bottom: 17px; transform: rotate(-28deg); }
.loading-mark span:nth-child(4) { right: 8px; bottom: 17px; transform: rotate(28deg); }
.loading-card p {
  margin: 0 0 5px;
  color: #6f93b5;
  font-size: 9px;
  font-weight: 800;
  letter-spacing: .2em;
}
.loading-card h1 { margin: 0; font-size: 24px; letter-spacing: .03em; }
.loading-card small { color: #728ba4; font-size: 11px; }
.loading-line {
  height: 3px;
  margin: 24px 0 13px;
  overflow: hidden;
  border-radius: 3px;
  background: rgba(255,255,255,.07);
}
.loading-line i {
  display: block;
  width: 45%;
  height: 100%;
  border-radius: inherit;
  background: linear-gradient(90deg, #278bff, #61c6ff);
  animation: loading-slide 1.15s ease-in-out infinite alternate;
}
@keyframes loading-slide {
  from { transform: translateX(0); }
  to { transform: translateX(122%); }
}
@media (prefers-reduced-motion: reduce) {
  .loading-line i { animation: none; width: 100%; opacity: .65; }
}
.logout-confirm-overlay {
  position: fixed;
  inset: 0;
  z-index: 15000;
  display: grid;
  place-items: center;
  padding:
    max(20px, env(safe-area-inset-top))
    max(20px, env(safe-area-inset-right))
    max(20px, env(safe-area-inset-bottom))
    max(20px, env(safe-area-inset-left));
  background: rgba(2, 9, 17, .7);
  backdrop-filter: blur(9px);
}
.logout-confirm-card {
  width: min(100%, 330px);
  padding: 25px;
  color: #f3f8ff;
  text-align: center;
  border: 1px solid rgba(132, 181, 226, .22);
  border-radius: 22px;
  background: linear-gradient(155deg, rgba(19, 42, 66, .98), rgba(7, 20, 35, .98));
  box-shadow: 0 24px 70px rgba(0, 0, 0, .45);
}
.logout-confirm-icon {
  width: 48px;
  height: 48px;
  display: grid;
  place-items: center;
  margin: 0 auto 14px;
  color: #ff8e88;
  border-radius: 15px;
  background: rgba(255, 78, 68, .12);
  font-size: 24px;
}
.logout-confirm-card h2 { margin: 0; font-size: 21px; }
.logout-confirm-card p {
  margin: 10px 0 20px;
  color: #8fa5ba;
  font-size: 12px;
  line-height: 1.65;
}
.logout-confirm-card > div:last-child {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
}
.logout-confirm-card button {
  min-height: 46px;
  border: 1px solid rgba(139, 177, 214, .2);
  border-radius: 13px;
  color: #d9e8f6;
  background: rgba(255,255,255,.055);
  font-size: 13px;
  font-weight: 700;
}
.logout-confirm-card button.is-danger {
  color: #fff;
  border-color: rgba(255, 76, 66, .55);
  background: linear-gradient(135deg, #ef534b, #d83e37);
}
.logout-confirm-card button:active { transform: scale(.98); }
</style>
