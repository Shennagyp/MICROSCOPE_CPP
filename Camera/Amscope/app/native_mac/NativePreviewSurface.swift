import AppKit
import SwiftUI

private enum NativeSlideToolMode: Equatable {
    case pan
    case distance
    case annotation
}

private struct NativeDistanceMeasurement {
    let start: CGPoint
    let end: CGPoint
    /// Optical calibration snapshot captured when this measurement starts.
    let micrometersPerSourcePixel: CGFloat?

    var distanceInPixels: CGFloat {
        hypot(end.x - start.x, end.y - start.y)
    }

    var distanceInMicrometers: CGFloat? {
        guard let micrometersPerSourcePixel,
              micrometersPerSourcePixel.isFinite,
              micrometersPerSourcePixel > 0 else {
            return nil
        }
        return distanceInPixels * micrometersPerSourcePixel
    }
}

private struct NativeSlideTextAnnotation {
    let location: CGPoint
    let text: String
}


/// Compact acquisition controls hosted independently inside each slide pane.
/// Both copies observe the same camera model and stay synchronized.
private struct NativeMicroscopePaneControls: View {
    @ObservedObject var model: NativeCameraModel
    let slide: NativeSlideID

    private var resolutionDescriptor: AmscopeCameraFeatureDescriptor? {
        model.previewResolutionDescriptor
    }

    private var isLiveTarget: Bool {
        model.activeSlide == slide
    }

    var body: some View {
        HStack(alignment: .center, spacing: 4) {
            Picker(
                "Objective",
                selection: Binding(
                    get: { model.microscopeConfiguration.activeObjectiveID },
                    set: { model.selectMicroscopeObjective($0) }
                )
            ) {
                ForEach(model.microscopeConfiguration.objectives) { objective in
                    Text(String(format: "%.0f×", objective.magnification))
                        .tag(objective.id)
                }
            }
            .labelsHidden()
            .pickerStyle(PopUpButtonPickerStyle())
            .font(.system(size: 9))
            .frame(width: 56)
            .disabled(model.isCameraOperationPending)
            .help("Select the active microscope objective")

            if let descriptor = resolutionDescriptor {
                Picker(
                    "Resolution",
                    selection: Binding(
                        get: { model.selectedPreviewResolutionSymbol ?? "" },
                        set: { model.selectManualPreviewResolution(to: $0) }
                    )
                ) {
                    if model.selectedPreviewResolutionSymbol == nil {
                        Text("Select…").tag("")
                    }
                    ForEach(descriptor.enumerationValues, id: \.self) { symbol in
                        Text(resolutionOptionTitle(symbol)).tag(symbol)
                    }
                }
                .labelsHidden()
                .pickerStyle(PopUpButtonPickerStyle())
                .font(.system(size: 9))
                .frame(width: 84)
                .disabled(!model.canChangePreviewResolution)
                .help("Choose a resolution. Manual selection disables automatic resolution.")
            } else {
                Text(model.canAccessFeatures ? "Unavailable" : "—")
                    .font(.system(size: 9))
                    .foregroundColor(.secondary)
                    .frame(width: 84, height: 20, alignment: .leading)
            }

            Toggle(
                "AUTO",
                isOn: Binding(
                    get: { model.isAutomaticResolutionEnabled },
                    set: { model.setAutomaticResolutionEnabled($0) }
                )
            )
            .font(.system(size: 8, weight: .semibold))
            .toggleStyle(SwitchToggleStyle())
            .disabled(resolutionDescriptor == nil ||
                      !model.canAccessFeatures ||
                      model.isCameraOperationPending ||
                      model.isFeatureWritePending ||
                      model.isFeatureSnapshotLoading)
            .help(model.automaticResolutionHelp)

            Button {
                model.selectActiveSlide(slide)
            } label: {
                HStack(spacing: 3) {
                    Image(systemName: "dot.radiowaves.left.and.right")
                        .font(.system(size: 9, weight: .semibold))
                    Text(isLiveTarget ? "LIVE" : "SET LIVE")
                        .font(.system(size: 8, weight: .bold))
                }
                .foregroundColor(isLiveTarget ? Color.red : Color.primary)
                .padding(.horizontal, 5)
                .frame(height: 21)
                .background(
                    Capsule().fill(
                        isLiveTarget
                            ? Color.red.opacity(0.12)
                            : Color.primary.opacity(0.07)
                    )
                )
            }
            .buttonStyle(.plain)
            .disabled(isLiveTarget)
            .help(
                isLiveTarget
                    ? "\(slide.title) is receiving the live camera feed."
                    : "Route the live camera feed to \(slide.title)."
            )
            .accessibilityLabel(
                isLiveTarget
                    ? "\(slide.title) is the live feed destination"
                    : "Route the live camera feed to \(slide.title)"
            )
        }
        .padding(.horizontal, 4)
        .padding(.vertical, 2)
        .fixedSize(horizontal: true, vertical: true)
        .accessibilityElement(children: .contain)
        .accessibilityLabel("\(slide.title) microscope and live-feed controls")
    }

    private func resolutionOptionTitle(_ symbol: String) -> String {
        guard let separator = symbol.firstIndex(of: ":") else {
            return symbol
        }
        return String(symbol[symbol.index(after: separator)...])
            .replacingOccurrences(of: "x", with: " × ")
    }
}

/// One AppKit consumer owns the bridge's single latest-frame mailbox and
/// routes frames to the selected slide pane. Each pane retains only its own
/// most recently presented CGImage; SwiftUI never observes individual frames.
struct NativeDualPreviewSurface: NSViewRepresentable {
    let bridge: AmscopeNativeCameraBridge
    let model: NativeCameraModel
    let activeSlide: NativeSlideID
    let measurementCalibration: NativeMicroscopeMeasurementCalibration?
    let onCapture: (NativeSlideID, CGImage) -> Void

    func makeNSView(context: Context) -> NativeDualPreviewNSView {
        let view = NativeDualPreviewNSView(frame: .zero)
        view.attach(
            to: bridge,
            activeSlide: activeSlide,
            measurementCalibration: measurementCalibration,
            model: model,
            onCapture: onCapture
        )
        return view
    }

    func updateNSView(_ nsView: NativeDualPreviewNSView, context: Context) {
        nsView.attach(
            to: bridge,
            activeSlide: activeSlide,
            measurementCalibration: measurementCalibration,
            model: model,
            onCapture: onCapture
        )
    }
}

final class NativeDualPreviewNSView: NSView, NSSplitViewDelegate {
    private let splitView = NSSplitView()
    private let slideOnePane = NativeSlidePreviewPane(slide: .slide1)
    private let slideTwoPane = NativeSlidePreviewPane(slide: .slide2)
    private weak var bridge: AmscopeNativeCameraBridge?
    private var measurementCalibration: NativeMicroscopeMeasurementCalibration?

    private var activeSlide: NativeSlideID = .slide1
    private var maximizedSlide: NativeSlideID?
    private var previousDividerFraction: CGFloat = 0.5
    private var didSetInitialSplitPosition = false
    private var onCapture: ((NativeSlideID, CGImage) -> Void)?
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
        // slide viewport and must never paint into the right workspace dock.
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

        slideOnePane.onToggleMaximize = { [weak self] in
            self?.toggleMaximizedSlide(.slide1)
        }
        slideTwoPane.onToggleMaximize = { [weak self] in
            self?.toggleMaximizedSlide(.slide2)
        }
        slideOnePane.onCaptureImage = { [weak self] image in
            self?.onCapture?(.slide1, image)
        }
        slideTwoPane.onCaptureImage = { [weak self] image in
            self?.onCapture?(.slide2, image)
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
        measurementCalibration: NativeMicroscopeMeasurementCalibration?,
        model: NativeCameraModel,
        onCapture: @escaping (NativeSlideID, CGImage) -> Void
    ) {
        if self.bridge !== bridge {
            self.bridge = bridge
            bridge.onFrameAvailable = { [weak self] in
                MainActor.assumeIsolated {
                    self?.consumeLatestFrame()
                }
            }
        }

        self.measurementCalibration = measurementCalibration
        slideOnePane.updateMeasurementCalibration(measurementCalibration)
        slideTwoPane.updateMeasurementCalibration(measurementCalibration)
        slideOnePane.updateMicroscopeControls(model)
        slideTwoPane.updateMicroscopeControls(model)
        self.onCapture = onCapture
        setActiveSlide(activeSlide)

        // Recover a frame if the view was recreated while a notification
        // was already pending. This remains the single mailbox consumer.
        consumeLatestFrame()
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
            if !slideOnePane.isImageFrozen {
                slideOnePane.display(image)
            }
        case .slide2:
            if !slideTwoPane.isImageFrozen {
                slideTwoPane.display(image)
            }
        }

        // Frozen panes still consume and release the newest lease and keep
        // acquisition counters current. They do not replace the stable source
        // image or rewrite toolbar state for every incoming frame.
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
    private let imageSizingButton = NSButton(title: "", target: nil, action: nil)
    private let fullResolutionButton = NSButton(title: "1:1", target: nil, action: nil)
    private let maximizeButton = NSButton(title: "", target: nil, action: nil)
    private let freezeButton = NSButton(title: "", target: nil, action: nil)
    private let captureButton = NSButton(title: "", target: nil, action: nil)
    private let panToolButton = NSButton(title: "", target: nil, action: nil)
    private let distanceToolButton = NSButton(title: "", target: nil, action: nil)
    private let annotationToolButton = NSButton(title: "", target: nil, action: nil)
    private let clearOverlayButton = NSButton(title: "", target: nil, action: nil)
    private let annotationStack = NSStackView()
    private let actionStack = NSStackView()
    private let toolStack = NSStackView()
    private let annotationPlate = NSView()
    private let actionPlate = NSView()
    private let toolPlate = NSView()
    private let microscopeControlsPlate = NSView()
    private var microscopeControlsHost: NSHostingView<NativeMicroscopePaneControls>?

    var onToggleMaximize: (() -> Void)?
    var onCaptureImage: ((CGImage) -> Void)?

    // The image view owns freeze state; expose it read-only to the frame router.
    var isImageFrozen: Bool { imageView.isImageFrozen }

    private var pulledFrameCount: UInt64 = 0
    private var droppedFrameCount: UInt64 = 0
    private var paneFrameCount: UInt64 = 0
    private var currentIsActiveTarget = false
    private var currentIsMaximized = false

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
        titleLabel.font = NSFont.systemFont(ofSize: 11, weight: .semibold)
        titleLabel.textColor = NSColor.labelColor
        wantsLayer = true
        layer?.cornerRadius = 4
        layer?.masksToBounds = true
        layer?.backgroundColor = NSColor.clear.cgColor
        layer?.isOpaque = false

        statusLabel.font = NSFont.systemFont(ofSize: 9, weight: .bold)
        statusLabel.textColor = NSColor.secondaryLabelColor
        statusLabel.textColor = NSColor.secondaryLabelColor
        frameStatisticsLabel.font = NSFont.monospacedDigitSystemFont(ofSize: 8, weight: .medium)
        frameStatisticsLabel.textColor = NSColor.secondaryLabelColor
        frameStatisticsLabel.lineBreakMode = .byTruncatingTail
        frameStatisticsLabel.setContentCompressionResistancePriority(.defaultLow, for: .horizontal)

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
        imageView.onOverlayStateChanged = { [weak self] in
            self?.updateToolButtonStates()
        }
        imageView.onFreezeStateChanged = { [weak self] in
            guard let self = self else { return }
            self.updatePresentationState(
                isActiveTarget: self.currentIsActiveTarget,
                isMaximized: self.currentIsMaximized
            )
            self.updateToolButtonStates()
        }

        configureIconButton(
            freezeButton,
            symbol: "pause.fill",
            description: "Freeze this slide image",
            action: #selector(toggleImageFreezeAction(_:))
        )

        maximizeButton.bezelStyle = .texturedRounded
        maximizeButton.controlSize = .small
        maximizeButton.imagePosition = .imageOnly
        maximizeButton.target = self
        maximizeButton.action = #selector(toggleMaximizeAction(_:))
        maximizeButton.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            maximizeButton.widthAnchor.constraint(equalToConstant: 24),
            maximizeButton.heightAnchor.constraint(equalToConstant: 22)
        ])

        configureIconButton(
            captureButton,
            symbol: "camera",
            description: "Capture current slide image",
            action: #selector(captureCurrentImageAction(_:))
        )
        configureIconButton(
            panToolButton,
            symbol: "cursorarrow",
            description: "Pointer and pan tool",
            action: #selector(selectPanToolAction(_:)),
            isToggle: true
        )
        configureIconButton(
            distanceToolButton,
            symbol: "ruler",
            description: "Measure straight-line distance in source pixels and calculated specimen micrometers",
            action: #selector(selectDistanceToolAction(_:)),
            isToggle: true
        )
        configureIconButton(
            annotationToolButton,
            symbol: "text.badge.plus",
            description: "Add a text annotation to this slide",
            action: #selector(selectAnnotationToolAction(_:)),
            isToggle: true
        )
        configureIconButton(
            clearOverlayButton,
            symbol: "trash",
            description: "Clear measurements and annotations",
            action: #selector(clearSlideOverlaysAction(_:))
        )

        // The former full-width header is removed. Specimen metadata is
        // presented as compact corner annotations over the image viewport;
        // actions remain available in a separate floating control cluster.
        configureAnnotationPlate(annotationPlate)
        configureAnnotationPlate(actionPlate)
        configureAnnotationPlate(toolPlate)
        configureAnnotationPlate(microscopeControlsPlate)

        annotationStack.orientation = .horizontal
        annotationStack.alignment = .centerY
        annotationStack.distribution = .fill
        annotationStack.spacing = 9
        annotationStack.translatesAutoresizingMaskIntoConstraints = false
        annotationStack.addArrangedSubview(titleLabel)
        annotationStack.addArrangedSubview(statusLabel)
        annotationStack.addArrangedSubview(frameStatisticsLabel)

        actionStack.orientation = .horizontal
        actionStack.alignment = .centerY
        actionStack.distribution = .fill
        actionStack.spacing = 5
        actionStack.translatesAutoresizingMaskIntoConstraints = false
        actionStack.addArrangedSubview(imageSizingButton)
        actionStack.addArrangedSubview(fullResolutionButton)
        actionStack.addArrangedSubview(freezeButton)
        actionStack.addArrangedSubview(captureButton)
        actionStack.addArrangedSubview(maximizeButton)

        toolStack.orientation = .horizontal
        toolStack.alignment = .centerY
        toolStack.distribution = .fill
        toolStack.spacing = 5
        toolStack.translatesAutoresizingMaskIntoConstraints = false
        toolStack.addArrangedSubview(panToolButton)
        toolStack.addArrangedSubview(distanceToolButton)
        toolStack.addArrangedSubview(annotationToolButton)
        toolStack.addArrangedSubview(clearOverlayButton)

        annotationPlate.translatesAutoresizingMaskIntoConstraints = false
        actionPlate.translatesAutoresizingMaskIntoConstraints = false
        toolPlate.translatesAutoresizingMaskIntoConstraints = false
        annotationPlate.addSubview(annotationStack)
        actionPlate.addSubview(actionStack)
        toolPlate.addSubview(toolStack)

        // The image viewport now occupies the complete pane. The annotation
        // plates float above it and do not reserve a header band.
        imageView.translatesAutoresizingMaskIntoConstraints = false
        addSubview(imageView)
        addSubview(annotationPlate)
        addSubview(actionPlate)
        addSubview(toolPlate)
        addSubview(microscopeControlsPlate)
        NSLayoutConstraint.activate([
            imageView.leadingAnchor.constraint(equalTo: leadingAnchor),
            imageView.trailingAnchor.constraint(equalTo: trailingAnchor),
            imageView.topAnchor.constraint(equalTo: topAnchor),
            imageView.bottomAnchor.constraint(equalTo: bottomAnchor),

            annotationStack.leadingAnchor.constraint(equalTo: annotationPlate.leadingAnchor, constant: 9),
            annotationStack.trailingAnchor.constraint(equalTo: annotationPlate.trailingAnchor, constant: -9),
            annotationStack.topAnchor.constraint(equalTo: annotationPlate.topAnchor, constant: 6),
            annotationStack.bottomAnchor.constraint(equalTo: annotationPlate.bottomAnchor, constant: -6),
            annotationPlate.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 10),
            annotationPlate.topAnchor.constraint(equalTo: topAnchor, constant: 10),

            actionStack.leadingAnchor.constraint(equalTo: actionPlate.leadingAnchor, constant: 5),
            actionStack.trailingAnchor.constraint(equalTo: actionPlate.trailingAnchor, constant: -5),
            actionStack.topAnchor.constraint(equalTo: actionPlate.topAnchor, constant: 4),
            actionStack.bottomAnchor.constraint(equalTo: actionPlate.bottomAnchor, constant: -4),
            actionPlate.trailingAnchor.constraint(equalTo: trailingAnchor, constant: -10),
            actionPlate.topAnchor.constraint(equalTo: topAnchor, constant: 10),

            toolStack.leadingAnchor.constraint(equalTo: toolPlate.leadingAnchor, constant: 5),
            toolStack.trailingAnchor.constraint(equalTo: toolPlate.trailingAnchor, constant: -5),
            toolStack.topAnchor.constraint(equalTo: toolPlate.topAnchor, constant: 4),
            toolStack.bottomAnchor.constraint(equalTo: toolPlate.bottomAnchor, constant: -4),
            toolPlate.leadingAnchor.constraint(equalTo: leadingAnchor, constant: 10),
            toolPlate.bottomAnchor.constraint(equalTo: bottomAnchor, constant: -10),

            microscopeControlsPlate.leadingAnchor.constraint(
                equalTo: toolPlate.trailingAnchor,
                constant: 8
            ),
            microscopeControlsPlate.bottomAnchor.constraint(equalTo: toolPlate.bottomAnchor),
            microscopeControlsPlate.trailingAnchor.constraint(
                lessThanOrEqualTo: trailingAnchor,
                constant: -10
            )
        ])
        updateImageSizingButton()
        updateToolButtonStates()
        updateFreezeButton()
    }

    private func configureIconButton(
        _ button: NSButton,
        symbol: String,
        description: String,
        action: Selector,
        isToggle: Bool = false
    ) {
        button.bezelStyle = .texturedRounded
        button.controlSize = .small
        button.imagePosition = .imageOnly
        button.target = self
        button.action = action
        button.toolTip = description
        button.setAccessibilityLabel(description)
        if isToggle {
            button.setButtonType(.toggle)
        }
        button.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            button.widthAnchor.constraint(equalToConstant: 26),
            button.heightAnchor.constraint(equalToConstant: 24)
        ])

        if #available(macOS 11.0, *) {
            button.image = NSImage(
                systemSymbolName: symbol,
                accessibilityDescription: description
            )
        } else {
            button.image = nil
            switch symbol {
            case "ruler": button.title = "R"
            case "camera": button.title = "C"
            case "trash": button.title = "×"
            case "cursorarrow": button.title = "↖"
            default: button.title = "T"
            }
        }
    }

    private func updateToolButtonStates() {
        panToolButton.state = imageView.toolMode == .pan ? .on : .off
        distanceToolButton.state = imageView.toolMode == .distance ? .on : .off
        annotationToolButton.state = imageView.toolMode == .annotation ? .on : .off

        let hasImage = imageView.hasImage
        panToolButton.isEnabled = hasImage
        distanceToolButton.isEnabled = hasImage
        annotationToolButton.isEnabled = hasImage
        captureButton.isEnabled = hasImage
        clearOverlayButton.isEnabled = imageView.hasOverlays
    }

    private func updateFreezeButton() {
        let frozen = imageView.isImageFrozen
        let description = frozen ? "Resume live image" : "Freeze this slide image"
        let symbol = frozen ? "play.fill" : "pause.fill"
        if #available(macOS 11.0, *) {
            freezeButton.image = NSImage(
                systemSymbolName: symbol,
                accessibilityDescription: description
            )
            freezeButton.imagePosition = .imageOnly
        } else {
            freezeButton.image = nil
            freezeButton.title = frozen ? ">" : "||"
        }
        freezeButton.toolTip = description
        freezeButton.setAccessibilityLabel(description)
        freezeButton.isEnabled = imageView.hasImage
    }

    private func configureAnnotationPlate(_ plate: NSView) {
        plate.wantsLayer = true
        plate.layer?.backgroundColor = NSColor.windowBackgroundColor
            .withAlphaComponent(0.20).cgColor
        plate.layer?.cornerRadius = 7
        plate.layer?.borderWidth = 1
        plate.layer?.borderColor = NSColor.separatorColor
            .withAlphaComponent(0.45).cgColor
        plate.layer?.masksToBounds = true
        plate.layer?.isOpaque = false
        plate.translatesAutoresizingMaskIntoConstraints = false
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

    func updateMeasurementCalibration(
        _ calibration: NativeMicroscopeMeasurementCalibration?
    ) {
        imageView.updateMeasurementCalibration(calibration)
    }

    func updateMicroscopeControls(_ model: NativeCameraModel) {
        if let host = microscopeControlsHost {
            host.rootView = NativeMicroscopePaneControls(model: model, slide: slide)
            return
        }

        let host = NSHostingView(
            rootView: NativeMicroscopePaneControls(model: model, slide: slide)
        )
        host.translatesAutoresizingMaskIntoConstraints = false
        host.setContentHuggingPriority(.required, for: .horizontal)
        host.setContentCompressionResistancePriority(.defaultHigh, for: .horizontal)
        microscopeControlsHost = host
        microscopeControlsPlate.addSubview(host)

        NSLayoutConstraint.activate([
            host.leadingAnchor.constraint(
                equalTo: microscopeControlsPlate.leadingAnchor,
                constant: 5
            ),
            host.trailingAnchor.constraint(
                equalTo: microscopeControlsPlate.trailingAnchor,
                constant: -5
            ),
            host.topAnchor.constraint(
                equalTo: microscopeControlsPlate.topAnchor,
                constant: 4
            ),
            host.bottomAnchor.constraint(
                equalTo: microscopeControlsPlate.bottomAnchor,
                constant: -4
            )
        ])
    }

    override func layout() {
        super.layout()
        // At minimum preview width with the dock open, each pane can become
        // too narrow for both bottom plates. The dock retains the controls.
        microscopeControlsPlate.isHidden = bounds.width < 435
    }

    func display(_ image: CGImage) {
        let hadImage = imageView.hasImage
        let hadOverlays = imageView.hasOverlays

        paneFrameCount &+= 1
        imageView.display(image)

        // Update control availability only when the pane receives its first
        // image or a source-dimension change clears existing overlays.
        if !hadImage {
            updateImageSizingButton()
            updateToolButtonStates()
            updateFreezeButton()
        } else if hadOverlays && !imageView.hasOverlays {
            updateToolButtonStates()
        }

        updateFrameStatisticsLabel()
    }

    private func updateFrameStatisticsLabel() {
        frameStatisticsLabel.stringValue =
            "PULLED \(pulledFrameCount) · PANE \(paneFrameCount) · DROPS \(droppedFrameCount)"
        frameStatisticsLabel.toolTip =
            "PULLED: cumulative driver frames pulled. PANE: images assigned to this slide preview. DROPS: cumulative driver-reported dropped frames."
    }

    func updatePresentationState(isActiveTarget: Bool, isMaximized: Bool) {
        currentIsActiveTarget = isActiveTarget
        currentIsMaximized = isMaximized
        if isActiveTarget {
            statusLabel.stringValue = imageView.isImageFrozen ? "FROZEN" : "LIVE"
            statusLabel.textColor = imageView.isImageFrozen
                ? NSColor.systemOrange
                : NSColor.systemRed
        } else {
            statusLabel.stringValue = imageView.hasImage
                ? (imageView.isImageFrozen ? "FROZEN IMAGE" : "HELD IMAGE")
                : "STANDBY"
            statusLabel.textColor = imageView.isImageFrozen
                ? NSColor.systemOrange
                : NSColor.secondaryLabelColor
        }
        layer?.borderWidth = 1
        layer?.borderColor = isActiveTarget
            ? NSColor.systemRed.cgColor
            : NSColor.separatorColor.cgColor
        let maximizeDescription = isMaximized ? "Restore split view" : "Maximize slide"
        let maximizeSymbol = isMaximized
            ? "arrow.down.right.and.arrow.up.left"
            : "arrow.up.left.and.arrow.down.right"
        if #available(macOS 11.0, *) {
            maximizeButton.image = NSImage(
                systemSymbolName: maximizeSymbol,
                accessibilityDescription: maximizeDescription
            )
            maximizeButton.imagePosition = .imageOnly
        } else {
            maximizeButton.image = nil
            maximizeButton.imagePosition = .noImage
            maximizeButton.title = isMaximized ? "Restore" : "Max"
        }
        maximizeButton.toolTip = maximizeDescription
        maximizeButton.setAccessibilityLabel(maximizeDescription)
        updateFreezeButton()
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

    @objc private func captureCurrentImageAction(_ sender: NSButton) {
        guard let image = imageView.currentImageForCapture() else { return }
        onCaptureImage?(image)
    }

    @objc private func toggleImageFreezeAction(_ sender: NSButton) {
        imageView.toggleImageFreeze()
    }

    @objc private func selectPanToolAction(_ sender: NSButton) {
        imageView.setToolMode(.pan)
        updateToolButtonStates()
    }

    @objc private func selectDistanceToolAction(_ sender: NSButton) {
        imageView.setToolMode(.distance)
        updateToolButtonStates()
    }

    @objc private func selectAnnotationToolAction(_ sender: NSButton) {
        imageView.setToolMode(.annotation)
        updateToolButtonStates()
    }

    @objc private func clearSlideOverlaysAction(_ sender: NSButton) {
        imageView.clearOverlays()
        updateToolButtonStates()
    }
}

private final class NativeSlideImageView: NSView {
    private let slideTitle: String
    private let imageContainer = NativeSlideImageContainerView()
    private var currentImage: CGImage?
    private(set) var toolMode: NativeSlideToolMode = .pan
    private(set) var isImageFrozen = false
    private var measurements: [NativeDistanceMeasurement] = []
    private var previewMeasurement: NativeDistanceMeasurement?
    private var textAnnotations: [NativeSlideTextAnnotation] = []
    private var measurementStart: CGPoint?
    private var measurementMicrometersPerPixelAtStart: CGFloat?
    private var measurementCalibration: NativeMicroscopeMeasurementCalibration?

    private(set) var sizingMode: NativeSlideImageSizingMode = .resizable
    var onSizingModeChanged: (() -> Void)?
    var onOverlayStateChanged: (() -> Void)?
    var onFreezeStateChanged: (() -> Void)?

    /// Retained only in Manual Scale mode. The Full Resolution command sets
    /// this to the physical 1:1 scale, making the document exactly source
    /// width × source height in physical display pixels.
    private var lockedScale: CGFloat?
    private var imageOrigin: CGPoint?
    private var dragLocation: CGPoint?

    var hasImage: Bool { currentImage != nil }

    private var currentMicrometersPerSourcePixel: CGFloat? {
        guard let micrometersPerPixel = measurementCalibration?.micrometersPerSourcePixel,
              micrometersPerPixel.isFinite,
              micrometersPerPixel > 0 else {
            return nil
        }
        return CGFloat(micrometersPerPixel)
    }

    var hasOverlays: Bool {
        !measurements.isEmpty || !textAnnotations.isEmpty || previewMeasurement != nil
    }

    func currentImageForCapture() -> CGImage? {
        currentImage
    }

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

    func updateMeasurementCalibration(
        _ calibration: NativeMicroscopeMeasurementCalibration?
    ) {
        measurementCalibration = calibration
        updateOverlayRendering()
    }

    func display(_ image: CGImage) {
        let dimensionsChanged = currentImage?.width != image.width ||
            currentImage?.height != image.height
        if dimensionsChanged && currentImage != nil {
            // Overlays use the source pixel grid. Clear them if that grid changes.
            clearOverlays()
        }
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
        updateOverlayRendering()
        needsDisplay = true
    }

    // MARK: - Slide tools and image-space overlays

    func toggleImageFreeze() {
        guard currentImage != nil else { return }
        isImageFrozen.toggle()
        if !isImageFrozen {
            // Resuming a moving image returns the pointer to pan mode. A
            // measurement tool must be selected again, which freezes the
            // next source frame before drawing a new measurement.
            toolMode = .pan
            measurementStart = nil
            measurementMicrometersPerPixelAtStart = nil
            previewMeasurement = nil
            updateOverlayRendering()
            window?.invalidateCursorRects(for: self)
            onOverlayStateChanged?()
        }
        onFreezeStateChanged?()
    }

    func setToolMode(_ mode: NativeSlideToolMode) {
        guard toolMode != mode else { return }
        toolMode = mode
        measurementStart = nil
        measurementMicrometersPerPixelAtStart = nil
        previewMeasurement = nil
        if mode != .pan && currentImage != nil && !isImageFrozen {
            isImageFrozen = true
            onFreezeStateChanged?()
        }
        updateOverlayRendering()
        window?.invalidateCursorRects(for: self)
    }

    func clearOverlays() {
        measurements.removeAll(keepingCapacity: false)
        textAnnotations.removeAll(keepingCapacity: false)
        measurementStart = nil
        measurementMicrometersPerPixelAtStart = nil
        previewMeasurement = nil
        updateOverlayRendering()
    }

    private func updateOverlayRendering() {
        imageContainer.updateOverlays(
            measurements: measurements,
            previewMeasurement: previewMeasurement,
            annotations: textAnnotations
        )
    }

    private func sourcePixel(at viewportPoint: CGPoint) -> CGPoint? {
        guard let image = currentImage, currentScale > 0 else { return nil }
        let origin = imageContainer.frame.origin
        let pixel = CGPoint(
            x: (viewportPoint.x - origin.x) / currentScale,
            y: (viewportPoint.y - origin.y) / currentScale
        )
        guard pixel.x >= 0,
              pixel.y >= 0,
              pixel.x <= CGFloat(image.width),
              pixel.y <= CGFloat(image.height) else {
            return nil
        }
        return pixel
    }

    private func addTextAnnotation(at sourcePoint: CGPoint) {
        let alert = NSAlert()
        alert.messageText = "Add Annotation"
        alert.informativeText = "The note will remain attached to this location in the slide image."
        alert.addButton(withTitle: "Add")
        alert.addButton(withTitle: "Cancel")

        let textField = NSTextField(frame: NSRect(x: 0, y: 0, width: 260, height: 24))
        textField.placeholderString = "Enter annotation text"
        alert.accessoryView = textField

        guard alert.runModal() == .alertFirstButtonReturn else { return }
        let text = textField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !text.isEmpty else { return }

        textAnnotations.append(
            NativeSlideTextAnnotation(location: sourcePoint, text: String(text.prefix(160)))
        )
        updateOverlayRendering()
        onOverlayStateChanged?()
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

    override func resetCursorRects() {
        let cursor: NSCursor
        switch toolMode {
        case .pan:
            cursor = .openHand
        case .distance:
            cursor = .crosshair
        case .annotation:
            cursor = .iBeam
        }
        addCursorRect(bounds, cursor: cursor)
    }

    override func mouseDown(with event: NSEvent) {
        let point = convert(event.locationInWindow, from: nil)

        switch toolMode {
        case .distance:
            guard let sourcePoint = sourcePixel(at: point) else { return }
            measurementStart = sourcePoint
            measurementMicrometersPerPixelAtStart = currentMicrometersPerSourcePixel
            previewMeasurement = NativeDistanceMeasurement(
                start: sourcePoint,
                end: sourcePoint,
                micrometersPerSourcePixel: measurementMicrometersPerPixelAtStart
            )
            updateOverlayRendering()

        case .annotation:
            guard let sourcePoint = sourcePixel(at: point) else { return }
            addTextAnnotation(at: sourcePoint)

        case .pan:
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
    }

    override func mouseDragged(with event: NSEvent) {
        let point = convert(event.locationInWindow, from: nil)

        switch toolMode {
        case .distance:
            guard let start = measurementStart,
                  let end = sourcePixel(at: point) else { return }
            previewMeasurement = NativeDistanceMeasurement(
                start: start,
                end: end,
                micrometersPerSourcePixel: measurementMicrometersPerPixelAtStart
            )
            updateOverlayRendering()

        case .annotation:
            break

        case .pan:
            guard let previousPoint = dragLocation else { return }
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
    }

    override func mouseUp(with event: NSEvent) {
        if toolMode == .distance,
           let start = measurementStart,
           let end = sourcePixel(at: convert(event.locationInWindow, from: nil)) {
            let measurement = NativeDistanceMeasurement(
                start: start,
                end: end,
                micrometersPerSourcePixel: measurementMicrometersPerPixelAtStart
            )
            if measurement.distanceInPixels >= 1 {
                measurements.append(measurement)
            }
        }
        measurementStart = nil
        measurementMicrometersPerPixelAtStart = nil
        previewMeasurement = nil
        dragLocation = nil
        updateOverlayRendering()
        onOverlayStateChanged?()
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
    private var measurements: [NativeDistanceMeasurement] = []
    private var previewMeasurement: NativeDistanceMeasurement?
    private var annotations: [NativeSlideTextAnnotation] = []

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

    func updateOverlays(
        measurements: [NativeDistanceMeasurement],
        previewMeasurement: NativeDistanceMeasurement?,
        annotations: [NativeSlideTextAnnotation]
    ) {
        self.measurements = measurements
        self.previewMeasurement = previewMeasurement
        self.annotations = annotations
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

        let scaleX = bounds.width / CGFloat(image.width)
        let scaleY = bounds.height / CGFloat(image.height)

        NSGraphicsContext.saveGraphicsState()
        for measurement in measurements {
            drawMeasurement(
                measurement,
                scaleX: scaleX,
                scaleY: scaleY,
                color: NSColor.systemYellow
            )
        }
        if let preview = previewMeasurement {
            drawMeasurement(
                preview,
                scaleX: scaleX,
                scaleY: scaleY,
                color: NSColor.systemOrange
            )
        }
        for annotation in annotations {
            drawAnnotation(annotation, scaleX: scaleX, scaleY: scaleY)
        }
        NSGraphicsContext.restoreGraphicsState()
    }

    private func drawMeasurement(
        _ measurement: NativeDistanceMeasurement,
        scaleX: CGFloat,
        scaleY: CGFloat,
        color: NSColor
    ) {
        let start = CGPoint(
            x: measurement.start.x * scaleX,
            y: measurement.start.y * scaleY
        )
        let end = CGPoint(
            x: measurement.end.x * scaleX,
            y: measurement.end.y * scaleY
        )

        let path = NSBezierPath()
        path.lineWidth = 2
        path.move(to: start)
        path.line(to: end)
        color.setStroke()
        path.stroke()

        for point in [start, end] {
            let marker = NSBezierPath(
                ovalIn: CGRect(x: point.x - 3, y: point.y - 3, width: 6, height: 6)
            )
            color.setFill()
            marker.fill()
        }

        let label: String
        if let micrometers = measurement.distanceInMicrometers {
            label = String(
                format: "%.3f µm  ·  %.1f px",
                Double(micrometers),
                Double(measurement.distanceInPixels)
            )
        } else {
            label = String(
                format: "%.1f px  ·  scale unavailable",
                Double(measurement.distanceInPixels)
            )
        }
        let attributes: [NSAttributedString.Key: Any] = [
            .font: NSFont.monospacedDigitSystemFont(ofSize: 10, weight: .semibold),
            .foregroundColor: NSColor.white
        ]
        let textSize = (label as NSString).size(withAttributes: attributes)
        let midpoint = CGPoint(
            x: (start.x + end.x) / 2 + 7,
            y: (start.y + end.y) / 2 + 5
        )
        let labelRect = CGRect(
            x: midpoint.x - 4,
            y: midpoint.y - 2,
            width: textSize.width + 8,
            height: textSize.height + 4
        )
        NSColor(calibratedWhite: 0.05, alpha: 0.82).setFill()
        NSBezierPath(roundedRect: labelRect, xRadius: 4, yRadius: 4).fill()
        (label as NSString).draw(
            at: CGPoint(x: midpoint.x, y: midpoint.y),
            withAttributes: attributes
        )
    }

    private func drawAnnotation(
        _ annotation: NativeSlideTextAnnotation,
        scaleX: CGFloat,
        scaleY: CGFloat
    ) {
        let point = CGPoint(
            x: annotation.location.x * scaleX,
            y: annotation.location.y * scaleY
        )
        let attributes: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: 11, weight: .semibold),
            .foregroundColor: NSColor.white
        ]
        let size = (annotation.text as NSString).size(withAttributes: attributes)
        let rect = CGRect(
            x: point.x + 7,
            y: point.y + 7,
            width: size.width + 10,
            height: size.height + 6
        )
        NSColor(calibratedWhite: 0.05, alpha: 0.84).setFill()
        NSBezierPath(roundedRect: rect, xRadius: 4, yRadius: 4).fill()
        (annotation.text as NSString).draw(
            at: CGPoint(x: rect.minX + 5, y: rect.minY + 3),
            withAttributes: attributes
        )

        let anchor = NSBezierPath(
            ovalIn: CGRect(x: point.x - 2, y: point.y - 2, width: 4, height: 4)
        )
        NSColor.systemYellow.setFill()
        anchor.fill()
    }
}
