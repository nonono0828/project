<template>
  <main class="workspace-page">
    <div class="grid-overlay" aria-hidden="true"></div>
    <section class="workspace-shell">
      <header>
        <img class="brand-logo" :src="zxBrandLogo" alt="" aria-hidden="true" />
        <div class="brand-copy">
          <p>SMART INSPECTION</p>
          <h1>智巡精灵</h1>
        </div>
        <button
          class="theme-toggle-button"
          :class="{ 'is-dark': isDark }"
          type="button"
          role="switch"
          :aria-checked="isDark"
          :aria-label="isDark ? '切换至日间模式' : '切换至夜间模式'"
          @click="toggleTheme"
        >
          <span class="theme-symbol" aria-hidden="true">{{ isDark ? '☾' : '☀' }}</span>
          <i aria-hidden="true"></i>
        </button>
        <button class="logout-button" type="button" @click="$emit('logout')">
          退出登录
        </button>
      </header>

      <div class="session-strip">
        <div class="session-avatar">{{ username.slice(0, 1).toUpperCase() }}</div>
        <div class="session-copy">
          <small>当前管理员</small>
          <strong>{{ username }}</strong>
          <small class="session-expiry">{{ sessionLabel }}</small>
        </div>
        <span :class="offlineSession || !networkOnline ? 'is-offline' : 'is-verified'">
          <i></i>{{ sessionStateLabel }}
        </span>
      </div>

      <section class="hero">
        <span>机器人一体化管理</span>
        <h2>选择工作模式</h2>
        <p>远程任务进入云端控制；靠近设备时通过机器人 Wi-Fi 进入现场配置。</p>
      </section>

      <section class="mode-grid">
        <button
          class="mode-card cloud-card"
          :class="{ 'is-disabled': offlineSession }"
          type="button"
          :disabled="offlineSession || refreshing"
          @click="$emit('cloud')"
        >
          <div class="mode-icon cloud-icon" aria-hidden="true">
            <i></i><i></i><i></i>
          </div>
          <div class="mode-copy">
            <span>远程作业</span>
            <strong>云端控制平台</strong>
            <p>{{ offlineSession ? '重新验证云端后即可进入' : '视频 · 运动控制 · 地图 · 数据 · 事件' }}</p>
          </div>
          <b aria-hidden="true">›</b>
        </button>

        <button class="mode-card local-card" type="button" @click="$emit('local')">
          <div class="mode-icon local-icon" aria-hidden="true">
            <i></i><i></i><i></i>
          </div>
          <div class="mode-copy">
            <span>设备近场维护</span>
            <strong>现场配置台</strong>
            <p>选择 Wi-Fi · 输入地址 · 4G 与视频设置</p>
          </div>
          <b aria-hidden="true">›</b>
        </button>
      </section>

      <section v-if="offlineSession || refreshError" class="recovery-card" role="status">
        <div>
          <strong>云端连接暂不可用</strong>
          <p>{{ refreshError || '当前保留有效登录会话，现场配置台仍可正常使用。' }}</p>
        </div>
        <button type="button" :disabled="refreshing" @click="$emit('refresh-session')">
          <span v-if="refreshing" class="button-spinner" aria-hidden="true"></span>
          {{ refreshing ? '验证中…' : '重新验证' }}
        </button>
      </section>

      <section class="safety-card">
        <i></i>
        <div>
          <strong>运动安全保护已启用</strong>
          <p>切换模式、退出登录、退到后台或失去焦点时，会立即释放全部运动按键。</p>
        </div>
      </section>

      <footer>
        <span>智巡精灵 App 1.2.10</span>
        <b>·</b>
        <span>蜘蛛001</span>
      </footer>
    </section>
  </main>
</template>

<script setup>
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import zxBrandLogo from '@/assets/zx-suture-mark.svg'

const props = defineProps({
  username: { type: String, default: 'admin' },
  offlineSession: { type: Boolean, default: false },
  expiresAt: { type: Number, default: null },
  refreshing: { type: Boolean, default: false },
  refreshError: { type: String, default: '' },
})
defineEmits(['cloud', 'local', 'refresh-session', 'logout'])

const networkOnline = ref(navigator.onLine)
const isDark = ref(localStorage.getItem('theme') === 'dark')

function toggleTheme() {
  isDark.value = !isDark.value
}

watch(isDark, (dark) => {
  document.documentElement.classList.toggle('dark', dark)
  document.documentElement.style.colorScheme = dark ? 'dark' : 'light'
  localStorage.setItem('theme', dark ? 'dark' : 'light')
}, { immediate: true })

const sessionLabel = computed(() => {
  if (!props.expiresAt) return '安全会话已建立'
  const date = new Date(props.expiresAt * 1000)
  if (Number.isNaN(date.getTime())) return '安全会话已建立'
  return `会话有效至 ${date.toLocaleString('zh-CN', {
    month: '2-digit',
    day: '2-digit',
    hour: '2-digit',
    minute: '2-digit',
    hour12: false,
  })}`
})
const sessionStateLabel = computed(() => {
  if (props.offlineSession) return '云端待验证'
  if (!networkOnline.value) return '网络已离线'
  return '身份已验证'
})

function syncNetworkState() {
  networkOnline.value = navigator.onLine
}

onMounted(() => {
  window.addEventListener('online', syncNetworkState)
  window.addEventListener('offline', syncNetworkState)
})
onBeforeUnmount(() => {
  window.removeEventListener('online', syncNetworkState)
  window.removeEventListener('offline', syncNetworkState)
})
</script>

<style scoped>
.workspace-page {
  position: relative;
  width: 100%;
  min-height: 100vh;
  min-height: 100dvh;
  overflow: hidden auto;
  color: #f7fbff;
  background:
    radial-gradient(circle at 84% 4%, rgba(34, 126, 255, .21), transparent 34%),
    radial-gradient(circle at 4% 94%, rgba(17, 201, 167, .11), transparent 30%),
    linear-gradient(155deg, #081321 0%, #07101d 48%, #09182a 100%);
  font-family: -apple-system, BlinkMacSystemFont, "SF Pro Display", "Segoe UI", sans-serif;
}
.grid-overlay {
  position: fixed;
  inset: 0;
  opacity: .12;
  pointer-events: none;
  background-image:
    linear-gradient(rgba(255,255,255,.06) 1px, transparent 1px),
    linear-gradient(90deg, rgba(255,255,255,.06) 1px, transparent 1px);
  background-size: 30px 30px;
  mask-image: linear-gradient(to bottom, #000, transparent 82%);
}
.workspace-shell {
  position: relative;
  z-index: 1;
  width: min(100%, 820px);
  min-height: 100vh;
  min-height: 100dvh;
  display: flex;
  flex-direction: column;
  margin: 0 auto;
  padding:
    max(22px, env(safe-area-inset-top))
    max(20px, env(safe-area-inset-right))
    max(18px, env(safe-area-inset-bottom))
    max(20px, env(safe-area-inset-left));
}
header {
  min-height: 48px;
  display: grid;
  grid-template-columns: 46px minmax(0, 1fr) 64px 64px;
  align-items: center;
  gap: 11px;
}
.brand-copy { min-width: 0; }
.brand-copy p {
  margin: 0 0 3px;
  color: #6f8ba8;
  font-size: 9px;
  font-weight: 800;
  letter-spacing: .18em;
}
.brand-copy h1 { margin: 0; font-size: 17px; line-height: 1.1; }
.brand-logo {
  display: block;
  width: 46px;
  height: 46px;
  flex: 0 0 46px;
  border-radius: 13px;
  object-fit: cover;
  box-shadow: 0 9px 28px rgba(0, 0, 0, .25);
}
.brand-mark {
  position: relative;
  width: 43px;
  height: 43px;
  flex: 0 0 43px;
  border: 1px solid rgba(98, 176, 255, .35);
  border-radius: 13px;
  background: linear-gradient(145deg, rgba(38, 139, 255, .22), rgba(16, 66, 112, .12));
}
.brand-mark i {
  position: absolute;
  left: 15px;
  top: 12px;
  width: 13px;
  height: 18px;
  border: 2px solid #75bdff;
  border-radius: 8px 8px 6px 6px;
}
.brand-mark span { position: absolute; width: 10px; height: 2px; border-radius: 2px; background: #75bdff; }
.brand-mark span:nth-child(1) { left: 7px; top: 14px; transform: rotate(26deg); }
.brand-mark span:nth-child(2) { right: 7px; top: 14px; transform: rotate(-26deg); }
.brand-mark span:nth-child(3) { left: 7px; bottom: 12px; transform: rotate(-26deg); }
.brand-mark span:nth-child(4) { right: 7px; bottom: 12px; transform: rotate(26deg); }
.logout-button {
  min-height: 44px;
  min-width: 64px;
  margin-left: 0;
  padding: 0 13px;
  border: 1px solid rgba(153, 187, 220, .17);
  border-radius: 12px;
  color: #8da6bd;
  background: rgba(255,255,255,.045);
  font-size: 11px;
  font-weight: 700;
  white-space: nowrap;
}
.logout-button:active { transform: scale(.97); }
.theme-toggle-button {
  position: relative;
  width: 58px;
  height: 32px;
  flex: 0 0 58px;
  margin-left: 0;
  padding: 0;
  border: 1px solid #a9d8b5;
  border-radius: 999px;
  color: #fff;
  background: #34c759;
  box-shadow: inset 0 1px 1px rgba(255,255,255,.28), 0 2px 7px rgba(52,199,89,.2);
  overflow: hidden;
}
.theme-toggle-button .theme-symbol {
  position: absolute;
  z-index: 1;
  left: 8px;
  top: 50%;
  width: 18px;
  height: 18px;
  display: grid;
  place-items: center;
  transform: translateY(-50%);
  color: #fff;
  font-size: 14px;
  line-height: 1;
}
.theme-toggle-button i {
  position: absolute;
  z-index: 2;
  top: 3px;
  left: 31px;
  width: 24px;
  height: 24px;
  border-radius: 50%;
  background: #fff;
  box-shadow: 0 1px 4px rgba(0,0,0,.2);
  transition: left .2s ease;
}
.theme-toggle-button.is-dark {
  border-color: #687887;
  background: #687887;
  box-shadow: inset 0 1px 1px rgba(255,255,255,.14), 0 2px 7px rgba(43,55,67,.18);
}
.theme-toggle-button.is-dark .theme-symbol { left: 32px; }
.theme-toggle-button.is-dark i { left: 3px; }
.session-strip {
  min-height: 58px;
  display: flex;
  align-items: center;
  gap: 10px;
  margin-top: 18px;
  padding: 10px 12px;
  border: 1px solid rgba(132, 177, 218, .13);
  border-radius: 15px;
  background: rgba(255,255,255,.035);
}
.session-avatar {
  width: 36px;
  height: 36px;
  display: grid;
  place-items: center;
  border-radius: 11px;
  color: #bde0ff;
  background: rgba(43, 139, 234, .18);
  font-size: 13px;
  font-weight: 800;
}
.session-copy { min-width: 0; display: grid; gap: 2px; }
.session-strip small { color: #607b94; font-size: 9px; }
.session-strip strong { font-size: 12px; }
.session-expiry {
  overflow: hidden;
  color: #547087 !important;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.session-strip > span {
  min-height: 28px;
  display: flex;
  align-items: center;
  gap: 6px;
  margin-left: auto;
  padding: 0 9px;
  border-radius: 999px;
  font-size: 9px;
  font-weight: 750;
}
.session-strip > span i { width: 6px; height: 6px; border-radius: 50%; }
.is-verified { color: #8de0c9; background: rgba(37, 167, 132, .11); }
.is-verified i { background: #45d6ac; box-shadow: 0 0 8px rgba(69,214,172,.6); }
.is-offline { color: #e3bc72; background: rgba(169, 117, 26, .12); }
.is-offline i { background: #e3aa43; }
.hero { margin-top: clamp(36px, 8vh, 72px); }
.hero > span {
  display: inline-flex;
  min-height: 26px;
  align-items: center;
  padding: 0 10px;
  border: 1px solid rgba(47, 149, 255, .28);
  border-radius: 999px;
  color: #70b9ff;
  background: rgba(15, 105, 200, .09);
  font-size: 11px;
  font-weight: 700;
}
.hero h2 {
  margin: 15px 0 8px;
  font-size: clamp(30px, 8vw, 44px);
  line-height: 1.05;
  letter-spacing: -.045em;
}
.hero p { max-width: 560px; margin: 0; color: #8299b0; font-size: 13px; line-height: 1.65; }
.mode-grid { display: grid; gap: 13px; margin-top: 30px; }
.mode-card {
  min-height: 128px;
  display: grid;
  grid-template-columns: 56px minmax(0, 1fr) 22px;
  align-items: center;
  gap: 15px;
  padding: 19px 17px;
  border: 1px solid rgba(139, 184, 227, .17);
  border-radius: 20px;
  color: #f5faff;
  background: linear-gradient(140deg, rgba(255,255,255,.075), rgba(255,255,255,.03));
  box-shadow: inset 0 1px 0 rgba(255,255,255,.08), 0 18px 50px rgba(0,0,0,.2);
  text-align: left;
}
.mode-card:active { transform: scale(.985); }
.mode-card:focus-visible,
.logout-button:focus-visible,
.recovery-card button:focus-visible {
  outline: 3px solid rgba(83, 172, 255, .35);
  outline-offset: 2px;
}
.mode-card.is-disabled {
  cursor: not-allowed;
  filter: saturate(.55);
  opacity: .58;
}
.cloud-card { border-color: rgba(56, 153, 255, .31); }
.local-card { border-color: rgba(26, 207, 169, .24); }
.mode-icon { position: relative; width: 56px; height: 56px; border-radius: 17px; }
.cloud-icon { background: linear-gradient(145deg, rgba(32, 135, 255, .28), rgba(26, 85, 152, .16)); }
.cloud-icon i { position: absolute; top: 22px; left: 16px; width: 24px; height: 15px; border: 2px solid #70b9ff; border-radius: 10px; }
.cloud-icon i:nth-child(1) { transform: translate(-7px, 4px); }
.cloud-icon i:nth-child(2) { transform: translate(7px, 4px); }
.cloud-icon i:nth-child(3) { width: 13px; height: 13px; transform: translate(0, -5px); background: #142b44; }
.local-icon { background: linear-gradient(145deg, rgba(26, 207, 169, .22), rgba(16, 115, 95, .13)); }
.local-icon i { position: absolute; left: 13px; height: 2px; border-radius: 2px; background: #5fe1c6; }
.local-icon i:nth-child(1) { top: 16px; width: 30px; }
.local-icon i:nth-child(2) { top: 26px; left: 18px; width: 20px; }
.local-icon i:nth-child(3) { top: 37px; left: 24px; width: 8px; height: 8px; border-radius: 50%; }
.mode-copy { min-width: 0; display: grid; gap: 4px; }
.mode-copy span { color: #718aa3; font-size: 10px; font-weight: 650; }
.mode-copy strong { font-size: 20px; letter-spacing: -.02em; }
.mode-copy p { margin: 1px 0 0; overflow: hidden; color: #8ca1b7; font-size: 11px; text-overflow: ellipsis; white-space: nowrap; }
.mode-card > b { color: #6f8ba8; font-size: 32px; font-weight: 200; }
.recovery-card {
  display: grid;
  grid-template-columns: minmax(0, 1fr) auto;
  align-items: center;
  gap: 12px;
  margin-top: 14px;
  padding: 13px 14px;
  border: 1px solid rgba(236, 178, 80, .2);
  border-radius: 15px;
  background: rgba(145, 91, 18, .09);
}
.recovery-card strong { color: #e8c585; font-size: 11px; }
.recovery-card p {
  margin: 4px 0 0;
  color: #8e7a5d;
  font-size: 9px;
  line-height: 1.5;
}
.recovery-card button {
  min-width: 84px;
  min-height: 40px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  padding: 0 11px;
  border: 1px solid rgba(237, 183, 89, .25);
  border-radius: 11px;
  color: #edca8b;
  background: rgba(205, 143, 47, .1);
  font-size: 10px;
  font-weight: 750;
}
.recovery-card button:disabled { opacity: .62; }
.button-spinner {
  width: 12px;
  height: 12px;
  border: 2px solid rgba(255,255,255,.24);
  border-top-color: currentColor;
  border-radius: 50%;
  animation: spin .8s linear infinite;
}
.safety-card {
  display: flex;
  gap: 11px;
  margin-top: 16px;
  padding: 14px 15px;
  border: 1px solid rgba(90, 213, 176, .14);
  border-radius: 15px;
  background: rgba(17, 102, 82, .075);
}
.safety-card > i {
  width: 8px;
  height: 8px;
  flex: 0 0 8px;
  margin-top: 4px;
  border-radius: 50%;
  background: #45d6ac;
  box-shadow: 0 0 12px rgba(69, 214, 172, .75);
}
.safety-card strong { display: block; color: #b9eadd; font-size: 11px; }
.safety-card p { margin: 4px 0 0; color: #728e8a; font-size: 10px; line-height: 1.5; }
footer {
  display: flex;
  justify-content: center;
  gap: 7px;
  margin-top: auto;
  padding-top: 22px;
  color: #536d84;
  font-size: 10px;
}
footer b { color: #365068; }
@keyframes spin { to { transform: rotate(360deg); } }
@media (min-width: 680px) {
  .mode-grid { grid-template-columns: repeat(2, minmax(0, 1fr)); }
  .mode-card { min-height: 168px; }
}
@media (max-height: 730px) {
  .session-strip { margin-top: 10px; }
  .hero { margin-top: 24px; }
  .mode-grid { margin-top: 20px; }
  .mode-card { min-height: 110px; padding-top: 14px; padding-bottom: 14px; }
}

/* App 1.2 industrial workspace */
.workspace-page {
  color: #edf5fc;
  background: #07111d;
}
.grid-overlay { display: none; }
.workspace-shell {
  width: min(100%, 820px);
  padding:
    max(18px, env(safe-area-inset-top))
    max(18px, env(safe-area-inset-right))
    max(18px, env(safe-area-inset-bottom))
    max(18px, env(safe-area-inset-left));
}
header {
  min-height: 58px;
  padding-bottom: 14px;
  border-bottom: 1px solid #203142;
}
.brand-copy p {
  color: #71869a;
  font-size: 10px;
  letter-spacing: .14em;
}
.brand-copy h1 {
  color: #f2f7fb;
  font-size: 19px;
}
.brand-mark {
  width: 44px;
  height: 44px;
  flex-basis: 44px;
  border-color: #315270;
  border-radius: 10px;
  background: #102337;
}
.brand-mark i { border-color: #65adf0; }
.brand-mark span { background: #65adf0; }
.logout-button {
  min-height: 44px;
  padding: 0 14px;
  border-color: #304253;
  border-radius: 9px;
  color: #a2b4c4;
  background: #0d1823;
  font-size: 12px;
}
.session-strip {
  min-height: 70px;
  margin-top: 14px;
  padding: 12px 13px;
  border-color: #26394b;
  border-radius: 12px;
  background: #0e1925;
}
.session-avatar {
  width: 40px;
  height: 40px;
  border: 1px solid #315170;
  border-radius: 9px;
  color: #c7e2fa;
  background: #122a40;
  font-size: 15px;
}
.session-copy { gap: 3px; }
.session-strip small {
  color: #73889c;
  font-size: 10px;
}
.session-strip strong {
  color: #f1f6fb;
  font-size: 14px;
}
.session-strip > span {
  min-height: 32px;
  padding: 0 10px;
  border: 1px solid transparent;
  border-radius: 8px;
  font-size: 10px;
}
.is-verified {
  color: #91d2bd;
  border-color: #29483f !important;
  background: #10231f;
}
.is-verified i {
  background: #35b88c;
  box-shadow: none;
}
.is-offline {
  color: #d9b66f;
  border-color: #514124 !important;
  background: #231d12;
}
.hero {
  margin-top: clamp(32px, 6vh, 54px);
}
.hero > span {
  min-height: auto;
  padding: 0;
  border: 0;
  border-radius: 0;
  color: #5fa8eb;
  background: transparent;
  font-size: 12px;
  letter-spacing: .04em;
}
.hero h2 {
  margin: 9px 0 8px;
  color: #f4f8fc;
  font-size: clamp(29px, 7vw, 39px);
  letter-spacing: -.025em;
}
.hero p {
  color: #879aab;
  font-size: 14px;
}
.mode-grid {
  gap: 12px;
  margin-top: 24px;
}
.mode-card {
  min-height: 128px;
  grid-template-columns: 52px minmax(0, 1fr) 18px;
  gap: 14px;
  padding: 18px 16px;
  border-color: #2a3d50;
  border-radius: 13px;
  background: #101c28;
  box-shadow: 0 12px 32px rgba(0, 0, 0, .18);
}
.mode-card:not(:disabled):active {
  border-color: #3c77a9;
  background: #122231;
}
.mode-card.is-disabled {
  filter: none;
  opacity: .5;
}
.cloud-card { border-left: 3px solid #438fd4; }
.local-card { border-left: 3px solid #3bab89; }
.mode-icon {
  width: 52px;
  height: 52px;
  border: 1px solid #30475c;
  border-radius: 10px;
}
.cloud-icon { background: #122b43; }
.cloud-icon i { border-color: #6cb1ed; }
.cloud-icon i:nth-child(3) { background: #122b43; }
.local-icon { background: #102a25; }
.local-icon i { background: #68cbae; }
.mode-copy { gap: 5px; }
.mode-copy span {
  color: #74899d;
  font-size: 11px;
}
.mode-copy strong {
  color: #f0f5fa;
  font-size: 19px;
}
.mode-copy p {
  color: #879aac;
  font-size: 12px;
}
.mode-card > b {
  color: #688096;
  font-size: 28px;
}
.recovery-card {
  margin-top: 12px;
  padding: 13px;
  border-color: #554424;
  border-radius: 11px;
  background: #211b12;
}
.recovery-card strong {
  color: #dfbd78;
  font-size: 12px;
}
.recovery-card p {
  color: #a28c64;
  font-size: 11px;
}
.recovery-card button {
  min-width: 92px;
  min-height: 44px;
  border-color: #6a552d;
  border-radius: 9px;
  color: #e4c681;
  background: #2c2417;
  font-size: 12px;
}
.safety-card {
  margin-top: 13px;
  padding: 13px 14px;
  border-color: #28443d;
  border-radius: 11px;
  background: #0d211f;
}
.safety-card > i {
  background: #35b88c;
  box-shadow: none;
}
.safety-card strong {
  color: #acd8ca;
  font-size: 12px;
}
.safety-card p {
  color: #789c91;
  font-size: 11px;
}
footer {
  color: #5d7285;
  font-size: 11px;
}
footer b { color: #34495c; }
@media (min-width: 680px) {
  .mode-card {
    min-height: 158px;
    align-content: center;
  }
}
@media (max-height: 730px) {
  .session-strip { margin-top: 10px; }
  .hero { margin-top: 22px; }
  .mode-grid { margin-top: 18px; }
  .mode-card { min-height: 108px; padding-top: 14px; padding-bottom: 14px; }
}

@media (max-width: 390px) {
  header { grid-template-columns: 42px minmax(0, 1fr) 58px 58px; gap: 7px; }
  .brand-logo { width: 42px; height: 42px; flex-basis: 42px; }
  .brand-copy p { font-size: 8px; }
  .brand-copy h1 { font-size: 16px; }
  .theme-toggle-button { transform: scale(.93); transform-origin: right center; }
  .logout-button { min-width: 58px; padding: 0 6px; font-size: 10px; }
}
</style>
