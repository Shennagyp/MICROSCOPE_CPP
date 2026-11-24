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
    private let imageScrollView = NativeSlideImageScrollView()
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

    override func layout() {
        super.layout()
        imageView.updateDocumentFrame(
            for: imageScrollView.contentView.bounds.size
        )
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

        imageScrollView.translatesAutoresizingMaskIntoConstraints = false
        imageScrollView.documentView = imageView
        addSubview(headerStack)
        addSubview(imageScrollView)
        NSLayoutConstraint.activate([
            headerStack.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 10),
            headerStack.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -10),
            headerStack.topAnchor.constraint(equalTo: topAnchor, constant: 6),
            headerStack.heightAnchor.constraint(equalToConstant: 28),
            imageScrollView.leadingAnchor.constraint(equalTo: leadingAnchor),
            imageScrollView.trailingAnchor.constraint(equalTo: trailingAnchor),
            imageScrollView.topAnchor.constraint(equalTo: headerStack.bottomAnchor, constant: 6),
            imageScrollView.bottomAnchor.constraint(equalTo: bottomAnchor)
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

private final class NativeSlideImageScrollView: NSScrollView {
    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        configure()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        configure()
    }

    private func configure() {
        borderType = .noBorder
        hasHorizontalScroller = false
        hasVerticalScroller = false
        autohidesScrollers = true
        drawsBackground = true
        backgroundColor = NSColor(calibratedWhite: 0.075, alpha: 1)
        horizontalScrollElasticity = .none
        verticalScrollElasticity = .none
        contentView.drawsBackground = true
        contentView.backgroundColor = backgroundColor
    }

    override func scrollWheel(with event: NSEvent) {
        guard let imageView = documentView as? NativeSlideImageView else {
            super.scrollWheel(with: event)
            return
        }

        let delta = event.hasPreciseScrollingDeltas
            ? event.scrollingDeltaY * 0.008
            : event.deltaY * 0.12
        guard delta != 0 else { return }

        let anchor = imageView.viewportPoint(fromWindowPoint: event.locationInWindow)
        imageView.zoom(by: delta, anchoredAt: anchor)
    }

    override func magnify(with event: NSEvent) {
        guard let imageView = documentView as? NativeSlideImageView else {
            super.magnify(with: event)
            return
        }

        let magnification = max(-0.95, event.magnification)
        let delta = log(1 + magnification) / log(2) * 0.35
        let anchor = imageView.viewportPoint(fromWindowPoint: event.locationInWindow)
        imageView.zoom(by: delta, anchoredAt: anchor)
    }
}

private final class NativeSlideImageView: NSView {
    private let slideTitle: String
    private var currentImage: CGImage?

    /// 0 = fit this pane; 1 = one camera pixel per physical display pixel.
    /// Each slide owns its own zoom and scroll position.
    private var zoomProgress: CGFloat = 0
    private var dragViewportLocation: CGPoint?
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

    private var imageScrollView: NativeSlideImageScrollView? {
        enclosingScrollView as? NativeSlideImageScrollView
    }

    private var clipView: NSClipView? {
        imageScrollView?.contentView
    }

    private var viewportSize: CGSize {
        clipView?.bounds.size ?? .zero
    }

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool {
        true
    }

    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        // Recompute the document/container size when the window moves between
        // displays. Actual Pixels always remains one physical display pixel
        // per camera pixel.
        updateDocumentFrame(for: viewportSize)
        needsDisplay = true
    }

    func display(_ image: CGImage) {
        // Replace the previous provider-backed image. Do not copy the pixels
        // or retain a frame history. Keep this pane's independent zoom state.
        currentImage = image
        updateDocumentFrame(for: viewportSize)
        needsDisplay = true
    }

    // MARK: - Image and container geometry

    /// AppKit points per camera pixel for one physical display pixel.
    private var actualPixelScale: CGFloat {
        1 / max(window?.backingScaleFactor ?? 1, 1)
    }

    /// Fit the complete image inside this pane, without enlarging source
    /// pixels beyond the display's native physical-pixel scale.
    private var fitScale: CGFloat {
        guard let image = currentImage,
              image.width > 0,
              image.height > 0,
              viewportSize.width > 0,
              viewportSize.height > 0 else {
            return actualPixelScale
        }

        let widthScale = viewportSize.width / CGFloat(image.width)
        let heightScale = viewportSize.height / CGFloat(image.height)
        return max(
            0.000001,
            min(min(widthScale, heightScale), actualPixelScale)
        )
    }

    /// Continuous logarithmic progression with exact Fit and Actual Pixels
    /// endpoints.
    private var currentScale: CGFloat {
        let fit = fitScale
        let actual = actualPixelScale
        guard actual > fit, zoomProgress > 0 else { return fit }
        return fit * pow(actual / fit, zoomProgress)
    }

    private func imageSize(at scale: CGFloat) -> CGSize {
        guard let image = currentImage else { return .zero }
        return CGSize(
            width: CGFloat(image.width) * scale,
            height: CGFloat(image.height) * scale
        )
    }

    /// The document view is the image container. In Fit it is at least the
    /// viewport size; as zoom increases, it grows to the scaled image size.
    /// The NSScrollView clip view remains fixed to the existing pane bounds.
    private func containerSize(at scale: CGFloat) -> CGSize {
        let viewport = viewportSize
        let image = imageSize(at: scale)
        return CGSize(
            width: max(viewport.width, image.width),
            height: max(viewport.height, image.height)
        )
    }

    private func imageOriginInContainer(
        at scale: CGFloat,
        container: CGSize
    ) -> CGPoint {
        let image = imageSize(at: scale)
        return CGPoint(
            x: max(0, (container.width - image.width) / 2),
            y: max(0, (container.height - image.height) / 2)
        )
    }

    func updateDocumentFrame(for newViewportSize: CGSize) {
        guard newViewportSize.width > 0, newViewportSize.height > 0 else {
            return
        }
        let desiredSize = containerSize(at: currentScale)
        if abs(frame.width - desiredSize.width) > 0.5 ||
           abs(frame.height - desiredSize.height) > 0.5 {
            setFrameSize(desiredSize)
        }
        clampScrollOrigin(clipView?.bounds.origin ?? .zero)
    }

    private func clampScrollOrigin(_ proposed: CGPoint) {
        guard let clipView else { return }
        let maxX = max(0, bounds.width - clipView.bounds.width)
        let maxY = max(0, bounds.height - clipView.bounds.height)
        let clamped = CGPoint(
            x: min(max(0, proposed.x), maxX),
            y: min(max(0, proposed.y), maxY)
        )
        clipView.scroll(to: clamped)
        imageScrollView?.reflectScrolledClipView(clipView)
    }

    // MARK: - Presentation modes and zoom

    private func setFitMode() {
        zoomProgress = 0
        updateDocumentFrame(for: viewportSize)
        clampScrollOrigin(.zero)
        needsDisplay = true
    }

    private func setActualPixelsMode(anchor: CGPoint? = nil) {
        guard currentImage != nil else { return }
        zoom(to: 1, anchoredAt: anchor ?? CGPoint(
            x: viewportSize.width / 2,
            y: viewportSize.height / 2
        ))
    }

    private func toggleFitAndActualPixels(at point: CGPoint) {
        if zoomProgress >= 0.999 {
            setFitMode()
        } else {
            setActualPixelsMode(anchor: point)
        }
    }

    func zoom(by progressDelta: CGFloat, anchoredAt anchor: CGPoint) {
        guard currentImage != nil,
              progressDelta.isFinite,
              progressDelta != 0,
              actualPixelScale > fitScale else {
            return
        }

        zoom(to: min(1, max(0, zoomProgress + progressDelta)), anchoredAt: anchor)
    }

    private func zoom(to progress: CGFloat, anchoredAt anchor: CGPoint) {
        guard currentImage != nil else { return }
        let nextProgress = min(1, max(0, progress))
        guard nextProgress != zoomProgress else { return }

        let oldScale = currentScale
        let oldClipOrigin = clipView?.bounds.origin ?? .zero
        let oldContainer = bounds.size
        let oldImageOrigin = imageOriginInContainer(
            at: oldScale,
            container: oldContainer
        )
        let pointInDocument = CGPoint(
            x: oldClipOrigin.x + anchor.x,
            y: oldClipOrigin.y + anchor.y
        )
        let pointInImage = CGPoint(
            x: (pointInDocument.x - oldImageOrigin.x) / oldScale,
            y: (pointInDocument.y - oldImageOrigin.y) / oldScale
        )

        zoomProgress = nextProgress
        let nextScale = currentScale
        updateDocumentFrame(for: viewportSize)

        if nextProgress <= 0.0001 {
            clampScrollOrigin(.zero)
        } else {
            let newImageOrigin = imageOriginInContainer(
                at: nextScale,
                container: bounds.size
            )
            let desiredClipOrigin = CGPoint(
                x: newImageOrigin.x + pointInImage.x * nextScale - anchor.x,
                y: newImageOrigin.y + pointInImage.y * nextScale - anchor.y
            )
            clampScrollOrigin(desiredClipOrigin)
        }
        needsDisplay = true
    }

    // MARK: - Panning

    /// Converts the event position into viewport-local coordinates. The
    /// document view conversion includes the scrolled document origin, so
    /// subtract the clip origin to obtain a stable point inside the viewport.
    func viewportPoint(fromWindowPoint point: NSPoint) -> CGPoint {
        let documentPoint = convert(point, from: nil)
        let clipOrigin = clipView?.bounds.origin ?? .zero
        return CGPoint(
            x: documentPoint.x - clipOrigin.x,
            y: documentPoint.y - clipOrigin.y
        )
    }

    private func viewportPoint(from event: NSEvent) -> CGPoint {
        viewportPoint(fromWindowPoint: event.locationInWindow)
    }

    override func mouseDown(with event: NSEvent) {
        let point = viewportPoint(from: event)
        if event.clickCount >= 2 {
            toggleFitAndActualPixels(at: point)
            dragViewportLocation = nil
            return
        }

        guard zoomProgress > 0 else {
            dragViewportLocation = nil
            return
        }
        dragViewportLocation = point
    }

    override func mouseDragged(with event: NSEvent) {
        guard zoomProgress > 0,
              let previousPoint = dragViewportLocation,
              let clipView else {
            return
        }

        let currentPoint = viewportPoint(from: event)
        let delta = CGPoint(
            x: previousPoint.x - currentPoint.x,
            y: previousPoint.y - currentPoint.y
        )
        let currentOrigin = clipView.bounds.origin
        clampScrollOrigin(CGPoint(
            x: currentOrigin.x + delta.x,
            y: currentOrigin.y + delta.y
        ))
        dragViewportLocation = currentPoint
    }

    override func mouseUp(with event: NSEvent) {
        dragViewportLocation = nil
    }

    // MARK: - Context menu

    override func menu(for event: NSEvent) -> NSMenu? {
        contextMenuAnchor = viewportPoint(from: event)

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
        let size = imageSize(at: scale)
        let origin = imageOriginInContainer(at: scale, container: bounds.size)
        let targetRect = CGRect(origin: origin, size: size)

        context.interpolationQuality =
            scale < actualPixelScale * 0.999 ? .high : .none
        context.draw(image, in: targetRect)
    }
}
