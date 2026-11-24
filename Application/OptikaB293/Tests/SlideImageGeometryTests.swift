import CoreGraphics
import Foundation

@main
struct SlideImageGeometryTests {
    static func main() {
        testPhysicalPixelScale()
        testAspectPreservingFitAndNoUpscaling()
        testDocumentSizeAtPhysicalOneToOne()
        testCenteringAndPanClamping()
        testViewportToSourceCoordinateMapping()
        testSourceToDocumentOverlayMapping()
        testAnchorPreservingZoom()
        testZoomCannotExceedPhysicalOneToOne()
        testInvalidInputsAreRejected()
        testSlideDistanceMeasurement()
        print("Slide image geometry tests passed.")
    }

    private static func testPhysicalPixelScale() {
        precondition(close(SlideImageGeometry.physicalPixelScale(backingScaleFactor: 2), 0.5))
        precondition(close(SlideImageGeometry.physicalPixelScale(backingScaleFactor: 1), 1))
        precondition(close(SlideImageGeometry.physicalPixelScale(backingScaleFactor: .nan), 1))
    }

    private static func testAspectPreservingFitAndNoUpscaling() {
        let fit = SlideImageGeometry.automaticFitScale(
            imageSize: CGSize(width: 4000, height: 2000),
            viewportSize: CGSize(width: 1000, height: 800),
            physicalPixelScale: 0.5
        )
        precondition(close(fit, 0.25))
        let noUpscale = SlideImageGeometry.automaticFitScale(
            imageSize: CGSize(width: 100, height: 50),
            viewportSize: CGSize(width: 1000, height: 800),
            physicalPixelScale: 0.5
        )
        precondition(close(noUpscale, 0.5))
    }

    private static func testDocumentSizeAtPhysicalOneToOne() {
        let size = SlideImageGeometry.documentSize(
            imageSize: CGSize(width: 4000, height: 2000),
            requestedScale: 0.5,
            physicalPixelScale: 0.5
        )
        precondition(size == CGSize(width: 2000, height: 1000))
    }

    private static func testCenteringAndPanClamping() {
        let centered = SlideImageGeometry.centeredOrigin(
            viewportSize: CGSize(width: 100, height: 80),
            documentSize: CGSize(width: 50, height: 40)
        )
        precondition(centered == CGPoint(x: 25, y: 20))
        let leading = SlideImageGeometry.clampedOrigin(
            CGPoint(x: 30, y: 20),
            viewportSize: CGSize(width: 100, height: 80),
            documentSize: CGSize(width: 400, height: 200)
        )
        precondition(leading == .zero)
        let trailing = SlideImageGeometry.clampedOrigin(
            CGPoint(x: -500, y: -200),
            viewportSize: CGSize(width: 100, height: 80),
            documentSize: CGSize(width: 400, height: 200)
        )
        precondition(trailing == CGPoint(x: -300, y: -120))
    }

    private static func testViewportToSourceCoordinateMapping() {
        let source = SlideImageGeometry.sourcePoint(
            at: CGPoint(x: 112, y: 70),
            imageOrigin: CGPoint(x: 12, y: 20),
            imageScale: 0.5,
            imageSize: CGSize(width: 1000, height: 500)
        )
        precondition(source == CGPoint(x: 200, y: 100))
        let outside = SlideImageGeometry.sourcePoint(
            at: CGPoint(x: 11, y: 70),
            imageOrigin: CGPoint(x: 12, y: 20),
            imageScale: 0.5,
            imageSize: CGSize(width: 1000, height: 500)
        )
        precondition(outside == nil)
        let edge = SlideImageGeometry.sourcePoint(
            at: CGPoint(x: 512, y: 270),
            imageOrigin: CGPoint(x: 12, y: 20),
            imageScale: 0.5,
            imageSize: CGSize(width: 1000, height: 500)
        )
        precondition(edge == CGPoint(x: 1000, y: 500))
    }

    private static func testSourceToDocumentOverlayMapping() {
        guard let scale = SlideImageGeometry.coordinateScale(
            imageSize: CGSize(width: 1000, height: 500),
            documentSize: CGSize(width: 500, height: 250)
        ) else {
            preconditionFailure("A non-empty image/document pair needs a coordinate scale.")
        }
        let documentPoint = scale.documentPoint(fromSourcePoint: CGPoint(x: 400, y: 200))
        precondition(documentPoint == CGPoint(x: 200, y: 100))
        precondition(scale.sourcePoint(fromDocumentPoint: documentPoint) == CGPoint(x: 400, y: 200))
    }

    private static func testAnchorPreservingZoom() {
        let imageSize = CGSize(width: 1000, height: 500)
        let viewportSize = CGSize(width: 100, height: 50)
        let anchor = CGPoint(x: 45, y: 22.5)
        let oldOrigin = CGPoint(x: 25, y: 12.5)
        guard let expectedSource = SlideImageGeometry.sourcePoint(
            at: anchor,
            imageOrigin: oldOrigin,
            imageScale: 0.05,
            imageSize: imageSize
        ) else {
            preconditionFailure("The initial anchor must map inside the source image.")
        }
        guard let transform = SlideImageGeometry.zoomTransform(
            imageSize: imageSize,
            viewportSize: viewportSize,
            currentScale: 0.05,
            currentOrigin: oldOrigin,
            anchor: anchor,
            delta: CGFloat(Foundation.log2(2.4)),
            minimumScale: 0.05,
            maximumScale: 0.2
        ) else {
            preconditionFailure("Zoom should produce a changed transform.")
        }
        precondition(close(transform.scale, 0.12))
        guard let newSource = SlideImageGeometry.sourcePoint(
            at: anchor,
            imageOrigin: transform.origin,
            imageScale: transform.scale,
            imageSize: imageSize
        ) else {
            preconditionFailure("The zoom anchor must remain inside the source image.")
        }
        precondition(close(newSource.x, expectedSource.x))
        precondition(close(newSource.y, expectedSource.y))
    }

    private static func testZoomCannotExceedPhysicalOneToOne() {
        guard let transform = SlideImageGeometry.zoomTransform(
            imageSize: CGSize(width: 1000, height: 500),
            viewportSize: CGSize(width: 100, height: 50),
            currentScale: 0.1,
            currentOrigin: .zero,
            anchor: CGPoint(x: 50, y: 25),
            delta: 8,
            minimumScale: 0.1,
            maximumScale: 0.5
        ) else {
            preconditionFailure("A larger zoom request should clamp to physical 1:1.")
        }
        precondition(close(transform.scale, 0.5))
    }

    private static func testInvalidInputsAreRejected() {
        precondition(SlideImageGeometry.coordinateScale(
            imageSize: .zero,
            documentSize: CGSize(width: 20, height: 20)
        ) == nil)
        precondition(SlideImageGeometry.sourcePoint(
            at: .zero,
            imageOrigin: .zero,
            imageScale: 0,
            imageSize: CGSize(width: 10, height: 10)
        ) == nil)
        precondition(SlideImageGeometry.zoomTransform(
            imageSize: CGSize(width: 10, height: 10),
            viewportSize: CGSize(width: 10, height: 10),
            currentScale: 0,
            currentOrigin: .zero,
            anchor: .zero,
            delta: 1,
            minimumScale: 0,
            maximumScale: 1
        ) == nil)
    }

    private static func testSlideDistanceMeasurement() {
        let calibrated = SlideDistanceMeasurement(
            start: CGPoint(x: 10, y: 10),
            end: CGPoint(x: 13, y: 14),
            micrometersPerSourcePixel: 0.5
        )
        precondition(close(calibrated.distanceInPixels, 5))
        precondition(close(calibrated.distanceInMicrometers ?? -1, 2.5))

        let uncalibrated = SlideDistanceMeasurement(
            start: .zero,
            end: CGPoint(x: 3, y: 4),
            micrometersPerSourcePixel: nil
        )
        precondition(close(uncalibrated.distanceInPixels, 5))
        precondition(uncalibrated.distanceInMicrometers == nil)

        let invalidCalibration = SlideDistanceMeasurement(
            start: .zero,
            end: CGPoint(x: 3, y: 4),
            micrometersPerSourcePixel: .nan
        )
        precondition(invalidCalibration.distanceInMicrometers == nil)

        let annotation = SlideTextAnnotation(
            location: CGPoint(x: 150, y: 80),
            text: "Cell nucleus"
        )
        precondition(annotation.location == CGPoint(x: 150, y: 80))
        precondition(annotation.text == "Cell nucleus")
    }

    private static func close(
        _ lhs: CGFloat,
        _ rhs: CGFloat,
        tolerance: CGFloat = 0.000001
    ) -> Bool {
        abs(lhs - rhs) <= tolerance
    }
}
