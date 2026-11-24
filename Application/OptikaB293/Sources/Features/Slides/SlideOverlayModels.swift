import CoreGraphics
import Foundation

struct SlideDistanceMeasurement: Equatable {
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

struct SlideTextAnnotation: Equatable {
    let location: CGPoint
    let text: String
}
