# HalftoneWallpaper

A live macOS desktop wallpaper: an interactive halftone dot-matrix aurora
(magenta / violet / royal blue) rendered behind the desktop icons — the
"Ripple" field extracted from a Canvas 2D mockup.

## How it works

A borderless `NSWindow` at `.desktopWindow` level hosts a `WKWebView` that
renders `Resources/ripple.html` — a self-contained Canvas 2D field (no
network, no server). Click-through by default so Finder icons work normally;
a timer feeds the global cursor position into the page so the field still
warps and glows under the pointer. One window per display, reconciled on
display-set changes.

## Build

    make            # swiftc + hand-assembled bundle + ad-hoc codesign
    make run        # or: open HalftoneWallpaper.app

First launch on a fresh account may need right-click → Open once
(ad-hoc signed).

## Run

- Launch: `open HalftoneWallpaper.app` (or `make run`)
- Quit: `pkill -x HalftoneWallpaper` or the ◐ menu-bar item → Quit
- Flags (run the binary directly, e.g. `HalftoneWallpaper.app/Contents/MacOS/HalftoneWallpaper --interactive`):
  - `--interactive` — accept real mouse events (desktop icons get covered)
  - `--no-pointer` — disable the synthetic cursor feed

## Layout

- `Sources/main.swift` — the whole app (~150 lines)
- `Resources/ripple.html` — standalone field, the shipped artifact
- `parts/` — the pieces `ripple.html` was assembled from (`_head.html` +
  `_field_extract.js` + `_driver.js`); keep in sync if you edit the field
- `Info.plist` — `LSUIElement` agent app, Retina-capable

## Cost

~2.7% CPU total at 60 fps on an M-series MacBook Pro (WebContent ~0.9%,
WebKit.GPU ~1.8%). Not tested: multi-display layouts beyond code paths,
`--interactive`, Gatekeeper on another account.

## Origin

Field math adapted from `.design/halftone-aurora.html` in the ohmygui repo.
This repo is the app's independent home.
