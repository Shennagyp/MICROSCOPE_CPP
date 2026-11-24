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

    var body: some View {
        if model.cameraState == .closed {
            Text("Open a camera to inspect its supported features.")
                .foregroundColor(.secondary)
        } else {
            HStack {
                Text("Camera Features")
                    .font(.headline)
                Spacer()
                if model.isFeatureSnapshotLoading {
                    Text("Loading…")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
                Button("Refresh") {
                    model.refreshFeatures()
                }
                .disabled(model.isFeatureSnapshotLoading ||
                          model.isFeatureWritePending ||
                          model.isCameraOperationPending)
            }

            ForEach(NativeFeatureGroup.allCases) { group in
                let descriptors = model.featureDescriptors.filter {
                    $0.group.rawValue == group.rawValue
                }
                if !descriptors.isEmpty {
                    Section(header: Text(group.title)) {
                        ForEach(descriptors, id: \.name) { descriptor in
                            NativeFeatureEditor(
                                model: model,
                                descriptor: descriptor
                            )
                        }
                    }
                }
            }
        }
    }
}

private struct NativeFeatureEditor: View {
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
        HStack(alignment: .center, spacing: 14) {
            VStack(alignment: .leading, spacing: 3) {
                Text(descriptor.displayName)
                if let unit = descriptor.unit, !unit.isEmpty {
                    Text(unit)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
                if let reason = descriptor.disabledReason, !reason.isEmpty {
                    Text(reason)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
                if let error = model.featureError(for: descriptor) {
                    Text(error)
                        .font(.caption)
                        .foregroundColor(.red)
                }
            }

            Spacer(minLength: 12)

            editor
        }
        .padding(.vertical, 3)
    }

    @ViewBuilder
    private var editor: some View {
        switch descriptor.kind {
        case .boolean:
            Toggle(
                "",
                isOn: Binding(
                    get: { model.booleanValue(for: descriptor) },
                    set: { model.writeBoolean($0, for: descriptor) }
                )
            )
            .labelsHidden()
            .disabled(editingDisabled)

        case .integer:
            if !descriptor.discreteIntegerValues.isEmpty {
                Picker(
                    "",
                    selection: Binding(
                        get: { model.integerValue(for: descriptor) },
                        set: { model.writeInteger($0, for: descriptor) }
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
                    get: { model.enumerationValue(for: descriptor) },
                    set: { model.writeEnumeration($0, for: descriptor) }
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
            Text(model.displayValue(for: descriptor))
                .foregroundColor(.secondary)
        }
    }

    private var textEditor: some View {
        HStack(spacing: 6) {
            TextField(
                "Value",
                text: Binding(
                    get: { model.draftValue(for: descriptor) },
                    set: { model.setDraftValue($0, for: descriptor) }
                )
            )
            .textFieldStyle(RoundedBorderTextFieldStyle())
            .frame(width: 140)
            .disabled(editingDisabled)

            Button("Set") {
                model.commitDraft(for: descriptor)
            }
            .disabled(editingDisabled || !descriptor.isWritable)
        }
    }
}
