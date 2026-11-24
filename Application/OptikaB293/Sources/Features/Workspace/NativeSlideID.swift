import Foundation

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
