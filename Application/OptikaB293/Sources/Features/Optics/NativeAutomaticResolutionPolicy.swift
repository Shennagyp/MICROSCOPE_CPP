import Foundation

private struct NativeCameraResolutionMode {
    let symbol: String
    let width: Int
    let height: Int
    var pixelCount: Int { width * height }

    init?(symbol: String) {
        guard let separator = symbol.firstIndex(of: ":") else { return nil }
        let dimensions = symbol[symbol.index(after: separator)...].lowercased().split(separator: "x")
        guard dimensions.count == 2,
              let width = Int(dimensions[0]),
              let height = Int(dimensions[1]),
              width > 0, height > 0 else { return nil }
        self.symbol = symbol
        self.width = width
        self.height = height
    }
}

struct NativeAutomaticResolutionChoice {
    let symbol: String
    let width: Int
    let height: Int
    let pixelCount: Int
    let minimumBandSamplingFactor: Double
    let maximumBandSamplingFactor: Double
    let meetsTarget: Bool
}

enum NativeAutomaticResolutionPolicy {
    static func choose(
        modeSymbols: [String],
        configuration: MicroscopeOpticalConfiguration
    ) -> NativeAutomaticResolutionChoice? {
        guard let objective = configuration.objectives.first(where: {
            $0.id == configuration.activeObjectiveID
        }) else { return nil }
        guard objective.magnification.isFinite && objective.magnification > 0,
              objective.numericalAperture.isFinite && objective.numericalAperture > 0,
              configuration.cameraPixelPitchMicrometers.isFinite && configuration.cameraPixelPitchMicrometers > 0,
              configuration.cameraRelayMagnification.isFinite && configuration.cameraRelayMagnification > 0,
              configuration.chromatinBandLowerNanometers.isFinite && configuration.chromatinBandLowerNanometers > 0,
              configuration.chromatinBandUpperNanometers.isFinite &&
                configuration.chromatinBandUpperNanometers >= configuration.chromatinBandLowerNanometers,
              configuration.targetRayleighSamplingFactor.isFinite && configuration.targetRayleighSamplingFactor > 0 else {
            return nil
        }

        let modes = modeSymbols.compactMap(NativeCameraResolutionMode.init(symbol:))
        guard !modes.isEmpty,
              let maximumMode = modes.max(by: { $0.pixelCount < $1.pixelCount }) else { return nil }

        let lowerRayleigh = 0.61 * (configuration.chromatinBandLowerNanometers / 1000.0) / objective.numericalAperture
        let upperRayleigh = 0.61 * (configuration.chromatinBandUpperNanometers / 1000.0) / objective.numericalAperture
        let candidates = modes.map { mode -> NativeAutomaticResolutionChoice in
            // Preserve the former provisional full-field readout interpretation.
            let scaleX = Double(maximumMode.width) / Double(mode.width)
            let scaleY = Double(maximumMode.height) / Double(mode.height)
            let samplingScale = max(scaleX, scaleY)
            let objectPixelPitch = configuration.cameraPixelPitchMicrometers * samplingScale /
                (objective.magnification * configuration.cameraRelayMagnification)
            let minimumFactor = lowerRayleigh / objectPixelPitch
            let maximumFactor = upperRayleigh / objectPixelPitch
            return NativeAutomaticResolutionChoice(
                symbol: mode.symbol,
                width: mode.width,
                height: mode.height,
                pixelCount: mode.pixelCount,
                minimumBandSamplingFactor: minimumFactor,
                maximumBandSamplingFactor: maximumFactor,
                meetsTarget: minimumFactor >= configuration.targetRayleighSamplingFactor
            )
        }
        return candidates.filter(\.meetsTarget).min(by: { $0.pixelCount < $1.pixelCount })
            ?? candidates.max(by: { $0.pixelCount < $1.pixelCount })
    }
}
