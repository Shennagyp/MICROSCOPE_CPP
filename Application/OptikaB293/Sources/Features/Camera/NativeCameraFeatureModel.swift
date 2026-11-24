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

    @Published private(set) var isFeatureSnapshotLoading = false
    @Published private(set) var isFeatureWritePending = false
    @Published private(set) var featureDescriptors: [AmscopeCameraFeatureDescriptor] = []
    @Published private(set) var featureValues: [String: AmscopeCameraFeatureValue] = [:]
    @Published private(set) var featureErrors: [String: String] = [:]
    @Published var featureDrafts: [String: String] = [:]
    @Published private(set) var selectedPreviewResolutionSymbol: String?

    var onPreviewResolutionReadbackChanged: (() -> Void)?

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
            $0.name == "AmcamResolutionIndex"
        }), descriptor.isAvailable, !descriptor.enumerationValues.isEmpty else {
            return nil
        }
        return descriptor
    }

    func beginNewCameraSession() { selectedPreviewResolutionSymbol = nil }
    func finishCameraOperation() {
        isFeatureWritePending = false
        isFeatureSnapshotLoading = false
    }
    func beginExternalMutation() { isFeatureWritePending = true }
    func finishExternalMutation() { isFeatureWritePending = false }
    func recordPreviewResolutionSelection(_ symbol: String) {
        selectedPreviewResolutionSymbol = symbol
    }
    func clearForClosedSession() {
        featureDescriptors = []
        featureValues = [:]
        selectedPreviewResolutionSymbol = nil
        featureErrors = [:]
        featureDrafts = [:]
        finishCameraOperation()
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
                if name == "AmcamResolutionIndex" {
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
        let access = accessSnapshot()
        guard access.canAccessFeatures &&
              !access.isCameraOperationPending &&
              descriptor.isAvailable &&
              descriptor.isWritable &&
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
}
