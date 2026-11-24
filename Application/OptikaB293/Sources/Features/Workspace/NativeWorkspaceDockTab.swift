import Foundation

enum NativeWorkspaceDockTab: String, CaseIterable, Identifiable {
    case cameraControls
    case smartGallery

    var id: String { rawValue }

    var title: String {
        switch self {
        case .cameraControls: return "Camera Controls"
        case .smartGallery: return "Smart Gallery"
        }
    }

    var symbolName: String {
        switch self {
        case .cameraControls: return "slider.horizontal.3"
        case .smartGallery: return "rectangle.stack"
        }
    }
}
