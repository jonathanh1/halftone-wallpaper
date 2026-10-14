
/* ---------- wallpaper driver (Ripple only, pinned variant) ---------- */
// Extracted for HalftoneWallpaper.app: same opts as halftone-aurora.html's ripple
// branch { spacing: 11, rMax: 4.8, set: FOCI, warp: true, glow: true, under: 0.22 }.
const cv = document.getElementById("cv");
const reducedMQ = matchMedia("(prefers-reduced-motion: reduce)");
// `?motion=full` forces animation, `?motion=off` forces the static frame. A wallpaper
// should move: absent a param, default to "full" — unless the host injected
// `window.__WP_MOTION` (e.g. the --still flag). matchMedia still authoritative via ?motion=off.
let motionMode = new URLSearchParams(location.search).get("motion") ?? window.__WP_MOTION ?? "full";
const motionOff = () => motionMode === "full" ? false : motionMode === "off" || reducedMQ.matches;
// Host-driven live override: --still, battery, or future controls re-evaluate the loop.
window.__setMotion = m => { motionMode = m; if (r2d) loop(); };

let r2d = null;
let raf = 0, last = 0;
const t0 = performance.now() / 1000;

const OPTS = { spacing: 11, rMax: 4.8, set: FOCI, warp: true, glow: true, shift: null, freeze: false, under: 0.22 };
function loop() {
  cancelAnimationFrame(raf);
  const step = now => {
    const t = motionOff() ? 4.2 : now / 1000 - t0;
    const dt = Math.min(0.033, (now - last) / 1000 || 0.016); last = now;
    OPTS.freeze = motionOff();
    r2d.render(t, dt, OPTS);
    if (!motionOff()) raf = requestAnimationFrame(step);
  };
  raf = requestAnimationFrame(step);
}

function resize() {
  W = innerWidth; H = innerHeight; ASPECT = W / H;
  // ≥2.6 covers 32:9 and 32:10 ultrawides (e.g. 5120×1440 Odyssey) while leaving
  // 21:9 and narrower on the 3-lobe set.
  OPTS.set = ASPECT >= 2.6 ? FOCI_WIDE : FOCI;
  const d = Math.min(window.devicePixelRatio || 1, 2);
  cv.width = Math.round(W * d); cv.height = Math.round(H * d);
  if (r2d) r2d.render._sp = 0;
  // Resizing clears the canvas; if we're showing a static frame there is no next tick to repaint.
  if (r2d) loop();
}
addEventListener("resize", resize);
reducedMQ.addEventListener?.("change", () => { if (r2d) loop(); });

// Real pointer path — used when the hosting window accepts events (--interactive).
trackPointer(cv, (x, y, vx, vy, kind) => r2d.impulseQueue(x, y, vx, vy, kind));

/* Synthetic pointer feed for the click-through window: the host watches the global
   cursor (NSEvent.mouseLocation) and injects CSS-pixel positions so the field still
   warps and glows under the pointer even though the window never receives events. */
window.__feed = function (x, y) {
  const now = performance.now();
  const dt = Math.max(8, now - pointer.pt) / 1000;
  if (pointer.pt) {
    pointer.vx = clamp((x - pointer.px) / dt, -2600, 2600);
    pointer.vy = clamp((y - pointer.py) / dt, -2600, 2600);
  }
  pointer.px = pointer.x = x; pointer.py = pointer.y = y; pointer.pt = now;
  if (r2d) r2d.impulseQueue(x, y, pointer.vx, pointer.vy, "hover");
};
window.__park = parkPointer;

resize();
r2d = make2D(cv);
loop();
