import CoreGraphics
import Foundation

struct SlideImageViewportTransform: Equatable {
    let scale: CGFloat
    let origin: CGPoint
}

struct SlideImageCoordinateScale: Equatable {
    let horizontal: CGFloat
    let vertical: CGFloat

    func documentPoint(fromSourcePoint point: CGPoint) -> CGPoint {
        CGPoint(x: point.x * horizontal, y: point.y * vertical)
    }

    func sourcePoint(fromDocumentPoint point: CGPoint) -> CGPoint? {
        guard horizontal.isFinite, horizontal > 0,
              vertical.isFinite, vertical > 0,
              point.x.isFinite, point.y.isFinite else { return nil }
        return CGPoint(x: point.x / horizontal, y: point.y / vertical)
    }
}

/// Deterministic mapping between source-image, document and viewport spaces.
enum SlideImageGeometry {
    static let scaleTolerance: CGFloat = 0.000001

    /// AppKit points per physical display pixel.
    static func physicalPixelScale(backingScaleFactor: CGFloat) -> CGFloat {
        let validBackingScale = backingScaleFactor.isFinite && backingScaleFactor > 0
            ? backingScaleFactor
            : 1
        return 1 / max(validBackingScale, 1)
    }

    /// Largest aspect-preserving scale that fits the full image without
    /// upscaling beyond one source pixel per physical display pixel.
    static func automaticFitScale(
        imageSize: CGSize,
        viewportSize: CGSize,
        physicalPixelScale: CGFloat
    ) -> CGFloat {
        guard isValidImageSize(imageSize),
              isValidViewportSize(viewportSize),
              isValidScale(physicalPixelScale) else {
            return physicalPixelScale
        }
        let widthScale = viewportSize.width / imageSize.width
        let heightScale = viewportSize.height / imageSize.height
        return min(min(widthScale, heightScale), physicalPixelScale)
    }

    /// The viewport clips the image document; it does not resize that document.
    static func documentSize(
        imageSize: CGSize,
        requestedScale: CGFloat,
        physicalPixelScale: CGFloat
    ) -> CGSize {
        guard isValidImageSize(imageSize),
              isValidScale(physicalPixelScale),
              requestedScale.isFinite else { return .zero }
        let boundedScale = min(max(requestedScale, 0), physicalPixelScale)
        return CGSize(
            width: imageSize.width * boundedScale,
            height: imageSize.height * boundedScale
        )
    }

    static func centeredOrigin(viewportSize: CGSize, documentSize: CGSize) -> CGPoint {
        CGPoint(
            x: (viewportSize.width - documentSize.width) / 2,
            y: (viewportSize.height - documentSize.height) / 2
        )
    }

    /// Centers small dimensions and clamps larger ones so blank space cannot
    /// be exposed beyond the source document.
    static func clampedOrigin(
        _ proposedOrigin: CGPoint,
        viewportSize: CGSize,
        documentSize: CGSize
    ) -> CGPoint {
        let safeOrigin = proposedOrigin.x.isFinite && proposedOrigin.y.isFinite
            ? proposedOrigin
            : centeredOrigin(viewportSize: viewportSize, documentSize: documentSize)
        let x = documentSize.width <= viewportSize.width
            ? (viewportSize.width - documentSize.width) / 2
            : min(0, max(viewportSize.width - documentSize.width, safeOrigin.x))
        let y = documentSize.height <= viewportSize.height
            ? (viewportSize.height - documentSize.height) / 2
            : min(0, max(viewportSize.height - documentSize.height, safeOrigin.y))
        return CGPoint(x: x, y: y)
    }

    /// The maximum image edge remains inclusive, preserving current hit testing.
    static func sourcePoint(
        at viewportPoint: CGPoint,
        imageOrigin: CGPoint,
        imageScale: CGFloat,
        imageSize: CGSize
    ) -> CGPoint? {
        guard isValidImageSize(imageSize),
              isValidScale(imageScale),
              viewportPoint.x.isFinite, viewportPoint.y.isFinite,
              imageOrigin.x.isFinite, imageOrigin.y.isFinite else {
            return nil
        }
        let point = CGPoint(
            x: (viewportPoint.x - imageOrigin.x) / imageScale,
            y: (viewportPoint.y - imageOrigin.y) / imageScale
        )
        guard point.x >= 0, point.y >= 0,
              point.x <= imageSize.width,
              point.y <= imageSize.height else { return nil }
        return point
    }

    /// Source-to-document overlay scale. Independent axes preserve correctness
    /// if a future presentation path rounds/resamples one dimension differently.
    static func coordinateScale(
        imageSize: CGSize,
        documentSize: CGSize
    ) -> SlideImageCoordinateScale? {
        guard isValidImageSize(imageSize),
              isValidImageSize(documentSize) else { return nil }
        return SlideImageCoordinateScale(
            horizontal: documentSize.width / imageSize.width,
            vertical: documentSize.height / imageSize.height
        )
    }

    /// Calculates pointer-anchored zoom. Pan-bound clamping takes precedence
    /// when the anchor cannot remain fixed without exposing blank viewport.
    static func zoomTransform(
        imageSize: CGSize,
        viewportSize: CGSize,
        currentScale: CGFloat,
        currentOrigin: CGPoint,
        anchor: CGPoint,
        delta: CGFloat,
        minimumScale: CGFloat,
        maximumScale: CGFloat
    ) -> SlideImageViewportTransform? {
        guard isValidImageSize(imageSize),
              isValidViewportSize(viewportSize),
              isValidScale(currentScale),
              isValidScale(maximumScale),
              minimumScale.isFinite, minimumScale >= 0,
              delta.isFinite, delta != 0,
              currentOrigin.x.isFinite, currentOrigin.y.isFinite,
              anchor.x.isFinite, anchor.y.isFinite else {
            return nil
        }

        let lowerBound = min(max(minimumScale, 0), maximumScale)
        let factor = CGFloat(Foundation.pow(2.0, Double(delta)))
        let candidate = currentScale * factor
        let finiteCandidate = candidate.isFinite
            ? candidate
            : (delta > 0 ? maximumScale : lowerBound)
        let nextScale = min(max(finiteCandidate, lowerBound), maximumScale)
        guard abs(nextScale - currentScale) > scaleTolerance else { return nil }

        let sourceAnchor = CGPoint(
            x: (anchor.x - currentOrigin.x) / currentScale,
            y: (anchor.y - currentOrigin.y) / currentScale
        )
        let proposedOrigin = CGPoint(
            x: anchor.x - sourceAnchor.x * nextScale,
            y: anchor.y - sourceAnchor.y * nextScale
        )
        let nextDocumentSize = documentSize(
            imageSize: imageSize,
            requestedScale: nextScale,
            physicalPixelScale: maximumScale
        )
        let boundedOrigin = clampedOrigin(
            proposedOrigin,
            viewportSize: viewportSize,
            documentSize: nextDocumentSize
        )
        return SlideImageViewportTransform(scale: nextScale, origin: boundedOrigin)
    }

    private static func isValidImageSize(_ size: CGSize) -> Bool {
        size.width.isFinite && size.width > 0 &&
        size.height.isFinite && size.height > 0
    }

    private static func isValidViewportSize(_ size: CGSize) -> Bool {
        size.width.isFinite && size.width > 0 &&
        size.height.isFinite && size.height > 0
    }

    private static func isValidScale(_ scale: CGFloat) -> Bool {
        scale.isFinite && scale > 0
    }
}
