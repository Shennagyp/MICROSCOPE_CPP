import Combine
import Foundation

@MainActor
final class NativeWorkspaceModel: ObservableObject {
    @Published private(set) var activeSlide: NativeSlideID = .slide1
    @Published var selectedWorkspaceDockTab: NativeWorkspaceDockTab = .cameraControls
    @Published var isWorkspaceDockVisible = true
    @Published private(set) var workspaceDockWidth: CGFloat = OptikaLayout.workspaceDockDefaultWidth

    private static let widthDefaultsKey = "OptikaB293.WorkspaceDockWidth.v1"
    private static let legacyWidthDefaultsKey = "AmscopeNative.WorkspaceDockWidth.v1"

    init() {
        let defaults = UserDefaults.standard
        let saved = (defaults.object(forKey: Self.widthDefaultsKey) as? Double)
            ?? (defaults.object(forKey: Self.legacyWidthDefaultsKey) as? Double)
        guard let saved, saved.isFinite, saved > 0 else { return }

        workspaceDockWidth = min(
            max(CGFloat(saved), OptikaLayout.workspaceDockMinimumWidth),
            OptikaLayout.workspaceDockMaximumWidth
        )

        // Migrate the old application preference once, without resetting
        // existing workspace sizing during the ownership refactor.
        if defaults.object(forKey: Self.widthDefaultsKey) == nil {
            defaults.set(Double(workspaceDockWidth), forKey: Self.widthDefaultsKey)
        }
    }

    func selectActiveSlide(_ slide: NativeSlideID) { activeSlide = slide }

    /// Activates a tab using conventional sidebar behavior.
    /// Selecting another tab changes content without hiding the dock.
    /// Selecting the already-open tab hides it.
    func activateWorkspaceDockTab(_ tab: NativeWorkspaceDockTab) {
        if isWorkspaceDockVisible && selectedWorkspaceDockTab == tab {
            isWorkspaceDockVisible = false
            return
        }
        selectedWorkspaceDockTab = tab
        isWorkspaceDockVisible = true
    }

    func closeWorkspaceDock() { isWorkspaceDockVisible = false }

    /// Updates live width during a drag. Persistence is deferred until the
    /// gesture ends; pointer-move events must not write UserDefaults.
    func updateWorkspaceDockWidth(
        _ width: CGFloat,
        maximum: CGFloat = OptikaLayout.workspaceDockMaximumWidth
    ) {
        guard width.isFinite && maximum.isFinite else { return }
        let upper = max(
            OptikaLayout.workspaceDockMinimumWidth,
            min(OptikaLayout.workspaceDockMaximumWidth, maximum)
        )
        let bounded = min(max(width, OptikaLayout.workspaceDockMinimumWidth), upper)
        guard abs(bounded - workspaceDockWidth) >= 0.5 else { return }
        workspaceDockWidth = bounded
    }

    func persistWorkspaceDockWidth() {
        UserDefaults.standard.set(
            Double(workspaceDockWidth),
            forKey: Self.widthDefaultsKey
        )
    }
}
