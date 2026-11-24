import AppKit
import SwiftUI

final class NativeSlidePreviewPane: NSView {
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
        titleLabel.textColor = OptikaPalette.nsPrimaryText
        wantsLayer = true
        layer?.cornerRadius = 4
        layer?.masksToBounds = true
        layer?.backgroundColor = OptikaPalette.nsClear.cgColor
        layer?.isOpaque = false

        statusLabel.font = NSFont.systemFont(ofSize: 9, weight: .bold)
        statusLabel.textColor = OptikaPalette.nsSecondaryText
        statusLabel.textColor = OptikaPalette.nsSecondaryText
        frameStatisticsLabel.font = NSFont.monospacedDigitSystemFont(ofSize: 8, weight: .medium)
        frameStatisticsLabel.textColor = OptikaPalette.nsSecondaryText
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
        plate.layer?.backgroundColor = OptikaPalette.nsSurface
            .withAlphaComponent(OptikaPalette.floatingPanelAlpha).cgColor
        plate.layer?.cornerRadius = 7
        plate.layer?.borderWidth = 1
        plate.layer?.borderColor = OptikaPalette.nsSeparator
            .withAlphaComponent(OptikaPalette.subtleBorderAlpha).cgColor
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

    func updateMicroscopeControls(_ model: NativeCameraModel, workspace: NativeWorkspaceModel, microscope: MicroscopeConfigurationModel) {
        if let host = microscopeControlsHost {
            host.rootView = NativeMicroscopePaneControls(model: model, workspace: workspace, microscope: microscope, slide: slide)
            return
        }

        let host = NSHostingView(
            rootView: NativeMicroscopePaneControls(model: model, workspace: workspace, slide: slide)
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
        microscopeControlsPlate.isHidden = bounds.width < OptikaLayout.microscopeControlsMinimumWidth
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
                ? OptikaPalette.nsWarning
                : OptikaPalette.nsLive
        } else {
            statusLabel.stringValue = imageView.hasImage
                ? (imageView.isImageFrozen ? "FROZEN IMAGE" : "HELD IMAGE")
                : "STANDBY"
            statusLabel.textColor = imageView.isImageFrozen
                ? OptikaPalette.nsWarning
                : OptikaPalette.nsSecondaryText
        }
        layer?.borderWidth = 1
        layer?.borderColor = isActiveTarget
            ? OptikaPalette.nsLive.cgColor
            : OptikaPalette.nsSeparator.cgColor
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

