import Foundation

struct MicroscopeObjective: Codable, Equatable, Identifiable {
    var id: String
    var name: String
    var magnification: Double
    var numericalAperture: Double
    var immersion: String

    var displayName: String {
        String(format: "%.0f× · NA %.2f", magnification, numericalAperture)
    }
}

enum CameraRelayPreset: String, Codable, CaseIterable, Identifiable, Hashable {
    case touptekFMA050
    case optikaM115
    case optikaM114
    case custom

    var id: String { rawValue }

    var title: String {
        switch self {
        case .touptekFMA050: return "ToupTek FMA050 (0.50×)"
        case .optikaM115: return "OPTIKA M-115 (0.35×)"
        case .optikaM114: return "OPTIKA M-114 (0.50×)"
        case .custom: return "Custom / combined path"
        }
    }

    var nominalMagnification: Double? {
        switch self {
        case .touptekFMA050, .optikaM114: return 0.5
        case .optikaM115: return 0.35
        case .custom: return nil
        }
    }
}

struct MicroscopeOpticalConfiguration: Codable, Equatable {
    private enum CodingKeys: String, CodingKey {
        case microscopeName
        case objectiveSeries
        case opticalTubeSystem
        case condenserNumericalAperture
        case cameraPixelPitchMicrometers
        case cameraOutputPixelFactor
        case cameraRelayPreset
        case cameraRelayCustomName
        case cameraRelayMagnification
        case chromatinBandLowerNanometers
        case chromatinBandUpperNanometers
        case targetRayleighSamplingFactor
        case objectives
        case activeObjectiveID
    }

    var microscopeName: String
    var objectiveSeries: String
    var opticalTubeSystem: String
    var condenserNumericalAperture: Double
    var cameraPixelPitchMicrometers: Double
    var cameraOutputPixelFactor: Double
    var cameraRelayPreset: CameraRelayPreset
    var cameraRelayCustomName: String
    var cameraRelayMagnification: Double
    var chromatinBandLowerNanometers: Double
    var chromatinBandUpperNanometers: Double
    var targetRayleighSamplingFactor: Double
    var objectives: [MicroscopeObjective]
    var activeObjectiveID: String

    static let optikaB293 = MicroscopeOpticalConfiguration(
        microscopeName: "OPTIKA B-293",
        objectiveSeries: "N-PLAN (Plan Achromat)",
        opticalTubeSystem: "Finite, 160 mm",
        condenserNumericalAperture: 1.25,
        cameraPixelPitchMicrometers: 2.2,
        cameraOutputPixelFactor: 1,
        cameraRelayPreset: .touptekFMA050,
        cameraRelayCustomName: "",
        cameraRelayMagnification: 0.5,
        chromatinBandLowerNanometers: 540,
        chromatinBandUpperNanometers: 600,
        targetRayleighSamplingFactor: 2.3,
        objectives: [
            MicroscopeObjective(
                id: "4x",
                name: "N-PLAN 4×",
                magnification: 4,
                numericalAperture: 0.10,
                immersion: "Dry"
            ),
            MicroscopeObjective(
                id: "10x",
                name: "N-PLAN 10×",
                magnification: 10,
                numericalAperture: 0.25,
                immersion: "Dry"
            ),
            MicroscopeObjective(
                id: "40x",
                name: "N-PLAN 40×",
                magnification: 40,
                numericalAperture: 0.65,
                immersion: "Dry"
            ),
            MicroscopeObjective(
                id: "100x",
                name: "N-PLAN 100×",
                magnification: 100,
                numericalAperture: 1.25,
                immersion: "Oil / Water"
            )
        ],
        activeObjectiveID: "4x"
    )
}

extension MicroscopeOpticalConfiguration {
    private enum LegacyCodingKeys: String, CodingKey {
        case cameraCustomBinningFactor
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        microscopeName = try container.decode(String.self, forKey: .microscopeName)
        objectiveSeries = try container.decode(String.self, forKey: .objectiveSeries)
        opticalTubeSystem = try container.decode(String.self, forKey: .opticalTubeSystem)
        condenserNumericalAperture = try container.decode(
            Double.self,
            forKey: .condenserNumericalAperture
        )
        cameraPixelPitchMicrometers = try container.decode(
            Double.self,
            forKey: .cameraPixelPitchMicrometers
        )
        let legacyContainer = try decoder.container(keyedBy: LegacyCodingKeys.self)
        cameraOutputPixelFactor = try container.decodeIfPresent(
            Double.self,
            forKey: .cameraOutputPixelFactor
        ) ?? (try legacyContainer.decodeIfPresent(
            Double.self,
            forKey: .cameraCustomBinningFactor
        ) ?? 1)
        cameraRelayMagnification = try container.decode(
            Double.self,
            forKey: .cameraRelayMagnification
        )
        if let savedPreset = try? container.decode(
            CameraRelayPreset.self,
            forKey: .cameraRelayPreset
        ) {
            cameraRelayPreset = savedPreset
        } else {
            // Older builds named the same 0.50× relay preset after the MU503.
            // Preserve its optical value without retaining a camera coupling.
            let legacyPreset = try? container.decode(
                String.self,
                forKey: .cameraRelayPreset
            )
            cameraRelayPreset = legacyPreset == "amscopeMU503Included"
                ? .touptekFMA050
                : .custom
        }
        cameraRelayCustomName = try container.decodeIfPresent(
            String.self,
            forKey: .cameraRelayCustomName
        ) ?? "Previously configured relay"
        chromatinBandLowerNanometers = try container.decode(
            Double.self,
            forKey: .chromatinBandLowerNanometers
        )
        chromatinBandUpperNanometers = try container.decode(
            Double.self,
            forKey: .chromatinBandUpperNanometers
        )
        targetRayleighSamplingFactor = try container.decode(
            Double.self,
            forKey: .targetRayleighSamplingFactor
        )
        objectives = try container.decode([MicroscopeObjective].self, forKey: .objectives)
        activeObjectiveID = try container.decode(String.self, forKey: .activeObjectiveID)
    }
}

