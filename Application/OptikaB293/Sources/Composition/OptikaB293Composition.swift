import Foundation

@MainActor
final class OptikaB293Composition {
    let bridge: AmscopeNativeCameraBridge
    let camera: NativeCameraModel
    let microscope: MicroscopeConfigurationModel
    let workspace = NativeWorkspaceModel()
    let gallery = NativeCaptureGalleryStore()

    init() {
        let bridge = AmscopeNativeCameraBridge()
        let microscope = MicroscopeConfigurationModel()
        let camera = NativeCameraModel(bridge: bridge, microscope: microscope)

        self.bridge = bridge
        self.microscope = microscope
        self.camera = camera

        microscope.onConfigurationChanged = { [weak camera] in
            camera?.microscopeConfigurationDidChange()
        }
    }
}
