import AppKit
import SwiftUI

struct NativeCameraControlsDock: View {
    @ObservedObject var model: NativeCameraModel
    @ObservedObject var features: NativeCameraFeatureModel
    @ObservedObject var workspace: NativeWorkspaceModel
    @ObservedObject var microscope: MicroscopeConfigurationModel
    @ObservedObject var resolutionCoordinator: MicroscopeResolutionCoordinator

    private var additionalFeatureDescriptors: [AmscopeCameraFeatureDescriptor] {
        features.genericEditorDescriptors
    }

    var body: some View {
        VStack(spacing: 0) {
            ScrollView(.vertical, showsIndicators: true) {
                VStack(alignment: .leading, spacing: 14) {
                    CameraDockConnectionSection(
                        model: model,
                        features: features,
                        resolutionCoordinator: resolutionCoordinator
                    )

                    Divider()

                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            Text("CAMERA FEATURES")
                                .font(.system(size: 13, weight: .bold))
                                .foregroundColor(OptikaPalette.secondaryText)

                            Spacer()

                            Button {
                                features.refreshFeatures()
                            } label: {
                                Image(systemName: "arrow.clockwise")
                                    .font(.system(size: 13, weight: .medium))
                            }
                            .buttonStyle(BorderlessButtonStyle())
                            .help("Refresh camera feature values")
                            .disabled(!model.canAccessFeatures ||
                                      features.isFeatureSnapshotLoading ||
                                      features.isFeatureWritePending ||
                                      model.isCameraOperationPending)
                        }

                        if !model.canAccessFeatures {
                            Text("Open a camera to load its controls.")
                                .font(.system(size: 12))
                                .foregroundColor(OptikaPalette.secondaryText)
                        } else if additionalFeatureDescriptors.isEmpty {
                            Text(features.previewResolutionDescriptor == nil
                                 ? "The camera reported no feature controls."
                                 : "No additional camera feature controls.")
                                .font(.system(size: 12))
                                .foregroundColor(OptikaPalette.secondaryText)
                        } else {
                            ForEach(
                                Array(additionalFeatureDescriptors.enumerated()),
                                id: \.element.name
                            ) { index, descriptor in
                                if index == 0 ||
                                   additionalFeatureDescriptors[index - 1].group.rawValue != descriptor.group.rawValue {
                                    Text(featureGroupTitle(descriptor.group.rawValue).uppercased())
                                        .font(.system(size: 13, weight: .bold))
                                        .foregroundColor(OptikaPalette.secondaryText)
                                        .padding(.top, index == 0 ? 0 : 5)
                                }

                                DockCameraFeatureControl(
                                    model: model,
                                    features: features,
                                    descriptor: descriptor
                                )
                                .frame(maxWidth: .infinity, alignment: .leading)
                            }
                        }
                    }
                }
                .padding(14)
                .frame(maxWidth: .infinity, alignment: .leading)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            Divider()

            VStack(alignment: .leading, spacing: 8) {
                Text("MICROSCOPE")
                    .font(.system(size: 13, weight: .bold))
                    .foregroundColor(OptikaPalette.secondaryText)

                HStack(spacing: 8) {
                    Text("Objective")
                        .font(.system(size: 12))
                        .foregroundColor(OptikaPalette.secondaryText)

                    Spacer(minLength: 6)

                    Picker(
                        "Objective",
                        selection: Binding(
                            get: { microscope.configuration.activeObjectiveID },
                            set: { microscope.selectObjective($0) }
                        )
                    ) {
                        ForEach(microscope.configuration.objectives) { objective in
                            Text(String(format: "%.0f×", objective.magnification))
                                .tag(objective.id)
                        }
                    }
                    .labelsHidden()
                    .pickerStyle(PopUpButtonPickerStyle())
                    .frame(maxWidth: 180)
                    .disabled(model.isCameraOperationPending)
                    .help("Current objective. Configure objectives in File → Microscope Configuration.")
                }

                HStack(spacing: 8) {
                    Toggle(
                        "Automatic resolution",
                        isOn: Binding(
                            get: { resolutionCoordinator.isAutomaticResolutionEnabled },
                            set: { resolutionCoordinator.setAutomaticResolutionEnabled($0) }
                        )
                    )
                    .font(.system(size: 12))
                    .disabled(!model.canConfigureAutomaticResolution)
                    .help(resolutionCoordinator.automaticResolutionHelp)

                    Spacer(minLength: 4)

                    if resolutionCoordinator.isAutomaticResolutionEnabled {
                        Text(resolutionCoordinator.automaticResolutionSummary)
                            .font(.system(size: 10))
                            .foregroundColor(OptikaPalette.secondaryText)
                            .lineLimit(2)
                            .multilineTextAlignment(.trailing)
                    }
                }

                HStack(spacing: 8) {
                    Text("Live frame destination")
                        .font(.system(size: 12))
                        .foregroundColor(OptikaPalette.secondaryText)

                    Spacer(minLength: 6)

                    Picker(
                        "Live frame destination",
                        selection: Binding(
                            get: { workspace.activeSlide },
                            set: { workspace.selectActiveSlide($0) }
                        )
                    ) {
                        Text("Slide 1").tag(NativeSlideID.slide1)
                        Text("Slide 2").tag(NativeSlideID.slide2)
                    }
                    .labelsHidden()
                    .pickerStyle(SegmentedPickerStyle())
                    .frame(width: 132)
                    .disabled(model.isCameraOperationPending)
                }
            }
            .padding(.horizontal, 14)
            .padding(.vertical, 10)
            .background(OptikaPalette.controlSurface.opacity(OptikaPalette.floatingPanelAlpha))
            .fixedSize(horizontal: false, vertical: true)
        }
    }
}

private struct CameraDockConnectionSection: View {
    @ObservedObject var model: NativeCameraModel
    @ObservedObject var features: NativeCameraFeatureModel
    @ObservedObject var resolutionCoordinator: MicroscopeResolutionCoordinator

    private var isConnected: Bool {
        model.openedCameraDeviceID != nil && model.cameraState != .closed
    }

    private var stateColor: Color {
        switch model.cameraState {
        case .open, .streaming: return OptikaPalette.success
        case .starting, .stopping: return OptikaPalette.warning
        case .faulted: return OptikaPalette.live
        case .closed: return OptikaPalette.secondaryText
        @unknown default: return OptikaPalette.secondaryText
        }
    }

    @ViewBuilder
    private var resolutionControls: some View {
        if let descriptor = features.previewResolutionDescriptor {
            VStack(alignment: .leading, spacing: 7) {
                HStack(spacing: 8) {
                    Text("Resolution")
                        .font(.system(size: 12, weight: .medium))

                    Spacer(minLength: 6)

                    Picker(
                        "Resolution",
                        selection: Binding(
                            get: { features.selectedPreviewResolutionSymbol ?? "" },
                            set: { resolutionCoordinator.selectManualPreviewResolution(to: $0) }
                        )
                    ) {
                        if features.selectedPreviewResolutionSymbol == nil {
                            Text("Select resolution").tag("")
                        }
                        ForEach(descriptor.enumerationValues, id: \.self) { symbol in
                            Text(resolutionOptionTitle(symbol)).tag(symbol)
                        }
                    }
                    .labelsHidden()
                    .pickerStyle(PopUpButtonPickerStyle())
                    .frame(maxWidth: 180)
                    .disabled(!model.canChangePreviewResolution)
                }

            }
            .padding(.top, 2)
        } else if model.canAccessFeatures {
            Text(features.isFeatureSnapshotLoading
                 ? "Loading resolution modes…"
                 : "Resolution selection is unavailable for this camera.")
                .font(.system(size: 11))
                .foregroundColor(OptikaPalette.secondaryText)
        }
    }

    private func resolutionOptionTitle(_ symbol: String) -> String {
        guard let separator = symbol.firstIndex(of: ":") else {
            return symbol
        }
        return String(symbol[symbol.index(after: separator)...])
            .replacingOccurrences(of: "x", with: " × ")
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 6) {
                Text("CAMERA")
                    .font(.system(size: 13, weight: .bold))
                    .foregroundColor(OptikaPalette.secondaryText)

                Spacer()

                Circle()
                    .fill(stateColor)
                    .frame(width: 7, height: 7)

                Text(model.cameraStateText.uppercased())
                    .font(.system(size: 13, weight: .bold))
                    .foregroundColor(OptikaPalette.secondaryText)

            }

            Text(isConnected ? model.activeCameraName : "No camera connected")
                .font(.system(size: 13, weight: .semibold))
                .lineLimit(1)
                .help("Right-click the camera name for Stop and Close actions.")
                .contextMenu {
                    if model.cameraState == .streaming {
                        Button {
                            model.stopAcquisition()
                        } label: {
                            HStack(spacing: 8) {
                                Image(systemName: "stop.fill")
                                Text("Stop")
                            }
                        }
                        .disabled(!model.canStop)
                    }

                    if model.canClose {
                        Button {
                            model.closeCamera()
                        } label: {
                            HStack(spacing: 8) {
                                Image(systemName: "xmark.circle")
                                Text("Close")
                            }
                        }
                        .disabled(!model.canClose)
                    }
                }

            if isConnected {
                resolutionControls
            }

            if !isConnected {
                Picker("Device", selection: $model.selectedDeviceID) {
                    Text("Select a camera").tag("")
                    ForEach(model.cameras, id: \.deviceID) { camera in
                        Text(camera.displayName).tag(camera.deviceID)
                    }
                }
                .pickerStyle(PopUpButtonPickerStyle())
                .disabled(!model.canConnectDetectedCamera)

                HStack {
                    Button {
                        model.refreshCameras()
                    } label: {
                        HStack(spacing: 5) {
                            Image(systemName: "arrow.clockwise")
                            Text("Refresh")
                        }
                    }
                    .disabled(model.isEnumeratingCameras || model.isCameraOperationPending)

                    Spacer()

                    Button {
                        model.openAndStartCamera(deviceID: model.selectedDeviceID)
                    } label: {
                        HStack(spacing: 5) {
                            Image(systemName: "play.fill")
                            Text("Open + Start")
                        }
                        .font(.system(size: 13, weight: .semibold))
                        .padding(.horizontal, 9)
                        .padding(.vertical, 5)
                        .background(
                            RoundedRectangle(cornerRadius: 5)
                                .fill(OptikaPalette.accentSelectionFill)
                        )
                    }
                    .buttonStyle(PlainButtonStyle())
                    .disabled(!model.canConnectDetectedCamera || model.selectedDeviceID.isEmpty)
                }
            } else if model.cameraState == .open {
                Button {
                    model.startAcquisition()
                } label: {
                    HStack(spacing: 6) {
                        Image(systemName: "play.fill")
                        Text("Start preview")
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 5)
                    .background(
                        RoundedRectangle(cornerRadius: 5)
                            .fill(OptikaPalette.accentSelectionFill)
                    )
                }
                .buttonStyle(PlainButtonStyle())
                .disabled(!model.canStart)
            } else if model.cameraState == .streaming {
                Text("Preview is running. Right-click the camera name to Stop or Close.")
                    .font(.system(size: 11))
                    .foregroundColor(OptikaPalette.secondaryText)
                    .fixedSize(horizontal: false, vertical: true)
            } else if model.cameraState == .starting || model.cameraState == .stopping {
                Text("Camera operation in progress…")
                    .font(.system(size: 11))
                    .foregroundColor(OptikaPalette.secondaryText)
            } else if model.cameraState == .faulted {
                Text("Camera faulted. Right-click the camera name to Close the session.")
                    .font(.system(size: 11))
                    .foregroundColor(OptikaPalette.secondaryText)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .padding(12)
        .background(
            RoundedRectangle(cornerRadius: 7)
                .fill(OptikaPalette.windowSurface.opacity(OptikaPalette.floatingPanelAlpha))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 7)
                .stroke(OptikaPalette.separator.opacity(OptikaPalette.dockBorderAlpha), lineWidth: 0.7)
        )
    }
}
