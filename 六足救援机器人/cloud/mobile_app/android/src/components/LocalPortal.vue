<template>
  <section class="local-portal">
    <header>
      <button type="button" class="back-button" aria-label="修改配置地址" @click="$emit('back')">
        <span>‹</span>
      </button>
      <div class="portal-title">
        <strong>机器人现场配置台</strong>
        <small>{{ displayAddress }}</small>
      </div>
      <div class="portal-actions">
        <button type="button" class="retry-button" @click="reloadPortal">刷新</button>
        <button type="button" class="logout-button" @click="$emit('logout')">退出</button>
      </div>
    </header>

    <div class="connection-tip" :class="{ loaded }">
      <i></i>
      <span>{{ loaded ? '配置页面已载入' : '正在连接机器人配置地址…' }}</span>
    </div>

    <div class="portal-frame">
      <iframe
        :key="frameKey"
        :src="url"
        title="机器人现场配置台"
        sandbox="allow-forms allow-same-origin allow-scripts"
        referrerpolicy="no-referrer"
        @load="onFrameLoad"
      ></iframe>

      <div v-if="timedOut && !loaded" class="load-error">
        <div class="error-icon">!</div>
        <strong>暂时无法打开配置页面</strong>
        <p>请确认手机已连接机器人 Wi‑Fi，并核对配置地址。</p>
        <button type="button" @click="reloadPortal">重新加载</button>
        <button type="button" class="secondary" @click="$emit('back')">修改地址</button>
      </div>
    </div>
  </section>
</template>

<script setup>
import { computed, onBeforeUnmount, ref, watch } from 'vue'

const props = defineProps({
  url: { type: String, required: true },
})
defineEmits(['back', 'logout'])

const frameKey = ref(0)
const loaded = ref(false)
const timedOut = ref(false)
let loadTimer = 0

const displayAddress = computed(() => {
  try {
    const parsed = new URL(props.url)
    return `${parsed.host}${parsed.pathname === '/' ? '' : parsed.pathname}`
  } catch {
    return props.url
  }
})

function startTimeout() {
  window.clearTimeout(loadTimer)
  loaded.value = false
  timedOut.value = false
  loadTimer = window.setTimeout(() => {
    if (!loaded.value) timedOut.value = true
  }, 10000)
}

function reloadPortal() {
  frameKey.value += 1
  startTimeout()
}

function onFrameLoad() {
  window.clearTimeout(loadTimer)
  loaded.value = true
  timedOut.value = false
}

watch(() => props.url, reloadPortal, { immediate: true })
onBeforeUnmount(() => window.clearTimeout(loadTimer))
</script>

<style scoped>
.local-portal {
  width: 100%;
  height: 100vh;
  height: 100dvh;
  display: grid;
  grid-template-rows: auto auto minmax(0, 1fr);
  overflow: hidden;
  color: #f5f9ff;
  background: #07101f;
}
header {
  min-height: calc(58px + env(safe-area-inset-top));
  display: grid;
  grid-template-columns: 48px minmax(0, 1fr) auto;
  align-items: center;
  gap: 8px;
  padding:
    env(safe-area-inset-top)
    max(10px, env(safe-area-inset-right))
    0
    max(10px, env(safe-area-inset-left));
  border-bottom: 1px solid rgba(123, 171, 218, .18);
  background: linear-gradient(180deg, #0c1b2d, #091625);
}
.back-button, .retry-button, .logout-button {
  min-width: 44px;
  min-height: 44px;
  border: 0;
  border-radius: 11px;
  color: #dceeff;
  background: rgba(255,255,255,.07);
}
.back-button span { display: block; margin-top: -3px; font-size: 32px; font-weight: 200; }
.retry-button { color: #79bcff; font-size: 12px; font-weight: 700; }
.portal-actions {
  display: flex;
  gap: 6px;
}
.logout-button {
  color: #ffaaa4;
  background: rgba(150, 42, 37, .15);
  font-size: 12px;
  font-weight: 700;
}
.portal-title { min-width: 0; display: flex; flex-direction: column; }
.portal-title strong { overflow: hidden; font-size: 14px; text-overflow: ellipsis; white-space: nowrap; }
.portal-title small { margin-top: 2px; overflow: hidden; color: #6f8ca7; font-size: 9px; text-overflow: ellipsis; white-space: nowrap; }
.connection-tip {
  min-height: 32px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 7px;
  padding: 5px 12px;
  color: #87a1b9;
  background: #081422;
  font-size: 10px;
}
.connection-tip i {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: #f8bd44;
  box-shadow: 0 0 10px rgba(248,189,68,.55);
}
.connection-tip.loaded { color: #75b9a6; }
.connection-tip.loaded i { background: #45d6ac; box-shadow: 0 0 10px rgba(69,214,172,.55); }
.portal-frame { position: relative; min-height: 0; }
iframe { width: 100%; height: 100%; border: 0; background: #f4f7fa; }
.load-error {
  position: absolute;
  inset: 0;
  z-index: 2;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 24px;
  color: #e8f3fc;
  background: #081422;
  text-align: center;
}
.error-icon {
  width: 48px;
  height: 48px;
  display: grid;
  place-items: center;
  margin-bottom: 13px;
  border: 1px solid rgba(248, 174, 76, .28);
  border-radius: 15px;
  color: #f6bd65;
  background: rgba(164, 104, 22, .12);
  font-size: 22px;
  font-weight: 800;
}
.load-error strong { font-size: 16px; }
.load-error p { margin: 7px 0 17px; color: #7890a6; font-size: 11px; line-height: 1.55; }
.load-error button {
  width: min(100%, 260px);
  min-height: 46px;
  border: 0;
  border-radius: 12px;
  color: #fff;
  background: #1687e5;
  font-size: 12px;
  font-weight: 700;
}
.load-error button.secondary {
  margin-top: 9px;
  color: #83a6c3;
  background: rgba(255,255,255,.06);
}
</style>
