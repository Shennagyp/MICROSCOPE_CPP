import SwiftUI
import Combine

private final class ObjectiveExposureSettingsViewState: ObservableObject {
    @Published var exposureDraft = ""
    @Published var isEditingExposure = false
    @Published var saveMessage = ""
}

struct ObjectiveExposureSettingsWindow: View {
    @ObservedObject var model: NativeCameraModel
    @ObservedObject var microscope: MicroscopeConfigurationModel
    @ObservedObject private var features: NativeCameraFeatureModel

    @ObservedObject private var viewState: ObjectiveExposureSettingsViewState

    init(model: NativeCameraModel, microscope: MicroscopeConfigurationModel) {
        self.model = model
        self.microscope = microscope
        self.features = model.features
        self.viewState = ObjectiveExposureSettingsViewState()
    }

    private var activeObjective: MicroscopeObjective? {
        microscope.configuration.objectives.first(where: {
            $0.id == microscope.configuration.activeObjectiveID
        })
    }

    private var exposureAutoDescriptor: AmscopeCameraFeatureDescriptor? {
        features.featureDescriptors.first(where: { $0.name == "ExposureAuto" })
    }

    private var exposureTimeDescriptor: AmscopeCameraFeatureDescriptor? {
        features.featureDescriptors.first(where: { $0.name == "ExposureTime" })
    }

    private var exposureMode: String? {
        features.featureValues["ExposureAuto"]?.enumerationSymbol
    }

    private var isAutomaticExposureEnabled: Bool {
        exposureMode.map { $0 != "Off" } ?? true
    }

    private var manualExposureReadback: Int64? {
        features.featureValues["ExposureTime"]?.integerValue
    }

    private var storedProfile: ObjectiveExposureProfile {
        microscope.configuration.exposureProfile(
            for: microscope.configuration.activeObjectiveID
        )
    }

    private var canApplyManualExposure: Bool {
        exposureMode == "Off" &&
        exposureTimeDescriptor?.isAvailable == true &&
        exposureTimeDescriptor?.isWritable == true &&
        !model.isCameraOperationPending &&
        !features.isFeatureSnapshotLoading &&
        !features.isFeatureWritePending &&
        Int64(viewState.exposureDraft.trimmingCharacters(in: .whitespacesAndNewlines)) != nil
    }

    private var canSaveProfile: Bool {
        guard model.canAccessFeatures,
              let exposureMode = exposureMode,
              exposureMode == "Off" || exposureMode == "Once" || exposureMode == "Continuous",
              let autoDescriptor = exposureAutoDescriptor,
              autoDescriptor.isAvailable,
              autoDescriptor.isWritable,
              !model.isCameraOperationPending,
              !features.isFeatureSnapshotLoading,
              !features.isFeatureWritePending else {
            return false
        }

        if exposureMode == "Off" {
            guard let readback = manualExposureReadback else { return false }
            let draft = Int64(viewState.exposureDraft.trimmingCharacters(in: .whitespacesAndNewlines))
            return draft == readback
        }
        return true
    }

    var body: some View {
        ScrollView(.vertical) {
            VStack(alignment: .leading, spacing: 14) {
                cameraSection
                objectiveSection
                exposureSection
                savedProfileSection
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(16)
        }
        .frame(minWidth: 500, minHeight: 420)
        .onAppear {
            if model.cameras.isEmpty {
                model.refreshCameras()
            }
            if model.canAccessFeatures {
                features.refreshFeatures()
            }
            loadStoredExposureDraft()
        }
        .onChange(of: microscope.configuration.activeObjectiveID) { _ in
            viewState.saveMessage = ""
            loadStoredExposureDraft()
        }
        .onChange(of: manualExposureReadback) { value in
            guard !viewState.isEditingExposure, let value = value else { return }
            viewState.exposureDraft = String(value)
        }
        .alert(isPresented: $model.isShowingError) {
            Alert(
                title: Text("Camera Error"),
                message: Text(model.errorMessage),
                dismissButton: .default(Text("OK"))
            )
        }
    }

    private var cameraSection: some View {
        GroupBox(label: Text("Camera")) {
            VStack(alignment: .leading, spacing: 10) {
                Picker("Device", selection: $model.selectedDeviceID) {
                    Text("Select a camera").tag("")
                    ForEach(model.cameras, id: \.deviceID) { camera in
                        Text(camera.displayName).tag(camera.deviceID)
                    }
                }
                .disabled(model.cameraState != .closed ||
                          model.isCameraOperationPending ||
                          model.isEnumeratingCameras)

                HStack {
                    Button("Refresh") {
                        model.refreshCameras()
                    }
                    .disabled(model.cameraState != .closed ||
                              model.isCameraOperationPending ||
                              model.isEnumeratingCameras)

                    Spacer()

                    Text(model.activeCameraName)
                        .foregroundColor(OptikaPalette.secondaryText)
                        .lineLimit(1)
                }

                HStack {
                    if model.cameraState == .closed {
                        Button("Open & Start") {
                            model.openAndStartCamera(deviceID: model.selectedDeviceID)
                        }
                        .disabled(model.selectedDeviceID.isEmpty ||
                                  !model.canConnectDetectedCamera)
                    } else if model.canStart {
                        Button("Start") {
                            model.startAcquisition()
                        }
                    } else if model.canStop {
                        Button("Stop") {
                            model.stopAcquisition()
                        }
                    }

                    if model.canClose {
                        Button("Close Camera") {
                            model.closeCamera()
                        }
                    }

                    Spacer()

                    Text(model.cameraStateText)
                        .foregroundColor(OptikaPalette.secondaryText)
                }

                if model.isCameraOperationPending || model.isEnumeratingCameras {
                    Text(model.isEnumeratingCameras ? "Searching for cameras…" : "Camera command in progress…")
                        .font(.caption)
                        .foregroundColor(OptikaPalette.secondaryText)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    private var objectiveSection: some View {
        GroupBox(label: Text("Objective")) {
            VStack(alignment: .leading, spacing: 8) {
                Picker(
                    "Active objective",
                    selection: Binding(
                        get: { microscope.configuration.activeObjectiveID },
                        set: { microscope.selectObjective($0) }
                    )
                ) {
                    ForEach(microscope.configuration.objectives) { objective in
                        Text(objective.name).tag(objective.id)
                    }
                }
                .disabled(model.isCameraOperationPending)

                if let objective = activeObjective {
                    Text("\(objective.displayName) · \(objective.immersion)")
                        .font(.caption)
                        .foregroundColor(OptikaPalette.secondaryText)
                }

                Text("Select the objective currently in the optical path. The B-293 nosepiece is manual; changing this selection applies that objective’s saved exposure profile.")
                    .font(.caption)
                    .foregroundColor(OptikaPalette.secondaryText)
                    .fixedSize(horizontal: false, vertical: true)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    private var exposureSection: some View {
        GroupBox(label: Text("Exposure")) {
            VStack(alignment: .leading, spacing: 12) {
                if !model.canAccessFeatures {
                    Text("Open the camera to configure exposure.")
                        .foregroundColor(OptikaPalette.secondaryText)
                } else if features.isFeatureSnapshotLoading {
                    Text("Reading camera exposure capabilities…")
                        .foregroundColor(OptikaPalette.secondaryText)
                } else if let autoDescriptor = exposureAutoDescriptor,
                          autoDescriptor.isAvailable {
                    Toggle(
                        "Automatic exposure",
                        isOn: Binding(
                            get: { isAutomaticExposureEnabled },
                            set: { enabled in
                                features.applyExposureProfile(
                                    ObjectiveExposureProfile(
                                        automaticExposureEnabled: enabled,
                                        manualExposureTimeMicroseconds:
                                            storedProfile.manualExposureTimeMicroseconds
                                    )
                                )
                            }
                        )
                    )
                    .disabled(exposureMode == nil ||
                              !autoDescriptor.isWritable ||
                              model.isCameraOperationPending ||
                              features.isFeatureWritePending ||
                              features.isFeatureSnapshotLoading)

                    HStack {
                        Text("Camera mode")
                        Spacer()
                        Text(exposureMode ?? "Reading…")
                            .foregroundColor(OptikaPalette.secondaryText)
                    }

                    Divider()

                    VStack(alignment: .leading, spacing: 6) {
                        HStack {
                            Text("Manual exposure time")
                            Spacer()
                            if let descriptor = exposureTimeDescriptor,
                               descriptor.hasNumericRange,
                               descriptor.integerRange {
                                Text("\(descriptor.minimumInteger)–\(descriptor.maximumInteger) µs")
                                    .font(.caption)
                                    .foregroundColor(OptikaPalette.secondaryText)
                            } else {
                                Text("µs")
                                    .font(.caption)
                                    .foregroundColor(OptikaPalette.secondaryText)
                            }
                        }

                        HStack {
                            TextField(
                                "Exposure time in microseconds",
                                text: $viewState.exposureDraft,
                                onEditingChanged: { editing in
                                    viewState.isEditingExposure = editing
                                },
                                onCommit: {
                                    applyManualExposure()
                                }
                            )
                            .textFieldStyle(RoundedBorderTextFieldStyle())
                            .disabled(exposureMode != "Off" ||
                                      exposureTimeDescriptor?.isAvailable != true ||
                                      model.isCameraOperationPending ||
                                      features.isFeatureWritePending ||
                                      features.isFeatureSnapshotLoading)

                            Button("Apply") {
                                applyManualExposure()
                            }
                            .disabled(!canApplyManualExposure)
                        }

                        if let readback = manualExposureReadback {
                            Text("Camera readback: \(readback) µs")
                                .font(.caption)
                                .foregroundColor(OptikaPalette.secondaryText)
                        }

                        if exposureMode != "Off" {
                            Text("Turn automatic exposure off before setting a manual exposure time.")
                                .font(.caption)
                                .foregroundColor(OptikaPalette.secondaryText)
                        }
                    }
                    .opacity(exposureMode == "Off" ? 1 : 0.65)
                } else {
                    Text("The connected camera does not expose configurable automatic exposure.")
                        .foregroundColor(OptikaPalette.secondaryText)
                }

                HStack {
                    Spacer()
                    Button("Save for This Objective") {
                        saveExposureProfile()
                    }
                    .disabled(!canSaveProfile)
                }

                if !viewState.saveMessage.isEmpty {
                    Text(viewState.saveMessage)
                        .font(.caption)
                        .foregroundColor(OptikaPalette.secondaryText)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    private var savedProfileSection: some View {
        GroupBox(label: Text("Saved objective profile")) {
            VStack(alignment: .leading, spacing: 5) {
                Text(storedProfile.automaticExposureEnabled ? "Automatic exposure: enabled" : "Automatic exposure: disabled")
                if let exposure = storedProfile.manualExposureTimeMicroseconds {
                    Text("Stored manual time: \(exposure) µs")
                        .foregroundColor(OptikaPalette.secondaryText)
                } else {
                    Text("No manual exposure time stored")
                        .foregroundColor(OptikaPalette.secondaryText)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }

    private func loadStoredExposureDraft() {
        if let stored = storedProfile.manualExposureTimeMicroseconds {
            viewState.exposureDraft = String(stored)
        } else if let readback = manualExposureReadback {
            viewState.exposureDraft = String(readback)
        } else {
            viewState.exposureDraft = ""
        }
        viewState.isEditingExposure = false
    }

    private func applyManualExposure() {
        guard canApplyManualExposure,
              let descriptor = exposureTimeDescriptor,
              let value = Int64(viewState.exposureDraft.trimmingCharacters(in: .whitespacesAndNewlines)) else {
            return
        }

        if descriptor.hasNumericRange && descriptor.integerRange {
            guard value >= descriptor.minimumInteger &&
                  value <= descriptor.maximumInteger else {
                viewState.saveMessage = "Exposure time is outside the camera-reported range."
                return
            }
        }

        viewState.saveMessage = ""
        features.writeInteger(value, for: descriptor)
    }

    private func saveExposureProfile() {
        guard canSaveProfile,
              let mode = exposureMode else {
            return
        }

        let automatic = mode != "Off"
        let manualTime = automatic
            ? storedProfile.manualExposureTimeMicroseconds
            : manualExposureReadback

        let profile = ObjectiveExposureProfile(
            automaticExposureEnabled: automatic,
            manualExposureTimeMicroseconds: manualTime
        )
        microscope.updateExposureProfile(
            profile,
            for: microscope.configuration.activeObjectiveID
        )
        viewState.saveMessage = "Exposure profile saved for \(activeObjective?.name ?? "this objective")."
    }
}
