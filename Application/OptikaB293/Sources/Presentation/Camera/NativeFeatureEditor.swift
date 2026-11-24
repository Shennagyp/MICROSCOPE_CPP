import SwiftUI

private enum NativeFeatureGroup: Int, CaseIterable, Identifiable {
    case general = 0
    case exposure
    case imageFormat
    case imageProcessing
    case color
    case trigger
    case sensor
    case transport
    case hardware

    var id: Int { rawValue }

    var title: String {
        switch self {
        case .general: return "General"
        case .exposure: return "Exposure"
        case .imageFormat: return "Image Format"
        case .imageProcessing: return "Image Processing"
        case .color: return "Color"
        case .trigger: return "Trigger"
        case .sensor: return "Sensor"
        case .transport: return "Transport"
        case .hardware: return "Hardware"
        }
    }
}

struct NativeFeatureSections: View {
    @ObservedObject var model: NativeCameraModel
    @ObservedObject var features: NativeCameraFeatureModel

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            if !model.canAccessFeatures {
                Text("Open a camera to inspect its supported features.")
                    .foregroundColor(OptikaPalette.secondaryText)
            } else {
                HStack {
                    Text("Camera Features")
                        .font(.headline)
                    Spacer()
                    if features.isFeatureSnapshotLoading {
                        Text("Loading…")
                            .font(.caption)
                            .foregroundColor(OptikaPalette.secondaryText)
                    }
                    Button("Refresh") {
                        features.refreshFeatures()
                    }
                    .disabled(features.isFeatureSnapshotLoading ||
                              features.isFeatureWritePending ||
                              model.isCameraOperationPending)
                }

                ForEach(NativeFeatureGroup.allCases) { group in
                    let descriptors = features.genericEditorDescriptors.filter {
                        $0.group.rawValue == group.rawValue
                    }
                    if !descriptors.isEmpty {
                        GroupBox(label: Text(group.title)) {
                            VStack(alignment: .leading, spacing: 6) {
                                ForEach(descriptors, id: \.name) { descriptor in
                                    NativeFeatureEditor(
                                        model: model,
                                        features: features,
                                        descriptor: descriptor
                                    )
                                }
                            }
                            .frame(maxWidth: .infinity, alignment: .leading)
                        }
                    }
                }
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

private struct NativeFeatureEditor: View {
    @ObservedObject var model: NativeCameraModel
    @ObservedObject var features: NativeCameraFeatureModel
    let descriptor: AmscopeCameraFeatureDescriptor

    private var editingDisabled: Bool {
        !model.canAccessFeatures ||
        !descriptor.isAvailable ||
        !descriptor.isWritable ||
        features.isFeatureWritePending ||
        features.isFeatureSnapshotLoading ||
        model.isCameraOperationPending
    }

    var body: some View {
        HStack(alignment: .center, spacing: 14) {
            VStack(alignment: .leading, spacing: 3) {
                Text(descriptor.displayName)
                    .fixedSize(horizontal: false, vertical: true)
                if let unit = descriptor.unit, !unit.isEmpty {
                    Text(unit)
                        .font(.caption)
                        .foregroundColor(OptikaPalette.secondaryText)
                }
                if let reason = descriptor.disabledReason, !reason.isEmpty {
                    Text(reason)
                        .font(.caption)
                        .foregroundColor(OptikaPalette.secondaryText)
                        .fixedSize(horizontal: false, vertical: true)
                }
                if let error = features.featureError(for: descriptor) {
                    Text(error)
                        .font(.caption)
                        .foregroundColor(OptikaPalette.live)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)

            editor
        }
        .padding(.vertical, 3)
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    @ViewBuilder
    private var editor: some View {
        switch descriptor.kind {
        case .boolean:
            Toggle(
                "",
                isOn: Binding(
                    get: { features.booleanValue(for: descriptor) },
                    set: { features.writeBoolean($0, for: descriptor) }
                )
            )
            .labelsHidden()
            .disabled(editingDisabled)

        case .integer:
            if !descriptor.discreteIntegerValues.isEmpty {
                Picker(
                    "",
                    selection: Binding(
                        get: { features.integerValue(for: descriptor) },
                        set: { features.writeInteger($0, for: descriptor) }
                    )
                ) {
                    ForEach(
                        descriptor.discreteIntegerValues.map(\.int64Value),
                        id: \.self
                    ) { value in
                        Text(String(value)).tag(value)
                    }
                }
                .labelsHidden()
                .disabled(editingDisabled)
                .frame(maxWidth: 220)
            } else {
                textEditor
            }

        case .floatingPoint, .string, .integerVector:
            textEditor

        case .enumeration:
            Picker(
                "",
                selection: Binding(
                    get: { features.enumerationValue(for: descriptor) },
                    set: { features.writeEnumeration($0, for: descriptor) }
                )
            ) {
                ForEach(descriptor.enumerationValues, id: \.self) { symbol in
                    Text(symbol).tag(symbol)
                }
            }
            .labelsHidden()
            .disabled(editingDisabled)
            .frame(maxWidth: 220)

        @unknown default:
            Text(features.displayValue(for: descriptor))
                .foregroundColor(OptikaPalette.secondaryText)
        }
    }

    private var textEditor: some View {
        HStack(spacing: 6) {
            TextField(
                "Value",
                text: Binding(
                    get: { features.draftValue(for: descriptor) },
                    set: { features.setDraftValue($0, for: descriptor) }
                )
            )
            .textFieldStyle(RoundedBorderTextFieldStyle())
            .frame(width: 140)
            .disabled(editingDisabled)

            Button("Set") {
                features.commitDraft(for: descriptor)
            }
            .disabled(editingDisabled || !descriptor.isWritable)
        }
    }
}
