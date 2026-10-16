# Halftone Wallpaper — Windows 11

Native port of the halftone-aurora wallpaper. Same field as
`Resources/ripple.html`, rendered on the GPU with **Direct3D 11** instead of
Canvas 2D — one fullscreen triangle, everything else is texture/buffer uploads.

## Build

Requires **Visual Studio 2022** (Community is fine) with the
*Desktop development with C++* workload, and the Windows 10/11 SDK.

```bat
cd windows
build.cmd        →  HalftoneWallpaper.exe
```

## Run

| Action | Command |
|---|---|
| Live wallpaper | `HalftoneWallpaper.exe` |
| Autostart at login | `HalftoneWallpaper.exe --install` |
| Remove autostart | `HalftoneWallpaper.exe --uninstall` |
| Frozen frame (~0 GPU/CPU) | `HalftoneWallpaper.exe --still` or tray → *Freeze field* |
| Draggable field (covers icons) | `HalftoneWallpaper.exe --interactive` (Esc quits) |
| No pointer glow | `HalftoneWallpaper.exe --no-pointer` |
| Quit | tray icon → **Quit** |

The window sits in `WorkerW` behind `SHELLDLL_DefView` — below the desktop
icons, above the static wallpaper (same level as macOS `.desktopWindow`).
Desktop icons, right-click menus and Alt-Tab are unaffected.

## Behavior parity with the macOS app

- **Battery-aware**: unplugging AC freezes the field (`PBT_APMPOWERSTATUSCHANGE`
  + `GetSystemPowerStatus`); replugging resumes.
- **Display-off**: freezes while the console display is off
  (`GUID_CONSOLE_DISPLAY_STATE`), resumes on wake.
- **Pointer warp without focus**: a ~30 Hz `GetCursorPos` feed drives the same
  impulse/spring math as the JS `__feed` (the window never sees the cursor).
- **Ultrawide**: aspect ≥ 2.6 switches to the 5-lobe `FOCI_WIDE` set.
- **Explorer restarts**: if Explorer tears down `WorkerW` (display change,
  taskbar rebuild), the view window is destroyed and re-embedded within ~500 ms;
  a persistent hidden message window keeps the tray icon + power/display
  notifications across the swap. Device loss on `Present` exits the app (the
  Run-key install restarts it on next login).
- One ~1100-line file, no dependencies beyond MSVC + the Windows SDK.
  Shaders compile at startup; there is no network access, telemetry, or updater.

## How it works

```
HalftoneWallpaper.exe
└── WS_CHILD window on WorkerW  (behind SHELLDLL_DefView → under icons)
    └── D3D11 swapchain, flip model
        └── 1 fullscreen triangle
            PS ← starTex (baked R16F layer) + glowTex (14px-cell field grid)
               + grainTex (exact JS 128² tile)
               + dotA/dotB StructuredBuffers (per-dot pos/r/color/flags)
```

- **CPU per frame** (~5k dots, <1 ms): spring/impulse sim + `fieldAt()` per dot
  → packed dot buffers; `fieldAt()` on the glow grid → premultiplied texture.
- **GPU per pixel**: star|glow base → 7×7 dot SDF `srcOver` (a warped dot
  displaces ±18 px ≈ 1.5 cells, plus ~10 px of halo reach — 3×3 loses it) →
  chromatic fringe + bloom (additive) → grain multiply. Identical paint order
  to the Canvas path; verified against the live `ripple.html` frame (mean |Δ|
  3.31/255, lumCorr 0.977, diffs confined to dot-edge anti-aliasing).
- Multi-monitor: one window spans the virtual screen. Mixed per-monitor DPI is
  treated as system DPI — **untested**.
- Mixed refresh rates composite at the fastest display's cadence.

## Status

Compiles and links clean — verified via `zig c++ -target x86_64-windows-gnu`
(recipe at the top of `build.cmd`) producing `HalftoneWallpaper.exe`
(PE32+ GUI x86-64, ~420 KB). Both shaders compile clean under Microsoft's
`dxc` (v1.9, run in a container) — `vs_5_0`/`ps_5_0`, no errors. The render
math was verified on macOS against the live `ripple.html` frame (mean |Δ|
3.31/255, lumCorr 0.977, diffs confined to dot-edge AA).

**UNVERIFIED at runtime** — the exe has never been executed on real Windows
hardware; the WorkerW embed, tray, power notifications, and swapchain path are
compile-verified only. D3DCompiler loads dynamically (`d3dcompiler_47.dll`,
in-box since Windows 10), so there are no redistributable dependencies.

## Not ported

- `HalftoneSaver.saver` equivalent (.scr screensaver) — the WorkerW window is
  the wallpaper; Windows screensavers are a separate mechanism. Open follow-up.
- Per-monitor independent DPI (one window spans the virtual screen instead).
