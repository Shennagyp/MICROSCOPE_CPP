import AppKit
import SwiftUI

struct MainWindowView: View {
    @ObservedObject var model: NativeCameraModel

    var body: some View {
        VStack(spacing: 0) {
            workspaceToolbar
            Divider()

            HSplitView {
                NativeDualPreviewSurface(
                    bridge: model.bridge,
                    activeSlide: model.activeSlide,
                    onActiveSlideChanged: model.selectActiveSlide,
                    onCapture: { slide, image in
                        model.captureGallery.capture(image, for: slide)
                    }
                )
                .frame(minWidth: 500, minHeight: 460)
                .clipped()
                .layoutPriority(1)

                if model.isWorkspaceDockVisible && !model.isWorkspaceDockFloating {
                    NativeWorkspaceDock(
                        model: model,
                        isFloating: false,
                        onClose: { model.isWorkspaceDockVisible = false },
                        onToggleFloating: { model.isWorkspaceDockFloating = true }
                    )
                    .frame(minWidth: 270, idealWidth: 340, maxWidth: 460)
                    .frame(maxHeight: .infinity)
                    .layoutPriority(0)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
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
                .accessibilityHidden(true)

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

private struct BottomDockPanelAnchor: NSViewRepresentable {
    @ObservedObject var model: NativeCameraModel

    func makeNSView(context: Context) -> BottomDockPanelAnchorNSView {
        BottomDockPanelAnchorNSView(model: model)
    }

    func updateNSView(_ nsView: BottomDockPanelAnchorNSView, context: Context) {
        nsView.synchronize()
    }
}

private final class BottomDockPanelAnchorNSView: NSView, NSWindowDelegate {
    private let model: NativeCameraModel
    private var floatingPanel: NSPanel?

    init(model: NativeCameraModel) {
        self.model = model
        super.init(frame: .zero)
    }

    required init?(coder: NSCoder) {
        fatalError("BottomDockPanelAnchorNSView does not support NSCoder initialization")
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        synchronize()
    }

    func synchronize() {
        guard model.isBottomDockDetached else {
            floatingPanel?.orderOut(nil)
            return
        }

        if floatingPanel == nil {
            let panel = NSPanel(
                contentRect: NSRect(x: 0, y: 0, width: 900, height: 58),
                styleMask: [.titled, .closable, .utilityWindow, .resizable],
                backing: .buffered,
                defer: false
            )
            panel.title = "Camera Controls"
            panel.isFloatingPanel = true
            panel.isReleasedWhenClosed = false
            panel.minSize = NSSize(width: 560, height: 42)
            panel.contentViewController = NSHostingController(
                rootView: BottomControlDock(model: model, isDetached: true)
            )
            panel.delegate = self
            floatingPanel = panel
        }

        if let panel = floatingPanel, !panel.isVisible {
            if let mainWindow = window {
                let mainFrame = mainWindow.frame
                let panelSize = panel.frame.size
                panel.setFrameOrigin(NSPoint(
                    x: mainFrame.midX - panelSize.width / 2,
                    y: mainFrame.maxY - panelSize.height - 50
                ))
            } else {
                panel.center()
            }
            panel.makeKeyAndOrderFront(nil)
        }
    }

    func windowWillClose(_ notification: Notification) {
        model.setBottomDockDetached(false)
    }
}
