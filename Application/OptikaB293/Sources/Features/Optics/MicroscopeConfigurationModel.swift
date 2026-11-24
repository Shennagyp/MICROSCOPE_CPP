import Combine
import Foundation

/// Owns optical configuration, configuration persistence and the editable draft.
/// Camera and presentation components receive this owner through composition.
@MainActor
final class MicroscopeConfigurationModel: ObservableObject {
    @Published private(set) var configuration: MicroscopeOpticalConfiguration = .optikaB293 {
        didSet {
            guard configuration != oldValue else { return }
            persistConfiguration()
            onConfigurationChanged?()
        }
    }

    let draftStore = MicroscopeConfigurationDraftStore(
        configuration: .optikaB293
    )

    /// One application-level consumer: camera automatic-resolution policy.
    var onConfigurationChanged: (() -> Void)?

    private static let defaultsKey = "AmscopeNative.MicroscopeConfiguration.v1"

    init() {
        if let data = UserDefaults.standard.data(forKey: Self.defaultsKey),
           let saved = try? JSONDecoder().decode(
               MicroscopeOpticalConfiguration.self,
               from: data
           ) {
            configuration = saved
        }
        draftStore.configuration = configuration
    }

    var measurementCalibration: NativeMicroscopeMeasurementCalibration? {
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

    func selectObjective(_ objectiveID: String) {
        guard configuration.objectives.contains(where: {
            $0.id == objectiveID
        }) else { return }

        var updated = configuration
        updated.activeObjectiveID = objectiveID
        configuration = updated
    }

    func updateConfiguration(_ updated: MicroscopeOpticalConfiguration) {
        configuration = updated
    }

    private func persistConfiguration() {
        guard let data = try? JSONEncoder().encode(configuration) else { return }
        UserDefaults.standard.set(data, forKey: Self.defaultsKey)
    }
}
