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
    @Published private(set) var isCameraOperationPending = false
    @Published private(set) var isEnumeratingCameras = false
    @Published private(set) var isFeatureSnapshotLoading = false
    @Published private(set) var isFeatureWritePending = false
    @Published private(set) var featureDescriptors: [AmscopeCameraFeatureDescriptor] = []
    @Published private(set) var featureValues: [String: AmscopeCameraFeatureValue] = [:]
    @Published var featureDrafts: [String: String] = [:]

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

    func openSelectedCamera() {
        guard canOpen else { return }
        isCameraOperationPending = true
        bridge.openDeviceID(selectedDeviceID) { [weak self] error, _, _ in
            guard let self = self else { return }
            self.isCameraOperationPending = false
            self.refreshState()
            if let error = error {
                self.present(error)
            } else {
                self.refreshFeatures()
            }
        }
    }

    func closeCamera() {
        guard canClose else { return }
        isCameraOperationPending = true
        bridge.close { [weak self] error, _, _ in
            guard let self = self else { return }
            self.isCameraOperationPending = false
            self.refreshState()
            if let error = error {
                self.present(error)
            } else {
                self.featureDescriptors = []
                self.featureValues = [:]
                self.featureDrafts = [:]
                self.isFeatureSnapshotLoading = false
            }
        }
    }

    func startAcquisition() {
        guard canStart else { return }
        isCameraOperationPending = true
        bridge.start { [weak self] error, _, _ in
            guard let self = self else { return }
            self.isCameraOperationPending = false
            self.refreshState()
            if let error = error {
                self.present(error)
            }
        }
    }

    func stopAcquisition() {
        guard canStop else { return }
        isCameraOperationPending = true
        bridge.stop { [weak self] error, _, _ in
            guard let self = self else { return }
            self.isCameraOperationPending = false
            self.refreshState()
            if let error = error {
                self.present(error)
            }
        }
    }

    func refreshFeatures() {
        guard cameraState == .open || cameraState == .streaming else { return }
        guard !isFeatureSnapshotLoading && !isFeatureWritePending else { return }

        isFeatureSnapshotLoading = true
        bridge.requestFeatureDescriptors { [weak self] descriptors, error, _, _, _ in
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

            // Keep only values whose descriptors still belong to this session.
            let availableNames = Set(self.featureDescriptors.map(\.name))
            self.featureValues = self.featureValues.filter {
                availableNames.contains($0.key)
            }

            // Reads are serialized by the native command worker. Each is a
            // feature snapshot and is discarded if a later write supersedes it.
            for descriptor in self.featureDescriptors where descriptor.isReadable {
                self.readFeature(descriptor.name)
            }
        }
    }

    private func readFeature(_ name: String) {
        bridge.readFeatureNamed(name) { [weak self] value, error, _, _, _ in
            guard let self = self else { return }
            guard let value = value, error == nil else {
                return
            }
            self.featureValues[name] = value
            self.featureDrafts[name] = self.stringValue(for: value)
        }
    }

    func displayValue(for descriptor: AmscopeCameraFeatureDescriptor) -> String {
        guard let value = featureValues[descriptor.name] else {
            return descriptor.isReadable ? "Reading…" : "—"
        }
        return stringValue(for: value)
    }

    func draftValue(for descriptor: AmscopeCameraFeatureDescriptor) -> String {
        featureDrafts[descriptor.name] ?? displayValue(for: descriptor)
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
        featureValues[descriptor.name]?.enumerationSymbol ?? ""
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
            let values = text.split(separator: ",").compactMap {
                Int64($0.trimmingCharacters(in: .whitespaces))
            }
            guard values.count == descriptor.elementCount else {
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
        featureDrafts[descriptor.name] =
            featureValues[descriptor.name].map { stringValue(for: $0) } ?? ""
    }

    private func writeFeature(
        _ value: AmscopeCameraFeatureValue,
        for descriptor: AmscopeCameraFeatureDescriptor
    ) {
        guard descriptor.isAvailable &&
              descriptor.isWritable &&
              !isFeatureWritePending &&
              !isCameraOperationPending else {
            return
        }

        isFeatureWritePending = true
        bridge.writeFeatureNamed(descriptor.name, value: value) {
            [weak self] error, _, _ in
            guard let self = self else { return }
            self.isFeatureWritePending = false
            if let error = error {
                self.present(error)
                self.restoreDraft(for: descriptor)
            } else {
                // A feature mutation may change availability/ranges of related
                // features, so refresh descriptors and values as one snapshot.
                self.refreshFeatures()
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
