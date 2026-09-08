// AtomS3R-M12 IMU dashboard
//
//   SSE /stream  -->  axis remap  -->  Madgwick AHRS  -->  three.js physical visualisation
//
// Instead of three abstract arrows, each sensor drives a physical metaphor: linear acceleration
// makes the board lurch from a "home" ghost on a spring; angular velocity draws a ring in the
// true plane of rotation with chevrons running at the real rate; the magnetic field is a
// gimballed dip needle the board turns underneath.
//
// All units as streamed by the board: accel [g], gyro [°/s], mag [µT].

import * as THREE from 'three';
import { OrbitControls } from './vendor/OrbitControls.js';
import { Madgwick } from './ahrs.js';
import { drawCompass, drawAccel, drawGyro, drawMag, clamp, sat } from './gauges.js';

// ------------------------------------------------------------------ config --

const remapAccelGyro = (v) => [v[0], v[1], v[2]];   // M5Unified already aligns to a common body frame
const remapMag = (v) => [v[0], v[1], v[2]];

const COL = { accel: 0x22d3ee, gyro: 0xfbbf24, mag: 0xe879f9, x: 0xf87171, y: 0x4ade80, z: 0x60a5fa,
              white: 0xffffff, bad: 0xf87171, slate: 0x475569 };
const HEALTH = { OK: '#e879f9', FAIR: '#fbbf24', POOR: '#f87171', DISTURBED: '#f87171', UNCAL: '#8b95a7' };

const K = {
  accMmPerG: 32, accSoftMax: 40, accDead: 0.04, accHoldTau: 0.06, accPeakRel: 4,
  springW0: 2 * Math.PI * 5, springZeta: 0.6, springDtMax: 1 / 30,
  gyroDead: 8, gyroFull: 120, gyroHot: 360, gyroCap: 720, gyroAtk: 0.05, gyroRel: 0.20, axisTau: 0.08,
  ringR: 21, magTau: 0.25, fanR: 16, bRefDefault: 48,
  calWindow: 240, calMinMoving: 50, calRotGate: 30, calOk: 0.06, calPoor: 0.15,
  cloudMmPerUt: 0.45, octantMin: 25, octantsRequired: 6,
  betaWarmup: 1.5, betaRun: 0.08,
};

// ------------------------------------------------------------------ state ---

const ahrs = new Madgwick(K.betaWarmup);
let warmupSamples = 0;
let lastT = null, lastSeq = null, drops = 0;
let headingOffset = 0;

const latest = { a: [0, 0, 1], g: [0, 0, 0], m: [0, 0, 0], mCal: [0, 0, 0] };

// derived, world frame (updated in onSample)
const D = {
  lin: new THREE.Vector3(), linHold: new THREE.Vector3(), peakG: 0,
  omegaW: new THREE.Vector3(), gyroMag: 0, axis: new THREE.Vector3(0, 0, 1), spinSign: 1, turns: 0,
  Bs: new THREE.Vector3(), strength: 0, dip: 62, bRef: K.bRefDefault, magNorthDeg: 0,
  health: 'UNCAL', calQ: 1, _cv: null,
};
const accelTrail = [];   // last ~1 s of [east, north] for the bullseye

// magnetometer hard/soft-iron calibration
const magCal = { active: false, min: [1e9, 1e9, 1e9], max: [-1e9, -1e9, -1e9], n: 0,
                 offset: [0, 0, 0], scale: [1, 1, 1], done: false, octants: new Array(8).fill(0) };
try {
  const saved = JSON.parse(localStorage.getItem('magCal') || 'null');
  if (saved && saved.offset && saved.scale) { Object.assign(magCal, { offset: saved.offset, scale: saved.scale, done: true }); if (saved.bRef) D.bRef = saved.bRef; }
} catch (_) { /* no storage, fine */ }

const magHist = [];   // {mag, moving} window for calibration quality

const $ = (id) => document.getElementById(id);
const ui = {
  espDot: $('espDot'), espState: $('espState'), rate: $('rate'), rssi: $('rssi'), loss: $('loss'), gap: $('gap'),
  overlay: $('overlay'), overlayMsg: $('overlayMsg'), useMag: $('useMag'), fusionMode: $('fusionMode'),
  heading: $('heading'), roll: $('roll'), pitch: $('pitch'), yaw: $('yaw'), magNote: $('magNote'),
  btnCal: $('btnCal'), btnZero: $('btnZero'), btnReset: $('btnReset'), vignette: $('vignette'),
  summ: { a: $('accSumm'), g: $('gyroSumm'), m: $('magSumm') },
  vals: { a: $('accelVals').querySelectorAll('b'), g: $('gyroVals').querySelectorAll('b'), m: $('magVals').querySelectorAll('b') },
  gauge: { a: $('gaugeA'), g: $('gaugeG'), m: $('gaugeM') }, compass: $('compass'),
};

// ------------------------------------------------------------------ scene ---

const view = $('view');
const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: true });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
view.appendChild(renderer.domElement);

const scene = new THREE.Scene();
scene.fog = new THREE.Fog(0x0b0f19, 120, 260);
const camera = new THREE.PerspectiveCamera(40, 1, 0.1, 500);
camera.position.set(66, 34, 74);
const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true; controls.dampingFactor = 0.08;
controls.minDistance = 30; controls.maxDistance = 200; controls.maxPolarAngle = Math.PI * 0.49;

scene.add(new THREE.HemisphereLight(0xbfd4ff, 0x0b0f19, 1.1));
const keyLight = new THREE.DirectionalLight(0xffffff, 1.6); keyLight.position.set(40, 80, 30); scene.add(keyLight);
const rim = new THREE.DirectionalLight(0x22d3ee, 0.5); rim.position.set(-60, 20, -40); scene.add(rim);

// Earth frame (NWU): x_north -> -Z, y_west -> -X, z_up -> +Y inside three.js (y up)
const earth = new THREE.Group();
earth.setRotationFromMatrix(new THREE.Matrix4().makeBasis(
  new THREE.Vector3(0, 0, -1), new THREE.Vector3(-1, 0, 0), new THREE.Vector3(0, 1, 0)));
scene.add(earth);

const grid = new THREE.GridHelper(200, 40, 0x233047, 0x18202f); grid.position.y = -16; scene.add(grid);
const ring1g = new THREE.Mesh(new THREE.RingGeometry(31, 32, 96), new THREE.MeshBasicMaterial({ color: 0x2d3a55, side: THREE.DoubleSide, transparent: true, opacity: 0.8 }));
ring1g.rotation.x = -Math.PI / 2; ring1g.position.y = -15.9; scene.add(ring1g);
const ringHalfG = new THREE.Mesh(new THREE.RingGeometry(15.6, 16, 96), new THREE.MeshBasicMaterial({ color: 0x2d3a55, side: THREE.DoubleSide, transparent: true, opacity: 0.35 }));
ringHalfG.rotation.x = -Math.PI / 2; ringHalfG.position.y = -15.9; scene.add(ringHalfG);
for (const [text, x, z, color] of [['N', 0, -38, '#f87171'], ['S', 0, 38, '#8b95a7'], ['E', 38, 0, '#8b95a7'], ['W', -38, 0, '#8b95a7']]) {
  const s = makeLabel(text, color); s.position.set(x, -15, z); s.scale.setScalar(7); scene.add(s);
}

// helpers ------------------------------------------------------------------
function basicMat(color, opacity, additive) {
  return new THREE.MeshBasicMaterial({ color, transparent: true, opacity, depthTest: false, depthWrite: false,
    blending: additive ? THREE.AdditiveBlending : THREE.NormalBlending });
}
function edgesOf(geo, color, opacity, additive) {
  return new THREE.LineSegments(new THREE.EdgesGeometry(geo), new THREE.LineBasicMaterial({ color, transparent: true, opacity,
    depthTest: false, blending: additive ? THREE.AdditiveBlending : THREE.NormalBlending }));
}
function makeLabel(text, color) {
  const c = document.createElement('canvas'); c.width = c.height = 128;
  const ctx = c.getContext('2d');
  ctx.font = 'bold 84px system-ui, sans-serif'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
  ctx.fillStyle = color; ctx.fillText(text, 64, 68);
  return new THREE.Sprite(new THREE.SpriteMaterial({ map: new THREE.CanvasTexture(c), transparent: true, depthWrite: false, depthTest: false }));
}

// cradle holds only the spring displacement; the board rotates inside it as before -------
const cradle = new THREE.Group(); earth.add(cradle);
const board = new THREE.Group(); cradle.add(board);
const boxGeo = new THREE.BoxGeometry(24, 24, 10);
{
  const body = new THREE.Mesh(boxGeo, new THREE.MeshStandardMaterial({ color: 0x1f2937, roughness: 0.55, metalness: 0.25 }));
  body.position.z = -1; board.add(body);
  board.add(new THREE.LineSegments(new THREE.EdgesGeometry(boxGeo), new THREE.LineBasicMaterial({ color: 0x3b4a66 })).translateZ(-1));
  const face = new THREE.Mesh(new THREE.BoxGeometry(22, 22, 1.2), new THREE.MeshStandardMaterial({ color: 0x0f172a, roughness: 0.3, metalness: 0.5 }));
  face.position.z = 4.6; board.add(face);
  const lens = new THREE.Mesh(new THREE.CylinderGeometry(4, 4.4, 3.5, 32), new THREE.MeshStandardMaterial({ color: 0x0b0f19, roughness: 0.25, metalness: 0.7 }));
  lens.rotation.x = Math.PI / 2; lens.position.z = 6.5; board.add(lens);
  const glass = new THREE.Mesh(new THREE.CircleGeometry(2.6, 32), new THREE.MeshStandardMaterial({ color: 0x1e3a8a, roughness: 0.1, metalness: 0.9, emissive: 0x1d4ed8, emissiveIntensity: 0.35 }));
  glass.position.z = 8.3; board.add(glass);
  const usb = new THREE.Mesh(new THREE.BoxGeometry(9, 1.2, 3.2), new THREE.MeshStandardMaterial({ color: 0x64748b, metalness: 0.9, roughness: 0.3 }));
  usb.position.set(0, -12.2, -2.5); board.add(usb);
  const grove = new THREE.Mesh(new THREE.BoxGeometry(10, 1.2, 4.5), new THREE.MeshStandardMaterial({ color: 0xf8fafc, roughness: 0.6 }));
  grove.position.set(0, 12.2, -2.5); board.add(grove);
  const led = new THREE.Mesh(new THREE.SphereGeometry(0.9, 16, 16), new THREE.MeshBasicMaterial({ color: 0x34d399 }));
  led.position.set(-8.5, 8.5, 5.3); board.add(led);
  for (const [dir, color] of [[[1, 0, 0], COL.x], [[0, 1, 0], COL.y], [[0, 0, 1], COL.z]])
    board.add(new THREE.ArrowHelper(new THREE.Vector3(...dir), new THREE.Vector3(0, 0, 0), 14, color, 2.5, 1.3));
}

// --- accel: home ghost + tether + footprint + plumb + trail (children of earth) ---
const ghost = new THREE.Mesh(boxGeo, basicMat(COL.accel, 0, true)); ghost.scale.setScalar(1.04); earth.add(ghost);
const ghostEdges = edgesOf(boxGeo, COL.accel, 0, true); earth.add(ghostEdges);
const tether = new THREE.Mesh(new THREE.CylinderGeometry(0.7, 0.7, 1, 10), basicMat(COL.accel, 0)); tether.renderOrder = 9; earth.add(tether);
const foot = new THREE.Mesh(new THREE.CircleGeometry(12, 32), basicMat(COL.accel, 0.08)); foot.rotation.x = -Math.PI / 2; earth.add(foot);
const footRim = new THREE.Mesh(new THREE.RingGeometry(11.3, 12, 48), basicMat(COL.accel, 0.45)); footRim.rotation.x = -Math.PI / 2; earth.add(footRim);
const plumb = new THREE.Line(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(), new THREE.Vector3()]),
  new THREE.LineBasicMaterial({ color: 0x334155, transparent: true, opacity: 0.6 })); earth.add(plumb);
const TRAIL_N = 36;
const trailBuf = new Float32Array(TRAIL_N * 3), trailCol = new Float32Array(TRAIL_N * 3);
for (let i = 0; i < TRAIL_N; i++) { const t = i / (TRAIL_N - 1); trailCol.set([0.13 * t, 0.82 * t, 0.93 * t], i * 3); }
const trailGeo = new THREE.BufferGeometry();
trailGeo.setAttribute('position', new THREE.BufferAttribute(trailBuf, 3));
trailGeo.setAttribute('color', new THREE.BufferAttribute(trailCol, 3));
const trail = new THREE.Line(trailGeo, new THREE.LineBasicMaterial({ vertexColors: true, transparent: true, blending: THREE.AdditiveBlending, depthTest: false })); earth.add(trail);

// --- gyro: spin ring + blur + chevrons + spindle + onion skins (children of cradle) ---
const spinRing = new THREE.Mesh(new THREE.TorusGeometry(K.ringR, 0.6, 8, 96), basicMat(COL.gyro, 0)); spinRing.renderOrder = 10; cradle.add(spinRing);
const blurRing = new THREE.Mesh(new THREE.TorusGeometry(K.ringR, 1.4, 8, 96), basicMat(COL.gyro, 0)); blurRing.renderOrder = 10; cradle.add(blurRing);
const spinner = new THREE.Group(); cradle.add(spinner);
const chevA = (2 * Math.PI) / 3;
for (let i = 0; i < 3; i++) {
  const cone = new THREE.Mesh(new THREE.ConeGeometry(1.8, 4, 12), basicMat(COL.gyro, 0));
  const a = i * chevA; cone.position.set(K.ringR * Math.cos(a), K.ringR * Math.sin(a), 0);
  cone.rotation.z = a - Math.PI / 2;   // apex along +theta (CCW tangent)
  spinner.add(cone);
}
const spindle = new THREE.Mesh(new THREE.CylinderGeometry(0.35, 0.35, 1, 8), basicMat(COL.gyro, 0)); cradle.add(spindle);
const onion = [edgesOf(boxGeo, COL.gyro, 0, true), edgesOf(boxGeo, COL.gyro, 0, true)]; onion.forEach((o) => cradle.add(o));

// --- mag: dip needle + fan + ground trace + sprites ---
const needle = new THREE.Group();
const nNorth = new THREE.Mesh(new THREE.CylinderGeometry(0.8, 0.8, 14, 12), basicMat(COL.mag, 0.95)); nNorth.position.y = 7; needle.add(nNorth);
const nTip = new THREE.Mesh(new THREE.ConeGeometry(1.6, 3, 12), basicMat(COL.mag, 0.95)); nTip.position.y = 14.5; needle.add(nTip);
const nSouth = new THREE.Mesh(new THREE.CylinderGeometry(0.8, 0.8, 14, 12), basicMat(COL.slate, 0.9)); nSouth.position.y = -7; needle.add(nSouth);
needle.renderOrder = 11; cradle.add(needle);
const pivot = new THREE.Mesh(new THREE.SphereGeometry(1.2, 12, 12), basicMat(0x64748b, 0.9)); cradle.add(pivot);
const spriteN = makeLabel('N', '#e879f9'), spriteQ = makeLabel('?', '#fbbf24');
spriteN.scale.setScalar(4); spriteQ.scale.setScalar(4); spriteN.position.y = spriteQ.position.y = 18; needle.add(spriteN, spriteQ);
const FAN_N = 18;
const fanBuf = new Float32Array(FAN_N * 3);
const fanGeo = new THREE.BufferGeometry(); fanGeo.setAttribute('position', new THREE.BufferAttribute(fanBuf, 3));
const fanIdx = []; for (let i = 1; i < FAN_N - 1; i++) fanIdx.push(0, i, i + 1); fanGeo.setIndex(fanIdx);
const dipFan = new THREE.Mesh(fanGeo, new THREE.MeshBasicMaterial({ color: COL.mag, transparent: true, opacity: 0.18, side: THREE.DoubleSide, depthTest: false, depthWrite: false })); cradle.add(dipFan);
const trace = new THREE.Line(new THREE.BufferGeometry().setFromPoints([new THREE.Vector3(), new THREE.Vector3()]),
  new THREE.LineDashedMaterial({ color: COL.mag, transparent: true, opacity: 0.5, dashSize: 2, gapSize: 2 })); earth.add(trace);

// --- mag calibration cloud + fit sphere (children of board) ---
const cloudGeo = new THREE.BufferGeometry();
const cloudPos = new Float32Array(3000 * 3);
cloudGeo.setAttribute('position', new THREE.BufferAttribute(cloudPos, 3)); cloudGeo.setDrawRange(0, 0);
const cloud = new THREE.Points(cloudGeo, new THREE.PointsMaterial({ color: COL.mag, size: 0.9, transparent: true, opacity: 0.85 })); cloud.visible = false; board.add(cloud);
const fitSphere = new THREE.Mesh(new THREE.SphereGeometry(1, 16, 12), new THREE.MeshBasicMaterial({ color: COL.mag, wireframe: true, transparent: true, opacity: 0.25 })); fitSphere.visible = false; board.add(fitSphere);

function resize() {
  const w = view.clientWidth, h = view.clientHeight;
  renderer.setSize(w, h, false);
  camera.aspect = w / h; camera.updateProjectionMatrix();
}
addEventListener('resize', resize); resize();
window.imu = { scene, camera, controls, board, cradle, ahrs, D, latest };

// --------------------------------------------------------------- samples ---

const bodyQ = new THREE.Quaternion();
const tmpV = new THREE.Vector3(), tmpV2 = new THREE.Vector3();
const UP = new THREE.Vector3(0, 0, 1);   // NWU up

const ema = (cur, target, dt, tau) => cur + (target - cur) * (1 - Math.exp(-dt / tau));

function onSample(s) {
  drops += lastSeq !== null && s.n > lastSeq + 1 ? s.n - lastSeq - 1 : 0;
  lastSeq = s.n;
  let dt = lastT === null ? 0.02 : (s.t - lastT) / 1000;
  lastT = s.t;
  if (!(dt > 0.001 && dt < 0.25)) dt = 0.02;

  const a = remapAccelGyro(s.a), g = remapAccelGyro(s.g), mRaw = remapMag(s.m);
  if (magCal.active) collectMag(mRaw);
  const m = [0, 1, 2].map((i) => (mRaw[i] - magCal.offset[i]) * magCal.scale[i]);
  latest.a = a; latest.g = g; latest.m = mRaw; latest.mCal = m;

  if (warmupSamples < 100 && ++warmupSamples === 100) ahrs.beta = K.betaRun;
  const rad = Math.PI / 180;
  if (ui.useMag.checked && magCal.done && !magCal.active)
    ahrs.update(g[0] * rad, g[1] * rad, g[2] * rad, a[0], a[1], a[2], m[0], m[1], m[2], dt);
  else
    ahrs.updateIMU(g[0] * rad, g[1] * rad, g[2] * rad, a[0], a[1], a[2], dt);

  const [q0, q1, q2, q3] = ahrs.q;
  bodyQ.set(q1, q2, q3, q0);

  // linear acceleration (world, gravity removed)
  tmpV.set(a[0], a[1], a[2]).applyQuaternion(bodyQ).sub(UP);
  if (warmupSamples < 100 || tmpV.length() < K.accDead) tmpV.set(0, 0, 0);
  D.lin.copy(tmpV);
  const linMag = tmpV.length();
  if (linMag > D.linHold.length()) D.linHold.copy(tmpV);
  else { D.linHold.x = ema(D.linHold.x, tmpV.x, dt, K.accHoldTau); D.linHold.y = ema(D.linHold.y, tmpV.y, dt, K.accHoldTau); D.linHold.z = ema(D.linHold.z, tmpV.z, dt, K.accHoldTau); }
  D.peakG = Math.max(linMag, D.peakG - K.accPeakRel * dt);
  accelTrail.push([-D.lin.y, D.lin.x]); if (accelTrail.length > 20) accelTrail.shift();   // [east, north]

  // angular velocity (world)
  tmpV.set(g[0], g[1], g[2]).applyQuaternion(bodyQ);
  D.omegaW.copy(tmpV);
  const mRawW = Math.hypot(g[0], g[1], g[2]);
  D.gyroMag = ema(D.gyroMag, mRawW < K.gyroDead ? 0 : mRawW, dt, mRawW > D.gyroMag ? K.gyroAtk : K.gyroRel);
  if (mRawW > K.gyroDead) {
    tmpV2.copy(tmpV).normalize();
    let sign = 1;
    if (tmpV2.dot(D.axis) < 0) { tmpV2.negate(); sign = -1; }
    D.axis.lerp(tmpV2, 1 - Math.exp(-dt / K.axisTau)).normalize();
    D.spinSign = ema(D.spinSign, sign, dt, K.axisTau);
  }
  D.turns += D.omegaW.z * dt / 360;

  // magnetic field (world)
  tmpV.set(m[0], m[1], m[2]).applyQuaternion(bodyQ);
  D.Bs.x = ema(D.Bs.x, tmpV.x, dt, K.magTau); D.Bs.y = ema(D.Bs.y, tmpV.y, dt, K.magTau); D.Bs.z = ema(D.Bs.z, tmpV.z, dt, K.magTau);
  D.strength = D.Bs.length();
  D.dip = Math.atan2(-D.Bs.z, Math.hypot(D.Bs.x, D.Bs.y)) * 180 / Math.PI;
  D.magNorthDeg = (Math.atan2(-D.Bs.y, D.Bs.x) * 180 / Math.PI + 360) % 360;

  magHist.push({ mag: Math.hypot(m[0], m[1], m[2]), moving: mRawW > K.calRotGate });
  if (magHist.length > K.calWindow) magHist.shift();
  updateHealth();
}

function updateHealth() {
  const moving = magHist.filter((h) => h.moving).map((h) => h.mag);
  if (moving.length >= K.calMinMoving) {
    const mean = moving.reduce((s, v) => s + v, 0) / moving.length;
    const cv = mean > 0 ? Math.sqrt(moving.reduce((s, v) => s + (v - mean) ** 2, 0) / moving.length) / mean : 1;
    D.calQ = cv < K.calOk ? 1 : cv < K.calPoor ? 0.5 : 0;
    D._cv = cv;
  }
  if (!magCal.done) D.health = 'UNCAL';
  else if (D.strength < 0.6 * D.bRef || D.strength > 1.5 * D.bRef) D.health = 'DISTURBED';
  else D.health = D.calQ >= 1 ? 'OK' : D.calQ >= 0.5 ? 'FAIR' : 'POOR';
}

// magnetometer calibration ---------------------------------------------------
function collectMag(mr) {
  for (let i = 0; i < 3; i++) { magCal.min[i] = Math.min(magCal.min[i], mr[i]); magCal.max[i] = Math.max(magCal.max[i], mr[i]); }
  const off = [0, 1, 2].map((i) => (magCal.max[i] + magCal.min[i]) / 2);
  const oct = (mr[0] > off[0] ? 1 : 0) | (mr[1] > off[1] ? 2 : 0) | (mr[2] > off[2] ? 4 : 0);
  magCal._octCount[oct]++;
  for (let i = 0; i < 8; i++) magCal.octants[i] = magCal._octCount[i] >= K.octantMin ? 1 : 0;
  if (magCal.n < 3000) {
    cloudPos.set([(mr[0] - off[0]) * K.cloudMmPerUt, (mr[1] - off[1]) * K.cloudMmPerUt, (mr[2] - off[2]) * K.cloudMmPerUt], magCal.n * 3);
    cloudGeo.setDrawRange(0, magCal.n + 1); cloudGeo.attributes.position.needsUpdate = true;
  }
  const span = [0, 1, 2].map((i) => (magCal.max[i] - magCal.min[i]) / 2);
  const avg = (span[0] + span[1] + span[2]) / 3;
  fitSphere.position.set(0, 0, 0); fitSphere.scale.setScalar(Math.max(1, avg * K.cloudMmPerUt));
  magCal.n++;
}
function startCal() {
  Object.assign(magCal, { active: true, min: [1e9, 1e9, 1e9], max: [-1e9, -1e9, -1e9], n: 0, octants: new Array(8).fill(0), _octCount: new Array(8).fill(0) });
  cloudGeo.setDrawRange(0, 0); cloud.visible = true; fitSphere.visible = true;
  ui.btnCal.textContent = 'Finish calibration'; ui.btnCal.classList.add('active');
}
function finishCal() {
  magCal.active = false; cloud.visible = false; fitSphere.visible = false;
  ui.btnCal.textContent = 'Calibrate mag'; ui.btnCal.classList.remove('active');
  const span = [0, 1, 2].map((i) => (magCal.max[i] - magCal.min[i]) / 2);
  const covered = magCal.octants.filter(Boolean).length;
  if (magCal.n < 200 || Math.min(...span) < 5 || covered < K.octantsRequired) {
    ui.magNote.textContent = `Calibration aborted: ${covered}/8 directions covered — roll it through every orientation.`; return;
  }
  const avg = (span[0] + span[1] + span[2]) / 3;
  magCal.offset = [0, 1, 2].map((i) => (magCal.max[i] + magCal.min[i]) / 2);
  magCal.scale = span.map((sp) => avg / sp);
  magCal.done = true; D.bRef = avg;
  try { localStorage.setItem('magCal', JSON.stringify({ offset: magCal.offset, scale: magCal.scale, bRef: avg })); } catch (_) {}
  ahrs.reset(); warmupSamples = 0; ahrs.beta = K.betaWarmup;
}
ui.btnCal.onclick = () => (magCal.active ? finishCal() : startCal());
ui.btnZero.onclick = () => { headingOffset = ahrs.heading(); };
ui.btnReset.onclick = () => { ahrs.reset(); warmupSamples = 0; ahrs.beta = K.betaWarmup; headingOffset = 0; D.turns = 0; };

// ------------------------------------------------------------------ SSE -----

const es = new EventSource('/stream');
es.addEventListener('imu', (e) => onSample(JSON.parse(e.data)));
es.addEventListener('status', (e) => onStatus(JSON.parse(e.data)));
es.onerror = () => { setBoard('backend down', false); ui.overlay.classList.remove('hidden'); ui.overlayMsg.textContent = 'lost connection to backend/server.py'; };

function setBoard(text, ok) {
  ui.espDot.className = 'dot' + (ok ? ' ok' : (text === 'stalled' || text === 'connecting') ? ' warn' : '');
  ui.espState.textContent = text;
}
function onStatus(st) {
  const live = st.esp === 'connected';
  setBoard(st.esp, live);
  ui.overlay.classList.toggle('hidden', live);
  ui.overlayMsg.textContent = live ? ''
    : st.esp === 'stalled' ? `no data for ${st.gap_ms} ms — Wi-Fi dropped?`
    : 'backend is looking for the board — check the IP printed on USB serial';
  ui.rate.textContent = typeof st.hz === 'number' ? st.hz.toFixed(0) : (st.hz ?? 0);
  ui.rssi.textContent = st.rssi == null ? '—' : st.rssi;
  ui.loss.textContent = typeof st.lost_pct === 'number' ? st.lost_pct.toFixed(1) : (st.lost_pct ?? 0);
  ui.gap.textContent = st.gap_ms ?? 0;
  ui.loss.parentElement.classList.toggle('warn', (st.lost_pct || 0) > 2);
  ui.gap.parentElement.classList.toggle('warn', (st.gap_ms || 0) > 150);
  if (!live) { lastT = null; lastSeq = null; }
}

// ---------------------------------------------------------------- render ----

const springX = new THREE.Vector3(), springV = new THREE.Vector3(), target = new THREE.Vector3();
const qTmp = new THREE.Quaternion(), qSpin = new THREE.Quaternion();
const Zaxis = new THREE.Vector3(0, 0, 1);
const colTmp = new THREE.Color(), colWhite = new THREE.Color(COL.white);
let spinAngle = 0;                                   // integrated chevron phase (radians)
const ONION_STEPS = [[4, 0.45], [7, 0.25]];          // [frames back, base opacity]
const QN = 10;                                       // preallocated onion quaternion ring (no per-frame alloc)
const quatRing = Array.from({ length: QN }, () => new THREE.Quaternion());
let quatHead = 0;
let lastDom = 0, prevNow = performance.now();

function frame(now) {
  requestAnimationFrame(frame);
  const dt = Math.min((now - prevNow) / 1000, K.springDtMax); prevNow = now;
  controls.update();

  board.quaternion.copy(bodyQ);

  // spring integration -> cradle.position
  const holdMag = D.linHold.length();
  if (holdMag > 1e-4) { const dTgt = K.accMmPerG * Math.min(holdMag, 1) + (holdMag > 1 ? 8 * Math.tanh((holdMag - 1) / 0.5) : 0); target.copy(D.linHold).multiplyScalar(Math.min(dTgt, K.accSoftMax) / holdMag); }
  else target.set(0, 0, 0);
  // semi-implicit (symplectic) Euler: damp the OLD velocity first, then add the
  // spring acceleration. Damping the already-accelerated velocity instead would
  // be unstable once dt hits the 1/30 s clamp (~<=37 fps).
  springV.addScaledVector(springV, -2 * K.springZeta * K.springW0 * dt)
         .addScaledVector(tmpV.copy(target).sub(springX).multiplyScalar(K.springW0 * K.springW0), dt);
  springX.addScaledVector(springV, dt);
  cradle.position.copy(springX);

  updateAccelVis(springX.length());
  updateGyroVis(dt);
  updateMagVis();

  renderer.render(scene, camera);
  if (now - lastDom > 50) { lastDom = now; updateDom(now); }
}
requestAnimationFrame(frame);

function updateAccelVis(xMag) {
  const k = sat((xMag - 3) / 9);
  ghost.material.opacity = 0.10 * k; ghostEdges.material.opacity = 0.55 * k;
  ghost.quaternion.copy(board.quaternion); ghostEdges.quaternion.copy(board.quaternion);
  const hot = sat((D.peakG - 1) / 1);
  colTmp.setHex(COL.accel).lerp(colWhite, hot);
  if (xMag > 2) {
    tether.visible = true; tether.material.opacity = sat((xMag - 2) / 6) * 0.9; tether.material.color.copy(colTmp);
    tether.quaternion.setFromUnitVectors(tmpV.set(0, 1, 0), tmpV2.copy(springX).normalize());
    tether.scale.y = xMag; tether.position.copy(springX).multiplyScalar(0.5);
  } else tether.visible = false;
  foot.position.set(springX.x, springX.y, -15.85); footRim.position.copy(foot.position);
  const fs = 1 - 0.25 * (springX.z / K.accSoftMax); foot.scale.setScalar(fs); footRim.scale.setScalar(fs);
  footRim.material.opacity = 0.45 + 0.3 * sat(Math.hypot(springX.x, springX.y) / 16);
  plumb.geometry.attributes.position.setXYZ(0, springX.x, springX.y, springX.z);
  plumb.geometry.attributes.position.setXYZ(1, springX.x, springX.y, -15.85);
  plumb.geometry.attributes.position.needsUpdate = true;
  for (let i = 0; i < TRAIL_N - 1; i++) trailBuf.copyWithin(i * 3, (i + 1) * 3, (i + 2) * 3);
  trailBuf.set([springX.x, springX.y, springX.z], (TRAIL_N - 1) * 3);
  trailGeo.attributes.position.needsUpdate = true;
  trail.visible = xMag > 1;
  ring1g.material.opacity = xMag >= 38 ? 1 : 0.8;
  ring1g.material.color.setHex(xMag >= 38 ? COL.accel : 0x2d3a55);
  if (ui.vignette) ui.vignette.style.opacity = 0.7 * sat((D.peakG - 1) / 1.5);
}

function updateGyroVis(dt) {
  const op = sat((D.gyroMag - 8) / 112) * 0.9;
  const hot = sat((D.gyroMag - 360) / 360);
  colTmp.setHex(COL.gyro).lerp(colWhite, hot);
  qTmp.setFromUnitVectors(tmpV.set(0, 0, 1), D.axis);
  spinRing.quaternion.copy(qTmp); spinRing.material.opacity = op; spinRing.material.color.copy(colTmp);
  blurRing.quaternion.copy(qTmp); blurRing.material.opacity = 0.5 * sat((D.gyroMag - 240) / 480);
  // chevrons run at the real rate: integrate a persistent phase, then compose it
  // with the plane orientation (copy(qTmp) alone would reset the spin every frame).
  spinAngle = (spinAngle + D.spinSign * Math.min(D.gyroMag, K.gyroCap) * dt * Math.PI / 180) % (Math.PI * 2);
  spinner.quaternion.copy(qTmp).multiply(qSpin.setFromAxisAngle(Zaxis, spinAngle));
  spinner.children.forEach((c) => (c.material.opacity = op));
  spindle.quaternion.setFromUnitVectors(tmpV.set(0, 1, 0), D.axis);
  spindle.scale.y = Math.min(2 * (16 + 0.03 * D.gyroMag), 80); spindle.material.opacity = op;
  // onion skins from a preallocated quaternion ring buffer
  quatRing[quatHead].copy(board.quaternion);
  const newest = quatHead; quatHead = (quatHead + 1) % QN;
  const kk = sat((D.gyroMag - 30) / 120);
  ONION_STEPS.forEach(([back, base], i) => {
    onion[i].quaternion.copy(quatRing[(newest - back + QN) % QN]);
    onion[i].material.opacity = base * kk;
  });
}

function updateMagVis() {
  const show = !magCal.active;
  needle.visible = dipFan.visible = trace.visible = pivot.visible = show;
  if (!show) return;
  const bs = D.Bs.length() > 1e-3 ? tmpV.copy(D.Bs).normalize() : tmpV.set(0, 1, 0);
  needle.quaternion.setFromUnitVectors(tmpV2.set(0, 1, 0), bs);
  const thick = clamp(D.strength / (D.bRef || 1), 0.5, 1.8);
  needle.scale.set(thick, 1, thick);
  const col = HEALTH[D.health];
  nNorth.material.color.set(col); nTip.material.color.set(col);
  const trusted = D.health === 'OK' || D.health === 'FAIR';
  spriteN.visible = trusted; spriteQ.visible = !trusted;
  nNorth.material.opacity = nTip.material.opacity = D.health === 'UNCAL' ? 0.35 : 0.95;
  const nh = Math.hypot(D.Bs.x, D.Bs.y) || 1;
  const hnx = D.Bs.x / nh, hny = D.Bs.y / nh;   // horizontal north unit (NWU)
  const dipRad = D.dip * Math.PI / 180;
  fanBuf.set([0, 0, 0], 0);
  for (let i = 1; i < FAN_N; i++) {
    const t = (i - 1) / (FAN_N - 2) * dipRad;
    fanBuf.set([K.fanR * Math.cos(t) * hnx, K.fanR * Math.cos(t) * hny, -K.fanR * Math.sin(t)], i * 3);
  }
  fanGeo.attributes.position.needsUpdate = true; fanGeo.computeBoundingSphere();
  dipFan.material.color.set(col);
  trace.geometry.attributes.position.setXYZ(0, springX.x, springX.y, -15.85);
  trace.geometry.attributes.position.setXYZ(1, hnx * 32, hny * 32, -15.85);
  trace.geometry.attributes.position.needsUpdate = true; trace.computeLineDistances();
  trace.material.color.set(col);
}

function updateDom() {
  const fmt = (v, d) => (v >= 0 ? '+' : '') + v.toFixed(d);
  const setVals = (els, v, d) => { for (let i = 0; i < 3; i++) els[i].textContent = fmt(v[i], d); els[3].textContent = Math.hypot(...v).toFixed(d); };
  setVals(ui.vals.a, latest.a, 3); setVals(ui.vals.g, latest.g, 1); setVals(ui.vals.m, latest.mCal, 1);

  const e = ahrs.euler();
  const heading = (ahrs.heading() - headingOffset + 360) % 360;
  ui.heading.textContent = heading.toFixed(1) + '°';
  ui.roll.textContent = fmt(e.roll, 1) + '°'; ui.pitch.textContent = fmt(e.pitch, 1) + '°'; ui.yaw.textContent = fmt(e.yaw, 1) + '°';
  const magOn = ui.useMag.checked && magCal.done && !magCal.active;
  ui.fusionMode.textContent = magOn ? '9-DOF Madgwick' : '6-DOF (yaw drifts)';

  ui.summ.a.textContent = D.peakG > 1.5 ? `Hard hit ${D.peakG.toFixed(1)} g`
    : (D.lin.z < -0.7 && Math.hypot(...latest.a) < 0.3) ? 'Falling'
    : D.lin.length() > K.accDead ? `Moving ${D.lin.length().toFixed(2)} g` : 'Still';
  const absG = latest.g.map(Math.abs);
  const domAxis = ['X', 'Y', 'Z'][absG.indexOf(Math.max(...absG))];
  ui.summ.g.textContent = D.gyroMag < 8 ? 'Still'
    : Math.abs(D.axis.z) > 0.8 ? `Spinning flat ${D.gyroMag.toFixed(0)} °/s`
    : Math.abs(D.axis.z) < 0.4 ? `Flipping ${D.gyroMag.toFixed(0)} °/s (${domAxis})` : `Tumbling ${D.gyroMag.toFixed(0)} °/s`;
  ui.summ.m.textContent = magCal.active ? `Calibrating… ${magCal.octants.filter(Boolean).length}/8 dirs`
    : `dip ${D.dip.toFixed(0)}° · ${D.strength.toFixed(0)} µT`;

  ui.magNote.className = 'note' + (magOn && D.health === 'OK' ? '' : ' warn');
  ui.magNote.textContent = magCal.active
    ? `Rotate through every orientation — ${magCal.octants.filter(Boolean).length}/8 covered, ${magCal.n} samples.`
    : !magCal.done ? 'Not calibrated — heading may be off. Click Calibrate mag and tumble the board.'
    : `${D.health}${D._cv != null ? ` · cv ${(D._cv * 100).toFixed(0)}%` : ''} · offset [${magCal.offset.map((v) => v.toFixed(0)).join(', ')}] µT`;

  drawCompass(ui.compass, { heading, magOn, magNorthDeg: (D.magNorthDeg - headingOffset + 360) % 360,
    trendDeg: clamp(-D.omegaW.z, -90, 90), healthColor: HEALTH[D.health] });
  drawAccel(ui.gauge.a, { east: -D.lin.y, north: D.lin.x, up: D.lin.z, absG: D.lin.length(), peakG: D.peakG, trail: accelTrail });
  drawGyro(ui.gauge.g, { absW: D.gyroMag, peakW: D.gyroMag, share: latest.g, turns: D.turns });
  drawMag(ui.gauge.m, { dip: D.dip, strength: D.strength, bRef: D.bRef, healthColor: HEALTH[D.health],
    badge: badgeText(), calibrating: magCal.active, octants: magCal.octants, calCount: magCal.n });
}

function badgeText() {
  if (magCal.active) return '';
  const cv = ((D._cv || 0) * 100).toFixed(0);
  switch (D.health) {
    case 'OK': return `OK · cv ${cv}%`;
    case 'FAIR': return `FAIR · cv ${cv}%`;
    case 'POOR': return 'POOR · recalibrate';
    case 'DISTURBED': return `DISTURBED · ${D.strength.toFixed(0)} µT`;
    default: return 'UNCALIBRATED';
  }
}
