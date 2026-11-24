import AppKit
import SwiftUI

/// The AppKit view owns display cadence and frame retention. SwiftUI only
/// constructs the view; incoming frames never become SwiftUI observable state.
struct NativePreviewSurface: NSViewRepresentable {
    let bridge: AmscopeNativeCameraBridge
    let onMetadataChanged: (String, String) -> Void

    func makeNSView(context: Context) -> NativePreviewNSView {
        let view = NativePreviewNSView()
        view.attach(to: bridge, onMetadataChanged: onMetadataChanged)
        return view
    }

    func updateNSView(_ nsView: NativePreviewNSView, context: Context) {
        nsView.attach(to: bridge, onMetadataChanged: onMetadataChanged)
        if bridge.state != .streaming {
            nsView.clearFrame()
        }
    }
}

final class NativePreviewNSView: NSView {
    private weak var bridge: AmscopeNativeCameraBridge?
    private var currentImage: CGImage?
    private var lastResolution = ""
    private var lastPixelFormat = ""
    private var onMetadataChanged: ((String, String) -> Void)?

    override var isFlipped: Bool { true }

    func attach(
        to bridge: AmscopeNativeCameraBridge,
        onMetadataChanged: @escaping (String, String) -> Void
    ) {
        self.bridge = bridge
        self.onMetadataChanged = onMetadataChanged
        bridge.onFrameAvailable = { [weak self] in
            self?.consumeLatestFrame()
        }
        // A view may be recreated while the stream is already active and the
        // mailbox's coalesced notification may belong to a previous view.
        consumeLatestFrame()
    }

    func clearFrame() {
        currentImage = nil
        needsDisplay = true
    }

    private func consumeLatestFrame() {
        guard let bridge = bridge,
              let lease = bridge.takeLatestFrame(),
              let image = lease.newCGImage() else {
            return
        }

        let resolution = "\(lease.width) × \(lease.height)"
        let pixelFormat: String
        switch lease.pixelFormat {
        case 1: pixelFormat = "RGB24"
        case 2: pixelFormat = "RAW8"
        case 3: pixelFormat = "RAW16"
        default: pixelFormat = "Unknown"
        }

        if resolution != lastResolution || pixelFormat != lastPixelFormat {
            lastResolution = resolution
            lastPixelFormat = pixelFormat
            onMetadataChanged?(resolution, pixelFormat)
        }

        // The CGDataProvider retains the immutable C++ lease; this reference
        // keeps the displayed image alive without copying its pixel payload.
        currentImage = image
        needsDisplay = true
    }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)

        NSColor(calibratedWhite: 0.075, alpha: 1).setFill()
        dirtyRect.fill()

        guard let image = currentImage,
              let context = NSGraphicsContext.current?.cgContext else {
            let message = "Select a camera and start acquisition"
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
            return
        }

        let imageSize = CGSize(width: CGFloat(image.width), height: CGFloat(image.height))
        let scale = min(
            bounds.width / imageSize.width,
            bounds.height / imageSize.height
        )
        guard scale.isFinite, scale > 0 else { return }

        let targetSize = CGSize(
            width: imageSize.width * scale,
            height: imageSize.height * scale
        )
        let targetRect = CGRect(
            x: (bounds.width - targetSize.width) / 2,
            y: (bounds.height - targetSize.height) / 2,
            width: targetSize.width,
            height: targetSize.height
        )

        context.interpolationQuality = .none
        context.draw(image, in: targetRect)
    }
}
