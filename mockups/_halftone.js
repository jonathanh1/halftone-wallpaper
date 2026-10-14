/* Shared runtime for the standalone halftone-aurora study pages.
   Aurora field uses "square-uv" units: x,y in screen-heights, y up from bottom.
   The same field feeds the 2D and WebGL renderers so they light identically;
   halftone-aurora.html is the reference implementation these mirror. */
"use strict";
(() => {
const HA = {};
HA.PAL = {
  magenta: [255, 47, 168],
  violet:  [139, 77, 255],
  royal:   [59, 91, 255],
  indigo:  [79, 70, 229],
};
HA.FOCI = [
  { fx: 0.14, y: -0.04, r: 0.36, c: HA.PAL.magenta, ph: 0.0, sp: 0.21, ax: 0.055 },
  { fx: 0.52, y:  0.00, r: 0.30, c: HA.PAL.violet,  ph: 2.1, sp: 0.16, ax: 0.045 },
  { fx: 0.88, y: -0.03, r: 0.38, c: HA.PAL.royal,   ph: 4.4, sp: 0.12, ax: 0.060 },
];
HA.FOCI_DRIFT = [
  { fx: 0.30, y: -0.05, r: 0.34, c: HA.PAL.indigo,  ph: 1.2, sp: 0.14, ax: 0.05 },
  { fx: 0.72, y:  0.00, r: 0.33, c: HA.PAL.magenta, ph: 3.9, sp: 0.18, ax: 0.05 },
];
// Lobes are wide, low ellipses so they read as one aurora band, not three hills.
// Portrait screens narrow the lobes (each hue keeps its own territory across ~390px)
// and lift them, so the band still owns the lower third of a tall phone.
const LOBE_X = 1.4, LOBE_Y = 0.8;
// Faint ribbon hugging the bottom edge bridges the valleys between lobes.
const RIBBON = 0.2;

const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
HA.clamp = clamp;
const sstep = (e0, e1, x) => { const t = clamp((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t); };
HA.sstep = sstep;
// Stable 2D hash: rebuilds (resize, toolbar show/hide) keep the same stipple, stars and ink variance.
const hash2 = (i, j) => { const s = Math.sin(i * 127.1 + j * 311.7) * 43758.5453; return s - Math.floor(s); };
HA.hash2 = hash2;

HA.dims = { W: innerWidth, H: innerHeight, ASPECT: innerWidth / innerHeight };
HA.fit = canvases => {
  HA.dims.W = innerWidth; HA.dims.H = innerHeight; HA.dims.ASPECT = innerWidth / innerHeight;
  const d = Math.min(window.devicePixelRatio || 1, 2);
  for (const c of canvases) { c.width = Math.round(innerWidth * d); c.height = Math.round(innerHeight * d); }
};

HA.fociAt = (t, set, shift) => {
  const sx = shift ? shift.x : 0, sy = shift ? shift.y : 0;
  const narrow = clamp(HA.dims.ASPECT / 1.5, 0.4, 1);
  const kx = LOBE_X * narrow, ky = LOBE_Y * (1.6 - 0.6 * narrow);
  const out = [];
  for (const f of set) {
    out.push({
      x: f.fx * HA.dims.ASPECT + Math.sin(t * f.sp + f.ph) * f.ax + sx,
      y: f.y + Math.sin(t * f.sp * 0.8 + f.ph * 1.7) * 0.03 + sy,
      rx: f.r * kx, ry: f.r * ky, c: f.c,
    });
  }
  return out;
};

/* Aurora structure. Noise is 1D in x because curtains are vertical: one fbm bends the
   band into folds, one sets the ionization edge (the bright, sharp lower hem of a
   curtain), and a coarse envelope carved by fine striations makes the rays. FRAG mirrors these. */
const frac = v => v - Math.floor(v);
const h11 = p => { p = frac(p * 0.1031); p *= p + 33.33; p *= p + p; return frac(p); };
const vn = x => { const i = Math.floor(x), f = x - i, a = h11(i); return a + (h11(i + 1) - a) * f * f * (3 - 2 * f); };
const fbm = x => (0.5 * vn(x) + 0.25 * vn(x * 2.03 + 17) + 0.125 * vn(x * 4.1 + 31)) / 0.875;

// Field at a square-uv point: intensity I, mixed color c, hi = ionization-hem highlight.
HA.fieldAt = (x, y, t, foci) => {
  const yy = y + 0.04 * Math.sin(x * 5.0 + t * 0.25) + 0.022 * Math.sin(x * 9.0 - t * 0.4)
    + 0.08 * (fbm(x * 1.7 + t * 0.045) - 0.5);
  const c = [0, 0, 0];
  let sum = 0;
  for (const f of foci) {
    const dx = (x - f.x) / f.rx, dy = (yy - f.y) / f.ry;
    const w = Math.exp(-(dx * dx + dy * dy));
    sum += w;
    for (let k = 0; k < 3; k++) c[k] += f.c[k] * w;
  }
  if (sum > 1e-4) for (let k = 0; k < 3; k++) c[k] /= sum;
  // Rays: a coarse envelope (where the curtain is bright) carved by fine striations a dot
  // or two wide, so the curtain reads as many thin rays rather than a few bars.
  const coarse = clamp((fbm(x * 9 + t * 0.04 + 3.1) - 0.28) * 1.6, 0, 1);
  const fine = 0.6 * vn(x * 48 + t * 0.05 + 9.7) + 0.4 * vn(x * 97 - t * 0.12 + 4.2);
  const ray = clamp(coarse * (0.25 + fine) + (fine - 0.5) * 0.3, 0, 1);
  const d = yy - (0.2 + 0.09 * (fbm(x * 3.1 - t * 0.05 + 11) - 0.5));
  const above = sstep(-0.035, 0.004, d);
  const env = Math.min(1, sum * 1.35);
  // Bright rays reach higher: the top edge breaks into spikes instead of a smooth fade.
  const fade = 1 - sstep(0.04, 0.44 + 0.26 * ray, yy);
  const diffuse = fade * Math.min(1, (sum + RIBBON * Math.exp(-yy * yy / 0.06)) * 1.15) * (1 - 0.5 * above * (1 - ray));
  const curtain = above * Math.exp(-Math.max(0, d) / (0.05 + 0.22 * ray)) * (0.2 + 0.9 * ray);
  // Hem: sharp below the fold, softer above it.
  const hi = env * Math.exp(-d * d / (d < 0 ? 0.0002 : 0.0025)) * (0.8 + 0.2 * ray);
  const I = diffuse + env * (0.5 * curtain + 0.45 * hi) * (1 - sstep(0.36, 0.7, yy));
  // Ray tops drift toward magenta, as high-altitude emission shifts hue.
  const m = 0.4 * above * sstep(0.05, 0.3, d);
  for (let k = 0; k < 3; k++) c[k] += (HA.PAL.magenta[k] - c[k]) * m;
  return { I, c, hi };
};

/* ---------- pointer ---------- */
const pointer = { x: -1e4, y: -1e4, vx: 0, vy: 0, down: false, px: -1e4, py: -1e4, pt: 0 };
HA.pointer = pointer;
HA.trackPointer = (el, cb) => {
  const onMove = (e, isDown) => {
    const r = el.getBoundingClientRect();
    const x = e.clientX - r.left, y = e.clientY - r.top;
    const now = performance.now();
    const dt = Math.max(8, now - pointer.pt) / 1000;
    if (pointer.pt) {
      pointer.vx = clamp((x - pointer.px) / dt, -2600, 2600);
      pointer.vy = clamp((y - pointer.py) / dt, -2600, 2600);
    }
    pointer.px = pointer.x = x; pointer.py = pointer.y = y; pointer.pt = now;
    cb(x, y, pointer.vx, pointer.vy, isDown ? "drag" : "hover");
  };
  el.addEventListener("pointerdown", e => {
    pointer.down = true; el.classList.add("dragging");
    try { el.setPointerCapture(e.pointerId); } catch {}
    onMove(e, true);
  });
  el.addEventListener("pointermove", e => onMove(e, pointer.down));
  const release = () => {
    if (!pointer.down) return;
    pointer.down = false; el.classList.remove("dragging");
    cb(pointer.x, pointer.y, 0, 0, "release");
  };
  el.addEventListener("pointerup", release);
  el.addEventListener("pointercancel", release);
};

/* ---------- motion gate ---------- */
const mq = matchMedia("(prefers-reduced-motion: reduce)");
const param = new URLSearchParams(location.search).get("motion");
HA.motionOff = () => param === "full" ? false : param === "off" || mq.matches;
HA.onMotionChange = fn => mq.addEventListener?.("change", fn);

/* ---------- 2D atmosphere layers (shared by make2D and custom loops like Cascade) ---------- */
// Draws in CSS-pixel space on a context already transformed by DPR.
HA.makeSky = (canvas, ctx) => {
  let stars = [];
  // Under-glow: the field sampled on a coarse grid and upscaled bilinearly beneath the dots,
  // so light bleeds into the gaps the way an emissive band does through a screen.
  const GLOW_CELL = 14;
  const glowC = document.createElement("canvas"), gctx = glowC.getContext("2d");
  let glowImg = null;
  // Ink grain: a fixed noise tile multiplied over the frame gives each dot a printed texture.
  const grainC = document.createElement("canvas");
  grainC.width = grainC.height = 128;
  {
    const g = grainC.getContext("2d"), im = g.createImageData(128, 128);
    for (let i = 0; i < 128 * 128; i++) {
      const v = 218 + 37 * hash2((i & 127) + 0.37, (i >> 7) + 0.71);
      im.data[i * 4] = im.data[i * 4 + 1] = im.data[i * 4 + 2] = v; im.data[i * 4 + 3] = 255;
    }
    g.putImageData(im, 0, 0);
  }
  const grain = ctx.createPattern(grainC, "repeat");

  // Star field: sparse, deterministic, thinning out as it nears the aurora.
  const buildStars = () => {
    const { W, H } = HA.dims;
    stars = [];
    const n = Math.round(W * H / 4500);
    for (let s = 0; s < n; s++) {
      const m = hash2(s + 0.5, s * 1.3), y = H * 0.7 * Math.pow(hash2(7.7, s), 1.3);
      stars.push({ x: hash2(s, 3.3) * W, y, r: 0.45 + 0.85 * m * m,
        a: (0.3 + 0.65 * hash2(s * 2.1, 9.2)) * (1 - sstep(H * 0.4, H * 0.7, y)),
        ph: m * 40, sp: 0.5 + 2.4 * hash2(s, s + 9) });
    }
  };
  const drawStars = (t, freeze) => {
    ctx.fillStyle = "#e6e9ff";
    for (const s of stars) {
      ctx.globalAlpha = s.a * (freeze ? 1 : 0.65 + 0.35 * Math.sin(t * s.sp + s.ph));
      ctx.beginPath(); ctx.arc(s.x, s.y, s.r, 0, 6.2832); ctx.fill();
    }
    ctx.globalAlpha = 1;
  };
  const drawGlow = (t, foci, strength) => {
    const { W, H } = HA.dims;
    const y0 = Math.floor(H * 0.25), cell = GLOW_CELL;
    const gw = Math.ceil(W / cell) + 1, gh = Math.ceil((H - y0) / cell) + 1;
    if (glowC.width !== gw || glowC.height !== gh || !glowImg) { glowC.width = gw; glowC.height = gh; glowImg = gctx.createImageData(gw, gh); }
    const px = glowImg.data;
    for (let j = 0; j < gh; j++) for (let i = 0; i < gw; i++) {
      const f = HA.fieldAt(i * cell / H, (H - y0 - j * cell) / H, t, foci);
      const a = Math.min(1, f.I) * strength, o = (j * gw + i) * 4;
      px[o] = f.c[0] * a; px[o + 1] = f.c[1] * a; px[o + 2] = f.c[2] * a; px[o + 3] = 255;
    }
    gctx.putImageData(glowImg, 0, 0);
    ctx.imageSmoothingEnabled = true; ctx.imageSmoothingQuality = "high";
    // texel i is centered on x = i·cell
    ctx.drawImage(glowC, -cell / 2, y0 - cell / 2, gw * cell, gh * cell);
  };
  // Grain re-rolls at 24fps; frozen frames keep one fixed offset so static output is stable.
  const drawGrain = (t, freeze) => {
    const f = freeze ? 0 : Math.floor(t * 24);
    const ox = (h11(f + 0.5) * 128) | 0, oy = (h11(f + 77.7) * 128) | 0;
    const m = ctx.getTransform();
    ctx.setTransform(1, 0, 0, 1, ox, oy);
    ctx.globalCompositeOperation = "multiply";
    ctx.fillStyle = grain; ctx.fillRect(-ox, -oy, canvas.width, canvas.height);
    ctx.globalCompositeOperation = "source-over";
    ctx.setTransform(m);
  };
  return { buildStars, drawStars, drawGlow, drawGrain };
};

/* Ink color for a dot of intensity I, field color c and hem highlight hi. The hem and
   overdriven dots bleach toward white (ink saturation); returns quantized RGB + whitening. */
const q = v => (v > 255 ? 255 : v) & 0xF8;
HA.ink = (c, I, hi, dens) => {
  const lum = (0.55 + 0.45 * Math.min(1, I)) * dens;
  const wht = clamp(hi * 0.3 + (I - 1.05) * 0.5, 0, 0.34);
  return {
    R: q(c[0] * lum + (255 - c[0] * lum) * wht),
    G: q(c[1] * lum + (235 - c[1] * lum) * wht),
    B: q(c[2] * lum + (255 - c[2] * lum) * wht),
    wht,
  };
};
HA.bucket = (map, R, G, B) => {
  const key = R << 16 | G << 8 | B;
  let b = map.get(key);
  if (!b) { b = { path: new Path2D(), col: `rgb(${R},${G},${B})` }; map.set(key, b); }
  return b.path;
};

/* ---------- Canvas 2D dot-field renderer ---------- */
// opts: { spacing, rMax, set, warp, glow, shift, freeze, under? } — `under` is the under-glow strength (default 0.22).
HA.make2D = canvas => {
  const ctx = canvas.getContext("2d");
  const DPR = Math.min(window.devicePixelRatio || 1, 2);
  const sky = HA.makeSky(canvas, ctx);
  let dots = [], impulses = [], spacingNow = 0;

  const build = spacing => {
    const { W, H } = HA.dims;
    dots = [];
    // Rows are anchored to the bottom edge, where the aurora lives.
    for (let j = 0, y = H + spacing * 0.5; y >= H * 0.30; j++, y -= spacing)
      for (let i = 0, x = spacing * 0.5; x <= W; i++, x += spacing)
        dots.push({ bx: x + (hash2(i, j) - 0.5) * spacing * 0.16, by: y + (hash2(j + 71, i) - 0.5) * spacing * 0.16, dx: 0, dy: 0, vx: 0, vy: 0,
          // Per-dot ink variance: dot gain and density wander cell to cell, like a real screen.
          g: 0.86 + 0.28 * hash2(i + 13, j + 29), l: 0.92 + 0.12 * hash2(j + 5, i + 41) });
    sky.buildStars();
  };

  const impulseQueue = (x, y, vx, vy, kind) => {
    if (kind === "release") return;
    const s = kind === "drag" ? 1.0 : 0.30;
    impulses.push({ x, y, vx: vx * s, vy: vy * s });
    if (impulses.length > 6) impulses.shift();
  };

  const render = (t, dt, opts) => {
    const { W, H } = HA.dims;
    const { spacing, rMax, set, warp, glow, shift, freeze, under = 0.22 } = opts;
    if (!dots.length || spacing !== spacingNow) { build(spacing); spacingNow = spacing; }
    ctx.setTransform(DPR, 0, 0, DPR, 0, 0);
    ctx.fillStyle = "#000"; ctx.fillRect(0, 0, W, H);
    const foci = HA.fociAt(t, set, shift && shift());
    sky.drawStars(t, freeze);
    sky.drawGlow(t, foci, under);
    const k = 96, c = 13;
    const buckets = new Map(), halos = new Map();
    const fringeR = new Path2D(), fringeB = new Path2D();
    for (const d of dots) {
      if (warp && !freeze) {
        d.vx += (-k * d.dx - c * d.vx) * dt;
        d.vy += (-k * d.dy - c * d.vy) * dt;
        d.dx = clamp(d.dx + d.vx * dt, -18, 18);
        d.dy = clamp(d.dy + d.vy * dt, -18, 18);
        for (const im of impulses) {
          const ox = d.bx - im.x, oy = d.by - im.y;
          const g = Math.exp(-(ox * ox + oy * oy) / (2 * 130 * 130));
          const sp = Math.hypot(im.vx, im.vy);
          const od = Math.max(1, Math.hypot(ox, oy));
          // drag direction + outward "parting" component → fluid warp, not smear
          d.vx += (im.vx * 0.8 + (ox / od) * sp * 0.5) * g * 0.12;
          d.vy += (im.vy * 0.8 + (oy / od) * sp * 0.5) * g * 0.12;
        }
      }
      const px = d.bx + d.dx, py = d.by + d.dy;
      const f = HA.fieldAt(d.bx / H, (H - d.by) / H, t, foci);
      let I = f.I;
      I += (freeze ? 0 : 0.07) * Math.sin(d.bx * 0.02 + d.by * 0.012 - t * 1.15) * (1 - sstep(0.1, 0.5, (H - d.by) / H));
      if (glow && pointer.x > -999) {
        const ox = px - pointer.x, oy = py - pointer.y;
        I += 0.55 * Math.exp(-(ox * ox + oy * oy) / (2 * 130 * 130)) * (pointer.down ? 1 : 0.45);
      }
      I = clamp(I, 0, 1.25);
      const r = rMax * d.g * Math.pow(Math.min(I, 1.1), 0.78);
      if (r < 0.28) continue;
      const { R, G, B, wht } = HA.ink(f.c, I, f.hi, d.l);
      const p = HA.bucket(buckets, R, G, B);
      p.moveTo(px + r, py); p.arc(px, py, r, 0, 6.2832);
      if (I > 0.95 || wht > 0.15) {
        // Bloom: hot dots spill light past their edge.
        const h = HA.bucket(halos, R, G, B), hr = r * 2.1;
        h.moveTo(px + hr, py); h.arc(px, py, hr, 0, 6.2832);
      }
      if (wht > 0.25) {
        // Chromatic fringe: red and blue rims split off the brightest dots.
        fringeR.moveTo(px + r + 0.8, py); fringeR.arc(px + 0.8, py, r, 0, 6.2832);
        fringeB.moveTo(px + r - 0.8, py); fringeB.arc(px - 0.8, py, r, 0, 6.2832);
      }
    }
    impulses.length = 0;
    ctx.globalCompositeOperation = "lighter";
    ctx.fillStyle = "rgba(255,40,110,0.75)"; ctx.fill(fringeR);
    ctx.fillStyle = "rgba(40,140,255,0.75)"; ctx.fill(fringeB);
    ctx.globalCompositeOperation = "source-over";
    for (const b of buckets.values()) { ctx.fillStyle = b.col; ctx.fill(b.path); }
    ctx.globalCompositeOperation = "lighter";
    ctx.globalAlpha = 0.1;
    for (const b of halos.values()) { ctx.fillStyle = b.col; ctx.fill(b.path); }
    ctx.globalAlpha = 1;
    ctx.globalCompositeOperation = "source-over";
    sky.drawGrain(t, freeze);
  };
  render.rebuild = () => { spacingNow = 0; };
  return { render, impulseQueue };
};

/* ---------- WebGL renderer (fine halftone shader) ---------- */
const FRAG = `
precision highp float;
uniform vec2 u_res;
uniform float u_time;
uniform float u_cell;    // cell size in device px
uniform float u_ribbon;
uniform vec4 u_f0; uniform vec4 u_f1; uniform vec4 u_f2; // xy square-uv center, zw lobe radii
uniform vec4 u_ptr;      // xy square-uv pos, z drag strength, w release pulse
uniform vec2 u_trail[8];
uniform float u_trailT[8];

float gau(vec2 uv, vec4 f){ vec2 d = (uv - f.xy) / f.zw; return exp(-dot(d,d)); }
float ss(float e0, float e1, float x){ float t = clamp((x-e0)/(e1-e0),0.0,1.0); return t*t*(3.0-2.0*t); }
float h11(float p){ p = fract(p*0.1031); p *= p + 33.33; p *= p + p; return fract(p); }
float vn(float x){ float i = floor(x), f = fract(x); return mix(h11(i), h11(i+1.0), f*f*(3.0-2.0*f)); }
float fbm(float x){ return (0.5*vn(x) + 0.25*vn(x*2.03+17.0) + 0.125*vn(x*4.1+31.0)) / 0.875; }
float h21(vec2 p){ vec3 p3 = fract(vec3(p.xyx)*0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x+p3.y)*p3.z); }

// Mirrors HA.fieldAt(): intensity, mixed color, and ionization-hem highlight.
float field(vec2 p, out vec3 col, out float hi){
  float t = u_time;
  float yy = p.y + 0.04*sin(p.x*5.0 + t*0.25) + 0.022*sin(p.x*9.0 - t*0.4) + 0.08*(fbm(p.x*1.7 + t*0.045) - 0.5);
  vec2 q = vec2(p.x, yy);
  float w0 = gau(q, u_f0), w1 = gau(q, u_f1), w2 = gau(q, u_f2);
  float sum = w0 + w1 + w2;
  col = (w0 * vec3(1.0,0.184,0.659) + w1 * vec3(0.545,0.302,1.0) + w2 * vec3(0.231,0.357,1.0)) / max(sum, 1e-4);
  float coarse = clamp((fbm(p.x*9.0 + t*0.04 + 3.1) - 0.28)*1.6, 0.0, 1.0);
  float fine = 0.6*vn(p.x*48.0 + t*0.05 + 9.7) + 0.4*vn(p.x*97.0 - t*0.12 + 4.2);
  float ray = clamp(coarse*(0.25 + fine) + (fine - 0.5)*0.3, 0.0, 1.0);
  float d = yy - (0.2 + 0.09*(fbm(p.x*3.1 - t*0.05 + 11.0) - 0.5));
  float above = ss(-0.035, 0.004, d);
  float env = min(1.0, sum*1.35);
  float fade = 1.0 - ss(0.04, 0.44 + 0.26*ray, yy);
  float diffuse = fade * min(1.0, (sum + u_ribbon*exp(-yy*yy/0.06))*1.15) * (1.0 - 0.5*above*(1.0 - ray));
  float curtain = above * exp(-max(0.0, d)/(0.05 + 0.22*ray)) * (0.2 + 0.9*ray);
  hi = env * exp(-d*d/(d < 0.0 ? 0.0002 : 0.0025)) * (0.8 + 0.2*ray);
  col = mix(col, vec3(1.0,0.184,0.659), 0.4*above*ss(0.05, 0.3, d));
  return diffuse + env*(0.5*curtain + 0.45*hi)*(1.0 - ss(0.36, 0.7, yy));
}

// Warp at a square-uv point: xy = lookup offset (pulls cells toward the pointer), z = light.
vec3 force(vec2 uv){
  vec2 off = vec2(0.0); float lit = 0.0;
  for (int i = 0; i < 8; i++) {
    float age = u_trailT[i];
    vec2 d = uv - u_trail[i];
    float w = step(age, 2.5) * exp(-age*3.0) * exp(-dot(d,d)/0.010);
    off += d * w * 0.30;
    lit += w * 0.85;
  }
  vec2 dp = uv - u_ptr.xy;
  float well = u_ptr.z * exp(-dot(dp,dp)/0.018);
  off += dp * well * (0.55 + 0.30 * u_ptr.w);
  lit += well * 0.55;
  return vec3(off, lit);
}

void main(){
  float px = 1.0 / u_res.y;
  float dpr = u_cell / 10.0;
  vec2 uv = gl_FragCoord.xy * px; // square-uv, y up
  vec3 F = force(uv);
  vec2 p = uv + F.xy;
  // Jacobian of the warp (finite differences). Dots are measured in screen space so
  // they stay round under the lens instead of smearing into ellipses.
  vec2 jx = (uv + vec2(px, 0.0) + force(uv + vec2(px, 0.0)).xy - p) / px;
  vec2 jy = (uv + vec2(0.0, px) + force(uv + vec2(0.0, px)).xy - p) / px;
  float det = max(jx.x * jy.y - jy.x * jx.y, 0.05);
  float fro = dot(jx, jx) + dot(jy, jy);
  float smax = sqrt(0.5 * (fro + sqrt(max(fro * fro - 4.0 * det * det, 0.0))));

  float cs = u_cell / u_res.y;
  vec2 g = p / cs;
  vec2 cid = floor(g);
  vec2 cc = (cid + 0.5) * cs;

  vec3 col; float hi;
  float I = field(cc, col, hi);
  I += 0.06 * sin(cc.x*30.0 - u_time*1.1) * (1.0 - ss(0.1,0.5,cc.y));
  float lit = min(F.z, 1.2);
  I += lit * 0.7;

  // cell-local offset (p-space) → screen space via J⁻¹, rescaled by the largest stretch
  // so the round dot always fits inside its (possibly sheared) cell.
  vec2 lp = (fract(g) - 0.5) * cs;
  vec2 s = vec2(jy.y * lp.x - jy.x * lp.y, -jx.y * lp.x + jx.x * lp.y) / det;
  float dist = length(s) * smax;
  // Per-cell ink variance: dot gain and density wander like a real screen.
  float gain = 0.86 + 0.28 * h21(cid + 0.5);
  float dens = 0.92 + 0.12 * h21(cid + 19.7);
  float rr = clamp(I * gain, 0.0, 1.0) * cs * 0.46;   // radius saturates below the cell edge: no merged blobs
  float aa = 1.5 * px * smax;
  float cov = smoothstep(rr, rr - aa, dist);

  float lum = (0.55 + 0.45 * min(1.0, I) + 0.30 * lit) * dens;   // light beyond full radius brightens instead of bloating
  vec3 ink = col * lum;
  ink = mix(ink, vec3(1.0, 0.86, 1.0), clamp(lit * 0.22, 0.0, 0.3));
  // Ink saturation: the hem and overdriven dots bleach toward white…
  float wht = clamp(hi * 0.3 + (I - 1.05) * 0.5, 0.0, 0.34);
  ink = mix(ink, vec3(1.0, 0.92, 1.0), wht);
  // …and split red/blue rims (chromatic fringe) off the hottest ones.
  vec2 fo = vec2(ss(0.16, 0.28, wht) * 0.8 * dpr * px, 0.0);
  float covR = smoothstep(rr, rr - aa, length(s - fo) * smax);
  float covB = smoothstep(rr, rr - aa, length(s + fo) * smax);
  vec3 c3 = ink * cov
    + 0.75 * (vec3(1.0, 0.16, 0.43) * max(covR - cov, 0.0) + vec3(0.16, 0.55, 1.0) * max(covB - cov, 0.0));

  // Under-glow: the unquantized field at this pixel bleeds light into the gaps; hot regions bloom.
  vec3 gcol; float ghi;
  float gI = min(1.0, field(p, gcol, ghi)) + lit * 0.5;
  c3 += gcol * gI * (0.2 + 0.12 * ss(0.8, 1.0, gI) + 0.1 * ghi) * (1.0 - 0.6 * cov);

  // Star field: sparse, unwarped, thinning toward the aurora.
  float sc = 34.0 * dpr * px;
  vec2 sid = floor(uv / sc);
  float sh = h21(sid + 3.7), sm = h21(sid + 8.9);
  vec2 spos = (sid + 0.2 + 0.6 * vec2(h21(sid + 11.1), h21(sid + 23.3))) * sc;
  float sr = (0.35 + 0.8 * sm * sm) * dpr * px;
  float star = step(0.86, sh) * smoothstep(sr + px, sr - 0.5 * px, length(uv - spos))
    * (0.22 + 0.6 * h21(sid + 5.5)) * (0.65 + 0.35 * sin(u_time * (0.5 + 2.4 * sm) + sm * 40.0)) * ss(0.3, 0.6, uv.y);
  c3 += vec3(0.9, 0.914, 1.0) * star;

  // Ink grain, re-rolled at 24fps.
  float gr = h21(gl_FragCoord.xy + floor(u_time * 24.0) * vec2(37.0, 17.0));
  gl_FragColor = vec4(c3 * (0.855 + 0.145 * gr), 1.0);
}`;
const VERT = `attribute vec2 a; void main(){ gl_Position = vec4(a,0.0,1.0); }`;

HA.makeGL = canvas => {
  const gl = canvas.getContext("webgl", { antialias: false, alpha: false });
  if (!gl) return null;
  const DPR = Math.min(window.devicePixelRatio || 1, 2);
  const sh = (type, src) => {
    const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) { console.error(gl.getShaderInfoLog(s)); return null; }
    return s;
  };
  const vs = sh(gl.VERTEX_SHADER, VERT), fs = sh(gl.FRAGMENT_SHADER, FRAG);
  if (!vs || !fs) return null;
  const prog = gl.createProgram();
  gl.attachShader(prog, vs);
  gl.attachShader(prog, fs);
  gl.linkProgram(prog);
  if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) { console.error(gl.getProgramInfoLog(prog)); return null; }
  gl.useProgram(prog);
  const buf = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, buf);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1,-1, 3,-1, -1,3]), gl.STATIC_DRAW);
  const loc = gl.getAttribLocation(prog, "a");
  gl.enableVertexAttribArray(loc); gl.vertexAttribPointer(loc, 2, gl.FLOAT, false, 0, 0);
  const U = {};
  for (const n of ["u_res","u_time","u_cell","u_ribbon","u_f0","u_f1","u_f2","u_ptr","u_trail","u_trailT"])
    U[n] = gl.getUniformLocation(prog, n);
  const trail = { pos: new Float32Array(16), age: new Float32Array(8).fill(9), head: 0 };
  let lastTrailX = 0, lastTrailY = 0, lastTrailT = 0;
  let dragS = 0, dragV = 0, pulse = 0, pulseV = 0;

  const pushTrail = (x, y) => {
    trail.pos[trail.head * 2] = x; trail.pos[trail.head * 2 + 1] = y;
    trail.age[trail.head] = 0;
    trail.head = (trail.head + 1) % 8;
  };
  const onPointer = (x, y, vx, vy, kind) => {
    const { H } = HA.dims;
    if (kind === "release") { pulseV += Math.min(3, Math.hypot(pointer.vx, pointer.vy) / 900); return; }
    const now = performance.now();
    const moved = Math.hypot(x - lastTrailX, y - lastTrailY);
    if (kind === "drag" && (moved > 10 || now - lastTrailT > 45)) {
      pushTrail(x / H, 1 - y / H); lastTrailX = x; lastTrailY = y; lastTrailT = now;
    }
  };
  const render = (t, dt) => {
    const target = pointer.down ? 1 : 0;
    dragV += ((target - dragS) * 150 - 16 * dragV) * dt;
    dragS = clamp(dragS + dragV * dt, -0.18, 1.15);
    pulseV += (-180 * pulse - 9 * pulseV) * dt;
    pulse = clamp(pulse + pulseV * dt, -0.6, 0.9);
    for (let i = 0; i < 8; i++) trail.age[i] += dt;
    const foci = HA.fociAt(t, HA.FOCI, null);
    const px = pointer.x > -999 ? pointer.x / HA.dims.H : 0.5 * HA.dims.ASPECT;
    const py = pointer.x > -999 ? 1 - pointer.y / HA.dims.H : -0.5;
    gl.viewport(0, 0, canvas.width, canvas.height);
    gl.uniform2f(U.u_res, canvas.width, canvas.height);
    gl.uniform1f(U.u_time, t);
    gl.uniform1f(U.u_cell, 10 * DPR);
    gl.uniform1f(U.u_ribbon, RIBBON);
    gl.uniform4f(U.u_f0, foci[0].x, foci[0].y, foci[0].rx, foci[0].ry);
    gl.uniform4f(U.u_f1, foci[1].x, foci[1].y, foci[1].rx, foci[1].ry);
    gl.uniform4f(U.u_f2, foci[2].x, foci[2].y, foci[2].rx, foci[2].ry);
    gl.uniform4f(U.u_ptr, px, py, dragS, pulse);
    gl.uniform2fv(U.u_trail, trail.pos);
    gl.uniform1fv(U.u_trailT, trail.age);
    gl.drawArrays(gl.TRIANGLES, 0, 3);
  };
  return { render, onPointer };
};

/* ---------- boot helper: rAF loop + caption + badge ---------- */
HA.chrome = ({ name, body, hint, file }) => {
  document.title = `Halftone Aurora — ${name}`;
  const cap = document.getElementById("cap");
  cap.innerHTML = `<b>${name}</b> — ${body}<span class="hint">${hint}</span><span class="badge" id="rmBadge">Reduced motion · static frame</span>`;
  const badge = document.getElementById("rmBadge");
  const apply = () => { badge.style.display = HA.motionOff() ? "inline-block" : "none"; };
  apply();
  HA.onMotionChange(apply);
  document.getElementById("fileName").textContent = file;
};

HA.start = step => {
  let raf = 0;
  const t0 = performance.now() / 1000;
  let last = 0;
  const tick = now => {
    const t = HA.motionOff() ? 4.2 : now / 1000 - t0;
    const dt = Math.min(0.033, (now - last) / 1000 || 0.016); last = now;
    step(t, dt);
    if (!HA.motionOff()) raf = requestAnimationFrame(tick);
  };
  const go = () => { cancelAnimationFrame(raf); last = 0; raf = requestAnimationFrame(tick); };
  HA.onMotionChange(go);
  go();
  return { restart: go };
};

window.HA = HA;
})();
