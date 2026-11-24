import Foundation
import SwiftUI

private struct NativeCameraResolutionMode {
    let symbol: String
    let width: Int
    let height: Int

    var pixelCount: Int { width * height }

    init?(symbol: String) {
        guard let separator = symbol.firstIndex(of: ":") else { return nil }
        let dimensions = symbol[symbol.index(after: separator)...]
            .lowercased()
            .split(separator: "x")
        guard dimensions.count == 2,
              let width = Int(dimensions[0]),
              let height = Int(dimensions[1]),
              width > 0,
              height > 0 else {
            return nil
        }
        self.symbol = symbol
        self.width = width
        self.height = height
    }
}

private struct NativeAutomaticResolutionChoice {
    let symbol: String
    let width: Int
    let height: Int
    let pixelCount: Int
    let minimumBandSamplingFactor: Double
    let maximumBandSamplingFactor: Double
    let meetsTarget: Bool
}

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
    let captureGallery = NativeCaptureGalleryStore()

    @Published private(set) var cameras: [AmscopeCameraDevice] = []
    @Published var selectedDeviceID = ""
    @Published private(set) var cameraState: AmscopeNativeCameraState = .closed
    @Published private(set) var openedCameraDeviceID: String?
    @Published private(set) var activeSlide: NativeSlideID = .slide1
    @Published var selectedWorkspaceDockTab: NativeWorkspaceDockTab = .cameraControls
    @Published var isWorkspaceDockVisible = true
    @Published private(set) var workspaceDockWidth: CGFloat = 350
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
    let microscopeConfigurationDraftStore = MicroscopeConfigurationDraftStore(
        configuration: .optikaB293
    )
    @Published var microscopeConfiguration: MicroscopeOpticalConfiguration = .optikaB293 {
        didSet {
            guard microscopeConfiguration != oldValue else { return }
            persistMicroscopeConfiguration()
            reconcileAutomaticResolution()
        }
    }
    @Published private(set) var isAutomaticResolutionEnabled = false {
        didSet {
            UserDefaults.standard.set(
                isAutomaticResolutionEnabled,
                forKey: Self.automaticResolutionDefaultsKey
            )
        }
    }

    private static let microscopeConfigurationDefaultsKey =
        "AmscopeNative.MicroscopeConfiguration.v1"
    private static let automaticResolutionDefaultsKey =
        "AmscopeNative.AutomaticResolutionEnabled.v1"
    private static let workspaceDockWidthDefaultsKey =
        "AmscopeNative.WorkspaceDockWidth.v1"

    private init() {
        let savedDockWidth = UserDefaults.standard.double(
            forKey: Self.workspaceDockWidthDefaultsKey
        )
        if savedDockWidth.isFinite && savedDockWidth > 0 {
            workspaceDockWidth = min(max(CGFloat(savedDockWidth), 280), 520)
        }

        if let data = UserDefaults.standard.data(
            forKey: Self.microscopeConfigurationDefaultsKey
        ),
        let saved = try? JSONDecoder().decode(
            MicroscopeOpticalConfiguration.self,
            from: data
        ) {
            microscopeConfiguration = saved
            microscopeConfigurationDraftStore.configuration = saved
        } else {
            microscopeConfiguration = .optikaB293
            microscopeConfigurationDraftStore.configuration = .optikaB293
        }
        isAutomaticResolutionEnabled = UserDefaults.standard.bool(
            forKey: Self.automaticResolutionDefaultsKey
        )

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

    /// Current generic optical calibration inputs. The app computes specimen
    /// scale from configured camera sensor, output-pixel, objective and relay
    /// specifications; the calculation does not identify a camera model.
    var measurementCalibration: NativeMicroscopeMeasurementCalibration? {
        let configuration = microscopeConfiguration
        guard let objective = configuration.objectives.first(where: {
            $0.id == configuration.activeObjectiveID
        }) else {
            return nil
        }

        return NativeMicroscopeMeasurementCalibration(
            sensorPixelPitchMicrometers: configuration.cameraPixelPitchMicrometers,
            outputPixelFactor: configuration.cameraOutputPixelFactor,
            objectiveMagnification: objective.magnification,
            reductionLensMagnification: configuration.cameraRelayMagnification
        )
    }

    var canConnectDetectedCamera: Bool {
        cameraState == .closed &&
            !isCameraOperationPending &&
            !isEnumeratingCameras
    }

    func selectActiveSlide(_ slide: NativeSlideID) {
        activeSlide = slide
    }

    /// Show the requested workspace tool from the permanent edge rail.
    /// Selecting from the rail always restores the in-window dock, avoiding
    /// stale floating state after a panel/window close transition.
    func showWorkspaceDock(_ tab: NativeWorkspaceDockTab) {
        selectedWorkspaceDockTab = tab
        isWorkspaceDockVisible = true
    }

    /// Hide the workspace tool dock without affecting camera or gallery state.
    func closeWorkspaceDock() {
        isWorkspaceDockVisible = false
    }

    func setWorkspaceDockWidth(_ width: CGFloat, maximum: CGFloat = 520) {
        guard width.isFinite, maximum.isFinite else { return }
        let upperBound = max(280, min(520, maximum))
        let boundedWidth = min(max(width, 280), upperBound)
        guard abs(boundedWidth - workspaceDockWidth) >= 0.5 else { return }

        workspaceDockWidth = boundedWidth
        UserDefaults.standard.set(
            Double(boundedWidth),
            forKey: Self.workspaceDockWidthDefaultsKey
        )
    }

    func selectMicroscopeObjective(_ objectiveID: String) {
        guard microscopeConfiguration.objectives.contains(where: {
            $0.id == objectiveID
        }) else { return }

        var updated = microscopeConfiguration
        updated.activeObjectiveID = objectiveID
        microscopeConfiguration = updated
    }

    func updateMicroscopeConfiguration(
        _ configuration: MicroscopeOpticalConfiguration
    ) {
        microscopeConfiguration = configuration
    }

    func setAutomaticResolutionEnabled(_ enabled: Bool) {
        guard enabled != isAutomaticResolutionEnabled else { return }
        isAutomaticResolutionEnabled = enabled
        if enabled {
            reconcileAutomaticResolution()
        }
    }

    func selectManualPreviewResolution(to symbol: String) {
        setAutomaticResolutionEnabled(false)
        changePreviewResolution(to: symbol)
    }

    var automaticResolutionHelp: String {
        "Chooses the lowest camera-reported mode that meets the Rayleigh sampling target for the active objective and configured H&E chromatin wavelength band. If no mode meets the target, the maximum reported mode is selected."
    }

    var automaticResolutionSummary: String {
        guard let descriptor = featureDescriptors.first(where: {
            $0.name == "AmcamResolutionIndex"
        }) else {
            return "Camera modes unavailable"
        }
        guard let choice = automaticResolutionChoice(for: descriptor) else {
            return "Check microscope calibration"
        }
        let lower = String(format: "%.2f", choice.minimumBandSamplingFactor)
        let upper = String(format: "%.2f", choice.maximumBandSamplingFactor)
        let band = String(
            format: "%.0f–%.0f nm",
            microscopeConfiguration.chromatinBandLowerNanometers,
            microscopeConfiguration.chromatinBandUpperNanometers
        )
        return "\(choice.width) × \(choice.height) · RF \(lower)–\(upper) · \(band)" +
            (choice.meetsTarget ? "" : " · target not met")
    }

    private func persistMicroscopeConfiguration() {
        guard let data = try? JSONEncoder().encode(microscopeConfiguration) else {
            return
        }
        UserDefaults.standard.set(
            data,
            forKey: Self.microscopeConfigurationDefaultsKey
        )
    }

    private func reconcileAutomaticResolution() {
        guard isAutomaticResolutionEnabled,
              canChangePreviewResolution,
              let descriptor = featureDescriptors.first(where: {
                  $0.name == "AmcamResolutionIndex"
              }),
              descriptor.isAvailable,
              let current = featureValues[descriptor.name]?.enumerationSymbol,
              let choice = automaticResolutionChoice(for: descriptor),
              current != choice.symbol else {
            return
        }

        changePreviewResolution(to: choice.symbol)
    }

    private func automaticResolutionChoice(
        for descriptor: AmscopeCameraFeatureDescriptor
    ) -> NativeAutomaticResolutionChoice? {
        guard let objective = microscopeConfiguration.objectives.first(where: {
            $0.id == microscopeConfiguration.activeObjectiveID
        }) else { return nil }

        let configuration = microscopeConfiguration
        guard objective.magnification.isFinite,
              objective.magnification > 0,
              objective.numericalAperture.isFinite,
              objective.numericalAperture > 0,
              configuration.cameraPixelPitchMicrometers.isFinite,
              configuration.cameraPixelPitchMicrometers > 0,
              configuration.cameraRelayMagnification.isFinite,
              configuration.cameraRelayMagnification > 0,
              configuration.chromatinBandLowerNanometers.isFinite,
              configuration.chromatinBandLowerNanometers > 0,
              configuration.chromatinBandUpperNanometers.isFinite,
              configuration.chromatinBandUpperNanometers >=
                configuration.chromatinBandLowerNanometers,
              configuration.targetRayleighSamplingFactor.isFinite,
              configuration.targetRayleighSamplingFactor > 0 else {
            return nil
        }

        let modes = descriptor.enumerationValues.compactMap {
            NativeCameraResolutionMode(symbol: $0)
        }
        guard !modes.isEmpty,
              let maximumMode = modes.max(by: {
                  $0.pixelCount < $1.pixelCount
              }) else {
            return nil
        }

        // The shortest wavelength has the smallest Rayleigh distance and
        // therefore defines the conservative camera-sampling requirement.
        let lowerBandRayleighMicrometers =
            0.61 * (configuration.chromatinBandLowerNanometers / 1000.0) /
            objective.numericalAperture
        let upperBandRayleighMicrometers =
            0.61 * (configuration.chromatinBandUpperNanometers / 1000.0) /
            objective.numericalAperture

        let candidates = modes.map { mode -> NativeAutomaticResolutionChoice in
            // MU503 modes are provisionally modeled as full-field modes whose
            // effective sample pitch scales with output dimensions. This
            // hardware mapping remains unverified.
            let scaleX = Double(maximumMode.width) / Double(mode.width)
            let scaleY = Double(maximumMode.height) / Double(mode.height)
            let samplingScale = max(scaleX, scaleY)
            let objectPixelPitchMicrometers =
                configuration.cameraPixelPitchMicrometers * samplingScale /
                (objective.magnification * configuration.cameraRelayMagnification)
            let minimumBandFactor = lowerBandRayleighMicrometers /
                objectPixelPitchMicrometers
            let maximumBandFactor = upperBandRayleighMicrometers /
                objectPixelPitchMicrometers
            return NativeAutomaticResolutionChoice(
                symbol: mode.symbol,
                width: mode.width,
                height: mode.height,
                pixelCount: mode.pixelCount,
                minimumBandSamplingFactor: minimumBandFactor,
                maximumBandSamplingFactor: maximumBandFactor,
                meetsTarget: minimumBandFactor >= configuration.targetRayleighSamplingFactor
            )
        }

        if let smallestValid = candidates
            .filter({ $0.meetsTarget })
            .min(by: { $0.pixelCount < $1.pixelCount }) {
            return smallestValid
        }

        // No mode reaches the requested factor: use maximum resolution and
        // report the shortfall instead of claiming that the target is met.
        return candidates.max(by: { $0.pixelCount < $1.pixelCount })
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
                if name == "AmcamResolutionIndex" {
                    self.reconcileAutomaticResolution()
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
