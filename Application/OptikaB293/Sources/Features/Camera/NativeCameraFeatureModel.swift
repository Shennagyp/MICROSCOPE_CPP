import Combine
import Foundation

struct NativeCameraFeatureAccessSnapshot: Equatable {
    let canAccessFeatures: Bool
    let isCameraOperationPending: Bool
    static let unavailable = NativeCameraFeatureAccessSnapshot(
        canAccessFeatures: false,
        isCameraOperationPending: true
    )
}

@MainActor
final class NativeCameraFeatureModel: ObservableObject {
    private let bridge: AmscopeNativeCameraBridge
    private let accessSnapshot: () -> NativeCameraFeatureAccessSnapshot
    private let presentError: (Error) -> Void
    private static let previewResolutionFeatureName = "AmcamResolutionIndex"

    @Published private(set) var isFeatureSnapshotLoading = false
    @Published private(set) var isFeatureWritePending = false
    @Published private(set) var featureDescriptors: [AmscopeCameraFeatureDescriptor] = []
    @Published private(set) var featureValues: [String: AmscopeCameraFeatureValue] = [:]
    @Published private(set) var featureErrors: [String: String] = [:]
    @Published var featureDrafts: [String: String] = [:]
    @Published private(set) var selectedPreviewResolutionSymbol: String?

    var onPreviewResolutionReadbackChanged: (() -> Void)?
    var onFeatureDescriptorsAvailable: (() -> Void)?
    var onCameraSessionEnded: (() -> Void)?

    init(
        bridge: AmscopeNativeCameraBridge,
        accessSnapshot: @escaping () -> NativeCameraFeatureAccessSnapshot,
        presentError: @escaping (Error) -> Void
    ) {
        self.bridge = bridge
        self.accessSnapshot = accessSnapshot
        self.presentError = presentError
    }

    var previewResolutionDescriptor: AmscopeCameraFeatureDescriptor? {
        guard let descriptor = featureDescriptors.first(where: {
            $0.name == Self.previewResolutionFeatureName
        }), descriptor.isAvailable, !descriptor.enumerationValues.isEmpty else {
            return nil
        }
        return descriptor
    }

    // This setting changes acquisition lifecycle and must use the session owner's
    // dedicated command rather than the generic camera-feature write path.
    var genericEditorDescriptors: [AmscopeCameraFeatureDescriptor] {
        featureDescriptors.filter { $0.name != Self.previewResolutionFeatureName }
    }

    func beginNewCameraSession() { clearForUnavailableSession() }
    func finishCameraOperation() {
        isFeatureWritePending = false
        isFeatureSnapshotLoading = false
    }
    func beginExternalMutation() { isFeatureWritePending = true }
    func finishExternalMutation() { isFeatureWritePending = false }
    func recordPreviewResolutionSelection(_ symbol: String) {
        selectedPreviewResolutionSymbol = symbol
    }
    func clearForUnavailableSession() {
        featureDescriptors = []
        featureValues = [:]
        selectedPreviewResolutionSymbol = nil
        featureErrors = [:]
        featureDrafts = [:]
        finishCameraOperation()
        onCameraSessionEnded?()
    }

    func refreshFeatures() {
        let access = accessSnapshot()
        guard access.canAccessFeatures && !access.isCameraOperationPending else { return }
        guard !isFeatureSnapshotLoading && !isFeatureWritePending else { return }

        isFeatureSnapshotLoading = true
        bridge.requestFeatureDescriptors { [weak self] descriptors, error, _, _, _ in
            MainActor.assumeIsolated {
                guard let self = self else { return }
                self.isFeatureSnapshotLoading = false

                let access = self.accessSnapshot()
                guard access.canAccessFeatures && !access.isCameraOperationPending else {
                    return
                }

                if let error = error {
                    self.presentError(error)
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
                self.onFeatureDescriptorsAvailable?()

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
                let access = self.accessSnapshot()
                guard access.canAccessFeatures && !access.isCameraOperationPending else {
                    return
                }
                guard let value = value, error == nil else {
                    if let error = error {
                        self.featureErrors[name] = error.localizedDescription
                    }
                    return
                }
                self.featureValues[name] = value
                self.featureDrafts[name] = self.stringValue(for: value)
                self.featureErrors[name] = nil
                if name == Self.previewResolutionFeatureName {
                    self.selectedPreviewResolutionSymbol = value.enumerationSymbol
                    self.onPreviewResolutionReadbackChanged?()
                }
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

    func applyExposureProfile(
        _ profile: ObjectiveExposureProfile,
        completion: ((Error?) -> Void)? = nil
    ) {
        guard let automaticDescriptor = featureDescriptors.first(where: {
            $0.name == "ExposureAuto"
        }), automaticDescriptor.isAvailable, automaticDescriptor.isWritable else {
            completion?(nil)
            return
        }

        let shouldWriteManualTime =
            !profile.automaticExposureEnabled &&
            profile.manualExposureTimeMicroseconds != nil
        let automaticMode = profile.automaticExposureEnabled ? "Continuous" : "Off"

        // The camera accepts ExposureTime only after ExposureAuto has been
        // switched to Off. Keep both writes on the existing serialized queue.
        writeFeature(
            AmscopeCameraFeatureValue.enumerationFeatureValue(automaticMode),
            for: automaticDescriptor,
            refreshAfterWrite: !shouldWriteManualTime
        ) { [weak self] error in
            guard let self = self, error == nil else {
                completion?(error)
                return
            }

            guard shouldWriteManualTime,
                  let requestedTime = profile.manualExposureTimeMicroseconds else {
                completion?(nil)
                return
            }

            guard let timeDescriptor = self.featureDescriptors.first(where: {
                $0.name == "ExposureTime"
            }), timeDescriptor.isAvailable else {
                let unavailableError = NSError(
                    domain: "OPTIKA-B293.Exposure",
                    code: 1,
                    userInfo: [
                        NSLocalizedDescriptionKey:
                            "The connected camera does not expose manual exposure time."
                    ]
                )
                self.presentError(unavailableError)
                self.refreshFeatures()
                completion?(unavailableError)
                return
            }

            // The cached descriptor may still say isWritable=false because it
            // was built while automatic exposure was active. The driver rebuilds
            // and validates the descriptor after the preceding Off write.
            self.writeFeature(
                AmscopeCameraFeatureValue.integerFeatureValue(requestedTime),
                for: timeDescriptor,
                allowTemporarilyNonWritable: true,
                refreshAfterWrite: true,
                completion: completion
            )
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
        for descriptor: AmscopeCameraFeatureDescriptor,
        allowTemporarilyNonWritable: Bool = false,
        refreshAfterWrite: Bool = true,
        completion: ((Error?) -> Void)? = nil
    ) {
        let access = accessSnapshot()
        guard descriptor.name != Self.previewResolutionFeatureName &&
              access.canAccessFeatures &&
              !access.isCameraOperationPending &&
              descriptor.isAvailable &&
              (descriptor.isWritable || allowTemporarilyNonWritable) &&
              !isFeatureWritePending &&
              !isFeatureSnapshotLoading else {
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
                    self.presentError(error)
                    self.restoreDraft(forFeatureName: featureName)
                }

                // Enqueueing a write advances the native feature-state generation.
                // Refresh after the final write in an ordered exposure update; on
                // failure, refresh even when the caller requested a chained write.
                if refreshAfterWrite || error != nil {
                    self.refreshFeatures()
                }
                completion?(error)
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
}
