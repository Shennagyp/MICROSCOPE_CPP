import Foundation

@MainActor
final class OptikaB293Composition {
    let camera = NativeCameraModel()
    let workspace = NativeWorkspaceModel()
    let gallery = NativeCaptureGalleryStore()
}
