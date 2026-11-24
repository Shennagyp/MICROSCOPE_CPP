import AppKit
import SwiftUI

enum NativeWorkspaceDockTab: String, CaseIterable, Identifiable {
    case cameraControls
    case smartGallery

    var id: String { rawValue }

    var title: String {
        switch self {
        case .cameraControls: return "Camera Controls"
        case .smartGallery: return "Smart Gallery"
        }
    }

    var symbolName: String {
        switch self {
        case .cameraControls: return "slider.horizontal.3"
        case .smartGallery: return "rectangle.stack"
        }
    }
}

struct NativeWorkspaceDock: View {
    @ObservedObject var model: NativeCameraModel
    let isFloating: Bool
    let onClose: () -> Void
    let onToggleFloating: () -> Void

    var body: some View {
        Group {
            switch model.selectedWorkspaceDockTab {
            case .cameraControls:
                NativeCameraControlsDock(model: model)
            case .smartGallery:
                NativeSmartGalleryDock(gallery: model.captureGallery)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .overlay(alignment: .topTrailing) {
            HStack(spacing: 5) {
                dockChromeButton(
                    symbol: isFloating ? "sidebar.right" : "arrow.up.right.square",
                    description: isFloating ? "Dock in the main window" : "Float this dock",
                    action: onToggleFloating
                )
                dockChromeButton(
                    symbol: "xmark",
                    description: "Hide dock",
                    action: onClose
                )
            }
            .padding(.top, 8)
            .padding(.trailing, 9)
            .zIndex(2)
        }
        .background(Color(nsColor: NSColor.controlBackgroundColor))
        .overlay(
            Rectangle()
                .fill(Color(nsColor: NSColor.separatorColor))
                .frame(width: 1),
            alignment: .leading
        )
    }


    private func dockChromeButton(
        symbol: String,
        description: String,
        action: @escaping () -> Void
    ) -> some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(.system(size: 13, weight: .semibold))
                .frame(width: 30, height: 30)
                .background(
                    RoundedRectangle(cornerRadius: 7)
                        .fill(Color(nsColor: NSColor.windowBackgroundColor).opacity(0.94))
                )
                .overlay(
                    RoundedRectangle(cornerRadius: 7)
                        .stroke(Color(nsColor: NSColor.separatorColor).opacity(0.75), lineWidth: 0.7)
                )
                .shadow(color: .black.opacity(0.12), radius: 3, x: 0, y: 1)
        }
        .buttonStyle(PlainButtonStyle())
        .foregroundColor(.primary)
        .help(description)
        .accessibilityLabel(description)
    }
}

struct NativeWorkspaceDockTabRail: View {
    @ObservedObject var model: NativeCameraModel

    static let width: CGFloat = 26
    private let tabHeight: CGFloat = 104

    var body: some View {
        VStack(spacing: 0) {
            Spacer(minLength: 0)

            VStack(spacing: 0) {
                dockTabButton(.cameraControls)
                dockTabButton(.smartGallery)
            }

            Spacer(minLength: 0)
        }
        .frame(width: Self.width)
        .frame(maxHeight: .infinity)
    }

    private func dockTabButton(_ tab: NativeWorkspaceDockTab) -> some View {
        let isSelected = model.selectedWorkspaceDockTab == tab
        let accent = Color(nsColor: NSColor.controlAccentColor)

        return Button {
            model.selectedWorkspaceDockTab = tab
            model.isWorkspaceDockVisible = true
        } label: {
            VStack(spacing: 4) {
                Image(systemName: tab.symbolName)
                    .font(.system(size: 10, weight: .semibold))
                    .frame(width: 11, height: 11)

                Text(tab.title)
                    .font(.system(size: 9, weight: .semibold))
                    .lineLimit(1)
                    .minimumScaleFactor(0.8)
                    .frame(width: 82, height: 12)
                    .rotationEffect(.degrees(90))
                    .frame(width: 12, height: 82)
            }
            .frame(width: Self.width, height: tabHeight)
            .background(
                Rectangle().fill(
                    isSelected
                        ? accent
                        : Color(nsColor: NSColor.windowBackgroundColor)
                )
            )
            .overlay(
                Rectangle().stroke(
                    isSelected
                        ? accent
                        : Color(nsColor: NSColor.separatorColor),
                    lineWidth: isSelected ? 1 : 0.7
                )
            )
            .overlay(alignment: .leading) {
                Rectangle()
                    .fill(isSelected ? Color.white.opacity(0.82) : .clear)
                    .frame(width: 1.5)
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(PlainButtonStyle())
        .foregroundColor(isSelected ? .white : Color(nsColor: NSColor.labelColor))
        .help(tab.title)
        .accessibilityLabel(tab.title)
        .accessibilityHint("Shows the \(tab.title) dock")
    }
}

struct NativeWorkspaceDockPanelAnchor: NSViewRepresentable {
    let model: NativeCameraModel

    func makeNSView(context: Context) -> NativeWorkspaceDockPanelAnchorNSView {
        let view = NativeWorkspaceDockPanelAnchorNSView(model: model)
        view.synchronize()
        return view
    }

    func updateNSView(
        _ nsView: NativeWorkspaceDockPanelAnchorNSView,
        context: Context
    ) {
        nsView.synchronize()
    }
}

@MainActor
final class NativeWorkspaceDockPanelAnchorNSView: NSView, NSWindowDelegate {
    private let model: NativeCameraModel
    private var floatingPanel: NSPanel?
    private var floatingPanelFrameWasRestored = false

    init(model: NativeCameraModel) {
        self.model = model
        super.init(frame: .zero)
    }

    required init?(coder: NSCoder) {
        fatalError("NativeWorkspaceDockPanelAnchorNSView does not support NSCoder initialization")
    }

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()

        guard window != nil else {
            // SwiftUI may temporarily detach/re-host representable views while
            // reconciling the root hierarchy. Preserve the user's dock state;
            // only hide and detach the panel until a host window is available.
            if let panel = floatingPanel {
                panel.parent?.removeChildWindow(panel)
                panel.orderOut(nil)
            }
            return
        }

        synchronize()
    }

    func synchronize() {
        guard let mainWindow = window else {
            floatingPanel?.orderOut(nil)
            return
        }

        guard model.isWorkspaceDockVisible && model.isWorkspaceDockFloating else {
            if let panel = floatingPanel {
                if panel.parent === mainWindow {
                    mainWindow.removeChildWindow(panel)
                }
                panel.orderOut(nil)
            }
            return
        }

        if floatingPanel == nil {
            let panel = NSPanel(
                contentRect: NSRect(x: 0, y: 0, width: 360, height: 680),
                styleMask: [.titled, .closable, .miniaturizable, .resizable, .utilityWindow],
                backing: .buffered,
                defer: false
            )
            panel.title = "AmScope Workspace"
            panel.isFloatingPanel = true
            panel.isReleasedWhenClosed = false
            panel.minSize = NSSize(width: 280, height: 320)
            panel.setFrameAutosaveName("AmscopeWorkspaceDock")
            floatingPanelFrameWasRestored = panel.setFrameUsingName("AmscopeWorkspaceDock")
            panel.contentViewController = NSHostingController(
                rootView: NativeWorkspaceDock(
                    model: model,
                    isFloating: true,
                    onClose: { [weak self] in
                        guard let self = self else { return }
                        self.model.isWorkspaceDockVisible = false
                        self.model.isWorkspaceDockFloating = false
                    },
                    onToggleFloating: { [weak self] in
                        self?.model.isWorkspaceDockFloating = false
                    }
                )
            )
            panel.delegate = self
            floatingPanel = panel
        }

        guard let panel = floatingPanel else { return }
        if panel.parent !== mainWindow {
            panel.parent?.removeChildWindow(panel)
            mainWindow.addChildWindow(panel, ordered: .above)
        }

        guard !panel.isVisible else { return }

        let mainFrame = mainWindow.frame
        var panelFrame = panel.frame
        let visibleFrame = mainWindow.screen?.visibleFrame ?? NSScreen.main?.visibleFrame

        if let visibleFrame = visibleFrame {
            panelFrame.size.width = min(panelFrame.size.width, visibleFrame.width)
            panelFrame.size.height = min(panelFrame.size.height, visibleFrame.height)

            if floatingPanelFrameWasRestored {
                // Keep the user's saved panel position and size, clamping only
                // when the current display cannot contain the saved frame.
                panelFrame.origin.x = min(
                    max(panelFrame.origin.x, visibleFrame.minX),
                    visibleFrame.maxX - panelFrame.width
                )
                panelFrame.origin.y = min(
                    max(panelFrame.origin.y, visibleFrame.minY),
                    visibleFrame.maxY - panelFrame.height
                )
            } else {
                panelFrame.origin.x = min(
                    max(mainFrame.maxX - panelFrame.width - 18, visibleFrame.minX),
                    visibleFrame.maxX - panelFrame.width
                )
                panelFrame.origin.y = min(
                    max(mainFrame.maxY - panelFrame.height - 54, visibleFrame.minY),
                    visibleFrame.maxY - panelFrame.height
                )
            }
        } else if !floatingPanelFrameWasRestored {
            panelFrame.origin = NSPoint(
                x: mainFrame.maxX - panelFrame.width - 18,
                y: mainFrame.maxY - panelFrame.height - 54
            )
        }

        panel.setFrame(panelFrame, display: false)
        panel.makeKeyAndOrderFront(nil)
    }

    func windowWillClose(_ notification: Notification) {
        model.isWorkspaceDockFloating = false
        model.isWorkspaceDockVisible = false
    }
}

private struct NativeCameraControlsDock: View {
    @ObservedObject var model: NativeCameraModel

    var body: some View {
        ScrollView(.vertical, showsIndicators: true) {
            VStack(alignment: .leading, spacing: 14) {
                CameraDockConnectionSection(model: model)

                Divider()

                VStack(alignment: .leading, spacing: 8) {
                    Text("MICROSCOPE")
                        .font(.system(size: 13, weight: .bold))
                        .foregroundColor(.secondary)

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
                    .pickerStyle(PopUpButtonPickerStyle())
                    .disabled(model.isCameraOperationPending)
                    .help("Current objective. Configure objectives in File → Microscope Configuration.")

                    Toggle(
                        "Automatic resolution",
                        isOn: Binding(
                            get: { model.isAutomaticResolutionEnabled },
                            set: { model.setAutomaticResolutionEnabled($0) }
                        )
                    )
                    .font(.system(size: 12))
                    .disabled(!model.canAccessFeatures || model.isCameraOperationPending)
                    .help(model.automaticResolutionHelp)

                    HStack {
                        Text("Live frame destination")
                            .font(.system(size: 12))
                            .foregroundColor(.secondary)
                        Spacer(minLength: 6)
                        Picker(
                            "Live frame destination",
                            selection: Binding(
                                get: { model.activeSlide },
                                set: { model.selectActiveSlide($0) }
                            )
                        ) {
                            Text("Slide 1").tag(NativeSlideID.slide1)
                            Text("Slide 2").tag(NativeSlideID.slide2)
                        }
                        .labelsHidden()
                        .pickerStyle(SegmentedPickerStyle())
                        .frame(width: 132)
                        .disabled(model.isCameraOperationPending)
                    }
                }

                Divider()

                VStack(alignment: .leading, spacing: 8) {
                    HStack {
                        Text("CAMERA FEATURES")
                            .font(.system(size: 13, weight: .bold))
                            .foregroundColor(.secondary)

                        Spacer()

                        Button {
                            model.refreshFeatures()
                        } label: {
                            Image(systemName: "arrow.clockwise")
                                .font(.system(size: 13, weight: .medium))
                        }
                        .buttonStyle(BorderlessButtonStyle())
                        .help("Refresh camera feature values")
                        .disabled(!model.canAccessFeatures ||
                                  model.isFeatureSnapshotLoading ||
                                  model.isFeatureWritePending ||
                                  model.isCameraOperationPending)
                    }

                    if !model.canAccessFeatures {
                        Text("Open a camera to load its controls.")
                            .font(.system(size: 12))
                            .foregroundColor(.secondary)
                    } else if model.featureDescriptors.isEmpty {
                        Text(model.isFeatureSnapshotLoading
                             ? "Loading camera features…"
                             : "The camera reported no feature controls.")
                            .font(.system(size: 12))
                            .foregroundColor(.secondary)
                    } else {
                        ForEach(
                            Array(model.featureDescriptors.enumerated()),
                            id: \.element.name
                        ) { index, descriptor in
                            if index == 0 ||
                               model.featureDescriptors[index - 1].group.rawValue != descriptor.group.rawValue {
                                Text(featureGroupTitle(descriptor.group.rawValue).uppercased())
                                    .font(.system(size: 13, weight: .bold))
                                    .foregroundColor(.secondary)
                                    .padding(.top, index == 0 ? 0 : 5)
                            }

                            DockCameraFeatureControl(
                                model: model,
                                descriptor: descriptor
                            )
                            .frame(maxWidth: .infinity, alignment: .leading)
                        }
                    }
                }
            }
            .padding(14)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }
}

private struct CameraDockConnectionSection: View {
    @ObservedObject var model: NativeCameraModel

    private var isConnected: Bool {
        model.openedCameraDeviceID != nil && model.cameraState != .closed
    }

    private var stateColor: Color {
        switch model.cameraState {
        case .open, .streaming: return .green
        case .starting, .stopping: return .orange
        case .faulted: return .red
        case .closed: return .secondary
        @unknown default: return .secondary
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 6) {
                Text("CAMERA")
                    .font(.system(size: 13, weight: .bold))
                    .foregroundColor(.secondary)

                Spacer()

                Circle()
                    .fill(stateColor)
                    .frame(width: 7, height: 7)

                Text(model.cameraStateText.uppercased())
                    .font(.system(size: 13, weight: .bold))
                    .foregroundColor(.secondary)
            }

            Text(isConnected ? model.activeCameraName : "No camera connected")
                .font(.system(size: 13, weight: .semibold))
                .lineLimit(1)
                .help("Right-click the camera name for Stop and Close actions.")
                .contextMenu {
                    if model.cameraState == .streaming {
                        Button {
                            model.stopAcquisition()
                        } label: {
                            HStack(spacing: 8) {
                                Image(systemName: "stop.fill")
                                Text("Stop")
                            }
                        }
                        .disabled(!model.canStop)
                    }

                    if model.canClose {
                        Button {
                            model.closeCamera()
                        } label: {
                            HStack(spacing: 8) {
                                Image(systemName: "xmark.circle")
                                Text("Close")
                            }
                        }
                        .disabled(!model.canClose)
                    }
                }

            if !isConnected {
                Picker("Device", selection: $model.selectedDeviceID) {
                    Text("Select a camera").tag("")
                    ForEach(model.cameras, id: \.deviceID) { camera in
                        Text(camera.displayName).tag(camera.deviceID)
                    }
                }
                .pickerStyle(PopUpButtonPickerStyle())
                .disabled(!model.canConnectDetectedCamera)

                HStack {
                    Button {
                        model.refreshCameras()
                    } label: {
                        HStack(spacing: 5) {
                            Image(systemName: "arrow.clockwise")
                            Text("Refresh")
                        }
                    }
                    .disabled(model.isEnumeratingCameras || model.isCameraOperationPending)

                    Spacer()

                    Button {
                        model.openAndStartCamera(deviceID: model.selectedDeviceID)
                    } label: {
                        HStack(spacing: 5) {
                            Image(systemName: "play.fill")
                            Text("Open + Start")
                        }
                        .font(.system(size: 13, weight: .semibold))
                        .padding(.horizontal, 9)
                        .padding(.vertical, 5)
                        .background(
                            RoundedRectangle(cornerRadius: 5)
                                .fill(Color.accentColor.opacity(0.18))
                        )
                    }
                    .buttonStyle(PlainButtonStyle())
                    .disabled(!model.canConnectDetectedCamera || model.selectedDeviceID.isEmpty)
                }
            } else if model.cameraState == .open {
                Button {
                    model.startAcquisition()
                } label: {
                    HStack(spacing: 6) {
                        Image(systemName: "play.fill")
                        Text("Start preview")
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 5)
                    .background(
                        RoundedRectangle(cornerRadius: 5)
                            .fill(Color.accentColor.opacity(0.18))
                    )
                }
                .buttonStyle(PlainButtonStyle())
                .disabled(!model.canStart)
            } else if model.cameraState == .streaming {
                Text("Preview is running. Right-click the camera name to Stop or Close.")
                    .font(.system(size: 11))
                    .foregroundColor(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            } else if model.cameraState == .starting || model.cameraState == .stopping {
                Text("Camera operation in progress…")
                    .font(.system(size: 11))
                    .foregroundColor(.secondary)
            } else if model.cameraState == .faulted {
                Text("Camera faulted. Right-click the camera name to Close the session.")
                    .font(.system(size: 11))
                    .foregroundColor(.secondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
        .padding(12)
        .background(
            RoundedRectangle(cornerRadius: 7)
                .fill(Color(nsColor: NSColor.windowBackgroundColor))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 7)
                .stroke(Color(nsColor: NSColor.separatorColor).opacity(0.7), lineWidth: 0.7)
        )
    }
}

private struct NativeSmartGalleryDock: View {
    @ObservedObject var gallery: NativeCaptureGalleryStore

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    Text("CAPTURES")
                        .font(.system(size: 13, weight: .bold))
                        .foregroundColor(.secondary)
                    Text(gallery.captures.count == 80
                         ? "Latest 80 captures"
                         : "\(gallery.captures.count) captures")
                        .font(.system(size: 13, weight: .semibold))
                }

                Spacer()

                if gallery.isCapturing {
                    Text("● Saving…")
                        .font(.system(size: 13, weight: .medium))
                        .foregroundColor(.secondary)
                }
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 9)

            if let error = gallery.captureErrorMessage {
                HStack(alignment: .top, spacing: 6) {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .foregroundColor(.orange)
                    Text(error)
                        .font(.system(size: 11))
                        .frame(maxWidth: .infinity, alignment: .leading)
                    Button {
                        gallery.dismissCaptureError()
                    } label: {
                        Image(systemName: "xmark")
                    }
                    .buttonStyle(BorderlessButtonStyle())
                }
                .padding(8)
                .background(Color.orange.opacity(0.10))
                .padding(.horizontal, 8)
                .padding(.bottom, 8)
            }

            Divider()

            if gallery.captures.isEmpty {
                VStack(spacing: 10) {
                    Image(systemName: "photo.on.rectangle.angled")
                        .font(.system(size: 28, weight: .light))
                        .foregroundColor(.secondary)
                    Text("No captures yet")
                        .font(.system(size: 13, weight: .semibold))
                    Text("Use the camera button on a slide's floating toolbar to save its current image.")
                        .font(.system(size: 12))
                        .foregroundColor(.secondary)
                        .multilineTextAlignment(.center)
                        .frame(maxWidth: 220)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .padding(16)
            } else {
                ScrollView(.vertical, showsIndicators: true) {
                    VStack(spacing: 8) {
                        ForEach(gallery.captures) { item in
                            captureRow(item)
                        }
                    }
                    .padding(10)
                }
            }

            Divider()
            HStack {
                Image(systemName: "folder")
                    .foregroundColor(.secondary)
                Button {
                    gallery.revealCaptureFolder()
                } label: {
                    HStack(spacing: 5) {
                        Image(systemName: "folder")
                        VStack(alignment: .leading, spacing: 1) {
                            Text("Pictures / AmScope Captures")
                                .font(.system(size: 11))
                            Text("Latest 80 shown; older PNGs stay on disk.")
                                .font(.system(size: 10))
                                .foregroundColor(.secondary)
                        }
                    }
                }
                .buttonStyle(BorderlessButtonStyle())
                .help("Open the complete capture folder in Finder")
                Spacer()
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 7)
        }
    }

    private func captureRow(_ item: NativeCaptureItem) -> some View {
        Button {
            NSWorkspace.shared.open(item.fileURL)
        } label: {
            HStack(spacing: 9) {
                Group {
                    if let image = NSImage(data: item.thumbnailData) {
                        Image(nsImage: image)
                            .resizable()
                            .aspectRatio(contentMode: .fit)
                    } else {
                        ZStack {
                            Color.black.opacity(0.08)
                            Image(systemName: "photo")
                                .foregroundColor(.secondary)
                        }
                    }
                }
                .frame(width: 116, height: 84)
                .background(Color.black.opacity(0.06))
                .clipShape(RoundedRectangle(cornerRadius: 4))

                VStack(alignment: .leading, spacing: 5) {
                    Text(item.slide.title)
                        .font(.system(size: 13, weight: .semibold))
                    Text(item.dimensionsText)
                        .font(.system(size: 13, design: .monospaced))
                        .foregroundColor(.secondary)
                    Text(item.capturedAt, style: .date)
                        .font(.system(size: 10))
                        .foregroundColor(.secondary)
                    Text(item.capturedAt, style: .time)
                        .font(.system(size: 10))
                        .foregroundColor(.secondary)
                }

                Spacer(minLength: 0)
                Image(systemName: "arrow.up.right")
                    .font(.system(size: 11))
                    .foregroundColor(.secondary)
            }
            .padding(7)
            .background(
                RoundedRectangle(cornerRadius: 6)
                    .fill(Color(nsColor: NSColor.windowBackgroundColor))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 6)
                    .stroke(Color(nsColor: NSColor.separatorColor).opacity(0.7), lineWidth: 0.6)
            )
        }
        .buttonStyle(PlainButtonStyle())
        .help("Open capture in the default image viewer")
        .contextMenu {
            Button {
                gallery.reveal(item)
            } label: {
                HStack(spacing: 8) {
                    Image(systemName: "folder")
                    Text("Show in Finder")
                }
            }

            Button {
                gallery.delete(item)
            } label: {
                HStack(spacing: 8) {
                    Image(systemName: "trash")
                    Text("Delete Capture")
                }
            }
        }
    }
}
