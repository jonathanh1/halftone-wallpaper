import ScreenSaver
import WebKit

/// macOS ScreenSaverView wrapper: hosts the same ripple.html in a WKWebView.
/// The page drives its own rAF, so no animateOneFrame timer is needed.
/// Preview tiles render the frozen frame (?motion=off) so System Settings
/// doesn't spin WebContent at 60fps for a thumbnail.
@objc(HalftoneSaverView)
final class HalftoneSaverView: ScreenSaverView {

	override init?(frame: NSRect, isPreview: Bool) {
		super.init(frame: frame, isPreview: isPreview)
		let web = WKWebView(frame: bounds)
		web.autoresizingMask = [.width, .height]
		addSubview(web)
		guard let url = Bundle(for: HalftoneSaverView.self).url(forResource: "ripple", withExtension: "html") else { return }
		let pageURL = isPreview
			? url.appending(queryItems: [URLQueryItem(name: "motion", value: "off")])
			: url
		web.loadFileURL(pageURL, allowingReadAccessTo: url.deletingLastPathComponent())
	}

	required init?(coder: NSCoder) {
		super.init(coder: coder)
	}

	override var hasConfigureSheet: Bool { false }
	override var configureSheet: NSWindow? { nil }
}
