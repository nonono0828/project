<template>
  <main class="setup-page">
    <div class="grid-overlay" aria-hidden="true"></div>
    <section class="setup-shell">
      <header>
        <button class="icon-button" type="button" aria-label="返回功能首页" @click="$emit('back')">‹</button>
        <div>
          <strong>现场配置连接</strong>
          <small>智巡精灵 · 近场维护</small>
        </div>
        <button class="logout-button" type="button" @click="$emit('logout')">退出</button>
      </header>

      <section class="intro">
        <span>LOCAL SETUP</span>
        <h1>连接机器人现场配置台</h1>
        <p v-if="isWindowsDesktop">可按需打开 Windows Wi-Fi 设置；输入配置地址后即可直接尝试打开。</p>
        <p v-else>先通过系统 Wi‑Fi 页面连接机器人热点，再输入设备提供的配置地址。</p>
      </section>

      <ol class="stepper" aria-label="现场配置步骤">
        <li :class="{ active: true, complete: canUseAddress }"><i>1</i><span>连接 Wi‑Fi</span></li>
        <li :class="{ active: canUseAddress, complete: addressReady }"><i>2</i><span>填写地址</span></li>
        <li :class="{ active: addressReady }"><i>3</i><span>打开配置台</span></li>
      </ol>

      <section class="setup-card wifi-card">
        <div class="card-heading">
          <div class="card-icon wifi-icon" aria-hidden="true"><i></i><i></i><i></i></div>
          <div>
            <small>步骤 1</small>
            <h2>选择并连接 Wi‑Fi</h2>
          </div>
          <span :class="canUseAddress ? 'status-ok' : 'status-waiting'">
            {{ wifiStatusLabel }}
          </span>
        </div>

        <p v-if="isWindowsDesktop">由 Windows 显示附近网络并处理 Wi-Fi 密码，智巡精灵不会读取或保存密码。</p>
        <p v-else>由 Android 系统显示附近网络并处理 Wi‑Fi 密码，智巡精灵不会读取或保存密码。</p>

        <p v-if="isWindowsDesktop" class="system-wifi-state">{{ systemWifiMessage }}</p>

        <button class="primary-action" type="button" :disabled="openingWifi" @click="openWifi">
          <span v-if="openingWifi" class="spinner"></span>
          {{ openingWifi ? '正在打开…' : '打开系统 Wi‑Fi' }}
        </button>

        <div v-if="wifiPanelReturned" class="returned-tip">
          <i></i>
          <span>{{ wifiReturnMessage }}</span>
        </div>

        <button
          v-if="!isWindowsDesktop && wifiPanelReturned && !wifiConfirmed"
          class="confirm-action"
          type="button"
          @click="wifiConfirmed = true"
        >
          我已连接机器人 Wi‑Fi
        </button>

        <p v-if="wifiError" class="inline-error">{{ wifiError }}</p>
      </section>

      <section class="setup-card address-card" :class="{ disabled: !canUseAddress }">
        <div class="card-heading">
          <div class="card-icon address-icon" aria-hidden="true">⌁</div>
          <div>
            <small>步骤 2</small>
            <h2>输入配置地址</h2>
          </div>
        </div>

        <label>
          <span>机器人配置地址</span>
          <input
            v-model.trim="addressInput"
            type="url"
            inputmode="url"
            autocomplete="url"
            spellcheck="false"
            :disabled="!canUseAddress"
            placeholder="例如：192.168.4.1:8081"
            @keydown.enter.prevent="openPortal"
          />
        </label>

        <div v-if="canUseAddress" class="address-suggestions">
          <button
            v-if="addressInput !== DEFAULT_PORTAL_ADDRESS"
            class="last-address"
            type="button"
            @click="addressInput = DEFAULT_PORTAL_ADDRESS"
          >
            使用默认地址：192.168.4.1:8081
          </button>
          <button
            v-if="lastAddress && addressInput !== lastAddress"
            class="last-address"
            type="button"
            @click="addressInput = lastAddress"
          >
            使用上次地址：{{ lastAddress }}
          </button>
        </div>

        <p v-if="isWindowsDesktop" class="address-help">可输入任意有效的 HTTP 或 HTTPS 配置地址；缺少协议时自动补充 http://。</p>
        <p v-else class="address-help">当前固件配置主机固定为 192.168.4.1，可按设备实际服务修改端口和路径；缺少协议时自动补充 http://。</p>
        <p v-if="addressError" class="inline-error" role="alert">{{ addressError }}</p>

        <button
          class="open-action"
          type="button"
          :disabled="!canUseAddress || !addressInput || checkingAddress"
          @click="openPortal"
        >
          {{ isWindowsDesktop ? '打开配置台' : (checkingAddress ? '正在检查配置地址…' : '校验地址并打开配置台') }}
          <span aria-hidden="true">›</span>
        </button>
      </section>

      <section class="privacy-note">
        <i></i>
        <p><strong>低权限设计</strong><br />使用系统 Wi‑Fi 面板，无需附近设备、定位或 Wi‑Fi 扫描权限。</p>
      </section>

      <details class="troubleshooting">
        <summary>连接不上时如何检查</summary>
        <ol>
          <li>确认机器人“本地配置热点”仍处于开启倒计时内。</li>
          <li>在系统 Wi‑Fi 页面确认手机连接的是机器人热点，即使系统提示“无法访问互联网”。</li>
          <li>关闭手机 VPN 或代理后，重新使用默认地址打开。</li>
        </ol>
      </details>
    </section>
  </main>
</template>

<script setup>
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue'
import {
  isNativeWifiSettingsAvailable,
  openSystemWifiSettings,
} from '@/native/wifiSettings'
import {
  normalizeDesktopPortalAddress,
  normalizeLocalPortalAddress,
} from '@/native/localPortalAddress'

const emit = defineEmits(['back', 'logout', 'open'])

const LAST_ADDRESS_KEY = 'zhixun_last_local_portal_address'
const DEFAULT_PORTAL_ADDRESS = 'http://192.168.4.1:8081/'
const lastAddress = ref(localStorage.getItem(LAST_ADDRESS_KEY) || '')
const addressInput = ref(lastAddress.value || DEFAULT_PORTAL_ADDRESS)
const addressError = ref('')
const wifiError = ref('')
const openingWifi = ref(false)
const awaitingWifiReturn = ref(false)
const wifiPanelReturned = ref(false)
const wifiConfirmed = ref(false)
const checkingAddress = ref(false)

const isWindowsDesktop = window.zhixunDesktop?.platform === 'windows'
const canUseAddress = computed(() => isWindowsDesktop || wifiConfirmed.value)
const addressReady = computed(() => canUseAddress.value && Boolean(addressInput.value))
const wifiStatusLabel = computed(() => {
  if (isWindowsDesktop) return '可直接继续'
  if (wifiConfirmed.value) return '已确认'
  return '待连接'
})
const systemWifiMessage = computed(() => {
  return '可打开 Windows Wi-Fi 设置选择网络；软件不读取连接状态，也不限制进入配置台。'
})
const wifiReturnMessage = computed(() => {
  if (isWindowsDesktop) return 'Windows Wi-Fi 设置已打开；可直接输入地址并尝试打开配置台。'
  return '已从系统 Wi‑Fi 页面返回。系统不会把连接结果交给 App，请确认手机已经连上机器人热点。'
})

async function openWifi() {
  window.dispatchEvent(new CustomEvent('robot-app-release-controls'))
  wifiError.value = ''
  if (!isWindowsDesktop) wifiConfirmed.value = false
  wifiPanelReturned.value = false
  openingWifi.value = true
  try {
    if (!isNativeWifiSettingsAvailable()) {
      wifiPanelReturned.value = true
      wifiError.value = '网页预览无法打开 Android 系统面板，请手动连接机器人 Wi‑Fi 后继续。'
      return
    }
    awaitingWifiReturn.value = true
    const result = await openSystemWifiSettings()
    if (!result?.opened) {
      awaitingWifiReturn.value = false
      throw new Error('未能打开系统 Wi‑Fi 页面')
    }
    if (isWindowsDesktop) {
      awaitingWifiReturn.value = false
      wifiPanelReturned.value = true
    }
  } catch (error) {
    awaitingWifiReturn.value = false
    wifiError.value = error?.message || '无法打开系统 Wi‑Fi 页面'
  } finally {
    openingWifi.value = false
  }
}

function onAppResume() {
  if (!awaitingWifiReturn.value) return
  awaitingWifiReturn.value = false
  wifiPanelReturned.value = true
}

async function probeAddress(url) {
  const controller = new AbortController()
  const timeout = window.setTimeout(() => controller.abort(), 6000)
  try {
    await fetch(url, {
      cache: 'no-store',
      credentials: 'omit',
      mode: 'no-cors',
      referrerPolicy: 'no-referrer',
      signal: controller.signal,
    })
  } finally {
    window.clearTimeout(timeout)
  }
}

async function openPortal() {
  addressError.value = ''
  if (!canUseAddress.value) {
    addressError.value = '请先连接并确认机器人 Wi‑Fi'
    return
  }
  checkingAddress.value = true
  try {
    const normalized = isWindowsDesktop
      ? normalizeDesktopPortalAddress(addressInput.value)
      : normalizeLocalPortalAddress(addressInput.value)
    if (!isWindowsDesktop) await probeAddress(normalized)
    addressInput.value = normalized
    lastAddress.value = normalized
    localStorage.setItem(LAST_ADDRESS_KEY, normalized)
    emit('open', normalized)
  } catch (error) {
    if (error?.name === 'AbortError') {
      addressError.value = '连接配置地址超时，请检查 Wi‑Fi 和地址'
    } else if (error instanceof TypeError) {
      addressError.value = '无法连接配置地址，请确认已连接机器人 Wi‑Fi'
    } else {
      addressError.value = error?.message || '配置地址不可用'
    }
  } finally {
    checkingAddress.value = false
  }
}

watch(addressInput, () => {
  addressError.value = ''
})

onMounted(() => window.addEventListener('robot-app-resume', onAppResume))
onBeforeUnmount(() => window.removeEventListener('robot-app-resume', onAppResume))
</script>

<style scoped>
.setup-page {
  position: relative;
  width: 100%;
  min-height: 100vh;
  min-height: 100dvh;
  overflow: hidden auto;
  color: #f7fbff;
  background:
    radial-gradient(circle at 85% 0%, rgba(29, 130, 235, .2), transparent 31%),
    linear-gradient(155deg, #081321, #07101d 52%, #091829);
  font-family: -apple-system, BlinkMacSystemFont, "SF Pro Display", "Segoe UI", sans-serif;
}
.grid-overlay {
  position: fixed;
  inset: 0;
  opacity: .1;
  pointer-events: none;
  background-image:
    linear-gradient(rgba(255,255,255,.06) 1px, transparent 1px),
    linear-gradient(90deg, rgba(255,255,255,.06) 1px, transparent 1px);
  background-size: 30px 30px;
  mask-image: linear-gradient(to bottom, #000, transparent 85%);
}
.setup-shell {
  position: relative;
  z-index: 1;
  width: min(100%, 720px);
  min-height: 100vh;
  min-height: 100dvh;
  margin: 0 auto;
  padding:
    max(14px, env(safe-area-inset-top))
    max(18px, env(safe-area-inset-right))
    max(22px, env(safe-area-inset-bottom))
    max(18px, env(safe-area-inset-left));
}
header {
  min-height: 54px;
  display: grid;
  grid-template-columns: 46px minmax(0, 1fr) 64px;
  align-items: center;
  gap: 9px;
}
header > div { display: grid; gap: 2px; min-width: 0; text-align: center; }
header strong { font-size: 14px; }
header small { color: #6c86a0; font-size: 9px; }
.icon-button, .logout-button {
  min-height: 44px;
  border: 1px solid rgba(137, 177, 214, .15);
  border-radius: 12px;
  color: #c9def1;
  background: rgba(255,255,255,.045);
}
.icon-button { font-size: 30px; font-weight: 200; }
.logout-button { min-width: 64px; padding: 0 7px; font-size: 10px; font-weight: 700; white-space: nowrap; }
.intro { margin-top: 30px; }
.intro > span {
  color: #69b6ff;
  font-size: 9px;
  font-weight: 800;
  letter-spacing: .16em;
}
.intro h1 { margin: 10px 0 7px; font-size: clamp(27px, 7vw, 38px); line-height: 1.1; letter-spacing: -.035em; }
.intro p { margin: 0; color: #8098af; font-size: 12px; line-height: 1.65; }
.stepper {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 4px;
  margin: 24px 0 0;
  padding: 0;
  list-style: none;
}
.stepper li {
  position: relative;
  display: flex;
  align-items: center;
  gap: 6px;
  color: #536e87;
  font-size: 9px;
}
.stepper li:not(:last-child)::after {
  content: "";
  position: absolute;
  left: 28px;
  right: 3px;
  top: 11px;
  height: 1px;
  background: rgba(124, 163, 200, .18);
}
.stepper i {
  position: relative;
  z-index: 1;
  width: 23px;
  height: 23px;
  display: grid;
  place-items: center;
  flex: 0 0 23px;
  border: 1px solid rgba(121, 162, 202, .19);
  border-radius: 50%;
  background: #0c1a2a;
  font-size: 9px;
  font-style: normal;
}
.stepper .active { color: #90badd; }
.stepper .active i { border-color: rgba(54, 151, 247, .58); color: #8fcaff; background: rgba(34, 126, 222, .14); }
.stepper .complete i { border-color: rgba(64, 212, 171, .54); color: #7ce0c4; background: rgba(36, 171, 134, .13); }
.setup-card {
  margin-top: 16px;
  padding: 18px;
  border: 1px solid rgba(136, 181, 221, .17);
  border-radius: 19px;
  background: linear-gradient(145deg, rgba(255,255,255,.07), rgba(255,255,255,.025));
  box-shadow: inset 0 1px 0 rgba(255,255,255,.07), 0 16px 45px rgba(0,0,0,.18);
}
.setup-card.disabled { opacity: .58; }
.card-heading {
  display: grid;
  grid-template-columns: 46px minmax(0, 1fr) auto;
  align-items: center;
  gap: 11px;
}
.card-heading > div:nth-child(2) { display: grid; gap: 3px; }
.card-heading small { color: #668198; font-size: 9px; }
.card-heading h2 { margin: 0; font-size: 15px; }
.card-icon {
  width: 46px;
  height: 46px;
  display: grid;
  place-items: center;
  border-radius: 14px;
  background: rgba(33, 132, 228, .16);
}
.wifi-icon { position: relative; }
.wifi-icon i { position: absolute; height: 2px; border-radius: 2px; background: #70bdff; }
.wifi-icon i:nth-child(1) { width: 27px; top: 13px; }
.wifi-icon i:nth-child(2) { width: 18px; top: 22px; }
.wifi-icon i:nth-child(3) { width: 7px; height: 7px; top: 31px; border-radius: 50%; }
.address-icon { color: #6fe1c5; background: rgba(30, 182, 145, .13); font-size: 24px; }
.card-heading > span {
  min-height: 26px;
  display: flex;
  align-items: center;
  padding: 0 8px;
  border-radius: 999px;
  font-size: 9px;
  font-weight: 700;
}
.status-waiting { color: #c5a862; background: rgba(163, 117, 31, .12); }
.status-ok { color: #79ddc0; background: rgba(36, 168, 130, .12); }
.setup-card > p { margin: 14px 0 0; color: #7790a7; font-size: 10px; line-height: 1.6; }
.system-wifi-state { color: #8eb9dc !important; }
.primary-action, .confirm-action, .open-action {
  width: 100%;
  min-height: 48px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  margin-top: 15px;
  border: 0;
  border-radius: 12px;
  font-size: 12px;
  font-weight: 750;
}
.primary-action { color: #fff; background: linear-gradient(135deg, #167fe7, #319cef); }
.confirm-action { color: #8de0c9; background: rgba(35, 159, 127, .13); border: 1px solid rgba(80, 210, 174, .18); }
.open-action { justify-content: space-between; padding: 0 15px; color: #fff; background: linear-gradient(135deg, #1687de, #1cb899); }
.open-action span { font-size: 23px; font-weight: 200; }
.primary-action:disabled, .open-action:disabled { opacity: .48; }
.returned-tip {
  display: flex;
  align-items: flex-start;
  gap: 8px;
  margin-top: 13px;
  padding: 11px;
  border-radius: 11px;
  color: #86a5bd;
  background: rgba(38, 119, 181, .09);
  font-size: 10px;
  line-height: 1.5;
}
.returned-tip i {
  width: 7px;
  height: 7px;
  flex: 0 0 7px;
  margin-top: 4px;
  border-radius: 50%;
  background: #62b6f7;
}
label { display: grid; gap: 7px; margin-top: 15px; }
label span { color: #8ea7bd; font-size: 10px; font-weight: 650; }
input {
  width: 100%;
  min-height: 48px;
  padding: 0 12px;
  border: 1px solid rgba(137, 174, 211, .22);
  border-radius: 12px;
  outline: none;
  color: #f5faff;
  background: rgba(4, 15, 28, .62);
  font-size: 16px;
}
input:focus { border-color: rgba(76, 164, 255, .8); box-shadow: 0 0 0 3px rgba(32, 132, 238, .13); }
input::placeholder { color: #4f687f; }
.last-address {
  max-width: 100%;
  min-height: 32px;
  margin-top: 9px;
  overflow: hidden;
  padding: 0 9px;
  border: 1px solid rgba(121, 166, 205, .14);
  border-radius: 9px;
  color: #78a8d1;
  background: rgba(48, 117, 176, .08);
  font-size: 9px;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.address-suggestions {
  display: flex;
  flex-wrap: wrap;
  gap: 7px;
  margin-top: 9px;
}
.address-suggestions .last-address { margin-top: 0; }
.address-help { color: #58738a !important; }
.inline-error {
  padding: 8px 9px;
  border: 1px solid rgba(248, 96, 96, .2);
  border-radius: 9px;
  color: #ff9d9d !important;
  background: rgba(157, 33, 33, .1);
}
.privacy-note {
  display: flex;
  gap: 10px;
  margin-top: 15px;
  padding: 13px 14px;
  border: 1px solid rgba(78, 203, 168, .12);
  border-radius: 13px;
  background: rgba(21, 115, 89, .06);
}
.privacy-note > i {
  width: 7px;
  height: 7px;
  flex: 0 0 7px;
  margin-top: 4px;
  border-radius: 50%;
  background: #45d6ac;
}
.privacy-note p { margin: 0; color: #668a81; font-size: 9px; line-height: 1.55; }
.privacy-note strong { color: #9bcdbc; font-size: 10px; }
.troubleshooting {
  margin-top: 12px;
  padding: 0 13px;
  border: 1px solid rgba(119, 164, 204, .12);
  border-radius: 12px;
  color: #668198;
  background: rgba(255,255,255,.025);
  font-size: 10px;
}
.troubleshooting summary {
  min-height: 42px;
  display: flex;
  align-items: center;
  cursor: pointer;
  color: #82a1bb;
  font-weight: 700;
}
.troubleshooting ol {
  display: grid;
  gap: 6px;
  margin: -2px 0 13px;
  padding-left: 18px;
  line-height: 1.55;
}
.spinner {
  width: 15px;
  height: 15px;
  border: 2px solid rgba(255,255,255,.3);
  border-top-color: #fff;
  border-radius: 50%;
  animation: spin .8s linear infinite;
}
@keyframes spin { to { transform: rotate(360deg); } }
@media (min-width: 760px) {
  .wifi-card, .address-card { padding: 22px; }
}

/* App 1.2 industrial local-setup surface */
.setup-page {
  color: #edf5fc;
  background: #07111d;
}
.grid-overlay { display: none; }
.setup-shell {
  width: min(100%, 720px);
  padding:
    max(14px, env(safe-area-inset-top))
    max(18px, env(safe-area-inset-right))
    max(22px, env(safe-area-inset-bottom))
    max(18px, env(safe-area-inset-left));
}
header {
  min-height: 60px;
  grid-template-columns: 46px minmax(0, 1fr) 64px;
  padding-bottom: 12px;
  border-bottom: 1px solid #203142;
}
header strong {
  color: #f1f6fb;
  font-size: 15px;
}
header small {
  color: #71869a;
  font-size: 10px;
}
.icon-button,
.logout-button {
  min-height: 44px;
  border-color: #304253;
  border-radius: 9px;
  color: #b6c8d8;
  background: #0d1823;
}
.logout-button { font-size: 11px; }
.intro { margin-top: 28px; }
.intro > span {
  color: #5fa8eb;
  font-size: 10px;
  letter-spacing: .14em;
}
.intro h1 {
  margin: 9px 0 8px;
  color: #f4f8fc;
  font-size: clamp(27px, 7vw, 36px);
  letter-spacing: -.025em;
}
.intro p {
  color: #879aab;
  font-size: 14px;
}
.stepper {
  gap: 6px;
  margin-top: 22px;
  padding: 13px 12px;
  border: 1px solid #26394b;
  border-radius: 11px;
  background: #0d1823;
}
.stepper li {
  gap: 7px;
  color: #60778c;
  font-size: 10px;
}
.stepper li:not(:last-child)::after {
  left: 29px;
  right: 2px;
  top: 12px;
  background: #2a3b4c;
}
.stepper i {
  width: 25px;
  height: 25px;
  flex-basis: 25px;
  border-color: #304355;
  background: #101c28;
  font-size: 10px;
}
.stepper .active { color: #9cb4c9; }
.stepper .active i {
  border-color: #397bb5;
  color: #8bc8ff;
  background: #12283c;
}
.stepper .complete i {
  border-color: #347f68;
  color: #84d6bd;
  background: #10251f;
}
.setup-card {
  margin-top: 14px;
  padding: 17px;
  border-color: #293c4e;
  border-radius: 13px;
  background: #101c28;
  box-shadow: 0 12px 32px rgba(0, 0, 0, .17);
}
.setup-card.disabled {
  opacity: .5;
}
.card-heading {
  grid-template-columns: 44px minmax(0, 1fr) auto;
  gap: 11px;
}
.card-heading small {
  color: #71869a;
  font-size: 10px;
}
.card-heading h2 {
  color: #f0f5fa;
  font-size: 16px;
}
.card-icon {
  width: 44px;
  height: 44px;
  border: 1px solid #31506c;
  border-radius: 9px;
  background: #122b43;
}
.wifi-icon i { background: #6cb1ed; }
.address-icon {
  color: #70cdb2;
  border-color: #31574b;
  background: #102a25;
}
.card-heading > span {
  min-height: 30px;
  padding: 0 9px;
  border: 1px solid transparent;
  border-radius: 7px;
  font-size: 10px;
}
.status-waiting {
  color: #d8b66e;
  border-color: #574625 !important;
  background: #241e13;
}
.status-ok {
  color: #8cd4bd;
  border-color: #2d5146 !important;
  background: #10241f;
}
.setup-card > p {
  color: #8498aa;
  font-size: 12px;
}
.primary-action,
.confirm-action,
.open-action {
  min-height: 50px;
  border-radius: 10px;
  font-size: 13px;
}
.primary-action {
  border: 1px solid #438fd4;
  background: #247bc7;
}
.confirm-action {
  color: #9bdac7;
  border-color: #315d50;
  background: #123027;
}
.open-action {
  border: 1px solid #438fd4;
  background: #247bc7;
}
.primary-action:not(:disabled):active,
.open-action:not(:disabled):active {
  background: #1f6eaf;
}
.returned-tip {
  padding: 12px;
  border: 1px solid #294963;
  border-radius: 9px;
  color: #92afc5;
  background: #102438;
  font-size: 11px;
}
.returned-tip i { background: #5fa8eb; }
label {
  gap: 8px;
  margin-top: 16px;
}
label span {
  color: #a8bacb;
  font-size: 12px;
}
input {
  min-height: 52px;
  padding: 0 13px;
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
.last-address {
  min-height: 38px;
  padding: 0 10px;
  border-color: #30485d;
  border-radius: 8px;
  color: #8bb8dd;
  background: #102337;
  font-size: 10px;
}
.address-help { color: #6d8498 !important; }
.inline-error {
  border-color: #633538;
  border-radius: 9px;
  color: #ffaaaa !important;
  background: #29171b;
  font-size: 11px !important;
}
.privacy-note {
  margin-top: 13px;
  border-color: #28443d;
  border-radius: 10px;
  background: #0d211f;
}
.privacy-note > i { background: #35b88c; }
.privacy-note p {
  color: #789c91;
  font-size: 10px;
}
.privacy-note strong {
  color: #acd8ca;
  font-size: 11px;
}
.troubleshooting {
  margin-top: 11px;
  border-color: #293b4d;
  border-radius: 10px;
  color: #7e93a6;
  background: #0d1823;
  font-size: 11px;
}
.troubleshooting summary {
  min-height: 46px;
  color: #9bafc1;
  font-size: 12px;
}
@media (min-width: 760px) {
  .wifi-card,
  .address-card { padding: 21px; }
}
</style>
