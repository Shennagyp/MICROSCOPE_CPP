import AppKit

enum NativeSlideImageSizingMode: Equatable {
    /// Recalculate image scale whenever this pane's viewport changes.
    case resizable
    /// Keep the selected display scale independent of viewport resizing.
    case lockedScale
    /// Map every source pixel to one physical display pixel.
    case fullResolution
}


final class NativeSlideImageView: NSView {
    private let slideTitle: String
    private let imageContainer = NativeSlideImageContainerView()
    private var currentImage: CGImage?
    private(set) var toolMode: NativeSlideToolMode = .pan
    private(set) var isImageFrozen = false
    private var measurements: [SlideDistanceMeasurement] = []
    private var previewMeasurement: SlideDistanceMeasurement?
    private var textAnnotations: [SlideTextAnnotation] = []
    private var measurementStart: CGPoint?
    private var measurementMicrometersPerPixelAtStart: CGFloat?
    private var measurementCalibration: NativeMicroscopeMeasurementCalibration?

    private(set) var sizingMode: NativeSlideImageSizingMode = .resizable
    var onSizingModeChanged: (() -> Void)?
    var onOverlayStateChanged: (() -> Void)?
    var onFreezeStateChanged: (() -> Void)?

    private static let buttonZoomDelta = CGFloat(Foundation.log2(1.25))

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
        toolTip = "Pinch or scroll to zoom · Ctrl +/− to zoom · Floating controls reset or adjust zoom · Drag to pan · 1:1 shows full resolution"
    }

    required init?(coder: NSCoder) {
        fatalError("NativeSlideImageView does not support NSCoder initialization")
    }

    override var acceptsFirstResponder: Bool { true }

    override func acceptsFirstMouse(for event: NSEvent?) -> Bool {
        true
    }

    override func keyDown(with event: NSEvent) {
        let modifiers = event.modifierFlags.intersection(.deviceIndependentFlagsMask)
        guard modifiers.contains(.control),
              let key = event.charactersIgnoringModifiers?.lowercased() else {
            super.keyDown(with: event)
            return
        }

        switch key {
        case "+", "=":
            zoomIn()
        case "-", "_":
            zoomOut()
        case "0":
            resetZoom()
        default:
            super.keyDown(with: event)
        }
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
        imageContainer.setImage(image)

        if dimensionsChanged {
            // The source document changed. Recompute its geometry once while
            // preserving the selected sizing mode and its scale policy.
            updateImageContainer(scale: currentScale, origin: nil)
        } else {
            // Normal live frames keep the same document geometry and overlays.
            // Replace only the CGImage reference and invalidate the image surface.
            imageContainer.needsDisplay = true
        }
    }

    // MARK: - Image scale and container bounds

    /// AppKit points per camera pixel for one physical display pixel.
    private var actualPixelScale: CGFloat {
        SlideImageGeometry.physicalPixelScale(
            backingScaleFactor: window?.backingScaleFactor ?? 1
        )
    }

    /// Maximum display scale is bounded to eight times physical 1:1.
    private var maximumZoomScale: CGFloat {
        SlideImageGeometry.maximumZoomScale(physicalPixelScale: actualPixelScale)
    }

    /// Largest aspect-preserving scale that keeps the complete source image
    /// inside the viewer viewport. Diagnostic content is never cropped just
    /// to eliminate margins. Upscaling beyond native 1:1 is not permitted.
    private var automaticFitScale: CGFloat {
        guard let image = currentImage else { return actualPixelScale }
        return SlideImageGeometry.automaticFitScale(
            imageSize: CGSize(width: CGFloat(image.width), height: CGFloat(image.height)),
            viewportSize: bounds.size,
            physicalPixelScale: actualPixelScale
        )
    }

    private var currentScale: CGFloat {
        switch sizingMode {
        case .resizable:
            return automaticFitScale
        case .lockedScale:
            return min(lockedScale ?? automaticFitScale, maximumZoomScale)
        case .fullResolution:
            return actualPixelScale
        }
    }

    /// Document dimensions at the requested display scale.
    private func imageContainerSize(at scale: CGFloat) -> CGSize {
        guard let image = currentImage else { return .zero }
        return SlideImageGeometry.documentSize(
            imageSize: CGSize(width: CGFloat(image.width), height: CGFloat(image.height)),
            requestedScale: scale,
            maximumScale: maximumZoomScale
        )
    }

    private func updateImageContainer(scale: CGFloat, origin: CGPoint?) {
        guard currentImage != nil else { return }
        let size = imageContainerSize(at: scale)
        let proposedOrigin = origin ?? SlideImageGeometry.centeredOrigin(
            viewportSize: bounds.size,
            documentSize: size
        )
        let boundedOrigin = SlideImageGeometry.clampedOrigin(
            proposedOrigin,
            viewportSize: bounds.size,
            documentSize: size
        )

        imageContainer.frame = CGRect(
            origin: boundedOrigin,
            size: size
        )
        imageContainer.needsDisplay = true
        imageOrigin = boundedOrigin
        needsDisplay = true
    }

    // MARK: - Slide tools and image-space overlays

    func toggleImageFreeze() {
        guard currentImage != nil else { return }
        isImageFrozen.toggle()
        imageContainer.setHighQualityInterpolation(isImageFrozen)
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
            imageContainer.setHighQualityInterpolation(true)
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
        guard let image = currentImage else { return nil }
        return SlideImageGeometry.sourcePoint(
            at: viewportPoint,
            imageOrigin: imageContainer.frame.origin,
            imageScale: currentScale,
            imageSize: CGSize(width: CGFloat(image.width), height: CGFloat(image.height))
        )
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
            SlideTextAnnotation(location: sourcePoint, text: String(text.prefix(160)))
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
        guard let image = currentImage else { return }

        let maximumScale = maximumZoomScale
        let minimumScale = min(automaticFitScale, maximumScale)
        let oldScale = currentScale
        let oldOrigin = imageOrigin ?? SlideImageGeometry.centeredOrigin(
            viewportSize: bounds.size,
            documentSize: imageContainerSize(at: oldScale)
        )

        guard let transform = SlideImageGeometry.zoomTransform(
            imageSize: CGSize(width: CGFloat(image.width), height: CGFloat(image.height)),
            viewportSize: bounds.size,
            currentScale: oldScale,
            currentOrigin: oldOrigin,
            anchor: anchor,
            delta: delta,
            minimumScale: minimumScale,
            maximumScale: maximumScale
        ) else {
            return
        }

        // AppKit owns the sizing-mode state machine; deterministic geometry
        // is delegated to the functional core.
        let nextMode: NativeSlideImageSizingMode =
            abs(transform.scale - actualPixelScale) < SlideImageGeometry.scaleTolerance
                ? .fullResolution
                : .lockedScale
        if sizingMode != nextMode {
            sizingMode = nextMode
            onSizingModeChanged?()
        }
        lockedScale = nextMode == .lockedScale ? transform.scale : nil
        updateImageContainer(scale: transform.scale, origin: transform.origin)
    }

    func zoomIn() {
        zoom(
            by: Self.buttonZoomDelta,
            anchoredAt: CGPoint(x: bounds.midX, y: bounds.midY)
        )
    }

    func zoomOut() {
        zoom(
            by: -Self.buttonZoomDelta,
            anchoredAt: CGPoint(x: bounds.midX, y: bounds.midY)
        )
    }

    func resetZoom() {
        guard currentImage != nil else { return }
        let modeChanged = sizingMode != .resizable
        sizingMode = .resizable
        lockedScale = nil
        updateImageContainer(scale: automaticFitScale, origin: nil)
        if modeChanged {
            onSizingModeChanged?()
        }
    }

    // MARK: - Pointer interaction and panning

    override func scrollWheel(with event: NSEvent) {
        guard currentImage != nil else {
            super.scrollWheel(with: event)
            return
        }

        window?.makeFirstResponder(self)
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

        window?.makeFirstResponder(self)
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
        window?.makeFirstResponder(self)
        let point = convert(event.locationInWindow, from: nil)

        switch toolMode {
        case .distance:
            guard let sourcePoint = sourcePixel(at: point) else { return }
            measurementStart = sourcePoint
            measurementMicrometersPerPixelAtStart = currentMicrometersPerSourcePixel
            previewMeasurement = SlideDistanceMeasurement(
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
            previewMeasurement = SlideDistanceMeasurement(
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
            let measurement = SlideDistanceMeasurement(
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
        OptikaPalette.nsPreviewBackground.setFill()
        dirtyRect.fill()
    }
}

