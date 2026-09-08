// 2D canvas widgets for the side panel + compass. Pure functions of (canvas, state).
// Kept out of app.js so the 3D file stays readable.

const TAU = Math.PI * 2;
export const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
export const sat = (v) => (v < 0 ? 0 : v > 1 ? 1 : v);
const lerp = (a, b, t) => a + (b - a) * t;

// dpr-aware canvas setup; returns a context whose units are CSS pixels.
export function prepare(canvas) {
  const dpr = Math.min(devicePixelRatio || 1, 2);
  const w = canvas.clientWidth || canvas.width;
  const h = canvas.clientHeight || canvas.height;
  const cw = Math.round(w * dpr), ch = Math.round(h * dpr);
  if (canvas.width !== cw || canvas.height !== ch) { canvas.width = cw; canvas.height = ch; }
  const ctx = canvas.getContext('2d');
  ctx.setTransform(cw / w, 0, 0, ch / h, 0, 0);
  ctx.clearRect(0, 0, w, h);
  return { ctx, w, h };
}

const C = {
  bg: '#0d1220', line: '#1f2937', tick: '#4b5567', text: '#e5e7eb', muted: '#8b95a7',
  x: '#f87171', y: '#4ade80', z: '#60a5fa',
  accel: '#22d3ee', gyro: '#fbbf24', mag: '#e879f9', ok: '#34d399', bad: '#f87171',
};

// ---------------------------------------------------------------- compass ---

// heading (deg CW from north), magOn, magNorthDeg (measured mag north on the rose),
// trendDeg (signed, where heading goes in 1 s), healthColor.
export function drawCompass(canvas, s) {
  const { ctx, w } = prepare(canvas);
  const R = w * 0.44, cx = w / 2, cy = w / 2;
  ctx.save(); ctx.translate(cx, cy);
  ctx.beginPath(); ctx.arc(0, 0, R, 0, TAU); ctx.fillStyle = C.bg; ctx.fill();
  ctx.strokeStyle = '#2b3648'; ctx.lineWidth = 2; ctx.stroke();

  // rotating rose
  ctx.save(); ctx.rotate(-s.heading * Math.PI / 180);
  for (let d = 0; d < 360; d += 10) {
    const major = d % 90 === 0, mid = d % 30 === 0;
    ctx.beginPath(); ctx.moveTo(0, -R); ctx.lineTo(0, -R + (major ? 16 : mid ? 10 : 5));
    ctx.strokeStyle = major ? C.text : C.tick; ctx.lineWidth = major ? 3 : 1.5; ctx.stroke();
    if (major) {
      ctx.save(); ctx.translate(0, -R + 34); ctx.rotate((s.heading - d) * Math.PI / 180);
      ctx.fillStyle = d === 0 ? C.bad : C.text; ctx.font = 'bold 22px system-ui'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
      ctx.fillText('NESW'[d / 90], 0, 0); ctx.restore();
    }
    ctx.rotate(Math.PI / 18);
  }
  // measured magnetic-north pointer on the rose (separates from index in 6-DOF / bad cal)
  if (s.magOn && s.magNorthDeg != null) {
    ctx.save(); ctx.rotate(s.magNorthDeg * Math.PI / 180);
    ctx.beginPath(); ctx.moveTo(0, -R + 6); ctx.lineTo(-4, -R + 20); ctx.lineTo(4, -R + 20); ctx.closePath();
    ctx.fillStyle = s.healthColor || C.mag; ctx.fill(); ctx.restore();
  }
  ctx.restore();

  // fixed top index needle
  ctx.beginPath(); ctx.moveTo(0, -R + 2); ctx.lineTo(-7, -R + 18); ctx.lineTo(7, -R + 18); ctx.closePath();
  ctx.fillStyle = s.magOn ? C.mag : C.gyro; ctx.fill();

  // heading trend arc (where it will point in 1 s)
  if (Math.abs(s.trendDeg) > 3) {
    const a0 = -Math.PI / 2, a1 = a0 + s.trendDeg * Math.PI / 180;
    ctx.beginPath(); ctx.arc(0, 0, R - 4, Math.min(a0, a1), Math.max(a0, a1));
    ctx.strokeStyle = C.gyro; ctx.lineWidth = 4; ctx.lineCap = 'round'; ctx.stroke();
  }

  ctx.fillStyle = C.text; ctx.font = 'bold 40px system-ui'; ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
  ctx.fillText(s.heading.toFixed(0) + '°', 0, 4);
  ctx.fillStyle = C.muted; ctx.font = '13px system-ui';
  ctx.fillText(s.magOn ? 'magnetic' : 'gyro yaw', 0, 34);
  ctx.restore();
}

// ----------------------------------------------------------- accel gauge ---

// state: east, north, up (g, gravity removed), mag |a_h|, absG, peakG
export function drawAccel(canvas, s) {
  const { ctx, w, h } = prepare(canvas);
  const cx = 55, cy = h / 2, R = 46;
  // bullseye, world frame, N up
  ctx.strokeStyle = C.line; ctx.lineWidth = 1;
  for (const g of [0.5, 1]) { ctx.beginPath(); ctx.arc(cx, cy, R * (g / 1.5), 0, TAU); ctx.stroke(); }
  ctx.beginPath(); ctx.arc(cx, cy, R, 0, TAU); ctx.strokeStyle = '#2b3648'; ctx.stroke();
  ctx.fillStyle = C.muted; ctx.font = '9px system-ui'; ctx.textAlign = 'center';
  ctx.fillText('N', cx, cy - R - 4); ctx.fillText('1.5g', cx + R - 9, cy - 4);
  // trail
  const rr = (gx, gy) => { const m = Math.hypot(gx, gy); const r = R * Math.min(1, m / 1.5); const a = Math.atan2(gy, gx); return [cx + r * Math.cos(a), cy - r * Math.sin(a)]; };
  if (s.trail && s.trail.length) {
    for (let i = 0; i < s.trail.length; i++) {
      const [px, py] = rr(s.trail[i][0], s.trail[i][1]);
      ctx.globalAlpha = (i / s.trail.length) * 0.5; ctx.fillStyle = C.accel;
      ctx.beginPath(); ctx.arc(px, py, 2, 0, TAU); ctx.fill();
    }
    ctx.globalAlpha = 1;
  }
  // peak-hold ring
  if (s.peakG > 0.05) { ctx.beginPath(); ctx.arc(cx, cy, R * Math.min(1, s.peakG / 1.5), 0, TAU); ctx.strokeStyle = 'rgba(34,211,238,0.35)'; ctx.setLineDash([2, 3]); ctx.stroke(); ctx.setLineDash([]); }
  // live dot
  const hot = sat((s.peakG - 1) / 1);
  const dotCol = s.peakG > 2 ? C.bad : `rgb(${lerp(34, 255, hot) | 0},${lerp(211, 255, hot) | 0},${lerp(238, 255, hot) | 0})`;
  const [dx, dy] = rr(s.east, s.north);
  ctx.beginPath(); ctx.arc(dx, dy, 5, 0, TAU); ctx.fillStyle = dotCol; ctx.fill();

  // vertical bar (up/down g)
  const bx = 108, bh = h - 24, by = 12;
  ctx.strokeStyle = C.line; ctx.strokeRect(bx, by, 10, bh);
  ctx.strokeStyle = C.tick; ctx.beginPath(); ctx.moveTo(bx, by + bh / 2); ctx.lineTo(bx + 10, by + bh / 2); ctx.stroke();
  const uz = clamp(s.up / 1.5, -1, 1) * (bh / 2);
  ctx.fillStyle = C.accel; ctx.fillRect(bx, by + bh / 2 - Math.max(0, uz), 10, Math.abs(uz));
  ctx.fillStyle = C.muted; ctx.font = '8px system-ui'; ctx.textAlign = 'left'; ctx.fillText('up', bx - 2, by - 3);

  // number
  ctx.textAlign = 'right'; ctx.fillStyle = dotCol; ctx.font = 'bold 26px "JetBrains Mono", Consolas, monospace';
  ctx.fillText(s.absG.toFixed(2), w - 6, h / 2 - 4);
  ctx.fillStyle = C.muted; ctx.font = '11px system-ui'; ctx.fillText('|a| g', w - 6, h / 2 + 12);
  ctx.fillText('peak ' + s.peakG.toFixed(2), w - 6, h / 2 + 28);
}

// ------------------------------------------------------------ gyro gauge ---

// state: absW (deg/s), peakW, share [x,y,z] signed, turns
export function drawGyro(canvas, s) {
  const { ctx, w, h } = prepare(canvas);
  const cx = 55, cy = h - 14, R = 42;
  const A0 = Math.PI * 1.2, A1 = -Math.PI * 0.2;            // 240 deg arc
  const map = (v) => Math.sqrt(sat(v / 720));               // sqrt so slow turns show
  ctx.lineWidth = 8; ctx.lineCap = 'round';
  ctx.beginPath(); ctx.arc(cx, cy, R, A1, A0, true); ctx.strokeStyle = C.line; ctx.stroke();
  const aVal = A0 + (A1 - A0) * map(s.absW);
  const hot = sat((s.absW - 360) / 360);
  ctx.beginPath(); ctx.arc(cx, cy, R, A0, aVal, true);
  ctx.strokeStyle = hot > 0 ? `rgb(${lerp(251,255,hot)|0},${lerp(191,255,hot)|0},${lerp(36,255,hot)|0})` : C.gyro; ctx.stroke();
  if (s.peakW > 8) { const ap = A0 + (A1 - A0) * map(s.peakW); ctx.beginPath(); ctx.arc(cx, cy, R + 6, ap - 0.02, ap + 0.02); ctx.strokeStyle = C.text; ctx.lineWidth = 3; ctx.stroke(); }
  ctx.fillStyle = C.muted; ctx.font = '9px system-ui'; ctx.textAlign = 'center';
  ctx.fillText('0', cx - R - 4, cy + 2); ctx.fillText('720', cx + R + 6, cy + 2);

  // axis-share bar under the arc
  const total = Math.abs(s.share[0]) + Math.abs(s.share[1]) + Math.abs(s.share[2]) || 1;
  const bx = 14, bw = 82, byy = h - 6, cols = [C.x, C.y, C.z];
  let px = bx;
  if (s.absW >= 8) for (let i = 0; i < 3; i++) {
    const seg = Math.abs(s.share[i]) / total * bw;
    ctx.fillStyle = cols[i]; ctx.fillRect(px, byy, seg, 4); px += seg;
  }

  // number + odometer
  ctx.textAlign = 'right'; ctx.fillStyle = C.gyro; ctx.font = 'bold 26px "JetBrains Mono", Consolas, monospace';
  ctx.fillText(s.absW.toFixed(0), w - 6, h / 2 - 6);
  ctx.fillStyle = C.muted; ctx.font = '11px system-ui'; ctx.fillText('|ω| °/s', w - 6, h / 2 + 10);
  ctx.fillText('turns ' + (s.turns >= 0 ? '+' : '') + s.turns.toFixed(1), w - 6, h / 2 + 26);
}

// ------------------------------------------------------------- mag gauge ---

// state: dip (deg), strength (uT), bRef, healthColor, badge, calibrating, octants[8], calCount
export function drawMag(canvas, s) {
  const { ctx, w, h } = prepare(canvas);
  // dip side-view (left)
  const cx = 52, cy = h / 2 + 6, R = 40;
  ctx.strokeStyle = C.line; ctx.lineWidth = 1;
  ctx.beginPath(); ctx.arc(cx, cy, R, Math.PI, TAU); ctx.stroke();            // upper? we draw horizon
  ctx.beginPath(); ctx.moveTo(cx - R, cy); ctx.lineTo(cx + R, cy); ctx.strokeStyle = '#2b3648'; ctx.stroke();
  // hatched ground
  ctx.strokeStyle = C.line;
  for (let i = -R; i < R; i += 7) { ctx.beginPath(); ctx.moveTo(cx + i, cy); ctx.lineTo(cx + i + 6, cy + 6); ctx.stroke(); }
  const dip = clamp(s.dip, -90, 90) * Math.PI / 180;
  ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(cx + R * Math.cos(dip), cy + R * Math.sin(dip));
  ctx.strokeStyle = s.healthColor || C.mag; ctx.lineWidth = 2.5; ctx.stroke();
  ctx.fillStyle = C.muted; ctx.font = '9px system-ui'; ctx.textAlign = 'center';
  ctx.fillText('dip ' + s.dip.toFixed(0) + '°', cx, cy - R - 3);

  if (s.calibrating) {
    // octant coverage grid instead of the badge
    const gx = 108, gy = 14;
    ctx.textAlign = 'left'; ctx.fillStyle = C.muted; ctx.font = '10px system-ui';
    ctx.fillText('coverage ' + s.octants.filter(Boolean).length + '/8', gx, gy - 2);
    for (let i = 0; i < 8; i++) {
      ctx.fillStyle = s.octants[i] ? C.mag : C.line;
      ctx.fillRect(gx + (i % 4) * 16, gy + 4 + ((i / 4) | 0) * 16, 13, 13);
    }
    ctx.fillStyle = C.muted; ctx.font = '10px system-ui'; ctx.fillText(s.calCount + ' samples', gx, gy + 46);
    return;
  }

  // strength bar (right)
  const bx = 104, bw = w - bx - 8, by = h / 2 - 18, bh = 10;
  ctx.strokeStyle = C.line; ctx.strokeRect(bx, by, bw, bh);
  ctx.fillStyle = 'rgba(52,211,153,0.15)'; ctx.fillRect(bx + bw * 0.25, by, bw * 0.4, bh);   // 25-65 uT green band
  const mk = bx + bw * clamp(s.strength / 100, 0, 1);
  ctx.fillStyle = s.healthColor || C.mag; ctx.beginPath(); ctx.moveTo(mk, by - 3); ctx.lineTo(mk - 4, by - 9); ctx.lineTo(mk + 4, by - 9); ctx.closePath(); ctx.fill();
  ctx.fillStyle = C.text; ctx.font = 'bold 15px "JetBrains Mono", Consolas, monospace'; ctx.textAlign = 'right';
  ctx.fillText(s.strength.toFixed(0) + ' µT', bx + bw, by - 12);
  // badge
  ctx.textAlign = 'left'; ctx.fillStyle = s.healthColor || C.mag; ctx.font = 'bold 12px system-ui';
  ctx.fillText(s.badge, bx, by + bh + 16);
}
