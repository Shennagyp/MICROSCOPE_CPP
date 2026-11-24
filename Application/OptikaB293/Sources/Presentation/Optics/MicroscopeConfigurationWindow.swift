import SwiftUI

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
                                .foregroundColor(OptikaPalette.secondaryText)
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
                                .foregroundColor(OptikaPalette.secondaryText)
                        }
                        Text("The B-293 nosepiece is manual. Select the objective currently in the optical path from the main-window dock.")
                            .font(.caption)
                            .foregroundColor(OptikaPalette.secondaryText)
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
                        .foregroundColor(OptikaPalette.secondaryText)

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

                        Text("Use 1.0 for native sensor readout. For binning or camera-side resampling, enter the linear sensor-to-output pixel factor (2.0 for 2× linear binning). Cropping or ROI alone remains 1.0. Update it when the camera readout mode changes. This is explicit setup data; the app does not guess it from a camera model or frame dimensions.")
                            .font(.caption)
                            .foregroundColor(OptikaPalette.secondaryText)

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
                            .foregroundColor(OptikaPalette.secondaryText)
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
                            .foregroundColor(OptikaPalette.secondaryText)
                        Toggle(
                            "Enable automatic camera resolution",
                            isOn: Binding(
                                get: { model.isAutomaticResolutionEnabled },
                                set: { model.setAutomaticResolutionEnabled($0) }
                            )
                        )
                        Text("Current automatic choice: \(model.automaticResolutionSummary)")
                            .font(.caption)
                            .foregroundColor(OptikaPalette.secondaryText)
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
                            .foregroundColor(OptikaPalette.secondaryText)
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

