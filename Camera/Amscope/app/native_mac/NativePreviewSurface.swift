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
        guard let bridge = bridge,
              let lease = bridge.takeLatestFrame(),
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
    private let selectButton = NSButton(title: "Set Live Target", target: nil, action: nil)
    private let maximizeButton = NSButton(title: "Maximize", target: nil, action: nil)
    private let headerStack = NSStackView()

    var onSelectLiveTarget: (() -> Void)?
    var onToggleMaximize: (() -> Void)?

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

    func display(_ image: CGImage) {
        imageView.display(image)
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
