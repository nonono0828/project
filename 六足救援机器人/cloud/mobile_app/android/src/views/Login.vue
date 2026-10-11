<template>
  <main class="login-page">
    <div class="grid-overlay" aria-hidden="true"></div>
    <section class="login-shell">
      <header class="brand-header">
        <img class="brand-logo" :src="zxBrandLogo" alt="" aria-hidden="true" />
        <div>
          <p>SMART INSPECTION</p>
          <h1>智巡精灵</h1>
        </div>
        <span class="version">{{ platformLabel }} {{ runtimeVersion }}</span>
      </header>

      <div class="login-copy">
        <span>机器人巡检管理平台</span>
        <h2>管理员登录</h2>
        <p>登录后可进入云端控制和机器人现场配置台。</p>
      </div>

      <div class="connectivity-strip" :class="{ offline: !networkOnline }" role="status">
        <i></i>
        <span>{{ networkOnline ? '网络已连接，可进行云端身份验证' : '当前网络离线，请联网后重试' }}</span>
      </div>

      <form class="login-card" :aria-busy="loading" @submit.prevent="submit">
        <label>
          <span>管理员账号</span>
          <input
            id="admin-username"
            ref="usernameInput"
            v-model.trim="username"
            name="username"
            type="text"
            autocomplete="username"
            inputmode="text"
            maxlength="64"
            placeholder="请输入管理员账号"
            :aria-invalid="Boolean(localError && !username)"
            :disabled="loading"
          />
        </label>

        <div class="field-group">
          <label for="admin-password">管理员密码</label>
          <div class="password-field">
            <input
              id="admin-password"
              ref="passwordInput"
              v-model="password"
              name="password"
              :type="showPassword ? 'text' : 'password'"
              autocomplete="current-password"
              maxlength="256"
              placeholder="请输入管理员密码"
              :aria-invalid="Boolean(localError || error)"
              :disabled="loading"
              @keydown="trackCapsLock"
              @keyup="trackCapsLock"
              @blur="capsLock = false"
            />
            <button
              type="button"
              :aria-label="showPassword ? '隐藏密码' : '显示密码'"
              :disabled="loading"
              @click="showPassword = !showPassword"
            >
              {{ showPassword ? '隐藏' : '显示' }}
            </button>
          </div>
        </div>

        <p v-if="capsLock" class="caps-warning" role="status">大写锁定已开启，请检查密码大小写。</p>

        <p v-if="localError || error" class="login-error" role="alert">
          {{ localError || error }}
        </p>

        <button class="login-button" type="submit" :disabled="loading">
          <span v-if="loading" class="spinner" aria-hidden="true"></span>
          {{ loading ? '正在验证…' : '安全登录' }}
        </button>

        <div class="security-note">
          <i></i>
          <span>账号由云端验证；客户端不保存管理员密码。</span>
        </div>
        <details class="credential-help">
          <summary>首次使用或忘记密码？</summary>
          <p>智巡精灵没有内置默认密码。管理员账号和密码需要先在云端服务器配置；重置密码后，旧登录会话会立即失效。</p>
        </details>
        <div v-if="insecureCloud" class="transport-warning">
          当前云端仍是 HTTP 调试链路。正式发布前请切换到 HTTPS / WSS。
        </div>
      </form>

      <footer>
        <span>智巡精灵</span>
        <b>·</b>
        <span>设备管理与远程巡检</span>
      </footer>
    </section>
  </main>
</template>

<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import zxBrandLogo from '@/assets/zx-suture-mark.svg'
import cfg from '@/config/config'

const props = defineProps({
  loading: { type: Boolean, default: false },
  error: { type: String, default: '' },
  initialUsername: { type: String, default: 'admin' },
})
const emit = defineEmits(['submit'])

const username = ref(props.initialUsername || 'admin')
const password = ref('')
const showPassword = ref(false)
const localError = ref('')
const usernameInput = ref(null)
const passwordInput = ref(null)
const capsLock = ref(false)
const networkOnline = ref(navigator.onLine)
const isWindowsDesktop = window.zhixunDesktop?.platform === 'windows'
const platformLabel = isWindowsDesktop ? 'WINDOWS' : 'APP'
const runtimeVersion = ref('1.2.10')
const insecureCloud = computed(() => {
  try {
    const url = new URL(cfg.API_BASE)
    return (
      url.protocol !== 'https:'
      && !['127.0.0.1', 'localhost'].includes(url.hostname)
    )
  } catch {
    return true
  }
})

watch(() => props.error, () => {
  if (props.error) {
    password.value = ''
    nextTick(() => passwordInput.value?.focus?.())
  }
})

function submit() {
  localError.value = ''
  if (!username.value) {
    localError.value = '请输入管理员账号'
    nextTick(() => usernameInput.value?.focus?.())
    return
  }
  if (!password.value) {
    localError.value = '请输入管理员密码'
    nextTick(() => passwordInput.value?.focus?.())
    return
  }
  emit('submit', { username: username.value, password: password.value })
}

function trackCapsLock(event) {
  capsLock.value = Boolean(event?.getModifierState?.('CapsLock'))
}

function syncNetworkState() {
  networkOnline.value = navigator.onLine
}

onMounted(async () => {
  window.addEventListener('online', syncNetworkState)
  window.addEventListener('offline', syncNetworkState)
  if (isWindowsDesktop) {
    try {
      runtimeVersion.value = await window.zhixunDesktop.getVersion()
    } catch {
      // The displayed fallback stays aligned with the shared app build.
    }
  }
  await nextTick()
  if (window.matchMedia?.('(pointer: fine)').matches) {
    usernameInput.value?.focus?.()
  }
})

onBeforeUnmount(() => {
  window.removeEventListener('online', syncNetworkState)
  window.removeEventListener('offline', syncNetworkState)
})
</script>

<style scoped>
.login-page {
  position: relative;
  width: 100%;
  min-height: 100vh;
  min-height: 100dvh;
  overflow: hidden auto;
  color: #f7fbff;
  background:
    radial-gradient(circle at 82% 5%, rgba(28, 129, 239, .2), transparent 34%),
    radial-gradient(circle at 8% 92%, rgba(24, 203, 170, .11), transparent 31%),
    linear-gradient(155deg, #081321 0%, #07101d 50%, #09192a 100%);
  font-family: -apple-system, BlinkMacSystemFont, "SF Pro Display", "Segoe UI", sans-serif;
}
.grid-overlay {
  position: absolute;
  inset: 0;
  opacity: .12;
  pointer-events: none;
  background-image:
    linear-gradient(rgba(255,255,255,.06) 1px, transparent 1px),
    linear-gradient(90deg, rgba(255,255,255,.06) 1px, transparent 1px);
  background-size: 30px 30px;
  mask-image: linear-gradient(to bottom, #000, transparent 80%);
}
.login-shell {
  position: relative;
  z-index: 1;
  width: min(100%, 480px);
  min-height: 100vh;
  min-height: 100dvh;
  display: flex;
  flex-direction: column;
  margin: 0 auto;
  padding:
    max(24px, env(safe-area-inset-top))
    max(20px, env(safe-area-inset-right))
    max(18px, env(safe-area-inset-bottom))
    max(20px, env(safe-area-inset-left));
}
.brand-header {
  min-height: 54px;
  display: flex;
  align-items: center;
  gap: 12px;
}
.brand-header p {
  margin: 0 0 3px;
  color: #6f8ba8;
  font-size: 9px;
  font-weight: 800;
  letter-spacing: .18em;
}
.brand-header h1 {
  margin: 0;
  font-size: 18px;
  line-height: 1.1;
  letter-spacing: .04em;
}
.version {
  margin-left: auto;
  padding: 6px 9px;
  border: 1px solid rgba(139, 184, 227, .2);
  border-radius: 999px;
  color: #7794b1;
  background: rgba(255,255,255,.035);
  font-size: 9px;
  font-weight: 700;
}
.brand-logo {
  display: block;
  width: 48px;
  height: 48px;
  flex: 0 0 48px;
  border-radius: 14px;
  object-fit: cover;
  box-shadow: 0 10px 30px rgba(0, 0, 0, .28);
}
.brand-mark {
  position: relative;
  width: 45px;
  height: 45px;
  flex: 0 0 45px;
  border: 1px solid rgba(98, 176, 255, .36);
  border-radius: 14px;
  background: linear-gradient(145deg, rgba(38, 139, 255, .22), rgba(16, 66, 112, .12));
  box-shadow: inset 0 1px 0 rgba(255,255,255,.12), 0 10px 32px rgba(0, 89, 190, .18);
}
.brand-mark i {
  position: absolute;
  left: 15px;
  top: 12px;
  width: 13px;
  height: 19px;
  border: 2px solid #75bdff;
  border-radius: 8px 8px 6px 6px;
}
.brand-mark span {
  position: absolute;
  width: 10px;
  height: 2px;
  border-radius: 2px;
  background: #75bdff;
}
.brand-mark span:nth-child(1) { left: 7px; top: 14px; transform: rotate(26deg); }
.brand-mark span:nth-child(2) { right: 7px; top: 14px; transform: rotate(-26deg); }
.brand-mark span:nth-child(3) { left: 7px; bottom: 12px; transform: rotate(-26deg); }
.brand-mark span:nth-child(4) { right: 7px; bottom: 12px; transform: rotate(26deg); }
.login-copy { margin-top: clamp(56px, 12vh, 108px); }
.login-copy > span {
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
.login-copy h2 {
  margin: 15px 0 7px;
  font-size: clamp(30px, 9vw, 42px);
  line-height: 1.06;
  letter-spacing: -.04em;
}
.login-copy p {
  margin: 0;
  color: #8097af;
  font-size: 13px;
  line-height: 1.6;
}
.connectivity-strip {
  min-height: 35px;
  display: flex;
  align-items: center;
  gap: 8px;
  margin-top: 22px;
  padding: 0 11px;
  border: 1px solid rgba(74, 210, 170, .13);
  border-radius: 11px;
  color: #79aa9c;
  background: rgba(28, 126, 98, .07);
  font-size: 9px;
}
.connectivity-strip i {
  width: 7px;
  height: 7px;
  flex: 0 0 7px;
  border-radius: 50%;
  background: #45d6ac;
  box-shadow: 0 0 10px rgba(69,214,172,.55);
}
.connectivity-strip.offline {
  color: #c19c65;
  border-color: rgba(230, 165, 68, .16);
  background: rgba(143, 88, 14, .08);
}
.connectivity-strip.offline i {
  background: #e7aa4e;
  box-shadow: 0 0 10px rgba(231,170,78,.45);
}
.login-card {
  display: grid;
  gap: 16px;
  margin-top: 13px;
  padding: 22px 19px;
  border: 1px solid rgba(139, 184, 227, .18);
  border-radius: 20px;
  background: linear-gradient(145deg, rgba(255,255,255,.075), rgba(255,255,255,.03));
  box-shadow: inset 0 1px 0 rgba(255,255,255,.08), 0 20px 56px rgba(0,0,0,.22);
  backdrop-filter: blur(18px);
}
label {
  display: grid;
  gap: 7px;
}
label > span {
  color: #91a8bf;
  font-size: 11px;
  font-weight: 650;
}
.field-group {
  display: grid;
  gap: 7px;
}
.field-group > label {
  color: #91a8bf;
  font-size: 11px;
  font-weight: 650;
}
input {
  width: 100%;
  min-height: 48px;
  padding: 0 13px;
  border: 1px solid rgba(137, 174, 211, .22);
  border-radius: 12px;
  outline: none;
  color: #f7fbff;
  background: rgba(4, 15, 28, .62);
  font-size: 16px;
  transition: border-color .2s, box-shadow .2s;
}
input:focus {
  border-color: rgba(76, 164, 255, .8);
  box-shadow: 0 0 0 3px rgba(32, 132, 238, .13);
}
input::placeholder { color: #4f687f; }
.password-field { position: relative; }
.password-field input { padding-right: 64px; }
.password-field button {
  position: absolute;
  top: 2px;
  right: 3px;
  min-width: 54px;
  min-height: 44px;
  border: 0;
  color: #70b9ff;
  background: transparent;
  font-size: 11px;
  font-weight: 700;
}
.login-button {
  min-height: 50px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 9px;
  border: 0;
  border-radius: 13px;
  color: #fff;
  background: linear-gradient(135deg, #157fe8, #2f9cf4);
  box-shadow: 0 12px 28px rgba(20, 126, 228, .24);
  font-size: 14px;
  font-weight: 750;
}
.login-button:disabled { opacity: .65; }
.login-error {
  margin: -3px 0;
  padding: 9px 10px;
  border: 1px solid rgba(248, 96, 96, .22);
  border-radius: 10px;
  color: #ff9d9d;
  background: rgba(157, 33, 33, .12);
  font-size: 11px;
  line-height: 1.45;
}
.caps-warning {
  margin: -5px 0;
  padding: 7px 9px;
  border-radius: 9px;
  color: #e4b66f;
  background: rgba(151, 93, 17, .1);
  font-size: 10px;
  line-height: 1.4;
}
.security-note {
  display: flex;
  align-items: center;
  gap: 8px;
  color: #688399;
  font-size: 10px;
  line-height: 1.45;
}
.security-note i {
  width: 7px;
  height: 7px;
  flex: 0 0 7px;
  border-radius: 50%;
  background: #45d6ac;
  box-shadow: 0 0 10px rgba(69, 214, 172, .6);
}
.transport-warning {
  padding: 9px 10px;
  border: 1px solid rgba(255, 172, 64, .2);
  border-radius: 10px;
  color: #e5b46e;
  background: rgba(159, 91, 12, .1);
  font-size: 10px;
  line-height: 1.5;
}
.spinner {
  width: 16px;
  height: 16px;
  border: 2px solid rgba(255,255,255,.35);
  border-top-color: #fff;
  border-radius: 50%;
  animation: spin .8s linear infinite;
}
footer {
  display: flex;
  justify-content: center;
  gap: 7px;
  margin-top: auto;
  padding-top: 24px;
  color: #536d84;
  font-size: 10px;
}
footer b { color: #365068; }
@keyframes spin { to { transform: rotate(360deg); } }
@media (max-height: 720px) {
  .login-copy { margin-top: 28px; }
  .connectivity-strip { margin-top: 14px; }
  .login-card { padding-top: 17px; padding-bottom: 17px; }
}
@media (min-width: 760px) {
  .login-shell { width: min(100%, 560px); }
}

/* App 1.2 industrial entry surface */
.login-page {
  color: #edf5fc;
  background: #07111d;
}
.grid-overlay { display: none; }
.login-shell {
  width: min(100%, 460px);
  padding:
    max(20px, env(safe-area-inset-top))
    max(18px, env(safe-area-inset-right))
    max(18px, env(safe-area-inset-bottom))
    max(18px, env(safe-area-inset-left));
}
.brand-header {
  min-height: 58px;
  padding-bottom: 14px;
  border-bottom: 1px solid #203142;
}
.brand-header p {
  margin-bottom: 4px;
  color: #71869a;
  font-size: 10px;
  letter-spacing: .14em;
}
.brand-header h1 {
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
  box-shadow: none;
}
.brand-mark i { border-color: #65adf0; }
.brand-mark span { background: #65adf0; }
.version {
  padding: 6px 8px;
  border-color: #2a3c4e;
  border-radius: 6px;
  color: #8094a8;
  background: #0c1824;
  font-size: 10px;
}
.login-copy {
  margin-top: clamp(42px, 9vh, 78px);
}
.login-copy > span {
  min-height: auto;
  padding: 0;
  border: 0;
  border-radius: 0;
  color: #5fa8eb;
  background: transparent;
  font-size: 12px;
  letter-spacing: .04em;
}
.login-copy h2 {
  margin: 10px 0 8px;
  color: #f4f8fc;
  font-size: clamp(30px, 8vw, 38px);
  letter-spacing: -.025em;
}
.login-copy p {
  color: #8599ac;
  font-size: 14px;
}
.connectivity-strip {
  min-height: 44px;
  margin-top: 26px;
  padding: 0 13px;
  border-color: #28443d;
  border-radius: 10px;
  color: #8bc5b4;
  background: #0d211f;
  font-size: 12px;
}
.connectivity-strip i {
  width: 8px;
  height: 8px;
  flex-basis: 8px;
  background: #35b88c;
  box-shadow: none;
}
.connectivity-strip.offline {
  color: #d7b36d;
  border-color: #514124;
  background: #231d12;
}
.connectivity-strip.offline i {
  background: #d7a84b;
  box-shadow: none;
}
.login-card {
  gap: 17px;
  margin-top: 10px;
  padding: 22px 18px;
  border-color: #26394b;
  border-radius: 14px;
  background: #0e1925;
  box-shadow: 0 18px 44px rgba(0, 0, 0, .24);
  backdrop-filter: none;
}
label,
.field-group {
  gap: 8px;
}
label > span,
.field-group > label {
  color: #a8bacb;
  font-size: 13px;
  font-weight: 650;
}
input {
  min-height: 52px;
  padding: 0 14px;
  border-color: #304558;
  border-radius: 10px;
  color: #f2f7fb;
  background: #09131e;
}
input:focus {
  border-color: #3f91dd;
  box-shadow: 0 0 0 3px rgba(63, 145, 221, .16);
}
input::placeholder { color: #586e82; }
.password-field button {
  right: 4px;
  min-width: 58px;
  min-height: 48px;
  color: #65adf0;
  font-size: 12px;
}
.login-button {
  min-height: 52px;
  border: 1px solid #428fd4;
  border-radius: 10px;
  background: #247bc7;
  box-shadow: none;
  font-size: 15px;
}
.login-button:not(:disabled):active { background: #1f6eaf; }
.login-error,
.caps-warning,
.transport-warning {
  border-radius: 9px;
  font-size: 12px;
}
.login-error {
  border-color: #633538;
  color: #ffaaaa;
  background: #29171b;
}
.caps-warning,
.transport-warning {
  color: #dab36b;
  background: #241d12;
}
.security-note {
  color: #71879a;
  font-size: 11px;
}
.security-note i {
  background: #35b88c;
  box-shadow: none;
}
footer {
  color: #5d7285;
  font-size: 11px;
}
footer b { color: #34495c; }
@media (max-height: 720px) {
  .login-copy { margin-top: 24px; }
  .connectivity-strip { margin-top: 16px; }
  .login-card { padding-top: 17px; padding-bottom: 17px; }
}
@media (min-width: 760px) {
  .login-shell { width: min(100%, 460px); }
}

/* Apple-style cloud login surface */
.login-page {
  display: grid;
  place-items: center;
  padding:
    max(16px, env(safe-area-inset-top))
    max(16px, env(safe-area-inset-right))
    max(16px, env(safe-area-inset-bottom))
    max(16px, env(safe-area-inset-left));
  color: #fff;
  isolation: isolate;
  background:
    radial-gradient(circle at 18% 12%, rgba(64, 142, 221, .22), transparent 34%),
    radial-gradient(circle at 84% 86%, rgba(45, 152, 145, .14), transparent 38%),
    linear-gradient(155deg, #091521 0%, #0b1b2b 46%, #07111c 100%);
}
.login-page::before,
.login-page::after {
  position: fixed;
  z-index: -1;
  content: "";
  pointer-events: none;
  border-radius: 50%;
  filter: blur(2px);
}
.login-page::before {
  top: -18%;
  right: -18%;
  width: min(76vw, 760px);
  aspect-ratio: 1;
  background: radial-gradient(circle, rgba(189, 220, 255, .3), rgba(80, 148, 224, .08) 42%, transparent 69%);
}
.login-page::after {
  bottom: -26%;
  left: -24%;
  width: min(86vw, 820px);
  aspect-ratio: 1;
  background: radial-gradient(circle, rgba(87, 190, 196, .17), rgba(64, 115, 166, .05) 46%, transparent 70%);
}
.grid-overlay {
  display: block;
  position: fixed;
  inset: 0;
  opacity: 1;
  pointer-events: none;
  background:
    linear-gradient(180deg, rgba(255, 255, 255, .04), transparent 24%),
    radial-gradient(circle at 50% 38%, transparent 0, transparent 26%, rgba(1, 5, 10, .16) 72%, rgba(1, 4, 8, .48) 100%);
  mask-image: none;
}
.login-shell {
  width: min(100%, 440px);
  min-height: 0;
  max-height: calc(100dvh - 32px);
  margin: 0;
  padding: 24px;
  overflow: hidden auto;
  border: 1px solid rgba(255, 255, 255, .24);
  border-radius: 30px;
  background:
    linear-gradient(145deg, rgba(255, 255, 255, .18), rgba(255, 255, 255, .07)),
    rgba(13, 23, 34, .34);
  box-shadow:
    inset 0 1px 0 rgba(255, 255, 255, .34),
    inset 0 -1px 0 rgba(255, 255, 255, .05),
    0 34px 90px rgba(0, 0, 0, .42),
    0 8px 28px rgba(0, 0, 0, .18);
  backdrop-filter: blur(34px) saturate(165%);
  -webkit-backdrop-filter: blur(34px) saturate(165%);
  scrollbar-width: none;
  animation: login-surface-in .5s cubic-bezier(.22, 1, .36, 1) both;
}
.login-shell::-webkit-scrollbar {
  display: none;
}
.brand-header {
  min-height: 48px;
  padding: 0;
  border: 0;
  gap: 12px;
}
.brand-header p {
  margin: 0 0 3px;
  color: rgba(226, 239, 252, .63);
  font-size: 9px;
  font-weight: 700;
  letter-spacing: .18em;
}
.brand-header h1 {
  color: #fff;
  font-size: 18px;
  font-weight: 650;
  letter-spacing: .025em;
  text-shadow: 0 1px 14px rgba(0, 0, 0, .28);
}
.brand-mark {
  width: 48px;
  height: 48px;
  flex-basis: 48px;
  border: 1px solid rgba(255, 255, 255, .27);
  border-radius: 15px;
  background:
    linear-gradient(145deg, rgba(255, 255, 255, .2), rgba(255, 255, 255, .07)),
    rgba(22, 98, 177, .24);
  box-shadow:
    inset 0 1px 0 rgba(255, 255, 255, .32),
    0 10px 26px rgba(1, 12, 24, .24);
}
.brand-mark i {
  border-color: rgba(225, 242, 255, .95);
}
.brand-mark span {
  background: rgba(225, 242, 255, .95);
}
.version {
  padding: 6px 10px;
  border: 1px solid rgba(255, 255, 255, .16);
  border-radius: 999px;
  color: rgba(235, 245, 255, .72);
  background: rgba(255, 255, 255, .09);
  font-size: 9px;
  font-weight: 650;
  letter-spacing: .035em;
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, .11);
}
.login-copy {
  margin-top: clamp(30px, 6vh, 48px);
}
.login-copy > span {
  min-height: 27px;
  padding: 0 11px;
  border: 1px solid rgba(255, 255, 255, .17);
  border-radius: 999px;
  color: rgba(232, 244, 255, .82);
  background: rgba(255, 255, 255, .085);
  font-size: 10px;
  font-weight: 620;
  letter-spacing: .035em;
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, .08);
}
.login-copy h2 {
  margin: 17px 0 8px;
  color: #fff;
  font-size: clamp(31px, 8vw, 39px);
  font-weight: 680;
  line-height: 1.08;
  letter-spacing: -.04em;
  text-shadow: 0 3px 22px rgba(0, 0, 0, .32);
}
.login-copy p {
  color: rgba(224, 237, 249, .68);
  font-size: 13px;
  line-height: 1.6;
}
.connectivity-strip {
  min-height: 38px;
  margin-top: 22px;
  padding: 0 12px;
  border: 1px solid rgba(144, 255, 218, .17);
  border-radius: 12px;
  color: rgba(195, 244, 228, .78);
  background: rgba(29, 126, 95, .12);
  font-size: 10px;
  backdrop-filter: blur(12px);
  -webkit-backdrop-filter: blur(12px);
}
.connectivity-strip i {
  width: 7px;
  height: 7px;
  flex-basis: 7px;
  background: #65dfb9;
  box-shadow: 0 0 0 4px rgba(101, 223, 185, .09), 0 0 14px rgba(101, 223, 185, .5);
}
.connectivity-strip.offline {
  color: rgba(255, 220, 170, .86);
  border-color: rgba(255, 196, 108, .18);
  background: rgba(141, 88, 19, .15);
}
.connectivity-strip.offline i {
  background: #ffc46c;
  box-shadow: 0 0 0 4px rgba(255, 196, 108, .09), 0 0 14px rgba(255, 196, 108, .42);
}
.login-card {
  gap: 16px;
  margin-top: 12px;
  padding: 20px;
  border: 1px solid rgba(255, 255, 255, .18);
  border-radius: 22px;
  background:
    linear-gradient(145deg, rgba(255, 255, 255, .13), rgba(255, 255, 255, .055)),
    rgba(5, 14, 24, .2);
  box-shadow:
    inset 0 1px 0 rgba(255, 255, 255, .18),
    0 14px 38px rgba(0, 0, 0, .14);
  backdrop-filter: blur(20px) saturate(140%);
  -webkit-backdrop-filter: blur(20px) saturate(140%);
}
label,
.field-group {
  gap: 8px;
}
label > span,
.field-group > label {
  color: rgba(235, 244, 252, .78);
  font-size: 12px;
  font-weight: 580;
}
input {
  min-height: 50px;
  padding: 0 14px;
  border: 1px solid rgba(255, 255, 255, .18);
  border-radius: 13px;
  color: #fff;
  background: rgba(3, 11, 19, .38);
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, .035);
  font-size: 16px;
  caret-color: #8cc8ff;
  transition: border-color .2s ease, background .2s ease, box-shadow .2s ease;
}
input:hover:not(:disabled) {
  border-color: rgba(255, 255, 255, .27);
  background: rgba(3, 11, 19, .45);
}
input:focus {
  border-color: rgba(140, 200, 255, .72);
  background: rgba(3, 11, 19, .5);
  box-shadow:
    0 0 0 4px rgba(76, 157, 232, .14),
    inset 0 1px 0 rgba(255, 255, 255, .05);
}
input::placeholder {
  color: rgba(207, 223, 238, .37);
}
.password-field button {
  right: 4px;
  min-width: 58px;
  min-height: 46px;
  border-radius: 10px;
  color: #a8d4ff;
  font-size: 11px;
  font-weight: 650;
  transition: color .2s ease, background .2s ease;
}
.password-field button:hover:not(:disabled) {
  color: #fff;
  background: rgba(255, 255, 255, .07);
}
.login-button {
  min-height: 51px;
  border: 1px solid rgba(255, 255, 255, .22);
  border-radius: 14px;
  color: #fff;
  background: linear-gradient(180deg, #3299f4 0%, #0879dc 100%);
  box-shadow:
    inset 0 1px 0 rgba(255, 255, 255, .28),
    0 12px 28px rgba(0, 91, 184, .3);
  font-size: 14px;
  font-weight: 680;
  letter-spacing: .02em;
  transition: transform .16s ease, filter .16s ease, box-shadow .16s ease;
}
.login-button:hover:not(:disabled) {
  filter: brightness(1.06);
  box-shadow:
    inset 0 1px 0 rgba(255, 255, 255, .3),
    0 14px 32px rgba(0, 91, 184, .36);
}
.login-button:active:not(:disabled) {
  transform: scale(.985);
  filter: brightness(.96);
}
.login-button:disabled {
  opacity: .62;
  box-shadow: none;
}
.login-error,
.caps-warning,
.transport-warning {
  border-radius: 12px;
  font-size: 11px;
  backdrop-filter: blur(10px);
  -webkit-backdrop-filter: blur(10px);
}
.login-error {
  border-color: rgba(255, 129, 129, .22);
  color: #ffc2c2;
  background: rgba(159, 32, 42, .18);
}
.caps-warning,
.transport-warning {
  color: #ffdcaa;
  border: 1px solid rgba(255, 199, 116, .16);
  background: rgba(139, 87, 18, .17);
}
.security-note {
  color: rgba(214, 230, 243, .55);
  font-size: 10px;
}
.security-note i {
  background: #65dfb9;
  box-shadow: 0 0 10px rgba(101, 223, 185, .45);
}
.credential-help {
  padding: 0 11px;
  border: 1px solid rgba(255, 255, 255, .13);
  border-radius: 11px;
  color: rgba(229, 240, 250, .68);
  background: rgba(255, 255, 255, .055);
  font-size: 11px;
}
.credential-help summary {
  min-height: 40px;
  display: flex;
  align-items: center;
  cursor: pointer;
  color: rgba(235, 245, 255, .84);
  font-weight: 650;
}
.credential-help p {
  margin: -1px 0 11px;
  color: rgba(211, 227, 241, .58);
  line-height: 1.55;
}
footer {
  margin-top: 18px;
  padding-top: 0;
  color: rgba(216, 231, 244, .46);
  font-size: 10px;
}
footer b {
  color: rgba(216, 231, 244, .28);
}
@keyframes login-surface-in {
  from {
    opacity: 0;
    transform: translateY(14px) scale(.985);
  }
  to {
    opacity: 1;
    transform: translateY(0) scale(1);
  }
}
@media (max-width: 520px) {
  .login-page {
    padding:
      max(12px, env(safe-area-inset-top))
      max(12px, env(safe-area-inset-right))
      max(12px, env(safe-area-inset-bottom))
      max(12px, env(safe-area-inset-left));
  }
  .login-shell {
    width: 100%;
    max-height: calc(100dvh - 24px);
    padding: 21px 18px 18px;
    border-radius: 27px;
  }
  .login-copy {
    margin-top: clamp(27px, 5.5vh, 43px);
  }
  .login-card {
    padding: 18px;
    border-radius: 20px;
  }
}
@media (max-height: 720px) {
  .login-shell {
    padding-top: 18px;
    padding-bottom: 16px;
  }
  .login-copy {
    margin-top: 20px;
  }
  .login-copy h2 {
    margin-top: 12px;
    font-size: 30px;
  }
  .connectivity-strip {
    margin-top: 14px;
  }
  .login-card {
    padding-top: 16px;
    padding-bottom: 16px;
  }
  footer {
    margin-top: 13px;
  }
}
@media (prefers-reduced-motion: reduce) {
  .login-shell {
    animation: none;
  }
  .login-button {
    transition: none;
  }
}

/* Wide desktop composition. The shared mobile surface remains unchanged. */
@media (min-width: 900px) {
  .login-page {
    padding: 32px;
  }

  .login-shell {
    width: min(100%, 1040px);
    min-height: min(610px, calc(100dvh - 64px));
    max-height: none;
    display: grid;
    grid-template-columns: minmax(0, 1fr) minmax(390px, 430px);
    grid-template-rows: auto minmax(0, 1fr) auto auto;
    grid-template-areas:
      "brand form"
      "copy form"
      "status form"
      "footer form";
    column-gap: 72px;
    row-gap: 18px;
    padding: 42px 48px;
    overflow: visible;
    border-radius: 32px;
    border-color: rgba(255, 255, 255, .82);
    background: rgba(255, 255, 255, .72);
    box-shadow:
      inset 0 1px 0 rgba(255, 255, 255, .95),
      0 28px 76px rgba(36, 53, 68, .16);
    backdrop-filter: blur(30px) saturate(135%);
    -webkit-backdrop-filter: blur(30px) saturate(135%);
  }

  .login-page .brand-header {
    grid-area: brand;
    min-height: 64px !important;
    padding: 0 !important;
    border: 0 !important;
    border-radius: 0 !important;
    background: transparent !important;
    box-shadow: none !important;
  }

  .brand-logo {
    width: 56px;
    height: 56px;
    flex-basis: 56px;
    border-radius: 16px;
    box-shadow: 0 14px 28px rgba(16, 25, 34, .18);
  }

  .brand-header p {
    margin-bottom: 5px;
    font-size: 10px;
  }

  .brand-header h1 {
    font-size: 22px;
  }

  .version {
    padding: 7px 11px;
    font-size: 9px;
  }

  .login-copy {
    grid-area: copy;
    align-self: center;
    margin: 0;
    padding: 32px 0;
  }

  .login-copy > span {
    min-height: 30px;
    padding: 0 13px;
    font-size: 11px;
  }

  .login-copy h2 {
    max-width: 460px;
    margin: 22px 0 13px;
    font-size: 48px;
    line-height: 1.08;
    letter-spacing: 0;
  }

  .login-copy p {
    max-width: 430px;
    margin: 0;
    font-size: 15px;
    line-height: 1.75;
  }

  .connectivity-strip {
    grid-area: status;
    width: min(100%, 430px);
    min-height: 42px;
    margin: 0;
    padding: 0 14px;
    font-size: 11px;
  }

  .login-card {
    grid-area: form;
    align-self: center;
    width: 100%;
    margin: 0;
    padding: 32px;
    gap: 18px;
    border-radius: 24px;
  }

  .login-card input {
    min-height: 52px;
  }

  .login-button {
    min-height: 52px;
  }

  footer {
    grid-area: footer;
    justify-content: flex-start;
    margin: 0;
    padding: 0;
    font-size: 11px;
  }

  :global(.dark) .login-shell {
    border-color: rgba(255, 255, 255, .12);
    background: rgba(17, 27, 37, .78);
    box-shadow:
      inset 0 1px 0 rgba(255, 255, 255, .1),
      0 30px 82px rgba(0, 0, 0, .38);
  }
}
</style>
