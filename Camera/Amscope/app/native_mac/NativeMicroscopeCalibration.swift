import Foundation

/// Sensor geometry known to the application. Unknown/custom cameras must have
/// their pixel pitch and current output binning specified in microscope setup.
enum NativeCameraSensorPreset: String, Codable, CaseIterable, Identifiable {
    case amscopeMU503
    case custom

    var id: String { rawValue }

    var title: String {
        switch self {
        case .amscopeMU503:
            return "AmScope MU503 · Aptina MT9P006"
        case .custom:
            return "Custom camera sensor"
        }
    }

    var nominalPixelPitchMicrometers: Double? {
        switch self {
        case .amscopeMU503: return 2.2
        case .custom: return nil
        }
    }

    /// Returns the physical sensor-pixel grouping represented by one output
    /// pixel for a reported camera frame size. nil means the mode is not
    /// described by this sensor profile and must not be assigned a scale.
    func binningFactor(
        imageWidth: Int,
        imageHeight: Int,
        customBinningFactor: Double
    ) -> Double? {
        guard imageWidth > 0, imageHeight > 0 else { return nil }

        switch self {
        case .amscopeMU503:
            // AmScope publishes these MU503 output modes and 1x1, 2x2 and 4x4
            // binning support. Only exact documented output modes are mapped.
            switch (imageWidth, imageHeight) {
            case (2560, 1922): return 1
            case (1280, 960): return 2
            case (640, 480): return 4
            default: return nil
            }

        case .custom:
            guard customBinningFactor.isFinite,
                  customBinningFactor > 0 else {
                return nil
            }
            return customBinningFactor
        }
    }
}

/// Converts source-image pixel distances to specimen-plane micrometers.
///
/// Formula:
/// specimen µm / output pixel =
///     sensor pixel pitch µm × output binning factor
///     / (objective magnification × reduction-lens magnification)
///
/// This is an optical estimate from configured component specifications.
/// A stage-micrometer calibration can later replace/refine it if the installed
/// optical path has tolerances or additional unmodeled elements.
struct NativeMicroscopeMeasurementCalibration: Equatable {
    let sensorPixelPitchMicrometers: Double
    let objectiveMagnification: Double
    let reductionLensMagnification: Double
    let sensorPreset: NativeCameraSensorPreset
    let customBinningFactor: Double

    func micrometersPerSourcePixel(
        imageWidth: Int,
        imageHeight: Int
    ) -> Double? {
        guard sensorPixelPitchMicrometers.isFinite,
              sensorPixelPitchMicrometers > 0,
              objectiveMagnification.isFinite,
              objectiveMagnification > 0,
              reductionLensMagnification.isFinite,
              reductionLensMagnification > 0,
              let binning = sensorPreset.binningFactor(
                imageWidth: imageWidth,
                imageHeight: imageHeight,
                customBinningFactor: customBinningFactor
              ) else {
            return nil
        }

        let scale = sensorPixelPitchMicrometers * binning /
            (objectiveMagnification * reductionLensMagnification)
        guard scale.isFinite, scale > 0 else { return nil }
        return scale
    }
}
