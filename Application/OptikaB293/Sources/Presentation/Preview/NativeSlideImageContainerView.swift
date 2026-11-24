import AppKit

final class NativeSlideImageContainerView: NSView {
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

        let physicalScale = SlideImageGeometry.physicalPixelScale(
            backingScaleFactor: window?.backingScaleFactor ?? 1
        )
        let displayedScale = bounds.width / max(CGFloat(image.width), 1)
        context.interpolationQuality =
            displayedScale < physicalScale * 0.999 ? .high : .none
        context.draw(
            image,
            in: CGRect(origin: .zero, size: bounds.size)
        )

        guard let coordinateScale = SlideImageGeometry.coordinateScale(
            imageSize: CGSize(width: image.width, height: image.height),
            documentSize: bounds.size
        ) else {
            return
        }

        NSGraphicsContext.saveGraphicsState()
        for measurement in measurements {
            drawMeasurement(
                measurement,
                coordinateScale: coordinateScale,
                color: OptikaPalette.nsMeasurement
            )
        }
        if let preview = previewMeasurement {
            drawMeasurement(
                preview,
                coordinateScale: coordinateScale,
                color: OptikaPalette.nsWarning
            )
        }
        for annotation in annotations {
            drawAnnotation(annotation, coordinateScale: coordinateScale)
        }
        NSGraphicsContext.restoreGraphicsState()
    }

    private func drawMeasurement(
        _ measurement: NativeDistanceMeasurement,
        coordinateScale: SlideImageCoordinateScale,
        color: NSColor
    ) {
        let start = coordinateScale.documentPoint(fromSourcePoint: measurement.start)
        let end = coordinateScale.documentPoint(fromSourcePoint: measurement.end)

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
            .foregroundColor: OptikaPalette.nsOverlayText
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
        OptikaPalette.nsMeasurementLabelBackground.setFill()
        NSBezierPath(roundedRect: labelRect, xRadius: 4, yRadius: 4).fill()
        (label as NSString).draw(
            at: CGPoint(x: midpoint.x, y: midpoint.y),
            withAttributes: attributes
        )
    }

    private func drawAnnotation(
        _ annotation: NativeSlideTextAnnotation,
        coordinateScale: SlideImageCoordinateScale
    ) {
        let point = coordinateScale.documentPoint(fromSourcePoint: annotation.location)
        let attributes: [NSAttributedString.Key: Any] = [
            .font: NSFont.systemFont(ofSize: 11, weight: .semibold),
            .foregroundColor: OptikaPalette.nsOverlayText
        ]
        let size = (annotation.text as NSString).size(withAttributes: attributes)
        let rect = CGRect(
            x: point.x + 7,
            y: point.y + 7,
            width: size.width + 10,
            height: size.height + 6
        )
        OptikaPalette.nsAnnotationLabelBackground.setFill()
        NSBezierPath(roundedRect: rect, xRadius: 4, yRadius: 4).fill()
        (annotation.text as NSString).draw(
            at: CGPoint(x: rect.minX + 5, y: rect.minY + 3),
            withAttributes: attributes
        )

        let anchor = NSBezierPath(
            ovalIn: CGRect(x: point.x - 2, y: point.y - 2, width: 4, height: 4)
        )
        OptikaPalette.nsMeasurement.setFill()
        anchor.fill()
    }
}

