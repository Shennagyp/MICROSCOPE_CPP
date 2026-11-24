import SwiftUI

@MainActor
final class MicroscopeConfigurationDraftStore: ObservableObject {
    @Published var configuration: MicroscopeOpticalConfiguration
    @Published private(set) var hasAppliedChanges = false

    init(configuration: MicroscopeOpticalConfiguration) {
        self.configuration = configuration
    }

    func update(
        _ mutation: (inout MicroscopeOpticalConfiguration) -> Void
    ) {
        var updated = configuration
        mutation(&updated)
        configuration = updated
        hasAppliedChanges = false
    }

    func restoreDefaults() {
        configuration = .optikaB293
        hasAppliedChanges = false
    }

    func markApplied(_ configuration: MicroscopeOpticalConfiguration) {
        self.configuration = configuration
        hasAppliedChanges = true
    }
}

struct MicroscopeConfigurationWindow: View {
    @ObservedObject var model: NativeCameraModel
    @ObservedObject var draftStore: MicroscopeConfigurationDraftStore

    init(model: NativeCameraModel) {
        self.model = model
        self.draftStore = model.microscopeConfigurationDraftStore
    }

    var body: some View {
        ScrollView(.vertical) {
            VStack(alignment: .leading, spacing: 14) {
                GroupBox(label: Text("Microscope")) {
                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            Text("Model")
                            Spacer()
                            TextField("Model", text: configurationStringBinding(\.microscopeName))
                                .frame(width: 250)
                        }
                        HStack {
                            Text("Objective series")
                            Spacer()
                            TextField("Series", text: configurationStringBinding(\.objectiveSeries))
                                .frame(width: 250)
                        }
                        HStack {
                            Text("Optical tube system")
                            Spacer()
                            Text(draftStore.configuration.opticalTubeSystem)
                                .foregroundColor(.secondary)
                        }
                        HStack {
                            Text("Condenser NA")
                            Spacer()
                            TextField(
                                "NA",
                                text: configurationNumberBinding(
                                    \.condenserNumericalAperture,
                                    precision: 2
                                )
                            )
                            .frame(width: 90)
                        }
                        HStack {
                            Text("Active objective")
                            Spacer()
                            Text(model.microscopeConfiguration.objectives.first(where: {
                                $0.id == model.microscopeConfiguration.activeObjectiveID
                            })?.displayName ?? "Not selected")
                                .foregroundColor(.secondary)
                        }
                        Text("The B-293 nosepiece is manual. Select the objective currently in the optical path from the main-window dock.")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }

                GroupBox(label: Text("Objective turret")) {
                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            Text("Objective")
                                .frame(maxWidth: .infinity, alignment: .leading)
                            Text("Magnification")
                                .frame(width: 105, alignment: .leading)
                            Text("N.A.")
                                .frame(width: 80, alignment: .leading)
                            Text("Immersion")
                                .frame(width: 115, alignment: .leading)
                        }
                        .font(.caption)
                        .foregroundColor(.secondary)

                        ForEach(draftStore.configuration.objectives) { objective in
                            HStack(spacing: 8) {
                                Text(objective.name)
                                    .frame(maxWidth: .infinity, alignment: .leading)
                                TextField(
                                    "×",
                                    text: objectiveNumberBinding(
                                        objective.id,
                                        keyPath: \.magnification,
                                        precision: 0
                                    )
                                )
                                .frame(width: 105)
                                TextField(
                                    "NA",
                                    text: objectiveNumberBinding(
                                        objective.id,
                                        keyPath: \.numericalAperture,
                                        precision: 2
                                    )
                                )
                                .frame(width: 80)
                                Picker(
                                    "Immersion",
                                    selection: objectiveStringBinding(
                                        objective.id,
                                        keyPath: \.immersion
                                    )
                                ) {
                                    Text("Dry").tag("Dry")
                                    Text("Oil / Water").tag("Oil / Water")
                                }
                                .labelsHidden()
                                .frame(width: 115)
                            }
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }

                GroupBox(label: Text("Camera imaging path")) {
                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            Text("Sensor pixel pitch (µm)")
                            Spacer()
                            TextField(
                                "Pixel pitch",
                                text: configurationNumberBinding(
                                    \.cameraPixelPitchMicrometers,
                                    precision: 3
                                )
                            )
                            .frame(width: 90)
                        }

                        HStack {
                            Text("Output pixel factor")
                            Spacer()
                            TextField(
                                "Factor",
                                text: configurationNumberBinding(
                                    \.cameraOutputPixelFactor,
                                    precision: 3
                                )
                            )
                            .frame(width: 90)
                        }

                        Text("Use 1.0 for native sensor readout. For binning or camera-side resampling, enter the linear factor by which one output pixel represents sensor pixels (for example 2.0 for 2×). Cropping or ROI alone remains 1.0. This value is explicit camera configuration; the app does not guess it from the camera model or image dimensions.")
                            .font(.caption)
                            .foregroundColor(.secondary)

                        HStack {
                            Text("Reduction lens")
                            Spacer()
                            Picker(
                                "Reduction lens",
                                selection: Binding(
                                    get: { draftStore.configuration.cameraRelayPreset },
                                    set: { preset in
                                        draftStore.update { updated in
                                            updated.cameraRelayPreset = preset
                                            if let magnification = preset.nominalMagnification {
                                                updated.cameraRelayMagnification = magnification
                                            }
                                        }
                                    }
                                )
                            ) {
                                ForEach(CameraRelayPreset.allCases) { preset in
                                    Text(preset.title).tag(preset)
                                }
                            }
                            .labelsHidden()
                            .frame(width: 260)
                        }
                        if draftStore.configuration.cameraRelayPreset == .custom {
                            HStack {
                                Text("Optical path name")
                                Spacer()
                                TextField(
                                    "Reduction lens / combined path",
                                    text: configurationStringBinding(\.cameraRelayCustomName)
                                )
                                .frame(width: 260)
                            }
                            HStack {
                                Text("Total reduction magnification")
                                Spacer()
                                TextField(
                                    "Magnification",
                                    text: configurationNumberBinding(
                                        \.cameraRelayMagnification,
                                        precision: 3
                                    )
                                )
                                .frame(width: 90)
                            }
                        } else {
                            HStack {
                                Text("Reduction magnification")
                                Spacer()
                                Text(String(format: "%.3f×", draftStore.configuration.cameraRelayMagnification))
                                    .monospacedDigit()
                            }
                        }

                        let effectiveScale = currentDraftScale
                        HStack {
                            Text("Calculated specimen scale")
                            Spacer()
                            Text(effectiveScale.map { String(format: "%.4f µm/px", $0) } ?? "Unavailable")
                                .monospacedDigit()
                                .foregroundColor(effectiveScale == nil ? .secondary : .primary)
                        }
                        Text("µm/source-pixel = sensor pixel pitch × output pixel factor ÷ (objective magnification × reduction-lens magnification). The application performs this calculation. The reduction value must represent the combined installed optical path, without counting any element twice.")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                }

                GroupBox(label: Text("Rayleigh sampling target")) {
                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            Text("Chromatin band minimum (nm)")
                            Spacer()
                            TextField(
                                "Minimum nm",
                                text: configurationNumberBinding(
                                    \.chromatinBandLowerNanometers,
                                    precision: 0
                                )
                            )
                            .frame(width: 90)
                        }
                        HStack {
                            Text("Chromatin band maximum (nm)")
                            Spacer()
                            TextField(
                                "Maximum nm",
                                text: configurationNumberBinding(
                                    \.chromatinBandUpperNanometers,
                                    precision: 0
                                )
                            )
                            .frame(width: 90)
                        }
                        HStack {
                            Text("Target Rayleigh sampling factor")
                            Spacer()
                            TextField(
                                "Factor",
                                text: configurationNumberBinding(
                                    \.targetRayleighSamplingFactor,
                                    precision: 2
                                )
                            )
                            .frame(width: 90)
                        }
                        Text("H&E chromatin profile: 540–600 nm starting band. The selector checks the whole band and uses its shortest wavelength for the conservative sampling requirement. This is not a measured spectrum of your stained slides or microscope; refine the band after spectral calibration.")
                            .font(.caption)
                            .foregroundColor(.secondary)
                        Toggle(
                            "Enable automatic camera resolution",
                            isOn: Binding(
                                get: { model.isAutomaticResolutionEnabled },
                                set: { model.setAutomaticResolutionEnabled($0) }
                            )
                        )
                        Text("Current automatic choice: \(model.automaticResolutionSummary)")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                }

                HStack {
                    Button("Restore B-293 defaults") {
                        draftStore.restoreDefaults()
                    }
                    Spacer()
                    if draftStore.hasAppliedChanges {
                        Text("Applied")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    Button("Apply changes") {
                        var applied = draftStore.configuration
                        applied.activeObjectiveID =
                            model.microscopeConfiguration.activeObjectiveID
                        model.updateMicroscopeConfiguration(applied)
                        draftStore.markApplied(applied)
                    }
                    .keyboardShortcut(.defaultAction)
                    .disabled(!isDraftValid)
                }
            }
            .padding(16)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .frame(minWidth: 560, minHeight: 480)
        .alert(isPresented: $model.isShowingError) {
            Alert(
                title: Text("Camera Error"),
                message: Text(model.errorMessage),
                dismissButton: .default(Text("OK"))
            )
        }
    }

    private var currentDraftScale: Double? {
        guard let objective = draftStore.configuration.objectives.first(where: {
            $0.id == model.microscopeConfiguration.activeObjectiveID
        }) else { return nil }

        return NativeMicroscopeMeasurementCalibration(
            sensorPixelPitchMicrometers: draftStore.configuration.cameraPixelPitchMicrometers,
            outputPixelFactor: draftStore.configuration.cameraOutputPixelFactor,
            objectiveMagnification: objective.magnification,
            reductionLensMagnification: draftStore.configuration.cameraRelayMagnification
        ).micrometersPerSourcePixel
    }

    private var isDraftValid: Bool {
        let candidate = draftStore.configuration
        return candidate.cameraPixelPitchMicrometers.isFinite &&
            candidate.cameraPixelPitchMicrometers > 0 &&
            candidate.cameraOutputPixelFactor.isFinite &&
            candidate.cameraOutputPixelFactor > 0 &&
            candidate.cameraRelayMagnification.isFinite &&
            candidate.cameraRelayMagnification > 0 &&
            candidate.chromatinBandLowerNanometers.isFinite &&
            candidate.chromatinBandLowerNanometers > 0 &&
            candidate.chromatinBandUpperNanometers.isFinite &&
            candidate.chromatinBandUpperNanometers >= candidate.chromatinBandLowerNanometers &&
            candidate.targetRayleighSamplingFactor.isFinite &&
            candidate.targetRayleighSamplingFactor > 0 &&
            candidate.condenserNumericalAperture.isFinite &&
            candidate.condenserNumericalAperture > 0 &&
            candidate.objectives.contains(where: {
                $0.id == model.microscopeConfiguration.activeObjectiveID
            }) &&
            candidate.objectives.allSatisfy({
                $0.magnification.isFinite &&
                $0.magnification > 0 &&
                $0.numericalAperture.isFinite &&
                $0.numericalAperture > 0
            })
    }

    private func configurationStringBinding(
        _ keyPath: WritableKeyPath<MicroscopeOpticalConfiguration, String>
    ) -> Binding<String> {
        Binding(
            get: { draftStore.configuration[keyPath: keyPath] },
            set: { value in
                draftStore.update { updated in
                    updated[keyPath: keyPath] = value
                }
            }
        )
    }

    private func configurationNumberBinding(
        _ keyPath: WritableKeyPath<MicroscopeOpticalConfiguration, Double>,
        precision: Int
    ) -> Binding<String> {
        Binding(
            get: {
                String(
                    format: "%.*f",
                    precision,
                    draftStore.configuration[keyPath: keyPath]
                )
            },
            set: { text in
                guard let value = Double(text), value.isFinite else { return }
                draftStore.update { updated in
                    updated[keyPath: keyPath] = value
                }
            }
        )
    }

    private func objectiveNumberBinding(
        _ objectiveID: String,
        keyPath: WritableKeyPath<MicroscopeObjective, Double>,
        precision: Int
    ) -> Binding<String> {
        Binding(
            get: {
                guard let objective = draftStore.configuration.objectives.first(where: {
                    $0.id == objectiveID
                }) else { return "" }
                return String(format: "%.*f", precision, objective[keyPath: keyPath])
            },
            set: { text in
                guard let value = Double(text), value.isFinite,
                      draftStore.configuration.objectives.contains(where: {
                          $0.id == objectiveID
                      }) else { return }
                draftStore.update { updated in
                    guard let index = updated.objectives.firstIndex(where: {
                        $0.id == objectiveID
                    }) else { return }
                    updated.objectives[index][keyPath: keyPath] = value
                }
            }
        )
    }

    private func objectiveStringBinding(
        _ objectiveID: String,
        keyPath: WritableKeyPath<MicroscopeObjective, String>
    ) -> Binding<String> {
        Binding(
            get: {
                draftStore.configuration.objectives.first(where: {
                    $0.id == objectiveID
                })?[keyPath: keyPath] ?? ""
            },
            set: { value in
                guard draftStore.configuration.objectives.contains(where: {
                    $0.id == objectiveID
                }) else { return }
                draftStore.update { updated in
                    guard let index = updated.objectives.firstIndex(where: {
                        $0.id == objectiveID
                    }) else { return }
                    updated.objectives[index][keyPath: keyPath] = value
                }
            }
        )
    }
}

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
        case cameraCustomBinningFactor
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
        cameraOutputPixelFactor = try container.decodeIfPresent(
            Double.self,
            forKey: .cameraOutputPixelFactor
        ) ?? (try container.decodeIfPresent(
            Double.self,
            forKey: .cameraCustomBinningFactor
        ) ?? 1)
        cameraRelayMagnification = try container.decode(
            Double.self,
            forKey: .cameraRelayMagnification
        )
        cameraRelayPreset = (try? container.decode(
            CameraRelayPreset.self,
            forKey: .cameraRelayPreset
        )) ?? .custom
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
