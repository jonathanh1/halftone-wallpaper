import Cocoa
import WebKit

// Borderless desktop-level window hosting a WKWebView that renders the Ripple
// halftone-aurora field. Click-through by default; desktop icons and Finder
// interaction are unaffected. `?` flags: --interactive (accept real mouse events),
// --no-pointer (disable the synthetic global-cursor feed).
final class DeskWindow: NSWindow {
    override var canBecomeKey: Bool { false }
    override var canBecomeMain: Bool { false }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    private var windows: [CGDirectDisplayID: DeskWindow] = [:]
    private var views: [CGDirectDisplayID: WKWebView] = [:]
    private let interactive = ProcessInfo.processInfo.arguments.contains("--interactive")
    private let pointerFeed = !ProcessInfo.processInfo.arguments.contains("--no-pointer")
    // --still: freeze the field on one frame — ~0% CPU, e.g. for battery life.
    private let still = ProcessInfo.processInfo.arguments.contains("--still")
    private var feedTarget: CGDirectDisplayID? = nil
    private var feedTimer: Timer?
    private var statusItem: NSStatusItem?

    func applicationDidFinishLaunching(_ n: Notification) {
        NSApp.setActivationPolicy(.accessory) // belt & suspenders on top of LSUIElement
        reconcileWindows()
        installStatusItem()

        NotificationCenter.default.addObserver(
            self, selector: #selector(screenParamsChanged),
            name: NSApplication.didChangeScreenParametersNotification, object: nil)
        NotificationCenter.default.addObserver(
            self, selector: #selector(windowScreenChanged),
            name: NSWindow.didChangeScreenNotification, object: nil)
        NotificationCenter.default.addObserver(
            self, selector: #selector(backingChanged),
            name: NSWindow.didChangeBackingPropertiesNotification, object: nil)

        if pointerFeed {
            // The window is click-through, so it never sees the cursor. Poll the global
            // position and inject it into the page so the field warps/glows anyway.
            feedTimer = Timer.scheduledTimer(withTimeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
                self?.feedPointer()
            }
        }
    }

    private func screenID(_ s: NSScreen) -> CGDirectDisplayID {
        (s.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? CGDirectDisplayID) ?? 0
    }

    // One window per screen; reconcile with the current display set.
    private func reconcileWindows() {
        var seen = Set<CGDirectDisplayID>()
        for screen in NSScreen.screens {
            let id = screenID(screen)
            seen.insert(id)
            if let w = windows[id] {
                if w.frame != screen.frame { w.setFrame(screen.frame, display: false) }
            } else {
                let w = makeWindow(screen: screen)
                windows[id] = w
            }
        }
        for (id, w) in windows where !seen.contains(id) {
            w.orderOut(nil)
            windows.removeValue(forKey: id)
            views.removeValue(forKey: id)
        }
    }

    private func makeWindow(screen: NSScreen) -> DeskWindow {
        let w = DeskWindow(contentRect: screen.frame, styleMask: .borderless,
                           backing: .buffered, defer: false)
        // Below desktop icons (kCGDesktopIconWindowLevel = desktop + 1), above the
        // desktop picture. Stationary + canJoinAllSpaces keeps it out of Mission
        // Control and pinned under the icons on every space.
        w.level = NSWindow.Level(rawValue: Int(CGWindowLevelForKey(.desktopWindow)))
        w.collectionBehavior = [.canJoinAllSpaces, .stationary, .ignoresCycle]
        w.isOpaque = true
        w.backgroundColor = .black
        w.hasShadow = false
        w.ignoresMouseEvents = !interactive
        w.isReleasedWhenClosed = false

        let webView = WKWebView(frame: w.contentView!.bounds)
        webView.autoresizingMask = [.width, .height]
        w.contentView = webView
        let url = Bundle.main.url(forResource: "ripple", withExtension: "html")!
        // --still freezes the field on one frame; loadFileURL keeps the query in location.search.
        let pageURL = still ? url.appending(queryItems: [URLQueryItem(name: "motion", value: "off")]) : url
        webView.loadFileURL(pageURL, allowingReadAccessTo: url.deletingLastPathComponent())

        w.orderFrontRegardless()
        views[screenID(screen)] = webView
        return w
    }

    private func installStatusItem() {
        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        if let b = item.button {
            b.image = NSImage(systemSymbolName: "circle.hexagongrid", accessibilityDescription: "Halftone Wallpaper")
            if b.image == nil { b.title = "◐" }
        }
        let menu = NSMenu()
        let quit = NSMenuItem(title: "Quit HalftoneWallpaper", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        quit.target = NSApp
        menu.addItem(quit)
        item.menu = menu
        statusItem = item
    }

    // MARK: pointer feed
    private func feedPointer() {
        let loc = NSEvent.mouseLocation // global screen coords, bottom-left origin
        var hit: (CGDirectDisplayID, NSScreen)?
        for s in NSScreen.screens where s.frame.contains(loc) { hit = (screenID(s), s); break }
        guard let (id, screen) = hit, let web = views[id] else {
            park()
            return
        }
        if feedTarget != id { park(); feedTarget = id }
        let x = loc.x - screen.frame.origin.x
        let y = screen.frame.height - (loc.y - screen.frame.origin.y) // flip to CSS top-down
        let js = String(format: "window.__feed && __feed(%.1f,%.1f);", x, y)
        web.evaluateJavaScript(js, completionHandler: nil)
    }

    private func park() {
        guard let id = feedTarget, let web = views[id] else { feedTarget = nil; return }
        web.evaluateJavaScript("window.__park && __park();", completionHandler: nil)
        feedTarget = nil
    }

    // MARK: display-change handling
    @objc private func screenParamsChanged() { reconcileWindows() }

    @objc private func windowScreenChanged(_ n: Notification) {
        guard let w = n.object as? DeskWindow, let s = w.screen else { return }
        if w.frame != s.frame { w.setFrame(s.frame, display: true) }
    }

    @objc private func backingChanged(_ n: Notification) {
        // DPR changed without a resize (e.g. moved to a non-Retina panel): re-fit canvases.
        for web in views.values {
            web.evaluateJavaScript("dispatchEvent(new Event('resize'));", completionHandler: nil)
        }
    }
}

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.run()
