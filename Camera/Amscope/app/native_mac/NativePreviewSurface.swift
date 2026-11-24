import AppKit
import SwiftUI

/// One AppKit consumer owns the bridge's single latest-frame mailbox and
/// routes frames to the selected slide pane. Each pane retains only its own
/// most recently presented CGImage; SwiftUI never observes individual frames.
struct NativeDualPreviewSurface: NSViewRepresentable {
    let bridge: AmscopeNativeCameraBridge
    let activeSlide: NativeSlideID
    let onActiveSlideChanged: (NativeSlideID) -> Void
    let bottomDockHeight: CGFloat

    func makeNSView(context: Context) -> NativeDualPreviewNSView {
        let view = NativeDualPreviewNSView(frame: .zero)
        view.attach(
            to: bridge,
            activeSlide: activeSlide,
            onActiveSlideChanged: onActiveSlideChanged,
            bottomDockHeight: bottomDockHeight
        )
        return view
    }

    func updateNSView(_ nsView: NativeDualPreviewNSView, context: Context) {
        nsView.attach(
            to: bridge,
            activeSlide: activeSlide,
            onActiveSlideChanged: onActiveSlideChanged,
            bottomDockHeight: bottomDockHeight
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
    private var bottomDockHeight: CGFloat = 51
    private var latestFramePixelSize: CGSize?
    private var lastAutoSizedFramePixelSize: CGSize?
    private var isApplyingAutomaticWindowSize = false
    private let previewHeaderHeight: CGFloat = 40
    private let windowSizingMargin: CGFloat = 24

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

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        resizeWindowForLatestFrame(force: true)
    }

    override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        resizeWindowForLatestFrame(force: true)
    }

    func attach(
        to bridge: AmscopeNativeCameraBridge,
        activeSlide: NativeSlideID,
        onActiveSlideChanged: @escaping (NativeSlideID) -> Void,
        bottomDockHeight: CGFloat
    ) {
        let normalizedDockHeight = max(0, bottomDockHeight)
        let dockHeightChanged = self.bottomDockHeight != normalizedDockHeight
        self.bottomDockHeight = normalizedDockHeight
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
        if dockHeightChanged {
            resizeWindowForLatestFrame(force: true)
        }
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
            resizeWindowForLatestFrame(force: true)
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
            resizeWindowForLatestFrame(force: true)
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

    func splitViewDidResizeSubviews(_ notification: Notification) {
        guard maximizedSlide == nil, !isApplyingAutomaticWindowSize else { return }
        let availableWidth = splitView.bounds.width - splitView.dividerThickness
        guard availableWidth > 0 else { return }
        previousDividerFraction = min(
            max(slideOnePane.frame.width / availableWidth, 0.1),
            0.9
        )
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

        latestFramePixelSize = CGSize(
            width: CGFloat(image.width),
            height: CGFloat(image.height)
        )

        switch activeSlide {
        case .slide1:
            slideOnePane.display(image)
        case .slide2:
            slideTwoPane.display(image)
        }

        resizeWindowForLatestFrame()
        updatePaneStates()
    }

    /// Sizes the preview workspace from the actual frame dimensions. A source
    /// pixel targets one display backing pixel; the screen bounds cap that
    /// target when the full image cannot fit. This is a presentation policy,
    /// independent of the camera's optical Auto Res selection.
    private func resizeWindowForLatestFrame(force: Bool = false) {
        guard !isApplyingAutomaticWindowSize,
              let pixelSize = latestFramePixelSize,
              pixelSize.width.isFinite,
              pixelSize.height.isFinite,
              pixelSize.width > 0,
              pixelSize.height > 0,
              let window = window,
              let screen = window.screen ?? NSScreen.main else {
            return
        }

        guard force || pixelSize != lastAutoSizedFramePixelSize else { return }

        let backingScale = max(window.backingScaleFactor, 1)
        let paneCount: CGFloat = maximizedSlide == nil ? 2 : 1
        let dividerAllowance = splitView.dividerThickness
        let safeWindowFrame = screen.visibleFrame.insetBy(
            dx: windowSizingMargin,
            dy: windowSizingMargin
        )
        let maximumContentSize = window.contentRect(
            forFrameRect: safeWindowFrame
        ).size

        let requestedImageWidth = pixelSize.width / backingScale
        let requestedImageHeight = pixelSize.height / backingScale
        let availableImageWidth = max(
            1,
            (maximumContentSize.width - dividerAllowance) / paneCount
        )
        let availableImageHeight = max(
            1,
            maximumContentSize.height - previewHeaderHeight - bottomDockHeight
        )
        let presentationScale = min(
            1,
            availableImageWidth / requestedImageWidth,
            availableImageHeight / requestedImageHeight
        )
        let desiredContentSize = NSSize(
            width: ceil(
                requestedImageWidth * presentationScale * paneCount +
                dividerAllowance
            ),
            height: ceil(
                requestedImageHeight * presentationScale +
                previewHeaderHeight +
                bottomDockHeight
            )
        )

        let minimumContentWidth: CGFloat = 960
        let minimumContentHeight: CGFloat = 560
        let targetContentSize = NSSize(
            width: min(
                max(desiredContentSize.width, minimumContentWidth),
                max(maximumContentSize.width, minimumContentWidth)
            ),
            height: min(
                max(desiredContentSize.height, minimumContentHeight),
                max(maximumContentSize.height, minimumContentHeight)
            )
        )

        lastAutoSizedFramePixelSize = pixelSize

        let currentContentSize = window.contentView?.bounds.size ?? .zero
        guard abs(currentContentSize.width - targetContentSize.width) > 1 ||
              abs(currentContentSize.height - targetContentSize.height) > 1 else {
            return
        }

        let previousFrame = window.frame
        isApplyingAutomaticWindowSize = true
        window.setContentSize(targetContentSize)

        var resizedFrame = window.frame
        resizedFrame.origin.x = previousFrame.midX - resizedFrame.width / 2
        resizedFrame.origin.y = previousFrame.midY - resizedFrame.height / 2

        let visibleFrame = screen.visibleFrame
        resizedFrame.origin.x = min(
            max(resizedFrame.origin.x, visibleFrame.minX),
            visibleFrame.maxX - resizedFrame.width
        )
        resizedFrame.origin.y = min(
            max(resizedFrame.origin.y, visibleFrame.minY),
            visibleFrame.maxY - resizedFrame.height
        )
        window.setFrame(resizedFrame, display: true, animate: false)

        if maximizedSlide == nil {
            let availableWidth = max(
                1,
                splitView.bounds.width - splitView.dividerThickness
            )
            splitView.setPosition(
                availableWidth * previousDividerFraction,
                ofDividerAt: 0
            )
        } else {
            applyMaximizedPosition()
        }

        isApplyingAutomaticWindowSize = false
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

    var hasImage: Bool { currentImage != nil }

    override var isFlipped: Bool { true }

    init(slideTitle: String) {
        self.slideTitle = slideTitle
        super.init(frame: .zero)
    }

    required init?(coder: NSCoder) {
        fatalError("NativeSlideImageView does not support NSCoder initialization")
    }

    func display(_ image: CGImage) {
        // Replacing the previous CGImage releases that slide's prior frame lease.
        currentImage = image
        needsDisplay = true
    }

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

        let imageSize = CGSize(width: CGFloat(image.width), height: CGFloat(image.height))
        let backingScale = max(window?.backingScaleFactor ?? 1, 1)
        let nativePixelScale = 1 / backingScale
        let scale = min(
            bounds.width / imageSize.width,
            bounds.height / imageSize.height,
            nativePixelScale
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
