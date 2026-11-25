import Combine
import Foundation

@MainActor
final class NativeCameraModel: ObservableObject {
    private let bridge: AmscopeNativeCameraBridge
    var frameFeed: NativeCameraFrameFeed { bridge }
    lazy var features = NativeCameraFeatureModel(
        bridge: bridge,
        accessSnapshot: { [weak self] in
            guard let self else { return .unavailable }
            return NativeCameraFeatureAccessSnapshot(
                canAccessFeatures: self.canAccessFeatures,
                isCameraOperationPending: self.isCameraOperationPending
            )
        },
        presentError: { [weak self] error in self?.present(error) }
    )
    @Published private(set) var cameras: [AmscopeCameraDevice] = []
    @Published var selectedDeviceID = ""
    @Published private(set) var cameraState: AmscopeNativeCameraState = .closed
    @Published private(set) var openedCameraDeviceID: String?
    @Published var errorMessage = ""
    @Published var isShowingError = false
    @Published private var isCameraCommandPending = false
    @Published private(set) var isCameraTerminationPending = false
    @Published private(set) var isEnumeratingCameras = false

    var isCameraOperationPending: Bool {
        isCameraCommandPending || isCameraTerminationPending
    }

    init(bridge: AmscopeNativeCameraBridge) {
        self.bridge = bridge

        bridge.onAcquisitionError = { [weak self] error in
            MainActor.assumeIsolated {
                guard let self = self else { return }
                self.present(error)
                self.refreshState()
                self.reconcileFeatureSnapshotForCurrentCameraState()
            }
        }
    }

    var selectedCameraName: String {
        cameras.first(where: { $0.deviceID == selectedDeviceID })?.displayName
            ?? "No camera selected"
    }

    var activeCameraName: String {
        guard let deviceID = openedCameraDeviceID else {
            return "Not connected"
        }
        return cameras.first(where: { $0.deviceID == deviceID })?.displayName
            ?? "Connected camera"
    }

    var canConnectDetectedCamera: Bool {
        cameraState == .closed &&
            !isCameraOperationPending &&
            !isEnumeratingCameras
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

    var canAccessFeatures: Bool {
        cameraState == .open || cameraState == .streaming
    }

    /// Automatic resolution is a persisted user preference, not a camera
    /// command. It remains editable while disconnected or while features load;
    /// the coordinator applies it when a usable feature snapshot is available.
    var canConfigureAutomaticResolution: Bool {
        !isCameraOperationPending
    }

    var canChangePreviewResolution: Bool {
        canAccessFeatures &&
            !isCameraOperationPending &&
            !features.isFeatureWritePending &&
            !features.isFeatureSnapshotLoading
    }

    var canOpen: Bool {
        cameraState == .closed &&
            !selectedDeviceID.isEmpty &&
            !isCameraOperationPending &&
            !isEnumeratingCameras
    }

    var canClose: Bool {
        cameraState != .closed && !isCameraOperationPending
    }

    var canStart: Bool {
        cameraState == .open && !isCameraOperationPending
    }

    var canStop: Bool {
        cameraState == .streaming && !isCameraOperationPending
    }

    func refreshCameras() {
        guard !isEnumeratingCameras && !isCameraOperationPending else { return }
        isEnumeratingCameras = true
        bridge.enumerateCameras { [weak self] devices, error, _, _ in
            MainActor.assumeIsolated {
                guard let self = self else { return }
                self.isEnumeratingCameras = false
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
    }

    func openSelectedCamera() {
        guard canOpen else { return }
        openCamera(deviceID: selectedDeviceID, startImmediately: false)
    }

    func openAndStartCamera(deviceID: String) {
        guard canConnectDetectedCamera &&
              cameras.contains(where: { $0.deviceID == deviceID }) else { return }
        openCamera(deviceID: deviceID, startImmediately: true)
    }

    private func openCamera(deviceID: String, startImmediately: Bool) {
        guard canConnectDetectedCamera,
              cameras.contains(where: { $0.deviceID == deviceID }) else { return }

        selectedDeviceID = deviceID
        features.beginNewCameraSession()
        isCameraCommandPending = true
        bridge.openDeviceID(deviceID) { [weak self] error, _, _ in
            MainActor.assumeIsolated {
                guard let self = self else { return }
                self.isCameraCommandPending = false
                self.features.finishCameraOperation()
                self.refreshState()
                if let error = error {
                    self.openedCameraDeviceID = nil
                    self.present(error)
                    self.reconcileFeatureSnapshotForCurrentCameraState()
                } else {
                    self.openedCameraDeviceID = deviceID
                    if startImmediately {
                        self.startAcquisition()
                    } else {
                        self.reconcileFeatureSnapshotForCurrentCameraState()
                    }
                }
            }
        }
    }
    func closeCamera() {
        guard canClose else { return }
        isCameraCommandPending = true
        bridge.close { [weak self] error, _, _ in
            MainActor.assumeIsolated {
                guard let self = self else { return }
                self.isCameraCommandPending = false
                self.features.finishCameraOperation()
                self.refreshState()
                if let error = error {
                    self.present(error)
                }
                self.reconcileFeatureSnapshotForCurrentCameraState()
            }
        }
    }

    func closeForTermination(completion: @escaping (Error?) -> Void) {
        guard !isCameraTerminationPending else {
            let error = NSError(
                domain: "OPTIKA-B293.Camera",
                code: 1,
                userInfo: [NSLocalizedDescriptionKey: "A camera shutdown is already pending."]
            )
            completion(error)
            return
        }

        if cameraState == .closed && !isCameraCommandPending {
            completion(nil)
            return
        }

        // Keep all camera controls and feature access gated while this close is
        // queued behind any command that was already accepted by the controller.
        isCameraTerminationPending = true
        bridge.close { [weak self] error, _, _ in
            MainActor.assumeIsolated {
                guard let self = self else {
                    completion(error)
                    return
                }

                self.isCameraCommandPending = false
                self.features.finishCameraOperation()
                self.refreshState()
                if let error = error {
                    self.present(error)
                }
                self.isCameraTerminationPending = false
                self.reconcileFeatureSnapshotForCurrentCameraState()
                completion(error)
            }
        }
    }

    func startAcquisition() {
        guard canStart else { return }
        isCameraCommandPending = true
        bridge.start { [weak self] error, _, _ in
            MainActor.assumeIsolated {
                guard let self = self else { return }
                self.isCameraCommandPending = false
                self.features.finishCameraOperation()
                self.refreshState()
                if let error = error {
                    self.present(error)
                }
                self.reconcileFeatureSnapshotForCurrentCameraState()
            }
        }
    }

    func stopAcquisition() {
        guard canStop else { return }
        isCameraCommandPending = true
        bridge.stop { [weak self] error, _, _ in
            MainActor.assumeIsolated {
                guard let self = self else { return }
                self.isCameraCommandPending = false
                self.features.finishCameraOperation()
                self.refreshState()
                if let error = error {
                    self.present(error)
                }
                self.reconcileFeatureSnapshotForCurrentCameraState()
            }
        }
    }

    func changePreviewResolution(to symbol: String) {
        guard canChangePreviewResolution,
              let descriptor = features.previewResolutionDescriptor,
              descriptor.enumerationValues.contains(symbol) else {
            return
        }

        isCameraCommandPending = true
        features.beginExternalMutation()
        bridge.changePreviewResolution(symbol) { [weak self] error, _, _ in
            MainActor.assumeIsolated {
                guard let self = self else { return }
                self.isCameraCommandPending = false
                self.features.finishExternalMutation()
                self.refreshState()
                if let error = error {
                    self.present(error)
                } else {
                    // Preserve the selection even when a backend exposes the
                    // modes without a generic feature readback.
                    self.features.recordPreviewResolutionSelection(symbol)
                }

                // The native controller serially stops acquisition, applies the
                // mode, and resumes acquisition when it was previously streaming.
                // A failed restart may leave the camera faulted, so reconcile access
                // before attempting another feature snapshot.
                self.reconcileFeatureSnapshotForCurrentCameraState()
            }
        }
    }

    private func reconcileFeatureSnapshotForCurrentCameraState() {
        guard !isCameraTerminationPending else {
            features.finishCameraOperation()
            return
        }

        guard canAccessFeatures else {
            if cameraState == .closed {
                openedCameraDeviceID = nil
            }
            features.clearForUnavailableSession()
            return
        }

        // Lifecycle requests advance the native session generation when queued.
        // A failed transition can therefore invalidate earlier feature reads too.
        features.refreshFeatures()
    }

    private func refreshState() {
        cameraState = bridge.state
    }

    private func present(_ error: Error) {
        errorMessage = error.localizedDescription
        isShowingError = true
    }
}
