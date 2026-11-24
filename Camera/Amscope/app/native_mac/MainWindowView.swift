import AppKit
import SwiftUI

struct MainWindowView: View {
    @ObservedObject var model: NativeCameraModel

    private let minimumPreviewWidth: CGFloat = 500
    private let dockDividerWidth: CGFloat = 6

    var body: some View {
        VStack(spacing: 0) {
            workspaceToolbar
            Divider()

            GeometryReader { geometry in
                let maximumDockWidth = max(
                    280,
                    geometry.size.width - minimumPreviewWidth - dockDividerWidth
                )
                let dockWidth = min(model.workspaceDockWidth, maximumDockWidth)

                HStack(spacing: 0) {
                    NativeDualPreviewSurface(
                        bridge: model.bridge,
                        activeSlide: model.activeSlide,
                        onActiveSlideChanged: model.selectActiveSlide,
                        onCapture: { slide, image in
                            model.captureGallery.capture(image, for: slide)
                        }
                    )
                    .frame(minWidth: minimumPreviewWidth, maxWidth: .infinity, maxHeight: .infinity)
                    .clipped()
                    .layoutPriority(1)

                    if model.isWorkspaceDockVisible && !model.isWorkspaceDockFloating {
                        NativeWorkspaceDockResizeDivider(
                            dockWidth: dockWidth,
                            maximumWidth: maximumDockWidth,
                            onResize: { model.setWorkspaceDockWidth($0, maximum: maximumDockWidth) }
                        )

                        NativeWorkspaceDock(
                            model: model,
                            isFloating: false,
                            onClose: { model.isWorkspaceDockVisible = false },
                            onToggleFloating: { model.isWorkspaceDockFloating = true }
                        )
                        .frame(width: dockWidth, maxHeight: .infinity)
                        .layoutPriority(0)
                    }
                }
                .frame(width: geometry.size.width, height: geometry.size.height)
            }
        }
        .frame(minWidth: 960, minHeight: 640)
        .background(Color.clear)
        .background(
            NativeWorkspaceDockPanelAnchor(model: model)
                .frame(width: 1, height: 1)
                .opacity(0)
        )
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

    private var workspaceToolbar: some View {
        HStack(spacing: 8) {
            Image(systemName: "microscope")
                .font(.system(size: 13, weight: .semibold))
                .foregroundColor(.secondary)

            Text("MICROSCOPE WORKSPACE")
                .font(.system(size: 10, weight: .bold))
                .tracking(0.7)

            Spacer()

            Button {
                model.selectedWorkspaceDockTab = .cameraControls
                model.isWorkspaceDockVisible = true
            } label: {
                HStack(spacing: 5) {
                    Image(systemName: "slider.horizontal.3")
                    Text("Camera Controls")
                }
            }
            .buttonStyle(.borderless)
            .font(.system(size: 10, weight: .medium))
            .help("Show Camera Controls dock")

            Button {
                model.selectedWorkspaceDockTab = .smartGallery
                model.isWorkspaceDockVisible = true
            } label: {
                HStack(spacing: 5) {
                    Image(systemName: "rectangle.stack")
                    Text("Smart Gallery")
                }
            }
            .buttonStyle(.borderless)
            .font(.system(size: 10, weight: .medium))
            .help("Show Smart Gallery dock")

            Button {
                model.isWorkspaceDockVisible.toggle()
            } label: {
                Image(systemName: model.isWorkspaceDockVisible ? "sidebar.right" : "sidebar.left")
                    .frame(width: 24, height: 24)
            }
            .buttonStyle(.borderless)
            .help(model.isWorkspaceDockVisible ? "Hide dock panel" : "Show dock panel")
            .accessibilityLabel(model.isWorkspaceDockVisible ? "Hide dock panel" : "Show dock panel")
        }
        .padding(.horizontal, 12)
        .frame(height: 36)
        .background(Color(nsColor: NSColor.windowBackgroundColor).opacity(0.55))
    }
}


private struct NativeWorkspaceDockResizeDivider: View {
    let dockWidth: CGFloat
    let maximumWidth: CGFloat
    let onResize: (CGFloat) -> Void

    @State private var dragStartWidth: CGFloat?
    @State private var isHovering = false

    var body: some View {
        Rectangle()
            .fill(Color(nsColor: NSColor.separatorColor).opacity(isHovering ? 0.9 : 0.45))
            .frame(width: 6)
            .overlay {
                Capsule()
                    .fill(isHovering ? Color.accentColor : Color(nsColor: NSColor.separatorColor))
                    .frame(width: isHovering ? 2 : 1)
                    .padding(.vertical, 5)
            }
            .contentShape(Rectangle())
            .onHover { hovering in
                isHovering = hovering
                if hovering {
                    NSCursor.resizeLeftRight.push()
                } else {
                    NSCursor.pop()
                }
            }
            .gesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { value in
                        if dragStartWidth == nil {
                            dragStartWidth = dockWidth
                        }
                        let start = dragStartWidth ?? dockWidth
                        onResize(min(max(start - value.translation.width, 280), maximumWidth))
                    }
                    .onEnded { value in
                        let start = dragStartWidth ?? dockWidth
                        onResize(min(max(start - value.translation.width, 280), maximumWidth))
                        dragStartWidth = nil
                    }
            )
            .help("Drag to resize the workspace dock")
            .accessibilityLabel("Resize workspace dock")
    }
}

struct DockCameraFeatureControl: View {
    @ObservedObject var model: NativeCameraModel
    let descriptor: AmscopeCameraFeatureDescriptor

    private var isResolutionControl: Bool {
        descriptor.name == "AmcamResolutionIndex"
    }

    private var editingDisabled: Bool {
        !model.canAccessFeatures ||
        !descriptor.isAvailable ||
        (isResolutionControl && model.isAutomaticResolutionEnabled) ||
        !(isResolutionControl
            ? model.canChangePreviewResolution
            : descriptor.isWritable) ||
        model.isFeatureWritePending ||
        model.isFeatureSnapshotLoading ||
        model.isCameraOperationPending
    }

    private var editorWidth: CGFloat {
        if isResolutionControl { return 126 }

        switch descriptor.kind {
        case .boolean:
            return 42
        case .integer, .enumeration:
            return 104
        case .floatingPoint, .string, .integerVector:
            return descriptor.isWritable ? 100 : 86
        @unknown default:
            return 86
        }
    }

    var body: some View {
        HStack(alignment: .center, spacing: 8) {
            Text(descriptor.displayName)
                .font(.system(size: 9, weight: .medium))
                .foregroundColor(.primary)
                .lineLimit(1)
                .truncationMode(.tail)
                .frame(width: 82, alignment: .leading)

            editor
                .frame(width: editorWidth, alignment: .leading)
        }
        .frame(minHeight: 36, alignment: .center)
        .padding(.horizontal, 8)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(Color(nsColor: NSColor.controlBackgroundColor))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 6)
                .stroke(Color(nsColor: NSColor.separatorColor).opacity(0.65), lineWidth: 0.5)
        )
        .fixedSize(horizontal: true, vertical: false)
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
        if isResolutionControl {
            parts.append("Changing resolution briefly stops and restarts acquisition")
        }
        if let error = model.featureError(for: descriptor) { parts.append(error) }
        return parts.joined(separator: " · ")
    }

    @ViewBuilder
    private var editor: some View {
        if !descriptor.isWritable && !isResolutionControl {
            Text(model.displayValue(for: descriptor))
                .font(.system(size: 9, design: .monospaced))
                .foregroundColor(.secondary)
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
                        .font(.system(size: 9))
                        .foregroundColor(.secondary)
                } else {
                    Picker(
                        "",
                        selection: Binding(
                            get: { model.enumerationValue(for: descriptor) },
                            set: { symbol in
                                if isResolutionControl {
                                    model.selectManualPreviewResolution(to: symbol)
                                } else {
                                    model.writeEnumeration(symbol, for: descriptor)
                                }
                            }
                        )
                    ) {
                        ForEach(descriptor.enumerationValues, id: \.self) { symbol in
                            Text(displayOption(symbol)).tag(symbol)
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
                    .font(.system(size: 9))
                    .foregroundColor(.secondary)
            }
        }
    }

    private func displayOption(_ symbol: String) -> String {
        guard isResolutionControl,
              let separator = symbol.firstIndex(of: ":") else {
            return symbol
        }
        let dimensions = symbol[symbol.index(after: separator)...]
        return String(dimensions).replacingOccurrences(of: "x", with: " × ")
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
            .font(.system(size: 9, design: .monospaced))
            .frame(width: 64)
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
