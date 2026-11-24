import Foundation

@MainActor
final class OptikaB293Composition {
    let bridge: AmscopeNativeCameraBridge
    let camera: NativeCameraModel
    let workspace = NativeWorkspaceModel()
    let gallery = NativeCaptureGalleryStore()

    init() {
        let bridge = AmscopeNativeCameraBridge()
        self.bridge = bridge
        self.camera = NativeCameraModel(bridge: bridge)
    }
}
