import AppKit
import SwiftUI

/// One AppKit consumer owns the bridge's single latest-frame mailbox and
/// routes frames to the selected slide pane. Each pane retains only its own
/// most recently presented CGImage; SwiftUI never observes individual frames.
struct NativeDualPreviewSurface: NSViewRepresentable {
    let bridge: AmscopeNativeCameraBridge
    let activeSlide: NativeSlideID
    let onActiveSlideChanged: (NativeSlideID) -> Void

    func makeNSView(context: Context) -> NativeDualPreviewNSView {
        let view = NativeDualPreviewNSView(frame: .zero)
        view.attach(
            to: bridge,
            activeSlide: activeSlide,
            onActiveSlideChanged: onActiveSlideChanged
        )
        return view
    }

    func updateNSView(_ nsView: NativeDualPreviewNSView, context: Context) {
        nsView.attach(
            to: bridge,
            activeSlide: activeSlide,
            onActiveSlideChanged: onActiveSlideChanged
        )
    }
}

final class NativeDualPreviewNSView: NSView, NSSplitViewDelegate {
    private let splitView = NSSplitView()
    private let slideOnePane = NativeSlidePreviewPane(slide: .slide1)
    private let slideTwoPane = NativeSlidePreviewPane(slide: .slide2)
    private weak var bridge: AmscopeNativeCameraBridge?

    private var activeSlide: NativeSlideID = .slide1
    private var maximizedSlide: NativeSlideID?
    private var previousDividerFraction: CGFloat = 0.5
    private var didSetInitialSplitPosition = false
    private var onActiveSlideChanged: ((NativeSlideID) -> Void)?
    private let minimumPaneWidth: CGFloat = 240
    private var didConfigureSubviews = false

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        configureSubviews()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        configureSubviews()
    }

    private func configureSubviews() {
        guard !didConfigureSubviews else { return }
        didConfigureSubviews = true

        splitView.isVertical = true
        splitView.dividerStyle = .thin
        splitView.delegate = self
        splitView.addArrangedSubview(slideOnePane)
        splitView.addArrangedSubview(slideTwoPane)
        splitView.frame = bounds
        splitView.autoresizingMask = [.width, .height]
        addSubview(splitView)

        slideOnePane.onSelectLiveTarget = { [weak self] in
            self?.selectActiveSlide(.slide1)
        }
        slideTwoPane.onSelectLiveTarget = { [weak self] in
            self?.selectActiveSlide(.slide2)
        }
        slideOnePane.onToggleMaximize = { [weak self] in
            self?.toggleMaximizedSlide(.slide1)
        }
        slideTwoPane.onToggleMaximize = { [weak self] in
            self?.toggleMaximizedSlide(.slide2)
        }
        updatePaneStates()
    }

    override func layout() {
        super.layout()
        splitView.frame = bounds
        if !didSetInitialSplitPosition && splitView.bounds.width > splitView.dividerThickness {
            splitView.setPosition(splitView.bounds.width / 2, ofDividerAt: 0)
            didSetInitialSplitPosition = true
        } else if maximizedSlide != nil {
            applyMaximizedPosition()
        }
    }

    func attach(
        to bridge: AmscopeNativeCameraBridge,
        activeSlide: NativeSlideID,
        onActiveSlideChanged: @escaping (NativeSlideID) -> Void
    ) {
        if self.bridge !== bridge {
            self.bridge = bridge
            bridge.onFrameAvailable = { [weak self] in
                MainActor.assumeIsolated {
                    self?.consumeLatestFrame()
                }
            }
        }

        self.onActiveSlideChanged = onActiveSlideChanged
        setActiveSlide(activeSlide)

        // Recover a frame if the view was recreated while a notification
        // was already pending. This remains the single mailbox consumer.
        consumeLatestFrame()
    }

    private func selectActiveSlide(_ slide: NativeSlideID) {
        setActiveSlide(slide)
        onActiveSlideChanged?(slide)
    }

    private func setActiveSlide(_ slide: NativeSlideID) {
        guard activeSlide != slide else { return }
        activeSlide = slide
        updatePaneStates()
    }

    private func updatePaneStates() {
        slideOnePane.updatePresentationState(
            isActiveTarget: activeSlide == .slide1,
            isMaximized: maximizedSlide == .slide1
        )
        slideTwoPane.updatePresentationState(
            isActiveTarget: activeSlide == .slide2,
            isMaximized: maximizedSlide == .slide2
        )
    }

    private func toggleMaximizedSlide(_ slide: NativeSlideID) {
        if maximizedSlide == slide {
            maximizedSlide = nil
            let availableWidth = max(1, splitView.bounds.width - splitView.dividerThickness)
            splitView.setPosition(
                availableWidth * previousDividerFraction,
                ofDividerAt: 0
            )
        } else {
            if maximizedSlide == nil {
                let availableWidth = max(
                    1,
                    splitView.bounds.width - splitView.dividerThickness
                )
                previousDividerFraction = min(
                    max(slideOnePane.frame.width / availableWidth, 0.1),
                    0.9
                )
            }
            maximizedSlide = slide
            applyMaximizedPosition()
        }
        updatePaneStates()
    }

    private func applyMaximizedPosition() {
        guard splitView.bounds.width > splitView.dividerThickness else { return }
        let position: CGFloat
        switch maximizedSlide {
        case .slide1:
            position = splitView.bounds.width - splitView.dividerThickness
        case .slide2:
            position = 0
        case .none:
            return
        }
        splitView.setPosition(position, ofDividerAt: 0)
    }

    func splitView(
        _ splitView: NSSplitView,
        constrainMinCoordinate proposedMinimumPosition: CGFloat,
        ofSubviewAt dividerIndex: Int
    ) -> CGFloat {
        switch maximizedSlide {
        case .slide1:
            return splitView.bounds.width - splitView.dividerThickness
        case .slide2:
            return 0
        case .none:
            return minimumPaneWidth
        }
    }

    func splitView(
        _ splitView: NSSplitView,
        constrainMaxCoordinate proposedMaximumPosition: CGFloat,
        ofSubviewAt dividerIndex: Int
    ) -> CGFloat {
        switch maximizedSlide {
        case .slide1:
            return splitView.bounds.width - splitView.dividerThickness
        case .slide2:
            return 0
        case .none:
            return max(
                minimumPaneWidth,
                splitView.bounds.width - splitView.dividerThickness - minimumPaneWidth
            )
        }
    }

    private func consumeLatestFrame() {
        guard let bridge = bridge else { return }
        slideOnePane.updateAcquisitionCounters(
            pulled: bridge.framesPulled,
            dropped: bridge.droppedFrames
        )
        slideTwoPane.updateAcquisitionCounters(
            pulled: bridge.framesPulled,
            dropped: bridge.droppedFrames
        )
        guard let lease = bridge.takeLatestFrame(),
              let image = lease.createCGImage() else {
            return
        }

        switch activeSlide {
        case .slide1:
            slideOnePane.display(image)
        case .slide2:
            slideTwoPane.display(image)
        }

        updatePaneStates()
    }
}

private final class NativeSlidePreviewPane: NSView {
    private let slide: NativeSlideID
    private let imageView: NativeSlideImageView
    private let titleLabel = NSTextField(labelWithString: "")
    private let statusLabel = NSTextField(labelWithString: "STANDBY")
    private let frameStatisticsLabel = NSTextField(labelWithString: "FRAMES 0 · VIEWED 0 · DROPS 0")
    private let selectButton = NSButton(title: "Set Live Target", target: nil, action: nil)
    private let maximizeButton = NSButton(title: "Maximize", target: nil, action: nil)
    private let headerStack = NSStackView()

    var onSelectLiveTarget: (() -> Void)?
    var onToggleMaximize: (() -> Void)?

    private var pulledFrameCount: UInt64 = 0
    private var droppedFrameCount: UInt64 = 0
    private var paneFrameCount: UInt64 = 0

    init(slide: NativeSlideID) {
        self.slide = slide
        self.imageView = NativeSlideImageView(slideTitle: slide.title)
        super.init(frame: .zero)
        configureSubviews()
    }

    required init?(coder: NSCoder) {
        fatalError("NativeSlidePreviewPane does not support NSCoder initialization")
    }

    private func configureSubviews() {
        titleLabel.stringValue = slide.title.uppercased()
        titleLabel.font = NSFont.systemFont(ofSize: 12, weight: .semibold)
        wantsLayer = true
        layer?.cornerRadius = 4

        statusLabel.font = NSFont.systemFont(ofSize: 10, weight: .semibold)
        statusLabel.textColor = NSColor.secondaryLabelColor
        frameStatisticsLabel.font = NSFont.monospacedDigitSystemFont(ofSize: 8, weight: .regular)
        frameStatisticsLabel.textColor = NSColor.secondaryLabelColor
        frameStatisticsLabel.lineBreakMode = .byTruncatingTail
        frameStatisticsLabel.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

        selectButton.bezelStyle = .rounded
        selectButton.controlSize = .small
        selectButton.target = self
        selectButton.action = #selector(selectLiveTargetAction(_:))

        maximizeButton.bezelStyle = .rounded
        maximizeButton.controlSize = .small
        maximizeButton.target = self
        maximizeButton.action = #selector(toggleMaximizeAction(_:))

        headerStack.orientation = .horizontal
        headerStack.alignment = .centerY
        headerStack.distribution = .fill
        headerStack.spacing = 8
        headerStack.translatesAutoresizingMaskIntoConstraints = false
        headerStack.addArrangedSubview(titleLabel)
        headerStack.addArrangedSubview(statusLabel)
        headerStack.addArrangedSubview(frameStatisticsLabel)
        let spacer = NSView()
        spacer.setContentHuggingPriority(.defaultLow, for: .horizontal)
        headerStack.addArrangedSubview(spacer)
        headerStack.addArrangedSubview(selectButton)
        headerStack.addArrangedSubview(maximizeButton)

        // The image view is always exactly the size of this existing pane
        // viewport. Only the CGImage's drawing rectangle changes.
        imageView.translatesAutoresizingMaskIntoConstraints = false
        addSubview(headerStack)
        addSubview(imageView)
        NSLayoutConstraint.activate([
            headerStack.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 10),
            headerStack.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -10),
            headerStack.topAnchor.constraint(equalTo: topAnchor, constant: 6),
            headerStack.heightAnchor.constraint(equalToConstant: 28),
            imageView.leadingAnchor.constraint(equalTo: leadingAnchor),
            imageView.trailingAnchor.constraint(equalTo: trailingAnchor),
            imageView.topAnchor.constraint(equalTo: headerStack.bottomAnchor, constant: 6),
            imageView.bottomAnchor.constraint(equalTo: bottomAnchor)
        ])
    }

    func updateAcquisitionCounters(pulled: UInt64, dropped: UInt64) {
        pulledFrameCount = pulled
        droppedFrameCount = dropped
        updateFrameStatisticsLabel()
    }

    func display(_ image: CGImage) {
        paneFrameCount &+= 1
        imageView.display(image)
        updateFrameStatisticsLabel()
    }

    private func updateFrameStatisticsLabel() {
        frameStatisticsLabel.stringValue =
            "PULLED \(pulledFrameCount) · PANE \(paneFrameCount) · DROPS \(droppedFrameCount)"
        frameStatisticsLabel.toolTip =
            "PULLED: cumulative driver frames pulled. PANE: images assigned to this slide preview. DROPS: cumulative driver-reported dropped frames."
    }

    func updatePresentationState(isActiveTarget: Bool, isMaximized: Bool) {
        if isActiveTarget {
            statusLabel.stringValue = "LIVE"
            statusLabel.textColor = NSColor.systemRed
            selectButton.title = "LIVE"
            selectButton.isEnabled = false
        } else {
            statusLabel.stringValue = imageView.hasImage ? "HELD IMAGE" : "STANDBY"
            statusLabel.textColor = NSColor.secondaryLabelColor
            selectButton.title = "Switch Live"
            selectButton.isEnabled = true
        }
        selectButton.isHidden = false
        layer?.borderWidth = 1
        layer?.borderColor = isActiveTarget
            ? NSColor.systemRed.cgColor
            : NSColor.separatorColor.cgColor
        maximizeButton.title = isMaximized ? "Restore" : "Maximize"
    }

    @objc private func selectLiveTargetAction(_ sender: NSButton) {
        onSelectLiveTarget?()
    }

    @objc private func toggleMaximizeAction(_ sender: NSButton) {
        onToggleMaximize?()
    }
}

private final class NativeSlideImageView: NSView {
    private let slideTitle: String
    private var currentImage: CGImage?

    /// The automatic zoom value starts at the largest aspect-preserving scale
    /// that covers the pane without outer black bars. It never changes this
    /// NSView's frame or the dimensions of its parent pane.
    private var zoomProgress: CGFloat = 0
    private var imageOrigin: CGPoint?
    private var dragLocation: CGPoint?
    private var contextMenuAnchor: CGPoint?

    var hasImage: Bool { currentImage != nil }
    override var isFlipped: Bool { true }

    init(slideTitle: String) {
        self.slideTitle = slideTitle
        super.init(frame: .zero)
        toolTip = "Scroll or pinch to zoom · Drag to pan · Double-click toggles Fill / 1:1 · Right-click for viewing modes"
    }

    required init?(coder: NSCoder) {
        fatalError("NativeSlideImageView does not support NSCoder initialization")
    }

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool {
        true
    }

    override func setFrameSize(_ newSize: NSSize) {
        super.setFrameSize(newSize)
        if let imageOrigin {
            self.imageOrigin = clampedOrigin(imageOrigin, at: currentScale)
        }
        needsDisplay = true
    }

    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        if zoomProgress >= 0.999 {
            zoomProgress = 1
        }
        if let imageOrigin {
            self.imageOrigin = clampedOrigin(imageOrigin, at: currentScale)
        }
        needsDisplay = true
    }

    func display(_ image: CGImage) {
        let dimensionsChanged = currentImage?.width != image.width ||
            currentImage?.height != image.height
        currentImage = image

        // A new sensor mode gets a fresh automatic fill scale. Same-size
        // frames preserve the user's current zoom and pan position.
        if dimensionsChanged {
            zoomProgress = 0
            imageOrigin = nil
        } else if let imageOrigin {
            self.imageOrigin = clampedOrigin(imageOrigin, at: currentScale)
        }
        needsDisplay = true
    }

    // MARK: - Image geometry

    /// AppKit points per camera pixel for one physical display pixel.
    private var actualPixelScale: CGFloat {
        1 / max(window?.backingScaleFactor ?? 1, 1)
    }

    /// The maximum initial zoom that fills the whole pane. Using max rather
    /// than min removes letterboxing: one image dimension exactly covers the
    /// viewport and the other may be cropped. Cap at 1:1 to avoid upscaling
    /// acquired pixels in the diagnostic viewer.
    private var automaticFillScale: CGFloat {
        guard let image = currentImage,
              image.width > 0,
              image.height > 0,
              bounds.width > 0,
              bounds.height > 0 else {
            return actualPixelScale
        }

        let widthScale = bounds.width / CGFloat(image.width)
        let heightScale = bounds.height / CGFloat(image.height)
        let fillScale = max(widthScale, heightScale)
        return min(fillScale, actualPixelScale)
    }

    /// Continuous zoom from the automatic no-bars fill value to exact 1:1.
    private var currentScale: CGFloat {
        let fillScale = automaticFillScale
        guard zoomProgress > 0, actualPixelScale > fillScale else {
            return fillScale
        }
        return fillScale * pow(actualPixelScale / fillScale, zoomProgress)
    }

    private func imageSize(at scale: CGFloat) -> CGSize {
        guard let image = currentImage else { return .zero }
        return CGSize(
            width: CGFloat(image.width) * scale,
            height: CGFloat(image.height) * scale
        )
    }

    private func centeredOrigin(at scale: CGFloat) -> CGPoint {
        let size = imageSize(at: scale)
        return CGPoint(
            x: (bounds.width - size.width) / 2,
            y: (bounds.height - size.height) / 2
        )
    }

    private func clampedOrigin(_ proposed: CGPoint, at scale: CGFloat) -> CGPoint {
        let size = imageSize(at: scale)

        // If a dimension is larger than the pane, clamp it so no empty
        // margins can enter from that edge. Smaller dimensions stay centered.
        let x: CGFloat
        if size.width <= bounds.width {
            x = (bounds.width - size.width) / 2
        } else {
            x = min(0, max(bounds.width - size.width, proposed.x))
        }

        let y: CGFloat
        if size.height <= bounds.height {
            y = (bounds.height - size.height) / 2
        } else {
            y = min(0, max(bounds.height - size.height, proposed.y))
        }

        return CGPoint(x: x, y: y)
    }

    private func resolvedOrigin(at scale: CGFloat) -> CGPoint {
        clampedOrigin(imageOrigin ?? centeredOrigin(at: scale), at: scale)
    }

    // MARK: - Automatic fill and zoom

    private func setAutomaticFill() {
        zoomProgress = 0
        imageOrigin = nil
        needsDisplay = true
    }

    private func setActualPixels(anchor: CGPoint? = nil) {
        guard currentImage != nil else { return }
        zoom(
            to: 1,
            anchoredAt: anchor ?? CGPoint(x: bounds.midX, y: bounds.midY)
        )
    }

    private func toggleFillAndActualPixels() {
        if zoomProgress >= 0.999 {
            setAutomaticFill()
        } else {
            setActualPixels()
        }
    }

    private func zoom(by delta: CGFloat, anchoredAt anchor: CGPoint) {
        guard currentImage != nil,
              delta.isFinite,
              delta != 0,
              actualPixelScale > automaticFillScale else {
            return
        }

        zoom(to: min(1, max(0, zoomProgress + delta)), anchoredAt: anchor)
    }

    private func zoom(to nextProgress: CGFloat, anchoredAt anchor: CGPoint) {
        guard currentImage != nil else { return }
        let clampedProgress = min(1, max(0, nextProgress))
        guard clampedProgress != zoomProgress else { return }

        let oldScale = currentScale
        let oldOrigin = resolvedOrigin(at: oldScale)
        let imagePoint = CGPoint(
            x: (anchor.x - oldOrigin.x) / oldScale,
            y: (anchor.y - oldOrigin.y) / oldScale
        )

        zoomProgress = clampedProgress
        let nextScale = currentScale
        let proposedOrigin = CGPoint(
            x: anchor.x - imagePoint.x * nextScale,
            y: anchor.y - imagePoint.y * nextScale
        )

        if clampedProgress <= 0.0001 {
            imageOrigin = nil
        } else {
            imageOrigin = clampedOrigin(proposedOrigin, at: nextScale)
        }
        needsDisplay = true
    }

    // MARK: - Mouse and trackpad interaction

    override func scrollWheel(with event: NSEvent) {
        guard currentImage != nil else {
            super.scrollWheel(with: event)
            return
        }

        let delta = event.hasPreciseScrollingDeltas
            ? event.scrollingDeltaY * 0.008
            : event.deltaY * 0.12
        zoom(
            by: delta,
            anchoredAt: convert(event.locationInWindow, from: nil)
        )
    }

    override func magnify(with event: NSEvent) {
        guard currentImage != nil else {
            super.magnify(with: event)
            return
        }

        let magnification = max(-0.95, event.magnification)
        let delta = log(1 + magnification) / log(2) * 0.35
        zoom(
            by: delta,
            anchoredAt: convert(event.locationInWindow, from: nil)
        )
    }

    override func mouseDown(with event: NSEvent) {
        let point = convert(event.locationInWindow, from: nil)
        if event.clickCount >= 2 {
            toggleFillAndActualPixels()
            dragLocation = nil
            return
        }

        let imageSize = imageSize(at: currentScale)
        let imageIsLargerThanPane =
            imageSize.width > bounds.width || imageSize.height > bounds.height
        guard zoomProgress > 0 || imageIsLargerThanPane else {
            dragLocation = nil
            return
        }
        dragLocation = point
    }

    override func mouseDragged(with event: NSEvent) {
        guard let previousPoint = dragLocation else { return }

        let point = convert(event.locationInWindow, from: nil)
        let delta = CGPoint(
            x: point.x - previousPoint.x,
            y: point.y - previousPoint.y
        )
        let proposed = CGPoint(
            x: resolvedOrigin(at: currentScale).x + delta.x,
            y: resolvedOrigin(at: currentScale).y + delta.y
        )
        imageOrigin = clampedOrigin(proposed, at: currentScale)
        dragLocation = point
        needsDisplay = true
    }

    override func mouseUp(with event: NSEvent) {
        dragLocation = nil
    }

    override func menu(for event: NSEvent) -> NSMenu? {
        contextMenuAnchor = convert(event.locationInWindow, from: nil)

        let menu = NSMenu()
        let fillItem = NSMenuItem(
            title: "Fill Pane (No Black Margins)",
            action: #selector(selectAutomaticFill(_:)),
            keyEquivalent: ""
        )
        fillItem.target = self
        fillItem.isEnabled = currentImage != nil
        menu.addItem(fillItem)

        let actualItem = NSMenuItem(
            title: "Actual Pixels (1:1)",
            action: #selector(selectActualPixels(_:)),
            keyEquivalent: ""
        )
        actualItem.target = self
        actualItem.isEnabled = currentImage != nil
        menu.addItem(actualItem)
        return menu
    }

    @objc private func selectAutomaticFill(_ sender: Any?) {
        setAutomaticFill()
    }

    @objc private func selectActualPixels(_ sender: Any?) {
        setActualPixels(anchor: contextMenuAnchor)
    }

    // MARK: - Rendering

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)

        NSColor(calibratedWhite: 0.075, alpha: 1).setFill()
        dirtyRect.fill()

        guard let image = currentImage,
              let context = NSGraphicsContext.current?.cgContext else {
            let message = "No image stored for \(slideTitle)"
            let attributes: [NSAttributedString.Key: Any] = [
                .font: NSFont.systemFont(ofSize: 13, weight: .medium),
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

        let scale = currentScale
        let size = imageSize(at: scale)
        let targetRect = CGRect(
            origin: resolvedOrigin(at: scale),
            size: size
        )

        context.interpolationQuality =
            abs(scale - actualPixelScale) > 0.000001 ? .high : .none
        context.draw(image, in: targetRect)
    }
}
