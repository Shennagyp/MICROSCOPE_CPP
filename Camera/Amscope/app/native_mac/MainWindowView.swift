import AppKit
import SwiftUI

struct MainWindowView: View {
    @ObservedObject var model: NativeCameraModel

    var body: some View {
        VStack(spacing: 0) {
            CameraConnectionBar(model: model)
            Divider()

            NativeDualPreviewSurface(
                bridge: model.bridge,
                activeSlide: model.activeSlide,
                onActiveSlideChanged: model.selectActiveSlide
            ) { resolution, pixelFormat in
                model.updateFrameMetadata(
                    resolution: resolution,
                    pixelFormat: pixelFormat
                )
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            Divider()
            BottomControlInformationBar(model: model)
        }
        .frame(minWidth: 960, minHeight: 640)
        .onAppear {
            if model.cameras.isEmpty {
                model.refreshCameras()
            }
        }
        .alert(isPresented: $model.isShowingError) {
            Alert(
                title: Text("Camera Error"),
                message: Text(model.errorMessage),
                dismissButton: .default(Text("OK"))
            )
        }
    }
}

private struct CameraConnectionBar: View {
    @ObservedObject var model: NativeCameraModel

    var body: some View {
        HStack(spacing: 10) {
            HStack(spacing: 5) {
                Image(systemName: "video")
                Text("CAMERA")
            }
            .font(.system(size: 11, weight: .semibold))

            if model.cameras.isEmpty {
                Text(model.isEnumeratingCameras ? "Detecting cameras…" : "No camera detected")
                    .font(.system(size: 12))
                    .foregroundColor(.secondary)
            } else {
                ForEach(model.cameras, id: \.deviceID) { camera in
                    Button {
                        model.openAndStartCamera(deviceID: camera.deviceID)
                    } label: {
                        HStack(spacing: 6) {
                            Image(systemName: "video.fill")
                            Text(camera.displayName)
                                .lineLimit(1)
                            Text("Open + Start")
                                .font(.system(size: 10, weight: .semibold))
                        }
                    }
                    .disabled(!model.canConnectDetectedCamera)
                    .help("Open this camera and start acquisition")
                }
            }

            if model.cameraState == .open {
                Button("Start") {
                    model.startAcquisition()
                }
                .disabled(!model.canStart)
            }

            if model.cameraState == .streaming {
                Button("Stop") {
                    model.stopAcquisition()
                }
                .disabled(!model.canStop)
            }

            if model.canClose {
                Button("Close") {
                    model.closeCamera()
                }
                .disabled(!model.canClose)
            }

            Spacer(minLength: 8)

            Text(model.cameraStateText)
                .font(.system(size: 11, weight: .medium))
                .foregroundColor(.secondary)

            Button {
                model.refreshCameras()
            } label: {
                Image(systemName: "arrow.clockwise")
            }
            .disabled(model.isEnumeratingCameras || model.isCameraOperationPending)
            .help("Refresh detected cameras")
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 9)
        .background(Color(nsColor: NSColor.windowBackgroundColor))
    }
}

private struct BottomControlInformationBar: View {
    @ObservedObject var model: NativeCameraModel

    var body: some View {
        HSplitView {
            editableControls
                .frame(minWidth: 340, maxWidth: .infinity, maxHeight: .infinity)

            readOnlyInformation
                .frame(minWidth: 340, maxWidth: .infinity, maxHeight: .infinity)
        }
        .frame(height: 116)
        .background(Color(nsColor: NSColor.windowBackgroundColor))
    }

    private var editableControls: some View {
        VStack(alignment: .leading, spacing: 7) {
            HStack(spacing: 5) {
                Image(systemName: "slider.horizontal.3")
                Text("ADJUST")
            }
            .font(.system(size: 10, weight: .semibold))
            .foregroundColor(.secondary)

            if model.informationBarFeatures.isEmpty {
                Text(model.canAccessFeatures
                     ? "No quick-adjustable numeric controls reported"
                     : "Connect a camera to load editable controls")
                    .font(.system(size: 12))
                    .foregroundColor(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
            } else {
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack(spacing: 9) {
                        ForEach(model.informationBarFeatures, id: \.name) { descriptor in
                            QuickFeatureControl(model: model, descriptor: descriptor)
                        }
                    }
                }
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 9)
    }

    private var readOnlyInformation: some View {
        VStack(alignment: .leading, spacing: 7) {
            HStack(spacing: 5) {
                Image(systemName: "info.circle")
                Text("INFORMATION")
            }
            .font(.system(size: 10, weight: .semibold))
            .foregroundColor(.secondary)

            HStack(spacing: 18) {
                InformationBarField(title: "Camera", value: model.activeCameraName)
                InformationBarField(title: "Resolution", value: model.resolutionText)
                InformationBarField(title: "Pixel Format", value: model.pixelFormatText)
            }

            HStack(spacing: 18) {
                InformationBarField(title: "Acquisition", value: model.cameraStateText)
                InformationBarField(title: "Live Target", value: model.activeSlide.title)
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 9)
    }
}

private struct QuickFeatureControl: View {
    @ObservedObject var model: NativeCameraModel
    let descriptor: AmscopeCameraFeatureDescriptor

    private var editingDisabled: Bool {
        !model.canAccessFeatures ||
        !descriptor.isAvailable ||
        !descriptor.isWritable ||
        model.isFeatureWritePending ||
        model.isFeatureSnapshotLoading ||
        model.isCameraOperationPending
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 5) {
            Text(descriptor.displayName)
                .font(.system(size: 11, weight: .medium))
                .lineLimit(1)

            HStack(spacing: 5) {
                TextField(
                    "Value",
                    text: Binding(
                        get: { model.draftValue(for: descriptor) },
                        set: { model.setDraftValue($0, for: descriptor) }
                    )
                )
                .textFieldStyle(RoundedBorderTextFieldStyle())
                .frame(width: 76)
                .disabled(editingDisabled)

                if let unit = descriptor.unit, !unit.isEmpty {
                    Text(unit)
                        .font(.system(size: 10))
                        .foregroundColor(.secondary)
                        .lineLimit(1)
                }

                Button("Set") {
                    model.commitDraft(for: descriptor)
                }
                .disabled(editingDisabled)
            }
        }
        .padding(8)
        .background(
            RoundedRectangle(cornerRadius: 7)
                .fill(Color(nsColor: NSColor.controlBackgroundColor))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 7)
                .stroke(Color(nsColor: NSColor.separatorColor), lineWidth: 1)
        )
        .frame(minWidth: 150, maxWidth: 190)
        .help(model.featureError(for: descriptor) ?? descriptor.displayName)
    }
}

private struct InformationBarField: View {
    let title: String
    let value: String

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(title)
                .font(.system(size: 10, weight: .medium))
                .foregroundColor(.secondary)

            Text(value)
                .font(.system(size: 12, weight: .medium, design: .monospaced))
                .foregroundColor(.primary)
                .lineLimit(1)
        }
        .fixedSize(horizontal: true, vertical: false)
    }
}
