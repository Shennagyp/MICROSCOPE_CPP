import Foundation
import SwiftUI

final class NativeCameraModel: ObservableObject {
    static let shared = NativeCameraModel()

    let bridge = AmscopeNativeCameraBridge()

    @Published private(set) var cameras: [AmscopeCameraDevice] = []
    @Published var selectedDeviceID = ""
    @Published private(set) var cameraState: AmscopeNativeCameraState = .closed
    @Published private(set) var resolutionText = "—"
    @Published private(set) var pixelFormatText = "—"
    @Published var errorMessage = ""
    @Published var isShowingError = false

    private init() {
        bridge.onAcquisitionError = { [weak self] error in
            self?.present(error)
            self?.refreshState()
        }
    }

    var selectedCameraName: String {
        cameras.first(where: { $0.deviceID == selectedDeviceID })?.displayName
            ?? "No camera selected"
    }

    var cameraStateText: String {
        switch cameraState {
        case .closed: return "Disconnected"
        case .open: return "Ready"
        case .starting: return "Starting"
        case .streaming: return "Streaming"
        case .stopping: return "Stopping"
        case .faulted: return "Faulted"
        @unknown default: return "Unknown"
        }
    }

    var canOpen: Bool {
        cameraState == .closed && !selectedDeviceID.isEmpty
    }

    var canClose: Bool {
        cameraState != .closed
    }

    var canStart: Bool {
        cameraState == .open
    }

    var canStop: Bool {
        cameraState == .streaming
    }

    func refreshCameras() {
        bridge.enumerateCameras { [weak self] devices, error, _, _ in
            guard let self = self else { return }
            if let error = error {
                self.present(error)
                return
            }

            self.cameras = devices
            if !devices.contains(where: { $0.deviceID == self.selectedDeviceID }) {
                self.selectedDeviceID = devices.first?.deviceID ?? ""
            }
            self.refreshState()
        }
    }

    func openSelectedCamera() {
        guard canOpen else { return }
        bridge.openDeviceID(selectedDeviceID) { [weak self] error, _, _ in
            guard let self = self else { return }
            self.refreshState()
            if let error = error {
                self.present(error)
            }
        }
    }

    func closeCamera() {
        guard canClose else { return }
        bridge.close { [weak self] error, _, _ in
            guard let self = self else { return }
            self.refreshState()
            if let error = error {
                self.present(error)
            }
        }
    }

    func startAcquisition() {
        guard canStart else { return }
        bridge.start { [weak self] error, _, _ in
            guard let self = self else { return }
            self.refreshState()
            if let error = error {
                self.present(error)
            }
        }
    }

    func stopAcquisition() {
        guard canStop else { return }
        bridge.stop { [weak self] error, _, _ in
            guard let self = self else { return }
            self.refreshState()
            if let error = error {
                self.present(error)
            }
        }
    }

    func updateFrameMetadata(resolution: String, pixelFormat: String) {
        // Avoid invalidating SwiftUI for every incoming frame. Only changes in
        // dimensions or format update these low-frequency properties.
        if resolutionText != resolution {
            resolutionText = resolution
        }
        if pixelFormatText != pixelFormat {
            pixelFormatText = pixelFormat
        }
    }

    private func refreshState() {
        cameraState = bridge.state
    }

    private func present(_ error: NSError) {
        errorMessage = error.localizedDescription
        isShowingError = true
    }
}
