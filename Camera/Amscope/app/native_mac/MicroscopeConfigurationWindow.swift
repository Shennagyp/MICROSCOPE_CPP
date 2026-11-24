import SwiftUI

struct MicroscopeConfigurationWindow: View {
    @ObservedObject var model: NativeCameraModel
    @State private var draft: MicroscopeOpticalConfiguration
    @State private var hasAppliedChanges = false

    init(model: NativeCameraModel) {
        self.model = model
        _draft = State(initialValue: model.microscopeConfiguration)
    }

    var body: some View {
        ScrollView(.vertical) {
            VStack(alignment: .leading, spacing: 14) {
                GroupBox(label: Text("Microscope")) {
                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            Text("Model")
                            Spacer()
                            TextField("Model", text: $draft.microscopeName)
                                .frame(width: 250)
                        }
                        HStack {
                            Text("Objective series")
                            Spacer()
                            TextField("Series", text: $draft.objectiveSeries)
                                .frame(width: 250)
                        }
                        HStack {
                            Text("Optical tube system")
                            Spacer()
                            Text(draft.opticalTubeSystem)
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

                        ForEach(draft.objectives) { objective in
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
                            Text("Camera sensor pixel pitch (µm)")
                            Spacer()
                            TextField(
                                "Pixel pitch",
                                text: configurationNumberBinding(
                                    \.cameraPixelPitchMicrometers,
                                    precision: 2
                                )
                            )
                            .frame(width: 90)
                        }
                        HStack {
                            Text("Total camera relay magnification")
                            Spacer()
                            TextField(
                                "Relay",
                                text: configurationNumberBinding(
                                    \.cameraRelayMagnification,
                                    precision: 3
                                )
                            )
                            .frame(width: 90)
                        }
                        Text("Enter the product of all lenses/reducers between the microscope camera port and the sensor. MU503's supplied reducer is 0.5×; OPTIKA M-115 is 0.35×. Use 0.175× if both are physically stacked.")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                }

                GroupBox(label: Text("Rayleigh sampling target")) {
                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            Text("Reference wavelength (nm)")
                            Spacer()
                            TextField(
                                "Wavelength",
                                text: configurationNumberBinding(
                                    \.referenceWavelengthNanometers,
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
                        Text("The default 550 nm is a green-light reference, not a measured spectral peak of the B-293 white LED. A target factor of 2.3 is used as the initial sampling criterion.")
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
                        draft = .optikaB293
                        hasAppliedChanges = false
                    }
                    Spacer()
                    if hasAppliedChanges {
                        Text("Applied")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                    Button("Apply changes") {
                        var applied = draft
                        applied.activeObjectiveID =
                            model.microscopeConfiguration.activeObjectiveID
                        model.updateMicroscopeConfiguration(applied)
                        draft = applied
                        hasAppliedChanges = true
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

    private var isDraftValid: Bool {
        draft.cameraPixelPitchMicrometers.isFinite &&
        draft.cameraPixelPitchMicrometers > 0 &&
        draft.cameraRelayMagnification.isFinite &&
        draft.cameraRelayMagnification > 0 &&
        draft.referenceWavelengthNanometers.isFinite &&
        draft.referenceWavelengthNanometers > 0 &&
        draft.targetRayleighSamplingFactor.isFinite &&
        draft.targetRayleighSamplingFactor > 0 &&
        draft.condenserNumericalAperture.isFinite &&
        draft.condenserNumericalAperture > 0 &&
        draft.objectives.contains(where: { $0.id == model.microscopeConfiguration.activeObjectiveID }) &&
        draft.objectives.allSatisfy({
            $0.magnification.isFinite &&
            $0.magnification > 0 &&
            $0.numericalAperture.isFinite &&
            $0.numericalAperture > 0
        })
    }

    private func configurationNumberBinding(
        _ keyPath: WritableKeyPath<MicroscopeOpticalConfiguration, Double>,
        precision: Int
    ) -> Binding<String> {
        Binding(
            get: {
                String(format: "%.*f", precision, draft[keyPath: keyPath])
            },
            set: { text in
                guard let value = Double(text), value.isFinite else { return }
                var updated = draft
                updated[keyPath: keyPath] = value
                draft = updated
                hasAppliedChanges = false
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
                guard let objective = draft.objectives.first(where: {
                    $0.id == objectiveID
                }) else { return "" }
                return String(format: "%.*f", precision, objective[keyPath: keyPath])
            },
            set: { text in
                guard let value = Double(text), value.isFinite,
                      let index = draft.objectives.firstIndex(where: {
                          $0.id == objectiveID
                      }) else { return }
                var updated = draft
                updated.objectives[index][keyPath: keyPath] = value
                draft = updated
                hasAppliedChanges = false
            }
        )
    }

    private func objectiveStringBinding(
        _ objectiveID: String,
        keyPath: WritableKeyPath<MicroscopeObjective, String>
    ) -> Binding<String> {
        Binding(
            get: {
                draft.objectives.first(where: {
                    $0.id == objectiveID
                })?[keyPath: keyPath] ?? ""
            },
            set: { value in
                guard let index = draft.objectives.firstIndex(where: {
                    $0.id == objectiveID
                }) else { return }
                var updated = draft
                updated.objectives[index][keyPath: keyPath] = value
                draft = updated
                hasAppliedChanges = false
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

struct MicroscopeOpticalConfiguration: Codable, Equatable {
    var microscopeName: String
    var objectiveSeries: String
    var opticalTubeSystem: String
    var condenserNumericalAperture: Double
    var cameraPixelPitchMicrometers: Double
    var cameraRelayMagnification: Double
    var referenceWavelengthNanometers: Double
    var targetRayleighSamplingFactor: Double
    var objectives: [MicroscopeObjective]
    var activeObjectiveID: String

    static let optikaB293 = MicroscopeOpticalConfiguration(
        microscopeName: "OPTIKA B-293",
        objectiveSeries: "N-PLAN (Plan Achromat)",
        opticalTubeSystem: "Finite, 160 mm",
        condenserNumericalAperture: 1.25,
        cameraPixelPitchMicrometers: 2.2,
        cameraRelayMagnification: 0.5,
        referenceWavelengthNanometers: 550,
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
