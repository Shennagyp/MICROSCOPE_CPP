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

    var displayPixelSize: (width: Int, height: Int) {
        let backingBounds = imageView.convertToBacking(imageView.bounds)
        return (
            width: Int(ceil(backingBounds.width)),
            height: Int(ceil(backingBounds.height))
        )
    }

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

    /// 0 = fit this pane; 1 = one camera pixel per physical display pixel.
    /// This state belongs to the individual slide viewer, not the split view.
    private var zoomProgress: CGFloat = 0
    private var imageOrigin: CGPoint?
    private var dragLocation: CGPoint?
    private var contextMenuAnchor: CGPoint?

    var hasImage: Bool { currentImage != nil }
    override var isFlipped: Bool { true }

    init(slideTitle: String) {
        self.slideTitle = slideTitle
        super.init(frame: .zero)
        toolTip = "Scroll or pinch to zoom · Drag to pan · Double-click toggles Fit / 1:1 · Right-click for viewing modes"
    }

    required init?(coder: NSCoder) {
        fatalError("NativeSlideImageView does not support NSCoder initialization")
    }

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool {
        true
    }

    override func setFrameSize(_ newSize: NSSize) {
        super.setFrameSize(newSize)
        if zoomProgress > 0, let imageOrigin {
            self.imageOrigin = clampedOrigin(imageOrigin, at: currentScale)
        }
        needsDisplay = true
    }

    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        // Keep Actual Pixels at exactly one physical display pixel per
        // camera pixel when moving the window between displays with
        // different backing scale factors.
        if zoomProgress >= 0.999 {
            zoomProgress = 1
            imageOrigin = centeredOrigin(at: currentScale)
        } else if zoomProgress > 0, let imageOrigin {
            self.imageOrigin = clampedOrigin(imageOrigin, at: currentScale)
        }
        needsDisplay = true
    }

    func display(_ image: CGImage) {
        // Replace the prior provider-backed image without copying or retaining
        // a frame history. The pane's independent zoom/pan state is preserved.
        currentImage = image
        if zoomProgress > 0, let imageOrigin {
            self.imageOrigin = clampedOrigin(imageOrigin, at: currentScale)
        }
        needsDisplay = true
    }

    // MARK: - Pixel geometry

    /// AppKit points per camera pixel for true 1:1 physical-pixel mapping.
    private var actualPixelScale: CGFloat {
        1 / max(window?.backingScaleFactor ?? 1, 1)
    }

    /// The largest scale that fits the complete image in this pane, capped
    /// at 1:1 so Fit never enlarges camera pixels beyond physical pixels.
    private var fitScale: CGFloat {
        guard let image = currentImage,
              image.width > 0,
              image.height > 0,
              bounds.width > 0,
              bounds.height > 0 else {
            return actualPixelScale
        }

        let widthScale = bounds.width / CGFloat(image.width)
        let heightScale = bounds.height / CGFloat(image.height)
        return max(
            0.000001,
            min(min(widthScale, heightScale), actualPixelScale)
        )
    }

    /// Logarithmic interpolation makes zoom progression feel uniform while
    /// preserving exact endpoints: Fit at 0 and Actual Pixels at 1.
    private var currentScale: CGFloat {
        let fit = fitScale
        let actual = actualPixelScale
        guard actual > fit, zoomProgress > 0 else { return fit }
        return fit * pow(actual / fit, zoomProgress)
    }

    private var displayedImageSize: CGSize {
        guard let image = currentImage else { return .zero }
        let scale = currentScale
        return CGSize(
            width: CGFloat(image.width) * scale,
            height: CGFloat(image.height) * scale
        )
    }

    private func centeredOrigin(at scale: CGFloat) -> CGPoint {
        let size: CGSize
        if let image = currentImage {
            size = CGSize(
                width: CGFloat(image.width) * scale,
                height: CGFloat(image.height) * scale
            )
        } else {
            size = .zero
        }
        return CGPoint(
            x: (bounds.width - size.width) / 2,
            y: (bounds.height - size.height) / 2
        )
    }

    private func resolvedOrigin(at scale: CGFloat) -> CGPoint {
        guard zoomProgress > 0, let imageOrigin else {
            return centeredOrigin(at: scale)
        }
        return clampedOrigin(imageOrigin, at: scale)
    }

    /// Prevents dragging the image completely away from the viewport. When
    /// an image dimension is smaller than the pane, it remains centered.
    private func clampedOrigin(_ proposed: CGPoint, at scale: CGFloat) -> CGPoint {
        guard let image = currentImage else { return .zero }
        let imageWidth = CGFloat(image.width) * scale
        let imageHeight = CGFloat(image.height) * scale

        let x: CGFloat
        if imageWidth <= bounds.width {
            x = (bounds.width - imageWidth) / 2
        } else {
            x = min(0, max(bounds.width - imageWidth, proposed.x))
        }

        let y: CGFloat
        if imageHeight <= bounds.height {
            y = (bounds.height - imageHeight) / 2
        } else {
            y = min(0, max(bounds.height - imageHeight, proposed.y))
        }

        return CGPoint(x: x, y: y)
    }

    // MARK: - Presentation modes and zoom

    private func setFitMode() {
        zoomProgress = 0
        imageOrigin = nil
        needsDisplay = true
    }

    private func setActualPixelsMode(anchor: CGPoint? = nil) {
        guard currentImage != nil else { return }
        zoom(to: 1, anchoredAt: anchor ?? CGPoint(
            x: bounds.midX,
            y: bounds.midY
        ))
    }

    private func toggleFitAndActualPixels(at point: CGPoint) {
        if zoomProgress >= 0.999 {
            setFitMode()
        } else {
            setActualPixelsMode(anchor: point)
        }
    }

    private func zoom(by progressDelta: CGFloat, anchoredAt anchor: CGPoint) {
        guard currentImage != nil,
              progressDelta.isFinite,
              progressDelta != 0 else {
            return
        }

        let oldScale = currentScale
        let oldOrigin = resolvedOrigin(at: oldScale)
        let imagePoint = CGPoint(
            x: (anchor.x - oldOrigin.x) / oldScale,
            y: (anchor.y - oldOrigin.y) / oldScale
        )
        let nextProgress = min(1, max(0, zoomProgress + progressDelta))

        if nextProgress <= 0.0001 {
            setFitMode()
            return
        }

        zoomProgress = nextProgress
        let nextScale = currentScale
        let proposedOrigin = CGPoint(
            x: anchor.x - imagePoint.x * nextScale,
            y: anchor.y - imagePoint.y * nextScale
        )
        imageOrigin = clampedOrigin(proposedOrigin, at: nextScale)
        needsDisplay = true
    }

    // MARK: - Native interaction

    override func scrollWheel(with event: NSEvent) {
        guard currentImage != nil else {
            super.scrollWheel(with: event)
            return
        }

        let delta = event.hasPreciseScrollingDeltas
            ? event.scrollingDeltaY * 0.008
            : event.deltaY * 0.12
        guard delta != 0 else { return }

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
        let progressDelta = log(1 + magnification) / log(2) * 0.35
        zoom(
            by: progressDelta,
            anchoredAt: convert(event.locationInWindow, from: nil)
        )
    }

    override func mouseDown(with event: NSEvent) {
        let location = convert(event.locationInWindow, from: nil)
        if event.clickCount >= 2 {
            toggleFitAndActualPixels(at: location)
            dragLocation = nil
            return
        }

        guard zoomProgress > 0 else {
            dragLocation = nil
            return
        }
        dragLocation = location
    }

    override func mouseDragged(with event: NSEvent) {
        guard zoomProgress > 0,
              let previousLocation = dragLocation else {
            return
        }

        let location = convert(event.locationInWindow, from: nil)
        let delta = CGPoint(
            x: location.x - previousLocation.x,
            y: location.y - previousLocation.y
        )
        let proposedOrigin = CGPoint(
            x: resolvedOrigin(at: currentScale).x + delta.x,
            y: resolvedOrigin(at: currentScale).y + delta.y
        )
        imageOrigin = clampedOrigin(proposedOrigin, at: currentScale)
        dragLocation = location
        needsDisplay = true
    }

    override func mouseUp(with event: NSEvent) {
        dragLocation = nil
    }

    override func menu(for event: NSEvent) -> NSMenu? {
        contextMenuAnchor = convert(event.locationInWindow, from: nil)

        let menu = NSMenu()
        let fitItem = NSMenuItem(
            title: "Fit Image to Pane",
            action: #selector(selectFitMode(_:)),
            keyEquivalent: ""
        )
        fitItem.target = self
        fitItem.isEnabled = currentImage != nil
        menu.addItem(fitItem)

        let actualItem = NSMenuItem(
            title: "Actual Pixels (1:1)",
            action: #selector(selectActualPixelsMode(_:)),
            keyEquivalent: ""
        )
        actualItem.target = self
        actualItem.isEnabled = currentImage != nil
        menu.addItem(actualItem)

        return menu
    }

    @objc private func selectFitMode(_ sender: Any?) {
        setFitMode()
    }

    @objc private func selectActualPixelsMode(_ sender: Any?) {
        setActualPixelsMode(anchor: contextMenuAnchor)
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
        let size = CGSize(
            width: CGFloat(image.width) * scale,
            height: CGFloat(image.height) * scale
        )
        let origin = resolvedOrigin(at: scale)
        let targetRect = CGRect(origin: origin, size: size)

        // High-quality filtering is used only while reducing the overview.
        // At native 1:1 mapping, disable interpolation to preserve pixel edges.
        context.interpolationQuality =
            scale < actualPixelScale * 0.999 ? .high : .none
        context.draw(image, in: targetRect)
    }
}
