import './styles.css';
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { RoomEnvironment } from 'three/addons/environments/RoomEnvironment.js';
import { createIcons, Maximize2, Plug, RefreshCw, RotateCcw, Settings2, Unplug, X } from 'lucide';
import { HandRig } from './hand-rig.js';
import { POSES } from './gestures.js';
import { formatPortLabel, parseDeviceLine } from './protocol.js';

const ICONS = { Maximize2, Plug, RefreshCw, RotateCcw, Settings2, Unplug, X };
const renderIcons = () => createIcons({ icons: ICONS });

const elements = {
  canvas: document.querySelector('#handCanvas'),
  stage: document.querySelector('.stage'),
  loading: document.querySelector('#loadingState'),
  loadingProgress: document.querySelector('#loadingProgress'),
  gestureName: document.querySelector('#gestureName'),
  gestureCode: document.querySelector('#gestureCode'),
  gestureGrid: document.querySelector('#gestureGrid'),
  pressureValue: document.querySelector('#pressureValue'),
  pressureFill: document.querySelector('#pressureFill'),
  deviceBadge: document.querySelector('#deviceBadge'),
  deviceStatus: document.querySelector('#deviceStatus'),
  serialSection: document.querySelector('#serialSection'),
  portSelect: document.querySelector('#portSelect'),
  connectButton: document.querySelector('#connectButton'),
  refreshPortsButton: document.querySelector('#refreshPortsButton'),
  thumbSettingsButton: document.querySelector('#thumbSettingsButton'),
  thumbSettingsBackdrop: document.querySelector('#thumbSettingsBackdrop'),
  thumbCalibrationControls: document.querySelector('#thumbCalibrationControls'),
  thumbPreviewGestureSelect: document.querySelector('#thumbPreviewGestureSelect'),
  syncThumbAllToggle: document.querySelector('#syncThumbAllToggle'),
  closeThumbSettingsButton: document.querySelector('#closeThumbSettingsButton'),
  finishThumbSettingsButton: document.querySelector('#finishThumbSettingsButton'),
  resetThumbCalibrationButton: document.querySelector('#resetThumbCalibrationButton'),
  resetViewButton: document.querySelector('#resetViewButton'),
  fullscreenButton: document.querySelector('#fullscreenButton'),
  speedRange: document.querySelector('#speedRange'),
  speedOutput: document.querySelector('#speedOutput'),
  eventLine: document.querySelector('#eventLine'),
  eventTime: document.querySelector('#eventTime'),
  inputSourceLabel: document.querySelector('#inputSourceLabel')
};

const THUMB_CALIBRATION_STORAGE_KEY = 'hydrogel.thumbCalibration.v1';
const THUMB_CALIBRATION_SYNC_STORAGE_KEY = 'hydrogel.thumbCalibration.syncAll.v1';
const THUMB_CALIBRATION_DEFAULTS = Object.freeze({
  base: -80,
  middle: -40,
  tip: -5,
  spread: -45,
  twist: 0,
  lift: 0.6,
  depth: 0
});
const THUMB_CALIBRATION_LIMITS = Object.freeze({
  base: [-180, 180, 0],
  middle: [-90, 90, 0],
  tip: [-90, 90, 0],
  spread: [-90, 90, 0],
  twist: [-90, 90, 0],
  lift: [-2, 2, 1],
  depth: [-2, 2, 1]
});

function clampCalibrationValue(field, value) {
  const [minimum, maximum, decimals] = THUMB_CALIBRATION_LIMITS[field];
  const numericValue = Number(value);
  const safeValue = Number.isFinite(numericValue) ? numericValue : THUMB_CALIBRATION_DEFAULTS[field];
  return Number(THREE.MathUtils.clamp(safeValue, minimum, maximum).toFixed(decimals));
}

function loadThumbCalibration() {
  try {
    const stored = JSON.parse(localStorage.getItem(THUMB_CALIBRATION_STORAGE_KEY) || '{}');
    return Object.fromEntries(Object.keys(THUMB_CALIBRATION_DEFAULTS).map((field) => [
      field,
      clampCalibrationValue(field, stored[field] ?? THUMB_CALIBRATION_DEFAULTS[field])
    ]));
  } catch {
    return { ...THUMB_CALIBRATION_DEFAULTS };
  }
}

let thumbCalibration = loadThumbCalibration();
let thumbCalibrationSyncAll = localStorage.getItem(THUMB_CALIBRATION_SYNC_STORAGE_KEY) !== 'false';

function getPoseForKey(key) {
  const numericKey = Number(key);
  const pose = POSES[numericKey];
  if (!pose || (numericKey !== 2 && !thumbCalibrationSyncAll)) return pose;
  const offsets = {
    base: thumbCalibration.base - THUMB_CALIBRATION_DEFAULTS.base,
    middle: thumbCalibration.middle - THUMB_CALIBRATION_DEFAULTS.middle,
    tip: thumbCalibration.tip - THUMB_CALIBRATION_DEFAULTS.tip,
    spread: thumbCalibration.spread - THUMB_CALIBRATION_DEFAULTS.spread,
    twist: thumbCalibration.twist - THUMB_CALIBRATION_DEFAULTS.twist,
    lift: thumbCalibration.lift - THUMB_CALIBRATION_DEFAULTS.lift,
    depth: thumbCalibration.depth - THUMB_CALIBRATION_DEFAULTS.depth
  };
  return {
    ...pose,
    thumb: [
      pose.thumb[0] + offsets.base,
      pose.thumb[1] + offsets.middle,
      pose.thumb[2] + offsets.tip
    ],
    thumbSpread: (pose.thumbSpread || 0) + offsets.spread,
    thumbTwist: (pose.thumbTwist || 0) + offsets.twist,
    thumbLift: (pose.thumbLift || 0) + offsets.lift,
    thumbDepth: (pose.thumbDepth || 0) + offsets.depth
  };
}

function formatCalibrationValue(field, value) {
  if (field === 'lift' || field === 'depth') {
    const prefix = value > 0 ? '+' : '';
    return `${prefix}${Number(value).toFixed(1)}`;
  }
  return `${Math.round(value)}°`;
}

function syncThumbCalibrationControls() {
  if (elements.syncThumbAllToggle) elements.syncThumbAllToggle.checked = thumbCalibrationSyncAll;
  elements.thumbCalibrationControls?.querySelectorAll('input[data-calibration]').forEach((input) => {
    const field = input.dataset.calibration;
    input.value = String(thumbCalibration[field]);
    const output = input.closest('label')?.querySelector('output');
    if (output) output.value = formatCalibrationValue(field, thumbCalibration[field]);
  });
}

function saveThumbCalibration() {
  localStorage.setItem(THUMB_CALIBRATION_STORAGE_KEY, JSON.stringify(thumbCalibration));
  localStorage.setItem(THUMB_CALIBRATION_SYNC_STORAGE_KEY, String(thumbCalibrationSyncAll));
}

renderIcons();

const renderer = new THREE.WebGLRenderer({
  canvas: elements.canvas,
  antialias: true,
  powerPreference: 'high-performance',
  preserveDrawingBuffer: true
});
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
renderer.outputColorSpace = THREE.SRGBColorSpace;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.18;
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;

const scene = new THREE.Scene();
scene.background = new THREE.Color(0xe7ebee);

const camera = new THREE.PerspectiveCamera(32, 1, 0.001, 100);
const controls = new OrbitControls(camera, elements.canvas);
controls.enableDamping = true;
controls.dampingFactor = 0.07;
controls.enablePan = false;
controls.minPolarAngle = Math.PI * 0.12;
controls.maxPolarAngle = Math.PI * 0.88;

const pmrem = new THREE.PMREMGenerator(renderer);
const environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
scene.environment = environment;
pmrem.dispose();

scene.add(new THREE.HemisphereLight(0xffffff, 0x77818a, 1.7));
const keyLight = new THREE.DirectionalLight(0xffffff, 3.2);
keyLight.position.set(2.6, 5.2, 4.2);
keyLight.castShadow = true;
keyLight.shadow.mapSize.set(2048, 2048);
scene.add(keyLight);
const rimLight = new THREE.DirectionalLight(0x90c9c2, 1.8);
rimLight.position.set(-4, 2, -3);
scene.add(rimLight);
const warmLight = new THREE.DirectionalLight(0xffd0a0, 1.1);
warmLight.position.set(2, -1, 3);
scene.add(warmLight);

let rig = null;
let model = null;
let modelBounds = null;
let floor = null;
let animationSpeed = Number(elements.speedRange.value);
let currentKey = 9;
let releaseMode = 'hold';
let controlMode = 'simulation';
let serialConnected = false;
let connectedPath = null;
let lastHardwareKey = 0;
let resetPoseTimer = null;
let portsRefreshing = false;
let suppressAutoConnectUntil = 0;

const clock = new THREE.Clock();

function resizeRenderer() {
  const width = Math.max(1, elements.stage.clientWidth);
  const height = Math.max(1, elements.stage.clientHeight);
  const pixelRatio = Math.min(window.devicePixelRatio, 2);
  const expectedWidth = Math.floor(width * pixelRatio);
  const expectedHeight = Math.floor(height * pixelRatio);
  if (elements.canvas.width !== expectedWidth || elements.canvas.height !== expectedHeight) {
    renderer.setPixelRatio(pixelRatio);
    renderer.setSize(width, height, false);
    camera.aspect = width / height;
    camera.updateProjectionMatrix();
  }
}

function frameModel() {
  if (!modelBounds) return;
  const center = modelBounds.getCenter(new THREE.Vector3());
  const size = modelBounds.getSize(new THREE.Vector3());
  const distance = Math.max(size.x, size.y, size.z) * 1.52;
  controls.target.copy(center).add(new THREE.Vector3(0, size.y * 0.04, 0));
  camera.position.set(
    center.x + distance * 1.05,
    center.y + distance * 0.08,
    center.z + distance * 1.26
  );
  camera.near = Math.max(0.0001, distance / 120);
  camera.far = distance * 30;
  camera.updateProjectionMatrix();
  controls.minDistance = distance * 0.5;
  controls.maxDistance = distance * 3;
  controls.update();
}

function addFloor() {
  if (!modelBounds) return;
  const size = modelBounds.getSize(new THREE.Vector3());
  const center = modelBounds.getCenter(new THREE.Vector3());
  const floorY = modelBounds.min.y - size.y * 0.04;
  const geometry = new THREE.CircleGeometry(Math.max(size.x, size.z, size.y * 0.45), 64);
  const material = new THREE.MeshStandardMaterial({ color: 0xd8dde1, roughness: 0.92, metalness: 0 });
  floor = new THREE.Mesh(geometry, material);
  floor.rotation.x = -Math.PI / 2;
  floor.position.set(center.x, floorY, center.z);
  floor.receiveShadow = true;
  scene.add(floor);
}

function updatePressure(value) {
  const pressure = Math.max(0, Math.round(Number(value) || 0));
  elements.pressureValue.textContent = String(pressure);
  elements.pressureFill.style.width = `${Math.min(100, pressure / 20)}%`;
}

function logEvent(message) {
  const now = new Date();
  elements.eventTime.textContent = now.toLocaleTimeString('zh-CN', { hour12: false });
  elements.eventLine.textContent = message;
}

function setGesture(key, source = 'SIM', pressure = 0) {
  const numericKey = Number(key);
  const pose = getPoseForKey(numericKey);
  if (!pose) return;
  currentKey = numericKey;
  if (resetPoseTimer) {
    clearTimeout(resetPoseTimer);
    resetPoseTimer = null;
  }
  if (rig) rig.setPose(pose);
  elements.gestureName.textContent = pose.name;
  elements.gestureCode.textContent = `${pose.code} · ${String(numericKey).padStart(2, '0')}`;
  elements.gestureGrid.querySelectorAll('button').forEach((button) => {
    button.classList.toggle('active', Number(button.dataset.key) === numericKey);
  });
  updatePressure(pressure);
  logEvent(`${source}  KEY ${numericKey}  ${pose.code}  Δ=${Math.round(pressure)}`);
}

function releaseGesture(key, source = 'HW') {
  if (releaseMode === 'reset' && Number(key) === currentKey) {
    setGesture(9, source, 0);
  } else {
    updatePressure(0);
    logEvent(`${source}  KEY ${key}  RELEASE`);
  }
}

function openThumbSettings() {
  syncThumbCalibrationControls();
  elements.thumbPreviewGestureSelect.value = '2';
  elements.thumbSettingsBackdrop.hidden = false;
  setGesture(2, 'CALIBRATION', 0);
  elements.closeThumbSettingsButton.focus();
}

function closeThumbSettings() {
  elements.thumbSettingsBackdrop.hidden = true;
  elements.thumbSettingsButton.focus();
}

function updateThumbCalibration(input) {
  const field = input.dataset.calibration;
  if (!(field in THUMB_CALIBRATION_DEFAULTS)) return;
  thumbCalibration = {
    ...thumbCalibration,
    [field]: clampCalibrationValue(field, input.value)
  };
  saveThumbCalibration();
  syncThumbCalibrationControls();
  if (rig) rig.setPose(getPoseForKey(currentKey));
}

function resetThumbCalibration() {
  thumbCalibration = { ...THUMB_CALIBRATION_DEFAULTS };
  saveThumbCalibration();
  syncThumbCalibrationControls();
  if (rig) rig.setPose(getPoseForKey(currentKey));
  logEvent('CALIBRATION  THUMB  RECOMMENDED');
}

function setThumbCalibrationScope(syncAll) {
  thumbCalibrationSyncAll = Boolean(syncAll);
  saveThumbCalibration();
  syncThumbCalibrationControls();
  if (rig) rig.setPose(getPoseForKey(currentKey));
  logEvent(`CALIBRATION  SCOPE  ${thumbCalibrationSyncAll ? 'ALL' : 'FIST'}`);
}

function setDeviceState(state, label) {
  elements.deviceBadge.dataset.state = state;
  elements.deviceStatus.textContent = label;
}

function updateConnectButton() {
  elements.connectButton.classList.toggle('connected', serialConnected);
  elements.connectButton.innerHTML = serialConnected
    ? '<span data-lucide="unplug"></span><span>断开</span>'
    : '<span data-lucide="plug"></span><span>连接</span>';
  renderIcons();
}

function handleDeviceLine(rawLine) {
  const message = parseDeviceLine(rawLine);
  if (!message) return;
  if (message.type === 'device') {
    setDeviceState('connected', `${message.name} v${message.version}`);
    logEvent(`设备确认  ${message.name}  FW ${message.version}`);
    return;
  }
  if (message.type === 'key') {
    lastHardwareKey = message.action === 'DOWN' ? message.key : 0;
    if (message.action === 'DOWN') setGesture(message.key, 'HW', message.pressure);
    else releaseGesture(message.key, 'HW');
    return;
  }
  if (message.type === 'heartbeat') {
    setDeviceState('connected', connectedPath ? `${connectedPath} · 设备在线` : '设备在线');
    updatePressure(message.pressure);
    if (message.key > 0 && message.key !== lastHardwareKey) {
      lastHardwareKey = message.key;
      setGesture(message.key, 'HW', message.pressure);
    }
  }
}

async function refreshPorts(autoConnect = false) {
  if (portsRefreshing) return;
  if (!window.handBridge) {
    elements.portSelect.innerHTML = '<option value="">仅桌面程序支持串口</option>';
    return;
  }
  portsRefreshing = true;
  elements.refreshPortsButton.disabled = true;
  try {
    const ports = await window.handBridge.listPorts();
    const preferredPorts = ports.filter((port) => {
      const identity = `${port.pnpId || ''} ${port.manufacturer || ''} ${port.friendlyName || ''}`;
      return !/BTHENUM|蓝牙|Bluetooth/i.test(identity);
    });
    const previous = elements.portSelect.value || localStorage.getItem('hydrogel.port') || '';
    elements.portSelect.replaceChildren();
    if (!ports.length) {
      elements.portSelect.add(new Option('未发现串口', ''));
      setDeviceState('offline', '未发现串口');
      return;
    }
    for (const port of ports) elements.portSelect.add(new Option(formatPortLabel(port), port.path));
    const selected = ports.find((port) => port.path === previous)?.path
      || preferredPorts[0]?.path
      || ports[0].path;
    elements.portSelect.value = selected;
    if (autoConnect
      && preferredPorts.length === 1
      && !serialConnected
      && Date.now() >= suppressAutoConnectUntil) {
      elements.portSelect.value = preferredPorts[0].path;
      await connectSelectedPort();
    }
  } catch (error) {
    setDeviceState('error', '串口枚举失败');
    logEvent(`ERROR  ${error.message}`);
  } finally {
    portsRefreshing = false;
    elements.refreshPortsButton.disabled = false;
  }
}

async function connectSelectedPort() {
  if (!window.handBridge) {
    logEvent('请运行 Hydrogel Hand 桌面程序以连接串口');
    return;
  }
  if (serialConnected) {
    suppressAutoConnectUntil = Date.now() + 10000;
    await window.handBridge.disconnect();
    return;
  }
  const portPath = elements.portSelect.value;
  if (!portPath) {
    logEvent('未选择串口');
    return;
  }
  try {
    setDeviceState('connecting', `连接 ${portPath}`);
    await window.handBridge.connect(portPath);
    localStorage.setItem('hydrogel.port', portPath);
  } catch (error) {
    serialConnected = false;
    connectedPath = null;
    updateConnectButton();
    setDeviceState('error', '连接失败');
    logEvent(`ERROR  ${error.message}`);
  }
}

async function setControlMode(mode) {
  controlMode = mode;
  document.querySelectorAll('.mode-button').forEach((button) => {
    button.classList.toggle('active', button.dataset.mode === mode);
  });
  const hardware = mode === 'hardware';
  elements.serialSection.hidden = !hardware;
  elements.inputSourceLabel.textContent = hardware ? 'STM32 USART1' : '数字键 1–9';
  if (hardware) {
    setDeviceState('offline', '等待串口');
    await refreshPorts(true);
  } else {
    if (serialConnected && window.handBridge) await window.handBridge.disconnect();
    setDeviceState('offline', '模拟模式');
  }
}

new GLTFLoader().load(
  `${import.meta.env.BASE_URL}models/robotic_hand.glb`,
  (gltf) => {
    model = gltf.scene;
    scene.add(model);
    model.traverse((object) => {
      if (object.isMesh) {
        object.castShadow = true;
        object.receiveShadow = true;
        const materials = Array.isArray(object.material) ? object.material : [object.material];
        for (const material of materials) {
          for (const value of Object.values(material || {})) {
            if (value?.isTexture) value.anisotropy = Math.min(8, renderer.capabilities.getMaxAnisotropy());
          }
        }
      }
    });
    rig = new HandRig(model);
    rig.setPose(POSES[currentKey], true);
    modelBounds = new THREE.Box3().setFromObject(model);
    addFloor();
    frameModel();
    elements.loadingProgress.textContent = '100%';
    requestAnimationFrame(() => elements.loading.classList.add('hidden'));
    logEvent('MODEL  READY  15 JOINTS');
    window.__handAppDebug.ready = true;
  },
  (event) => {
    if (event.total > 0) {
      elements.loadingProgress.textContent = `${Math.min(99, Math.round((event.loaded / event.total) * 100))}%`;
    }
  },
  (error) => {
    elements.loadingProgress.textContent = '加载失败';
    setDeviceState('error', '模型加载失败');
    logEvent(`MODEL ERROR  ${error.message || error}`);
  }
);

elements.gestureGrid.addEventListener('pointerdown', (event) => {
  const button = event.target.closest('button[data-key]');
  if (!button || controlMode !== 'simulation') return;
  button.setPointerCapture?.(event.pointerId);
  setGesture(Number(button.dataset.key), 'SIM', 900 + Number(button.dataset.key) * 75);
});

elements.gestureGrid.addEventListener('pointerup', (event) => {
  const button = event.target.closest('button[data-key]');
  if (!button || controlMode !== 'simulation' || releaseMode !== 'reset') return;
  releaseGesture(Number(button.dataset.key), 'SIM');
});

window.addEventListener('keydown', (event) => {
  if (event.repeat || controlMode !== 'simulation' || event.key < '1' || event.key > '9') return;
  setGesture(Number(event.key), 'KEYBOARD', 1000 + Number(event.key) * 80);
});

window.addEventListener('keyup', (event) => {
  if (controlMode !== 'simulation' || releaseMode !== 'reset' || event.key < '1' || event.key > '9') return;
  releaseGesture(Number(event.key), 'KEYBOARD');
});

document.querySelectorAll('.mode-button').forEach((button) => {
  button.addEventListener('click', () => setControlMode(button.dataset.mode));
});

document.querySelectorAll('.release-button').forEach((button) => {
  button.addEventListener('click', () => {
    releaseMode = button.dataset.release;
    document.querySelectorAll('.release-button').forEach((item) => item.classList.toggle('active', item === button));
  });
});

elements.speedRange.addEventListener('input', () => {
  animationSpeed = Number(elements.speedRange.value);
  elements.speedOutput.textContent = animationSpeed <= 4 ? '慢速' : animationSpeed >= 10 ? '快速' : '中速';
});

elements.thumbSettingsButton.addEventListener('click', openThumbSettings);
elements.closeThumbSettingsButton.addEventListener('click', closeThumbSettings);
elements.finishThumbSettingsButton.addEventListener('click', closeThumbSettings);
elements.resetThumbCalibrationButton.addEventListener('click', resetThumbCalibration);
elements.syncThumbAllToggle.addEventListener('change', () => {
  setThumbCalibrationScope(elements.syncThumbAllToggle.checked);
});
elements.thumbPreviewGestureSelect.addEventListener('change', () => {
  setGesture(Number(elements.thumbPreviewGestureSelect.value), 'CALIBRATION', 0);
});
elements.thumbCalibrationControls.addEventListener('input', (event) => {
  const input = event.target.closest('input[data-calibration]');
  if (input) updateThumbCalibration(input);
});
elements.thumbSettingsBackdrop.addEventListener('pointerdown', (event) => {
  if (event.target === elements.thumbSettingsBackdrop) closeThumbSettings();
});
window.addEventListener('keydown', (event) => {
  if (event.key === 'Escape' && !elements.thumbSettingsBackdrop.hidden) closeThumbSettings();
});

elements.refreshPortsButton.addEventListener('click', () => refreshPorts(false));
elements.connectButton.addEventListener('click', connectSelectedPort);
elements.resetViewButton.addEventListener('click', frameModel);
elements.fullscreenButton.addEventListener('click', async () => {
  if (window.handBridge) await window.handBridge.toggleFullscreen();
  else if (!document.fullscreenElement) await document.documentElement.requestFullscreen();
  else await document.exitFullscreen();
});

if (window.handBridge) {
  window.handBridge.onSerialLine(handleDeviceLine);
  window.handBridge.onSerialStatus((status) => {
    connectedPath = status.path || null;
    serialConnected = status.state === 'connected';
    updateConnectButton();
    if (status.state === 'connected') setDeviceState('connecting', `${status.path} · 等待设备`);
    else if (status.state === 'connecting') setDeviceState('connecting', status.message || '正在连接');
    else if (status.state === 'error') setDeviceState('error', status.message || '串口错误');
    else if (controlMode === 'hardware') setDeviceState('offline', status.message || '串口已断开');
  });
}

new ResizeObserver(resizeRenderer).observe(elements.stage);
renderer.setAnimationLoop(() => {
  resizeRenderer();
  const delta = Math.min(clock.getDelta(), 0.05);
  if (rig) rig.update(delta, animationSpeed);
  controls.update();
  renderer.render(scene, camera);
});

setInterval(() => {
  if (controlMode === 'hardware' && !serialConnected) refreshPorts(true);
}, 3000);

window.__handAppDebug = {
  ready: false,
  get currentKey() { return currentKey; },
  get jointState() { return rig?.getJointState() || null; },
  setGesture: (key) => setGesture(key, 'TEST', 1200),
  get canvasSize() { return { width: elements.canvas.width, height: elements.canvas.height }; }
};

updateConnectButton();
updatePressure(0);
syncThumbCalibrationControls();
