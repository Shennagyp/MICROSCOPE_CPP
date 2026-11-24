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
            ) { resolution, pixelFormat in
                model.updateFrameMetadata(
                    resolution: resolution,
                    pixelFormat: pixelFormat
                )
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            if !model.isBottomDockDetached {
                BottomControlDock(model: model, isDetached: false)
            }
        }
        .frame(minWidth: 960, minHeight: 560)
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
            .background(Color(nsColor: NSColor.windowBackgroundColor))
        }
        .fixedSize(horizontal: false, vertical: true)
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
                            Array(model.featureDescriptors.enumerated()),
                            id: \.element.name
                        ) { index, descriptor in
                            if index > 0 &&
                               model.featureDescriptors[index - 1].group.rawValue != descriptor.group.rawValue {
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
                    Text(model.cameraStateText.uppercased())
                        .font(.system(size: 8, weight: .semibold))
                        .foregroundColor(.secondary)
                }
                .frame(maxWidth: 112, alignment: .leading)

                if model.cameraState == .open {
                    Button("Start") { model.startAcquisition() }
                        .font(.system(size: 9))
                        .disabled(!model.canStart)
                }
                if model.cameraState == .streaming {
                    Button("Stop") { model.stopAcquisition() }
                        .font(.system(size: 9))
                        .disabled(!model.canStop)
                }
                if model.canClose {
                    Button("Close") { model.closeCamera() }
                        .font(.system(size: 9))
                        .disabled(!model.canClose)
                }
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

private struct DockCameraFeatureControl: View {
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
        VStack(alignment: .leading, spacing: 2) {
            HStack(spacing: 4) {
                Text(featureGroupTitle(descriptor.group.rawValue))
                    .font(.system(size: 8, weight: .medium))
                    .foregroundColor(.secondary)
                if let unit = descriptor.unit, !unit.isEmpty {
                    Text(unit)
                        .font(.system(size: 8))
                        .foregroundColor(.secondary)
                        .lineLimit(1)
                }
                if model.featureError(for: descriptor) != nil {
                    Circle()
                        .fill(Color.red)
                        .frame(width: 5, height: 5)
                }
            }

            HStack(spacing: 4) {
                Text(descriptor.displayName)
                    .font(.system(size: 9, weight: .medium))
                    .lineLimit(1)
                    .frame(width: 76, alignment: .leading)
                editor
                    .frame(width: 94)
            }
        }
        .frame(width: 178, height: 40, alignment: .leading)
        .padding(.horizontal, 5)
        .background(
            RoundedRectangle(cornerRadius: 4)
                .fill(Color(nsColor: NSColor.controlBackgroundColor))
        )
        .help(featureHelpText)
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
                .font(.system(size: 9, design: .monospaced))
                .foregroundColor(.secondary)
                .lineLimit(1)
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
                    .font(.system(size: 9))
                    .foregroundColor(.secondary)
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
