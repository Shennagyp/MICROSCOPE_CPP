import AppKit
import SwiftUI

struct NativeWorkspaceDockResizeDivider: NSViewRepresentable {
    let dockWidth: CGFloat
    let maximumWidth: CGFloat
    let onResize: (CGFloat) -> Void

    func makeNSView(context: Context) -> NativeWorkspaceDockResizeDividerView {
        NativeWorkspaceDockResizeDividerView(
            dockWidth: dockWidth,
            maximumWidth: maximumWidth,
            onResize: onResize
        )
    }

    func updateNSView(
        _ nsView: NativeWorkspaceDockResizeDividerView,
        context: Context
    ) {
        nsView.update(
            dockWidth: dockWidth,
            maximumWidth: maximumWidth,
            onResize: onResize
        )
    }
}

@MainActor
final class NativeWorkspaceDockResizeDividerView: NSView {
    private var dockWidth: CGFloat
    private var maximumWidth: CGFloat
    private var onResize: ((CGFloat) -> Void)?

    private var dragStartWindowX: CGFloat?
    private var dragStartWidth: CGFloat?
    private var isHovering = false
    private var isDragging = false
    private var resizeTrackingArea: NSTrackingArea?

    init(
        dockWidth: CGFloat,
        maximumWidth: CGFloat,
        onResize: @escaping (CGFloat) -> Void
    ) {
        self.dockWidth = dockWidth
        self.maximumWidth = maximumWidth
        self.onResize = onResize
        super.init(frame: .zero)
        wantsLayer = true
        layer?.backgroundColor = OptikaPalette.nsClear.cgColor
    }

    required init?(coder: NSCoder) {
        fatalError("NativeWorkspaceDockResizeDividerView does not support NSCoder initialization")
    }

    override var mouseDownCanMoveWindow: Bool { false }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()

        if let resizeTrackingArea = resizeTrackingArea {
            removeTrackingArea(resizeTrackingArea)
        }

        let area = NSTrackingArea(
            rect: .zero,
            options: [.mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect],
            owner: self,
            userInfo: nil
        )
        addTrackingArea(area)
        resizeTrackingArea = area
    }

    override func resetCursorRects() {
        addCursorRect(bounds, cursor: NSCursor.resizeLeftRight)
    }

    override func mouseEntered(with event: NSEvent) {
        isHovering = true
        needsDisplay = true
    }

    override func mouseExited(with event: NSEvent) {
        isHovering = false
        needsDisplay = true
    }

    override func mouseDown(with event: NSEvent) {
        dragStartWindowX = event.locationInWindow.x
        dragStartWidth = dockWidth
        isDragging = true
        needsDisplay = true
    }

    override func mouseDragged(with event: NSEvent) {
        guard let dragStartWindowX = dragStartWindowX,
              let dragStartWidth = dragStartWidth else {
            return
        }

        let horizontalDelta = event.locationInWindow.x - dragStartWindowX
        let upperBound = max(OptikaLayout.workspaceDockMinimumWidth, maximumWidth)
        let nextWidth = min(max(dragStartWidth - horizontalDelta, OptikaLayout.workspaceDockMinimumWidth), upperBound)
        onResize?(nextWidth)
    }

    override func mouseUp(with event: NSEvent) {
        mouseDragged(with: event)
        dragStartWindowX = nil
        dragStartWidth = nil
        isDragging = false
        needsDisplay = true
    }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)

        let emphasized = isHovering || isDragging
        let lineWidth: CGFloat = emphasized ? 2 : 1
        let lineRect = CGRect(
            x: (bounds.width - lineWidth) / 2,
            y: 5,
            width: lineWidth,
            height: max(0, bounds.height - 10)
        )

        (emphasized ? OptikaPalette.nsAccent : OptikaPalette.nsSeparator).setFill()
        NSBezierPath(
            roundedRect: lineRect,
            xRadius: lineWidth / 2,
            yRadius: lineWidth / 2
        ).fill()
    }

    func update(
        dockWidth: CGFloat,
        maximumWidth: CGFloat,
        onResize: @escaping (CGFloat) -> Void
    ) {
        self.dockWidth = dockWidth
        self.maximumWidth = maximumWidth
        self.onResize = onResize
    }
}

