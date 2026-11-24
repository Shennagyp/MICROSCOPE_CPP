import AppKit
import SwiftUI

/// The native preview is deliberately an AppKit view rather than a SwiftUI
/// Image or per-frame observable property. The frame-lease renderer will be
/// integrated here; it must consume the latest C++ lease during draw and must
/// not copy pixel bytes or dispatch one SwiftUI update per acquired frame.
struct NativePreviewSurface: NSViewRepresentable {
    func makeNSView(context: Context) -> NativePreviewNSView {
        NativePreviewNSView()
    }

    func updateNSView(_ nsView: NativePreviewNSView, context: Context) {
        // High-rate frame state is intentionally not driven by SwiftUI updates.
    }
}

final class NativePreviewNSView: NSView {
    override var isFlipped: Bool { true }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)

        NSColor(calibratedWhite: 0.075, alpha: 1).setFill()
        dirtyRect.fill()

        let message = "Camera not connected"
        let attributes: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: 14, weight: .medium),
            .foregroundColor: NSColor.secondaryLabelColor
        ]
        let textSize = (message as NSString).size(withAttributes: attributes)
        let textRect = NSRect(
            x: (bounds.width - textSize.width) / 2,
            y: (bounds.height - textSize.height) / 2,
            width: textSize.width,
            height: textSize.height
        )
        (message as NSString).draw(in: textRect, withAttributes: attributes)
    }
}
