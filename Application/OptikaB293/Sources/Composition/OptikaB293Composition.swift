import Foundation

@MainActor
final class OptikaB293Composition {
    let camera: NativeCameraModel
    let microscope: MicroscopeConfigurationModel
    let resolutionCoordinator: MicroscopeResolutionCoordinator
    let workspace = NativeWorkspaceModel()
    let gallery = NativeCaptureGalleryStore()

    init() {
        let bridge = AmscopeNativeCameraBridge()
        let microscope = MicroscopeConfigurationModel()
        let camera = NativeCameraModel(bridge: bridge)
        let resolutionCoordinator = MicroscopeResolutionCoordinator(
            camera: camera,
            microscope: microscope
        )

        self.microscope = microscope
        self.camera = camera
        self.resolutionCoordinator = resolutionCoordinator
    }
}
