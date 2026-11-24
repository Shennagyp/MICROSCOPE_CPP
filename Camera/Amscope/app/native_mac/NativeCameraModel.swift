import Foundation
import SwiftUI

enum NativeSlideID: String, CaseIterable, Identifiable, Hashable {
    case slide1
    case slide2

    var id: String { rawValue }

    var title: String {
        switch self {
        case .slide1: return "Slide 1"
        case .slide2: return "Slide 2"
        }
    }
}
@MainActor
final class NativeCameraModel: ObservableObject {
    static let shared = NativeCameraModel()

    let bridge = AmscopeNativeCameraBridge()

    @Published private(set) var cameras: [AmscopeCameraDevice] = []
    @Published var selectedDeviceID = ""
    @Published private(set) var cameraState: AmscopeNativeCameraState = .closed
    @Published private(set) var openedCameraDeviceID: String?
    @Published private(set) var activeSlide: NativeSlideID = .slide1
    @Published private(set) var isBottomDockDetached = false
    @Published private(set) var isBottomDockCollapsed = false
    @Published var errorMessage = ""
    @Published var isShowingError = false
    @Published private(set) var isCameraOperationPending = false
    @Published private(set) var isEnumeratingCameras = false
    @Published private(set) var isFeatureSnapshotLoading = false
    @Published private(set) var isFeatureWritePending = false
    @Published private(set) var featureDescriptors: [AmscopeCameraFeatureDescriptor] = []
    @Published private(set) var featureValues: [String: AmscopeCameraFeatureValue] = [:]
    @Published private(set) var featureErrors: [String: String] = [:]
    @Published var featureDrafts: [String: String] = [:]

    private init() {
        bridge.onAcquisitionError = { [weak self] error in
            MainActor.assumeIsolated {
                guard let self = self else { return }
                self.present(error)
                self.refreshState()
                self.isFeatureWritePending = false
                self.isFeatureSnapshotLoading = false
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

    func selectActiveSlide(_ slide: NativeSlideID) {
        activeSlide = slide
    }

    func setBottomDockDetached(_ detached: Bool) {
        isBottomDockDetached = detached
    }

    func setBottomDockCollapsed(_ collapsed: Bool) {
        isBottomDockCollapsed = collapsed
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

    var canChangePreviewResolution: Bool {
        canAccessFeatures &&
            !isCameraOperationPending &&
            !isFeatureWritePending &&
            !isFeatureSnapshotLoading
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
        isCameraOperationPending = true
        bridge.openDeviceID(deviceID) { [weak self] error, _, _ in
            MainActor.assumeIsolated {
                guard let self = self else { return }
                self.isCameraOperationPending = false
                self.isFeatureWritePending = false
                self.isFeatureSnapshotLoading = false
                self.refreshState()
                if let error = error {
                    self.openedCameraDeviceID = nil
                    self.present(error)
                } else {
                    self.openedCameraDeviceID = deviceID
                    if startImmediately {
                        self.startAcquisition()
                    } else {
                        self.refreshFeatures()
                    }
                }
            }
        }
    }
    func closeCamera() {
        guard canClose else { return }
        isCameraOperationPending = true
        bridge.close { [weak self] error, _, _ in
            MainActor.assumeIsolated {
                guard let self = self else { return }
                self.isCameraOperationPending = false
                self.isFeatureWritePending = false
                self.isFeatureSnapshotLoading = false
                self.refreshState()
                if let error = error {
                    self.present(error)
                } else {
                    self.openedCameraDeviceID = nil
                    self.featureDescriptors = []
                    self.featureValues = [:]
                    self.featureErrors = [:]
                    self.featureDrafts = [:]
                    self.isFeatureSnapshotLoading = false
                }
            }
        }
    }

    func startAcquisition() {
        guard canStart else { return }
        isCameraOperationPending = true
        bridge.start { [weak self] error, _, _ in
            MainActor.assumeIsolated {
                guard let self = self else { return }
                self.isCameraOperationPending = false
                self.isFeatureWritePending = false
                self.isFeatureSnapshotLoading = false
                self.refreshState()
                if let error = error {
                    self.present(error)
                } else {
                    self.refreshFeatures()
                }
            }
        }
    }

    func stopAcquisition() {
        guard canStop else { return }
        isCameraOperationPending = true
        bridge.stop { [weak self] error, _, _ in
            MainActor.assumeIsolated {
                guard let self = self else { return }
                self.isCameraOperationPending = false
                self.isFeatureWritePending = false
                self.isFeatureSnapshotLoading = false
                self.refreshState()
                if let error = error {
                    self.present(error)
                } else {
                    self.refreshFeatures()
                }
            }
        }
    }

    func refreshFeatures() {
        guard cameraState == .open || cameraState == .streaming else { return }
        guard !isFeatureSnapshotLoading && !isFeatureWritePending else { return }

        isFeatureSnapshotLoading = true
        bridge.requestFeatureDescriptors { [weak self] descriptors, error, _, _, _ in
            MainActor.assumeIsolated {
                guard let self = self else { return }
                self.isFeatureSnapshotLoading = false

                if let error = error {
                    self.present(error)
                    return
                }

                self.featureDescriptors = descriptors
                    .filter { $0.isAvailable }
                    .sorted {
                        if $0.group.rawValue != $1.group.rawValue {
                            return $0.group.rawValue < $1.group.rawValue
                        }
                        return $0.displayName.localizedCaseInsensitiveCompare(
                            $1.displayName
                        ) == .orderedAscending
                    }

                // Start a fresh feature snapshot. Old values are not shown as if
                // they still describe the current camera state.
                self.featureValues = [:]
                self.featureErrors = [:]
                self.featureDrafts = [:]

                // Reads are serialized by the native command worker. Each is a
                // feature snapshot and is discarded if a later write supersedes it.
                for descriptor in self.featureDescriptors where descriptor.isReadable {
                    self.readFeature(descriptor.name)
                }
            }
        }
    }

    private func readFeature(_ name: String) {
        // The Objective-C++ bridge guarantees that completions are delivered
        // on the main queue. Assert that executor contract rather than creating
        // an extra Task/dispatch for each (low-frequency) feature response.
        bridge.readFeatureNamed(name) { [weak self] value, error, _, _, _ in
            MainActor.assumeIsolated {
                guard let self = self else { return }
                guard let value = value, error == nil else {
                    if let error = error {
                        self.featureErrors[name] = error.localizedDescription
                    }
                    return
                }
                self.featureValues[name] = value
                self.featureDrafts[name] = self.stringValue(for: value)
                self.featureErrors[name] = nil
            }
        }
    }

    func displayValue(for descriptor: AmscopeCameraFeatureDescriptor) -> String {
        guard let value = featureValues[descriptor.name] else {
            if featureErrors[descriptor.name] != nil {
                return "Unavailable"
            }
            return descriptor.isReadable ? "Reading…" : "—"
        }
        return stringValue(for: value)
    }

    func featureError(for descriptor: AmscopeCameraFeatureDescriptor) -> String? {
        featureErrors[descriptor.name]
    }

    func draftValue(for descriptor: AmscopeCameraFeatureDescriptor) -> String {
        if let draft = featureDrafts[descriptor.name] {
            return draft
        }
        return descriptor.isReadable ? displayValue(for: descriptor) : ""
    }

    func setDraftValue(_ value: String, for descriptor: AmscopeCameraFeatureDescriptor) {
        featureDrafts[descriptor.name] = value
    }

    func booleanValue(for descriptor: AmscopeCameraFeatureDescriptor) -> Bool {
        featureValues[descriptor.name]?.booleanValue ?? false
    }

    func integerValue(for descriptor: AmscopeCameraFeatureDescriptor) -> Int64 {
        featureValues[descriptor.name]?.integerValue ?? 0
    }

    func enumerationValue(for descriptor: AmscopeCameraFeatureDescriptor) -> String {
        featureValues[descriptor.name]?.enumerationSymbol ??
            descriptor.enumerationValues.first ?? ""
    }

    func writeBoolean(_ value: Bool, for descriptor: AmscopeCameraFeatureDescriptor) {
        writeFeature(
            AmscopeCameraFeatureValue.booleanFeatureValue(value),
            for: descriptor
        )
    }

    func writeInteger(_ value: Int64, for descriptor: AmscopeCameraFeatureDescriptor) {
        writeFeature(
            AmscopeCameraFeatureValue.integerFeatureValue(value),
            for: descriptor
        )
    }

    func writeFloatingPoint(
        _ value: Double,
        for descriptor: AmscopeCameraFeatureDescriptor
    ) {
        guard value.isFinite else { return }
        writeFeature(
            AmscopeCameraFeatureValue.floatingPointFeatureValue(value),
            for: descriptor
        )
    }

    func writeEnumeration(_ symbol: String, for descriptor: AmscopeCameraFeatureDescriptor) {
        writeFeature(
            AmscopeCameraFeatureValue.enumerationFeatureValue(symbol),
            for: descriptor
        )
    }

    func changePreviewResolution(to symbol: String) {
        guard canChangePreviewResolution,
              let descriptor = featureDescriptors.first(where: {
                  $0.name == "AmcamResolutionIndex"
              }),
              descriptor.isAvailable,
              descriptor.isReadable,
              descriptor.enumerationValues.contains(symbol) else {
            return
        }

        isCameraOperationPending = true
        isFeatureWritePending = true
        bridge.changePreviewResolution(symbol) { [weak self] error, _, _ in
            MainActor.assumeIsolated {
                guard let self = self else { return }
                self.isCameraOperationPending = false
                self.isFeatureWritePending = false
                self.refreshState()
                if let error = error {
                    self.present(error)
                }

                // Restart can fail after the new resolution was applied. Refresh
                // in either outcome so the UI reports the camera's actual state.
                self.refreshFeatures()
            }
        }
    }

    func commitDraft(for descriptor: AmscopeCameraFeatureDescriptor) {
        let text = draftValue(for: descriptor).trimmingCharacters(in: .whitespacesAndNewlines)

        switch descriptor.kind {
        case .integer:
            if descriptor.discreteIntegerValues.isEmpty {
                guard let value = Int64(text) else {
                    restoreDraft(for: descriptor)
                    return
                }
                writeInteger(value, for: descriptor)
            }
        case .floatingPoint:
            guard let value = Double(text), value.isFinite else {
                restoreDraft(for: descriptor)
                return
            }
            writeFloatingPoint(value, for: descriptor)
        case .string:
            writeFeature(
                AmscopeCameraFeatureValue.stringFeatureValue(text),
                for: descriptor
            )
        case .integerVector:
            let components = text.split(separator: ",")
            let values = components.compactMap {
                Int64($0.trimmingCharacters(in: .whitespaces))
            }
            guard !components.isEmpty &&
                  values.count == components.count &&
                  (!descriptor.hasElementCount ||
                   values.count == Int(descriptor.elementCount)) else {
                restoreDraft(for: descriptor)
                return
            }
            writeFeature(
                AmscopeCameraFeatureValue.integerVectorFeatureValue(
                    values.map { NSNumber(value: $0) }
                ),
                for: descriptor
            )
        case .boolean, .enumeration:
            break
        @unknown default:
            break
        }
    }

    private func restoreDraft(for descriptor: AmscopeCameraFeatureDescriptor) {
        restoreDraft(forFeatureName: descriptor.name)
    }

    private func restoreDraft(forFeatureName name: String) {
        featureDrafts[name] =
            featureValues[name].map { stringValue(for: $0) } ?? ""
    }

    private func writeFeature(
        _ value: AmscopeCameraFeatureValue,
        for descriptor: AmscopeCameraFeatureDescriptor
    ) {
        guard canAccessFeatures &&
              descriptor.isAvailable &&
              descriptor.isWritable &&
              !isFeatureWritePending &&
              !isFeatureSnapshotLoading &&
              !isCameraOperationPending else {
            return
        }

        isFeatureWritePending = true
        let featureName = descriptor.name
        bridge.writeFeatureNamed(featureName, value: value) {
            [weak self] error, _, _ in
            MainActor.assumeIsolated {
                guard let self = self else { return }
                self.isFeatureWritePending = false
                if let error = error {
                    self.present(error)
                    self.restoreDraft(forFeatureName: featureName)
                } else {
                    // A feature mutation may change availability/ranges of related
                    // features, so refresh descriptors and values as one snapshot.
                    self.refreshFeatures()
                }
            }
        }
    }

    private func stringValue(for value: AmscopeCameraFeatureValue) -> String {
        switch value.kind {
        case .boolean:
            return value.booleanValue ? "On" : "Off"
        case .integer:
            return String(value.integerValue)
        case .integerVector:
            return value.integerVectorValue.map { $0.stringValue }.joined(separator: ", ")
        case .floatingPoint:
            return String(value.floatingPointValue)
        case .string:
            return value.stringValue
        case .enumeration:
            return value.enumerationSymbol
        @unknown default:
            return "—"
        }
    }

    private func refreshState() {
        cameraState = bridge.state
    }

    func handleTerminationCloseFailure(_ error: Error) {
        refreshState()
        present(error)
    }

    private func present(_ error: Error) {
        errorMessage = error.localizedDescription
        isShowingError = true
    }
}
