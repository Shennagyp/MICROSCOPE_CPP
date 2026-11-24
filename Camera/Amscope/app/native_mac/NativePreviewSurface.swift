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

        // Establish a hard AppKit clipping boundary matching the SwiftUI
        // workspace frame. Oversized image documents must remain inside the
        // slide viewport and must never paint into the bottom control dock.
        wantsLayer = true
        layer?.masksToBounds = true

        splitView.wantsLayer = true
        splitView.layer?.masksToBounds = true
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

private enum NativeSlideImageSizingMode {
    /// Recalculate image scale whenever this pane's viewport changes.
    case resizable
    /// Keep the selected display scale independent of viewport resizing.
    case lockedScale
    /// Map every source pixel to one physical display pixel.
    case fullResolution
}

private final class NativeSlidePreviewPane: NSView {
    private let slide: NativeSlideID
    private let imageView: NativeSlideImageView
    private let titleLabel = NSTextField(labelWithString: "")
    private let statusLabel = NSTextField(labelWithString: "STANDBY")
    private let frameStatisticsLabel = NSTextField(labelWithString: "FRAMES 0 · VIEWED 0 · DROPS 0")
    private let selectButton = NSButton(title: "Set Live Target", target: nil, action: nil)
    private let imageSizingButton = NSButton(title: "", target: nil, action: nil)
    private let fullResolutionButton = NSButton(title: "1:1", target: nil, action: nil)
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
        layer?.masksToBounds = true
        layer?.backgroundColor = NSColor.clear.cgColor
        layer?.isOpaque = false

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

        imageSizingButton.bezelStyle = .texturedRounded
        imageSizingButton.controlSize = .small
        imageSizingButton.imagePosition = .imageOnly
        imageSizingButton.target = self
        imageSizingButton.action = #selector(toggleImageSizingModeAction(_:))
        imageSizingButton.setContentHuggingPriority(.required, for: .horizontal)
        imageSizingButton.setContentCompressionResistancePriority(.required, for: .horizontal)
        imageSizingButton.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            imageSizingButton.widthAnchor.constraint(equalToConstant: 24),
            imageSizingButton.heightAnchor.constraint(equalToConstant: 22)
        ])

        fullResolutionButton.bezelStyle = .rounded
        fullResolutionButton.controlSize = .small
        fullResolutionButton.font = NSFont.monospacedDigitSystemFont(
            ofSize: 10,
            weight: .semibold
        )
        fullResolutionButton.target = self
        fullResolutionButton.action = #selector(showFullResolutionAction(_:))
        fullResolutionButton.toolTip =
            "View the original camera image at 1:1: one camera pixel per physical display pixel."
        fullResolutionButton.setAccessibilityLabel(
            "View image at full resolution, 1:1"
        )
        fullResolutionButton.setContentHuggingPriority(.required, for: .horizontal)
        fullResolutionButton.setContentCompressionResistancePriority(.required, for: .horizontal)
        fullResolutionButton.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            fullResolutionButton.widthAnchor.constraint(equalToConstant: 36),
            fullResolutionButton.heightAnchor.constraint(equalToConstant: 22)
        ])

        imageView.onSizingModeChanged = { [weak self] in
            self?.updateImageSizingButton()
        }

        maximizeButton.bezelStyle = .rounded
        maximizeButton.controlSize = .small
        maximizeButton.target = self
        maximizeButton.action = #selector(toggleMaximizeAction(_:))

        // The upper slide-control strip has no backing fill. Its buttons,
        // symbols, and labels remain normal opaque controls.
        headerStack.wantsLayer = true
        headerStack.layer?.backgroundColor = NSColor.clear.cgColor
        headerStack.layer?.isOpaque = false
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
        headerStack.addArrangedSubview(imageSizingButton)
        headerStack.addArrangedSubview(fullResolutionButton)
        headerStack.addArrangedSubview(maximizeButton)

        // The viewer remains the same fixed-size pane. Only the image
        // container inside it is allowed to change dimensions.
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
        updateImageSizingButton()
    }

    private func updateImageSizingButton() {
        let isResizable = imageView.sizingMode == .resizable
        let symbolName = isResizable ? "lock.open" : "lock.fill"
        let description = isResizable
            ? "Lock current image scale"
            : "Resume automatic image resizing"
        if #available(macOS 11.0, *) {
            imageSizingButton.image = NSImage(
                systemSymbolName: symbolName,
                accessibilityDescription: description
            )
            imageSizingButton.imagePosition = .imageOnly
        } else {
            imageSizingButton.image = nil
            imageSizingButton.imagePosition = .noImage
            imageSizingButton.title = isResizable ? "Lock" : "Auto"
        }
        imageSizingButton.toolTip = isResizable
            ? "Lock Current Scale: preserve the current image scale when this viewer is resized."
            : "Auto Resize: resume resizing the image automatically with this viewer."
        imageSizingButton.setAccessibilityLabel(description)
        imageSizingButton.isEnabled = imageView.hasImage
        fullResolutionButton.isEnabled = imageView.hasImage
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
        updateImageSizingButton()
    }

    @objc private func selectLiveTargetAction(_ sender: NSButton) {
        onSelectLiveTarget?()
    }

    @objc private func toggleImageSizingModeAction(_ sender: NSButton) {
        imageView.toggleSizingMode()
        updateImageSizingButton()
    }

    @objc private func showFullResolutionAction(_ sender: NSButton) {
        imageView.showFullResolution()
        updateImageSizingButton()
    }

    @objc private func toggleMaximizeAction(_ sender: NSButton) {
        onToggleMaximize?()
    }
}

private final class NativeSlideImageView: NSView {
    private let slideTitle: String
    private let imageContainer = NativeSlideImageContainerView()
    private var currentImage: CGImage?

    private(set) var sizingMode: NativeSlideImageSizingMode = .resizable
    var onSizingModeChanged: (() -> Void)?

    /// Retained only in Manual Scale mode. The Full Resolution command sets
    /// this to the physical 1:1 scale, making the document exactly source
    /// width × source height in physical display pixels.
    private var lockedScale: CGFloat?
    private var imageOrigin: CGPoint?
    private var dragLocation: CGPoint?

    var hasImage: Bool { currentImage != nil }
    override var isFlipped: Bool { true }

    init(slideTitle: String) {
        self.slideTitle = slideTitle
        super.init(frame: .zero)
        wantsLayer = true
        layer?.masksToBounds = true
        addSubview(imageContainer)
        toolTip = "Scroll or pinch to zoom · Drag to pan · Double-click toggles automatic/manual sizing · 1:1 shows full resolution"
    }

    required init?(coder: NSCoder) {
        fatalError("NativeSlideImageView does not support NSCoder initialization")
    }

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool {
        true
    }

    override func setFrameSize(_ newSize: NSSize) {
        let oldSize = bounds.size
        super.setFrameSize(newSize)
        guard oldSize != bounds.size else { return }

        switch sizingMode {
        case .resizable:
            updateImageContainer(scale: automaticFitScale, origin: nil)
        case .lockedScale, .fullResolution:
            updateImageContainer(
                scale: currentScale,
                origin: imageOrigin
            )
        }
    }

    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        // Full Resolution derives its scale from the current display's
        // backing scale. A user-locked scale is intentionally left untouched.
        updateImageContainer(
            scale: currentScale,
            origin: sizingMode == .resizable ? nil : imageOrigin
        )
    }

    func display(_ image: CGImage) {
        let dimensionsChanged = currentImage?.width != image.width ||
            currentImage?.height != image.height
        currentImage = image
        imageContainer.display(image)

        if sizingMode == .resizable {
            updateImageContainer(scale: automaticFitScale, origin: nil)
        } else if dimensionsChanged {
            // Camera mode changes replace the source document dimensions.
            // Keep the selected display scale; Full Resolution remains 1:1
            // because currentScale is derived from the new source pixel grid.
            updateImageContainer(scale: currentScale, origin: nil)
        } else {
            updateImageContainer(scale: currentScale, origin: imageOrigin)
        }
        needsDisplay = true
    }

    // MARK: - Image scale and container bounds

    /// AppKit points per camera pixel for one physical display pixel.
    private var actualPixelScale: CGFloat {
        1 / max(window?.backingScaleFactor ?? 1, 1)
    }

    /// Largest aspect-preserving scale that keeps the complete source image
    /// inside the viewer viewport. Diagnostic content is never cropped just
    /// to eliminate margins. Upscaling beyond native 1:1 is not permitted.
    private var automaticFitScale: CGFloat {
        guard let image = currentImage,
              image.width > 0,
              image.height > 0,
              bounds.width > 0,
              bounds.height > 0 else {
            return actualPixelScale
        }

        let widthScale = bounds.width / CGFloat(image.width)
        let heightScale = bounds.height / CGFloat(image.height)
        return min(min(widthScale, heightScale), actualPixelScale)
    }

    private var currentScale: CGFloat {
        switch sizingMode {
        case .resizable:
            return automaticFitScale
        case .lockedScale:
            return min(lockedScale ?? automaticFitScale, actualPixelScale)
        case .fullResolution:
            return actualPixelScale
        }
    }

    /// Exact document dimensions for 1:1 viewing. This represents the
    /// complete source frame, not merely the visible viewport. On Retina
    /// displays each source pixel occupies 1 / backingScaleFactor points.
    private func fullResolutionContainerSize(for image: CGImage) -> CGSize {
        let pointsPerSourcePixel = actualPixelScale
        return CGSize(
            width: CGFloat(image.width) * pointsPerSourcePixel,
            height: CGFloat(image.height) * pointsPerSourcePixel
        )
    }

    /// Document dimensions at the requested scale. At 1:1, return the
    /// complete original image dimensions without fitting or cropping them
    /// to the viewport. The viewport clips; it does not resize the document.
    private func imageContainerSize(at scale: CGFloat) -> CGSize {
        guard let image = currentImage else { return .zero }
        let boundedScale = min(max(scale, 0), actualPixelScale)

        if boundedScale == actualPixelScale {
            return fullResolutionContainerSize(for: image)
        }

        return CGSize(
            width: CGFloat(image.width) * boundedScale,
            height: CGFloat(image.height) * boundedScale
        )
    }

    private func centeredOrigin(for size: CGSize) -> CGPoint {
        CGPoint(
            x: (bounds.width - size.width) / 2,
            y: (bounds.height - size.height) / 2
        )
    }

    private func clampedOrigin(_ proposed: CGPoint, for size: CGSize) -> CGPoint {
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

    private func updateImageContainer(scale: CGFloat, origin: CGPoint?) {
        guard currentImage != nil else { return }
        let size = imageContainerSize(at: scale)
        let proposedOrigin = origin ?? centeredOrigin(for: size)
        let boundedOrigin = clampedOrigin(proposedOrigin, for: size)

        imageContainer.frame = CGRect(
            origin: boundedOrigin,
            size: size
        )
        imageContainer.needsDisplay = true
        imageOrigin = boundedOrigin
        needsDisplay = true
    }

    // MARK: - Sizing mode

    func toggleSizingMode() {
        guard currentImage != nil else { return }

        switch sizingMode {
        case .resizable:
            // Lock the scale currently on screen; do not silently jump to 1:1.
            let scaleToLock = currentScale
            sizingMode = .lockedScale
            lockedScale = scaleToLock
            updateImageContainer(scale: scaleToLock, origin: imageOrigin)
        case .lockedScale, .fullResolution:
            sizingMode = .resizable
            lockedScale = nil
            updateImageContainer(scale: currentScale, origin: nil)
        }
        onSizingModeChanged?()
    }

    /// Show the complete source image at physical 1:1 scale. This is a
    /// distinct state from locking the current (possibly reduced) scale.
    func showFullResolution() {
        guard currentImage != nil else { return }
        sizingMode = .fullResolution
        lockedScale = nil
        updateImageContainer(scale: actualPixelScale, origin: nil)
        onSizingModeChanged?()
    }

    // MARK: - Manual zoom

    private func zoom(by delta: CGFloat, anchoredAt anchor: CGPoint) {
        guard currentImage != nil,
              delta.isFinite,
              delta != 0 else {
            return
        }

        let oldScale = currentScale
        let maximumScale = actualPixelScale
        let minimumScale = min(automaticFitScale, maximumScale)
        let oldOrigin = imageOrigin ?? centeredOrigin(
            for: imageContainerSize(at: oldScale)
        )
        let imagePoint = CGPoint(
            x: (anchor.x - oldOrigin.x) / oldScale,
            y: (anchor.y - oldOrigin.y) / oldScale
        )

        let factor = pow(2, delta)
        let nextScale = min(
            maximumScale,
            max(minimumScale, oldScale * factor)
        )
        guard abs(nextScale - oldScale) > 0.000001 else { return }

        // Manual zoom locks this pane only when the scale actually changes.
        // Exactly 1:1 is represented separately so backing-scale changes can
        // preserve native-pixel mapping without changing ordinary locked zoom.
        let nextMode: NativeSlideImageSizingMode =
            abs(nextScale - maximumScale) < 0.000001
                ? .fullResolution
                : .lockedScale
        if sizingMode != nextMode {
            sizingMode = nextMode
            onSizingModeChanged?()
        }
        lockedScale = nextMode == .lockedScale ? nextScale : nil
        let proposedOrigin = CGPoint(
            x: anchor.x - imagePoint.x * nextScale,
            y: anchor.y - imagePoint.y * nextScale
        )
        updateImageContainer(scale: nextScale, origin: proposedOrigin)
    }

    // MARK: - Pointer interaction and panning

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
            toggleSizingMode()
            dragLocation = nil
            return
        }

        let size = imageContainer.frame.size
        guard size.width > bounds.width || size.height > bounds.height else {
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
        let proposedOrigin = CGPoint(
            x: (imageOrigin ?? imageContainer.frame.origin).x + delta.x,
            y: (imageOrigin ?? imageContainer.frame.origin).y + delta.y
        )
        updateImageContainer(scale: currentScale, origin: proposedOrigin)
        dragLocation = point
    }

    override func mouseUp(with event: NSEvent) {
        dragLocation = nil
    }

    override func menu(for event: NSEvent) -> NSMenu? {
        let menu = NSMenu()
        let resizableItem = NSMenuItem(
            title: "Resizable: Follow Viewer Size",
            action: #selector(selectResizableMode(_:)),
            keyEquivalent: ""
        )
        resizableItem.target = self
        resizableItem.state = sizingMode == .resizable ? .on : .off
        resizableItem.isEnabled = currentImage != nil
        menu.addItem(resizableItem)

        let manualScaleItem = NSMenuItem(
            title: "Lock Current Scale",
            action: #selector(selectManualScaleMode(_:)),
            keyEquivalent: ""
        )
        manualScaleItem.target = self
        manualScaleItem.state = sizingMode == .resizable ? .off : .on
        manualScaleItem.isEnabled = currentImage != nil
        menu.addItem(manualScaleItem)

        let fullResolutionItem = NSMenuItem(
            title: "View Full Resolution (1:1)",
            action: #selector(showFullResolutionFromMenu(_:)),
            keyEquivalent: ""
        )
        fullResolutionItem.target = self
        fullResolutionItem.isEnabled = currentImage != nil
        menu.addItem(fullResolutionItem)
        return menu
    }

    @objc private func selectResizableMode(_ sender: Any?) {
        guard currentImage != nil else { return }
        if sizingMode != .resizable {
            toggleSizingMode()
        }
    }

    @objc private func selectManualScaleMode(_ sender: Any?) {
        guard currentImage != nil else { return }
        let scaleToLock = currentScale
        sizingMode = .lockedScale
        lockedScale = scaleToLock
        updateImageContainer(scale: scaleToLock, origin: imageOrigin)
        onSizingModeChanged?()
    }

    @objc private func showFullResolutionFromMenu(_ sender: Any?) {
        showFullResolution()
    }

    // MARK: - Rendering

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)
        NSColor(calibratedWhite: 0.075, alpha: 1).setFill()
        dirtyRect.fill()
    }
}

private final class NativeSlideImageContainerView: NSView {
    private var currentImage: CGImage?

    override var isFlipped: Bool { true }

    /// Route gestures to the fixed viewer, which owns zoom, pan, and context
    /// menu handling. This child exists only to draw the scaled image.
    override func hitTest(_ point: NSPoint) -> NSView? {
        superview
    }

    func display(_ image: CGImage) {
        currentImage = image
        needsDisplay = true
    }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)

        guard let image = currentImage,
              let context = NSGraphicsContext.current?.cgContext else {
            return
        }

        let physicalScale = 1 / max(window?.backingScaleFactor ?? 1, 1)
        let displayedScale = bounds.width / max(CGFloat(image.width), 1)
        context.interpolationQuality =
            displayedScale < physicalScale * 0.999 ? .high : .none
        context.draw(
            image,
            in: CGRect(origin: .zero, size: bounds.size)
        )
    }
}
