import AppKit
import SwiftUI

struct NativeDualPreviewSurface: NSViewRepresentable {
    let bridge: AmscopeNativeCameraBridge
    let model: NativeCameraModel
    let workspace: NativeWorkspaceModel
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
            workspace: workspace,
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
            workspace: workspace,
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
        workspace: NativeWorkspaceModel,
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
        slideOnePane.updateMicroscopeControls(model, workspace: workspace)
        slideTwoPane.updateMicroscopeControls(model, workspace: workspace)
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

