import CoreGraphics
import Foundation

enum NativeSlideToolMode: Equatable {
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
