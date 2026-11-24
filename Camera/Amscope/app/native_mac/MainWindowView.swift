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
            HStack(spacing: 8) {
                if model.isBottomDockCollapsed {
                    HStack(spacing: 6) {
                        Image(systemName: "slider.horizontal.3")
                            .foregroundColor(.secondary)
                        Text("Camera Controls")
                            .font(.system(size: 11, weight: .medium))
                    }
                    .padding(.leading, 10)
                    Spacer(minLength: 0)
                } else {
                    HSplitView {
                        editableControls
                            .frame(minWidth: 300, maxWidth: .infinity)
                        readOnlyInformation
                            .frame(minWidth: 270, maxWidth: .infinity)
                    }
                }

                Button {
                    model.setBottomDockCollapsed(!model.isBottomDockCollapsed)
                } label: {
                    Image(systemName: model.isBottomDockCollapsed ? "chevron.up" : "chevron.down")
                        .frame(width: 22, height: 24)
                }
                .buttonStyle(.borderless)
                .help(model.isBottomDockCollapsed ? "Expand camera controls" : "Collapse camera controls")

                Button {
                    model.setBottomDockDetached(!isDetached)
                } label: {
                    Image(systemName: isDetached ? "pin.fill" : "arrow.up.right.and.arrow.down.left")
                        .frame(width: 22, height: 24)
                }
                .buttonStyle(.borderless)
                .help(isDetached ? "Dock controls below the camera views" : "Detach controls into a floating panel")
                .padding(.trailing, 8)
            }
            .frame(height: model.isBottomDockCollapsed ? 28 : 50)
            .background(Color(nsColor: NSColor.windowBackgroundColor))
        }
        .fixedSize(horizontal: false, vertical: true)
    }

    private var editableControls: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 8) {
                if model.cameras.isEmpty {
                    Text(model.isEnumeratingCameras ? "Detecting cameras…" : "No camera detected")
                        .font(.system(size: 11))
                        .foregroundColor(.secondary)
                    Button { model.refreshCameras() } label: {
                        Image(systemName: "arrow.clockwise")
                    }
                    .buttonStyle(.borderless)
                    .disabled(model.isEnumeratingCameras || model.isCameraOperationPending)
                } else {
                    ForEach(model.cameras, id: \.deviceID) { camera in
                        Button {
                            model.openAndStartCamera(deviceID: camera.deviceID)
                        } label: {
                            HStack(spacing: 5) {
                                Image(systemName: "video.fill")
                                Text(camera.displayName)
                                    .lineLimit(1)
                                Text("Open + Start")
                                    .font(.system(size: 10, weight: .semibold))
                            }
                        }
                        .controlSize(.small)
                        .disabled(!model.canConnectDetectedCamera)
                        .help("Open this camera and start acquisition")
                    }
                }

                if model.cameraState == .open {
                    Button("Start") { model.startAcquisition() }
                        .controlSize(.small)
                        .disabled(!model.canStart)
                }
                if model.cameraState == .streaming {
                    Button("Stop") { model.stopAcquisition() }
                        .controlSize(.small)
                        .disabled(!model.canStop)
                }
                if model.canClose {
                    Button("Close") { model.closeCamera() }
                        .controlSize(.small)
                        .disabled(!model.canClose)
                }

                if !model.informationBarFeatures.isEmpty {
                    Rectangle()
                        .fill(Color(nsColor: NSColor.separatorColor))
                        .frame(width: 1, height: 26)

                    ForEach(model.informationBarFeatures, id: \.name) { descriptor in
                        QuickFeatureControl(model: model, descriptor: descriptor)
                    }
                }
            }
            .padding(.horizontal, 10)
            .frame(height: 46)
        }
    }

    private var readOnlyInformation: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 14) {
                InformationValue(title: "Camera", value: model.activeCameraName)
                InformationValue(title: "Resolution", value: model.resolutionText)
                InformationValue(title: "Format", value: model.pixelFormatText)
                InformationValue(title: "State", value: model.cameraStateText)
                InformationValue(title: "Live", value: model.activeSlide.title)
            }
            .padding(.horizontal, 10)
            .frame(height: 46)
        }
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
        HStack(spacing: 4) {
            Text(descriptor.displayName)
                .font(.system(size: 10, weight: .medium))
                .lineLimit(1)

            TextField(
                "Value",
                text: Binding(
                    get: { model.draftValue(for: descriptor) },
                    set: { model.setDraftValue($0, for: descriptor) }
                )
            )
            .textFieldStyle(RoundedBorderTextFieldStyle())
            .frame(width: 62)
            .disabled(editingDisabled)

            if let unit = descriptor.unit, !unit.isEmpty {
                Text(unit)
                    .font(.system(size: 9))
                    .foregroundColor(.secondary)
                    .lineLimit(1)
            }

            Button("Set") {
                model.commitDraft(for: descriptor)
            }
            .controlSize(.small)
            .disabled(editingDisabled)
        }
        .padding(.horizontal, 6)
        .padding(.vertical, 4)
        .background(
            RoundedRectangle(cornerRadius: 5)
                .fill(Color(nsColor: NSColor.controlBackgroundColor))
        )
        .help(model.featureError(for: descriptor) ?? descriptor.displayName)
    }
}

private struct InformationValue: View {
    let title: String
    let value: String

    var body: some View {
        HStack(spacing: 4) {
            Text(title)
                .font(.system(size: 9, weight: .medium))
                .foregroundColor(.secondary)
            Text(value)
                .font(.system(size: 10, weight: .medium, design: .monospaced))
                .lineLimit(1)
        }
        .fixedSize(horizontal: true, vertical: false)
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
