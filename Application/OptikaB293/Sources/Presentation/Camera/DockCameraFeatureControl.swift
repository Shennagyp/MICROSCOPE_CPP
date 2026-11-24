import SwiftUI

struct DockCameraFeatureControl: View {
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
        VStack(alignment: .leading, spacing: 6) {
            // Feature names vary by camera and may be much longer than a
            // compact two-column row can display. Let the label wrap fully
            // and keep the editor on its own line.
            Text(descriptor.displayName)
                .font(.system(size: 12, weight: .medium))
                .foregroundColor(OptikaPalette.primaryText)
                .fixedSize(horizontal: false, vertical: true)
                .frame(maxWidth: .infinity, alignment: .leading)

            editor
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .frame(maxWidth: .infinity, minHeight: 42, alignment: .leading)
        .padding(.horizontal, 8)
        .padding(.vertical, 8)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(OptikaPalette.controlSurface.opacity(0.20))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 6)
                .stroke(OptikaPalette.separator.opacity(0.65), lineWidth: 0.5)
        )
        .help(featureHelpText)
        .accessibilityElement(children: .contain)
        .accessibilityLabel(descriptor.displayName)
    }

    private var featureHelpText: String {
        var parts = [descriptor.displayName, featureGroupTitle(descriptor.group.rawValue)]
        if let unit = descriptor.unit, !unit.isEmpty { parts.append("Unit: \(unit)") }
        if descriptor.hasNumericRange {
            if descriptor.integerRange {
                parts.append("Range: \(descriptor.minimumInteger)–\(descriptor.maximumInteger)")
                if descriptor.hasIntegerIncrement {
                    parts.append("Step: \(descriptor.integerIncrement)")
                }
            } else {
                parts.append("Range: \(descriptor.minimumFloatingPoint)–\(descriptor.maximumFloatingPoint)")
                if descriptor.hasFloatingPointIncrement {
                    parts.append("Step: \(descriptor.floatingPointIncrement)")
                }
            }
        }
        if let reason = descriptor.disabledReason, !reason.isEmpty { parts.append(reason) }
        if let error = model.featureError(for: descriptor) { parts.append(error) }
        return parts.joined(separator: " · ")
    }

    @ViewBuilder
    private var editor: some View {
        if !descriptor.isWritable {
            Text(model.displayValue(for: descriptor))
                .font(.system(size: 12, design: .monospaced))
                .foregroundColor(OptikaPalette.secondaryText)
                .lineLimit(1)
                .multilineTextAlignment(.leading)
        } else {
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
                .toggleStyle(SwitchToggleStyle())
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
                        ForEach(descriptor.discreteIntegerValues.map(\.int64Value), id: \.self) { value in
                            Text(String(value)).tag(value)
                        }
                    }
                    .labelsHidden()
                    .pickerStyle(PopUpButtonPickerStyle())
                    .disabled(editingDisabled)
                } else {
                    textEditor
                }

            case .enumeration:
                if descriptor.enumerationValues.isEmpty {
                    Text("No options")
                        .font(.system(size: 12))
                        .foregroundColor(OptikaPalette.secondaryText)
                } else {
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
                    .pickerStyle(MenuPickerStyle())
                    .disabled(editingDisabled)
                }

            case .floatingPoint, .string, .integerVector:
                textEditor

            @unknown default:
                Text(model.displayValue(for: descriptor))
                    .font(.system(size: 11))
                    .foregroundColor(OptikaPalette.secondaryText)
            }
        }
    }

    private var textEditor: some View {
        HStack(spacing: 3) {
            TextField(
                "Value",
                text: Binding(
                    get: { model.draftValue(for: descriptor) },
                    set: { model.setDraftValue($0, for: descriptor) }
                )
            )
            .textFieldStyle(RoundedBorderTextFieldStyle())
            .font(.system(size: 11, design: .monospaced))
            .frame(width: 88)
            .disabled(editingDisabled)

            Button("Set") {
                model.commitDraft(for: descriptor)
            }
            .disabled(editingDisabled)
        }
    }
}

func featureGroupTitle(_ rawValue: Int) -> String {
    switch rawValue {
    case 0: return "General"
    case 1: return "Exposure"
    case 2: return "Image Format"
    case 3: return "Image Processing"
    case 4: return "Color"
    case 5: return "Trigger"
    case 6: return "Sensor"
    case 7: return "Transport"
    case 8: return "Hardware"
    default: return "Other"
    }
}

