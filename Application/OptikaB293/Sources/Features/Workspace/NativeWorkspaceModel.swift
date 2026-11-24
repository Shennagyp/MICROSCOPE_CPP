import Combine
import Foundation

@MainActor
final class NativeWorkspaceModel: ObservableObject {
    @Published private(set) var activeSlide: NativeSlideID = .slide1
    @Published var selectedWorkspaceDockTab: NativeWorkspaceDockTab = .cameraControls
    @Published var isWorkspaceDockVisible = true
    @Published private(set) var workspaceDockWidth: CGFloat = OptikaLayout.workspaceDockDefaultWidth

    private static let widthDefaultsKey = "OptikaB293.WorkspaceDockWidth.v1"

    init() {
        let saved = UserDefaults.standard.double(forKey: Self.widthDefaultsKey)
        guard saved.isFinite && saved > 0 else { return }
        workspaceDockWidth = min(
            max(CGFloat(saved), OptikaLayout.workspaceDockMinimumWidth),
            OptikaLayout.workspaceDockMaximumWidth
        )
    }

    func selectActiveSlide(_ slide: NativeSlideID) { activeSlide = slide }

    func toggleWorkspaceDock(_ tab: NativeWorkspaceDockTab) {
        selectedWorkspaceDockTab = tab
        isWorkspaceDockVisible.toggle()
    }

    func closeWorkspaceDock() { isWorkspaceDockVisible = false }

    func setWorkspaceDockWidth(
        _ width: CGFloat,
        maximum: CGFloat = OptikaLayout.workspaceDockMaximumWidth
    ) {
        guard width.isFinite && maximum.isFinite else { return }
        let upper = max(OptikaLayout.workspaceDockMinimumWidth,
                        min(OptikaLayout.workspaceDockMaximumWidth, maximum))
        let bounded = min(max(width, OptikaLayout.workspaceDockMinimumWidth), upper)
        guard abs(bounded - workspaceDockWidth) >= 0.5 else { return }
        workspaceDockWidth = bounded
        UserDefaults.standard.set(Double(bounded), forKey: Self.widthDefaultsKey)
    }
}
