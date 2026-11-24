import AppKit
import SwiftUI

struct MainWindowView: View {
    @ObservedObject var model: NativeCameraModel

    var body: some View {
        VStack(spacing: 0) {
            NativeDualPreviewSurface(
                bridge: model.bridge,
                activeSlide: model.activeSlide,
                onActiveSlideChanged: model.selectActiveSlide
            )
            // The slide workspace owns only the space above the dock.
            // Clip the representable at this boundary so a native child view
            // can never paint over the pathologist's control strip.
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .clipped()
            .layoutPriority(1)

            if !model.isBottomDockDetached {
                // The dock is a separate, vertically reserved sibling, never
                // an overlay on either specimen viewport.
                BottomControlDock(model: model, isDetached: false)
                    .fixedSize(horizontal: false, vertical: true)
                    .layoutPriority(0)
            }
        }
        .frame(minWidth: 960, minHeight: 560)
        .background(Color.clear)
        .background(
            BottomDockPanelAnchor(model: model)
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
}

private struct BottomControlDock: View {
    @ObservedObject var model: NativeCameraModel
    let isDetached: Bool

    var body: some View {
        VStack(spacing: 0) {
            Divider()
            HStack(spacing: 7) {
                CameraConnectionControl(model: model)

                Rectangle()
                    .fill(Color(nsColor: NSColor.separatorColor))
                    .frame(width: 1, height: 28)

                SlideNavigationControl(model: model)

                Rectangle()
                    .fill(Color(nsColor: NSColor.separatorColor))
                    .frame(width: 1, height: 28)

                ObjectiveDockControl(model: model)

                Toggle(
                    "Auto Res",
                    isOn: Binding(
                        get: { model.isAutomaticResolutionEnabled },
                        set: { model.setAutomaticResolutionEnabled($0) }
                    )
                )
                .toggleStyle(CheckboxToggleStyle())
                .font(.system(size: 9))
                .help(model.automaticResolutionHelp)

                if model.isBottomDockCollapsed {
                    Spacer(minLength: 0)
                    Text("CONTROLS")
                        .font(.system(size: 9, weight: .semibold))
                        .foregroundColor(.secondary)
                } else {
                    Rectangle()
                        .fill(Color(nsColor: NSColor.separatorColor))
                        .frame(width: 1, height: 28)

                    featureControls
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                }

                Button {
                    model.setBottomDockCollapsed(!model.isBottomDockCollapsed)
                } label: {
                    Image(systemName: model.isBottomDockCollapsed ? "chevron.up" : "chevron.down")
                        .frame(width: 20, height: 24)
                }
                .buttonStyle(.borderless)
                .help(model.isBottomDockCollapsed ? "Expand camera controls" : "Collapse camera controls")

                Button {
                    model.setBottomDockDetached(!isDetached)
                } label: {
                    Image(systemName: isDetached ? "pin.fill" : "arrow.up.right.and.arrow.down.left")
                        .frame(width: 20, height: 24)
                }
                .buttonStyle(.borderless)
                .help(isDetached ? "Dock controls below the camera views" : "Detach controls into a floating panel")
                .padding(.trailing, 7)
            }
            .frame(height: model.isBottomDockCollapsed ? 28 : 50)
            .background(Color.clear)
        }
        .fixedSize(horizontal: false, vertical: true)
        .background(Color.clear)
    }

    private var dockFeatureDescriptors: [AmscopeCameraFeatureDescriptor] {
        let primaryNames = [
            "ExposureAuto",
            "ExposureTime",
            "AmcamAnalogGainPercent",
            "AmcamResolutionIndex"
        ]
        let primarySet = Set(primaryNames)
        let primary = primaryNames.compactMap { name in
            model.featureDescriptors.first(where: { $0.name == name })
        }
        return primary + model.featureDescriptors.filter {
            !primarySet.contains($0.name)
        }
    }

    private var featureControls: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 7) {
                if model.canAccessFeatures {
                    Button {
                        model.refreshFeatures()
                    } label: {
                        Image(systemName: "arrow.clockwise")
                    }
                    .buttonStyle(.borderless)
                    .help("Refresh camera feature values")
                    .disabled(model.isFeatureSnapshotLoading ||
                              model.isFeatureWritePending ||
                              model.isCameraOperationPending)

                    if model.featureDescriptors.isEmpty {
                        Text(model.isFeatureSnapshotLoading ? "Loading controls…" : "No feature controls reported")
                            .font(.system(size: 10))
                            .foregroundColor(.secondary)
                    } else {
                        ForEach(
                            Array(dockFeatureDescriptors.enumerated()),
                            id: \.element.name
                        ) { index, descriptor in
                            if index > 0 &&
                               dockFeatureDescriptors[index - 1].group.rawValue != descriptor.group.rawValue {
                                Rectangle()
                                    .fill(Color(nsColor: NSColor.separatorColor))
                                    .frame(width: 1, height: 30)
                            }
                            DockCameraFeatureControl(
                                model: model,
                                descriptor: descriptor
                            )
                        }
                    }
                } else {
                    Text("Connect a camera to load its controls")
                        .font(.system(size: 10))
                        .foregroundColor(.secondary)
                }
            }
            .padding(.horizontal, 7)
            .frame(height: 46)
        }
    }
}

private struct CameraConnectionControl: View {
    @ObservedObject var model: NativeCameraModel

    private var isConnected: Bool {
        model.openedCameraDeviceID != nil && model.cameraState != .closed
    }

    private var indicatorColor: Color {
        switch model.cameraState {
        case .open, .streaming:
            return .green
        case .starting, .stopping:
            return .orange
        case .faulted:
            return .red
        case .closed:
            return .secondary
        @unknown default:
            return .secondary
        }
    }

    var body: some View {
        HStack(spacing: 5) {
            Circle()
                .fill(indicatorColor)
                .frame(width: 7, height: 7)

            if isConnected {
                VStack(alignment: .leading, spacing: 1) {
                    Text(model.activeCameraName)
                        .font(.system(size: 10, weight: .medium))
                        .lineLimit(1)
                        .help("Right-click for camera actions")
                        .contextMenu {
                            if model.cameraState == .streaming {
                                Button {
                                    model.stopAcquisition()
                                } label: {
                                    Label("Stop", systemImage: "stop.fill")
                                }
                                .disabled(!model.canStop)
                            }

                            if model.canClose {
                                Button {
                                    model.closeCamera()
                                } label: {
                                    Label("Close", systemImage: "xmark.circle")
                                }
                                .disabled(!model.canClose)
                            }
                        }

                    Text(model.cameraStateText.uppercased())
                        .font(.system(size: 8, weight: .semibold))
                        .foregroundColor(.secondary)
                }
                .frame(maxWidth: 112, alignment: .leading)
            } else if model.cameras.isEmpty {
                Text(model.isEnumeratingCameras ? "Detecting…" : "No camera")
                    .font(.system(size: 10, weight: .medium))
                    .foregroundColor(.secondary)
            } else {
                Picker(
                    "Camera",
                    selection: $model.selectedDeviceID
                ) {
                    ForEach(model.cameras, id: \.deviceID) { camera in
                        Text(camera.displayName).tag(camera.deviceID)
                    }
                }
                .labelsHidden()
                .pickerStyle(PopUpButtonPickerStyle())
                .frame(width: 136)
                .disabled(!model.canConnectDetectedCamera)

                Button("Open + Start") {
                    model.openAndStartCamera(deviceID: model.selectedDeviceID)
                }
                .font(.system(size: 9, weight: .medium))
                .disabled(!model.canConnectDetectedCamera || model.selectedDeviceID.isEmpty)
            }

            Button {
                model.refreshCameras()
            } label: {
                Image(systemName: "arrow.clockwise")
            }
            .buttonStyle(.borderless)
            .help("Refresh detected cameras")
            .disabled(model.isEnumeratingCameras || model.isCameraOperationPending)
        }
        .padding(.leading, 10)
    }
}

private struct SlideNavigationControl: View {
    @ObservedObject var model: NativeCameraModel

    var body: some View {
        HStack(spacing: 4) {
            Text("LIVE")
                .font(.system(size: 8, weight: .semibold))
                .foregroundColor(.secondary)

            Picker(
                "Live slide",
                selection: Binding(
                    get: { model.activeSlide },
                    set: { model.selectActiveSlide($0) }
                )
            ) {
                Text("1").tag(NativeSlideID.slide1)
                Text("2").tag(NativeSlideID.slide2)
            }
            .labelsHidden()
            .pickerStyle(SegmentedPickerStyle())
            .frame(width: 76)
            .help("Select which slide receives the live camera frames")
        }
    }
}

private struct ObjectiveDockControl: View {
    @ObservedObject var model: NativeCameraModel

    var body: some View {
        Picker(
            "Objective",
            selection: Binding(
                get: { model.microscopeConfiguration.activeObjectiveID },
                set: { model.selectMicroscopeObjective($0) }
            )
        ) {
            ForEach(model.microscopeConfiguration.objectives) { objective in
                Text(String(format: "%.0f×", objective.magnification))
                    .tag(objective.id)
            }
        }
        .labelsHidden()
        .pickerStyle(MenuPickerStyle())
        .frame(width: 68)
        .disabled(model.isCameraOperationPending)
        .help(
            "Current manually selected microscope objective. Numerical aperture: " +
            (model.microscopeConfiguration.objectives.first(where: {
                $0.id == model.microscopeConfiguration.activeObjectiveID
            }).map { String(format: "%.2f", $0.numericalAperture) } ?? "unknown") +
            ". Configure objectives in File → Microscope Configuration."
        )
    }
}

private struct DockCameraFeatureControl: View {
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
        isResolutionControl ? 128 : 110
    }

    var body: some View {
        HStack(spacing: 6) {
            if descriptor.isWritable || isResolutionControl {
                editor
                    .frame(width: editorWidth)
            } else {
                editor
                    .fixedSize(horizontal: true, vertical: false)
            }
        }
        .frame(
            minWidth: descriptor.isWritable || isResolutionControl ? editorWidth + 12 : 0,
            minHeight: 36,
            alignment: .center
        )
        .padding(.horizontal, 6)
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

private func featureGroupTitle(_ rawValue: Int) -> String {
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
