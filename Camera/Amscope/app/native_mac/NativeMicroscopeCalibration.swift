import Foundation

/// Application-level optical calibration. It contains physical specifications,
/// not a camera-model lookup or a frame-resolution heuristic.
struct NativeMicroscopeMeasurementCalibration: Equatable {
    /// Physical pitch of one photosite on the camera sensor, in micrometers.
    let sensorPixelPitchMicrometers: Double

    /// Linear number of physical sensor pixels represented by one output pixel.
    /// This is 1 for native readout, 2 for 2× binning/downsampling, etc.
    /// Cropping/ROI alone does not change this factor.
    let outputPixelFactor: Double

    /// Magnification of the objective currently in the optical path.
    let objectiveMagnification: Double

    /// Effective combined magnification of the reduction/relay optics between
    /// objective and sensor (for example, a 0.5× reducer).
    let reductionLensMagnification: Double

    /// Specimen-plane micrometers represented by one pixel in the source frame.
    ///
    /// µm/source-pixel =
    ///   (sensor pixel pitch × output pixel factor)
    ///   / (objective magnification × reduction-lens magnification)
    ///
    /// This is calculated by the application from the configured component
    /// specifications. It is independent of camera brand, preview zoom,
    /// window size and display backing scale.
    var micrometersPerSourcePixel: Double? {
        guard sensorPixelPitchMicrometers.isFinite,
              sensorPixelPitchMicrometers > 0,
              outputPixelFactor.isFinite,
              outputPixelFactor > 0,
              objectiveMagnification.isFinite,
              objectiveMagnification > 0,
              reductionLensMagnification.isFinite,
              reductionLensMagnification > 0 else {
            return nil
        }

        let scale = sensorPixelPitchMicrometers * outputPixelFactor /
            (objectiveMagnification * reductionLensMagnification)
        guard scale.isFinite, scale > 0 else { return nil }
        return scale
    }
}
