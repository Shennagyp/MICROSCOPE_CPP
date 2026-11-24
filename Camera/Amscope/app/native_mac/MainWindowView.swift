import AppKit
import SwiftUI

struct MainWindowView: View {
    @ObservedObject var model: NativeCameraModel

    var body: some View {
        VStack(spacing: 0) {
            NativePreviewSurface(bridge: model.bridge) { resolution, pixelFormat in
                model.updateFrameMetadata(
                    resolution: resolution,
                    pixelFormat: pixelFormat
                )
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            Divider()

            HStack(spacing: 20) {
                InformationBarField(title: "Camera", value: model.selectedCameraName)
                InformationBarField(title: "Resolution", value: model.resolutionText)
                InformationBarField(title: "Pixel Format", value: model.pixelFormatText)
                InformationBarField(title: "State", value: model.cameraStateText)

                ForEach(model.informationBarFeatures, id: \.name) { descriptor in
                    EditableFeatureInformationField(
                        model: model,
                        descriptor: descriptor
                    )
                }

                Spacer(minLength: 8)
            }
            .font(.system(size: 12))
            .padding(.horizontal, 14)
            .padding(.vertical, 9)
            .background(Color(nsColor: NSColor.windowBackgroundColor))
        }
        .frame(minWidth: 960, minHeight: 640)
        .alert(isPresented: $model.isShowingError) {
            Alert(
                title: Text("Camera Error"),
                message: Text(model.errorMessage),
                dismissButton: .default(Text("OK"))
            )
        }
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
        }
        .fixedSize(horizontal: true, vertical: false)
    }
}

private struct EditableFeatureInformationField: View {
    @ObservedObject var model: NativeCameraModel
    let descriptor: AmscopeCameraFeatureDescriptor

    private var isEditing: Binding<Bool> {
        Binding(
            get: {
                model.informationBarEditingFeatureName == descriptor.name
            },
            set: { presented in
                if presented {
                    model.informationBarEditingFeatureName = descriptor.name
                } else if model.informationBarEditingFeatureName == descriptor.name {
                    model.informationBarEditingFeatureName = nil
                }
            }
        )
    }

    private var displayValue: String {
        let value = model.displayValue(for: descriptor)
        guard let unit = descriptor.unit, !unit.isEmpty else {
            return value
        }
        return "\(value) \(unit)"
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(descriptor.displayName)
                .font(.system(size: 10, weight: .medium))
                .foregroundColor(.secondary)

            Text(displayValue)
                .font(.system(size: 12, weight: .medium, design: .monospaced))
                .foregroundColor(descriptor.isWritable ? .primary : .secondary)
                .contentShape(Rectangle())
                .onTapGesture(count: 2) {
                    guard model.canAccessFeatures &&
                          descriptor.isWritable &&
                          !model.isFeatureWritePending &&
                          !model.isFeatureSnapshotLoading else {
                        return
                    }
                    model.informationBarEditingFeatureName = descriptor.name
                }
                .popover(isPresented: isEditing) {
                    VStack(alignment: .leading, spacing: 10) {
                        Text("Edit \(descriptor.displayName)")
                            .font(.headline)

                        HStack(spacing: 8) {
                            TextField(
                                "Value",
                                text: Binding(
                                    get: { model.draftValue(for: descriptor) },
                                    set: { model.setDraftValue($0, for: descriptor) }
                                )
                            )
                            .textFieldStyle(RoundedBorderTextFieldStyle())
                            .frame(width: 150)

                            if let unit = descriptor.unit {
                                Text(unit)
                                    .foregroundColor(.secondary)
                            }
                        }

                        HStack {
                            Spacer()
                            Button("Cancel") {
                                model.informationBarEditingFeatureName = nil
                            }
                            Button("Apply") {
                                model.commitDraft(for: descriptor)
                                model.informationBarEditingFeatureName = nil
                            }
                            .keyboardShortcut(.defaultAction)
                            .disabled(model.isFeatureWritePending)
                        }
                    }
                    .padding(14)
                    .frame(width: 300)
                }
                .help(descriptor.isWritable
                    ? "Double-click to edit"
                    : descriptor.displayName)
        }
        .fixedSize(horizontal: true, vertical: false)
    }
}
