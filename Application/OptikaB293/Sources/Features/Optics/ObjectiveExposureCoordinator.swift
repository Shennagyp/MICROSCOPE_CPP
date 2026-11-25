import Foundation

/// Applies the saved camera exposure profile for the currently selected
/// microscope objective. Camera writes remain serialized by NativeCameraFeatureModel.
@MainActor
final class ObjectiveExposureCoordinator {
    private weak var camera: NativeCameraModel?
    private weak var microscope: MicroscopeConfigurationModel?
    private var lastAttemptedObjectiveID: String?
    private var isApplying = false

    init(camera: NativeCameraModel, microscope: MicroscopeConfigurationModel) {
        self.camera = camera
        self.microscope = microscope

        microscope.onActiveObjectiveChanged = { [weak self] in
            self?.lastAttemptedObjectiveID = nil
            self?.applyActiveObjectiveProfile()
        }

        camera.features.onFeatureDescriptorsAvailable = { [weak self] in
            self?.applyActiveObjectiveProfile()
        }

        camera.features.onCameraSessionEnded = { [weak self] in
            self?.lastAttemptedObjectiveID = nil
            self?.isApplying = false
        }
    }

    private func applyActiveObjectiveProfile() {
        guard !isApplying,
              let camera,
              let microscope,
              camera.canAccessFeatures,
              !camera.isCameraOperationPending,
              !camera.features.isFeatureSnapshotLoading,
              !camera.features.isFeatureWritePending else {
            return
        }

        let objectiveID = microscope.configuration.activeObjectiveID
        guard lastAttemptedObjectiveID != objectiveID,
              let automaticExposure = camera.features.featureDescriptors.first(where: {
                  $0.name == "ExposureAuto"
              }),
              automaticExposure.isAvailable,
              automaticExposure.isWritable else {
            return
        }

        lastAttemptedObjectiveID = objectiveID
        isApplying = true

        let profile = microscope.configuration.exposureProfile(for: objectiveID)
        camera.features.applyExposureProfile(profile) { [weak self] _ in
            self?.isApplying = false
        }
    }
}
