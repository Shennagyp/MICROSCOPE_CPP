import Foundation

@main
struct NativeWorkspaceModelTests {
    @MainActor
    static func main() {
        testSelectingAnotherTabKeepsDockOpen()
        testSelectingOpenTabHidesDock()
        testSelectingTabWhileHiddenOpensDock()
        testClosingDockIsIdempotent()
        testDockWidthClamping()
        print("Native workspace model tests passed.")
    }

    @MainActor
    private static func testSelectingAnotherTabKeepsDockOpen() {
        let workspace = NativeWorkspaceModel()
        precondition(workspace.isWorkspaceDockVisible)
        precondition(workspace.selectedWorkspaceDockTab == .cameraControls)

        workspace.activateWorkspaceDockTab(.smartGallery)

        precondition(workspace.isWorkspaceDockVisible)
        precondition(workspace.selectedWorkspaceDockTab == .smartGallery)
    }

    @MainActor
    private static func testSelectingOpenTabHidesDock() {
        let workspace = NativeWorkspaceModel()
        workspace.activateWorkspaceDockTab(.cameraControls)

        precondition(!workspace.isWorkspaceDockVisible)
        precondition(workspace.selectedWorkspaceDockTab == .cameraControls)
    }

    @MainActor
    private static func testSelectingTabWhileHiddenOpensDock() {
        let workspace = NativeWorkspaceModel()
        workspace.closeWorkspaceDock()
        workspace.activateWorkspaceDockTab(.smartGallery)

        precondition(workspace.isWorkspaceDockVisible)
        precondition(workspace.selectedWorkspaceDockTab == .smartGallery)
    }

    @MainActor
    private static func testClosingDockIsIdempotent() {
        let workspace = NativeWorkspaceModel()
        workspace.closeWorkspaceDock()
        workspace.closeWorkspaceDock()
        precondition(!workspace.isWorkspaceDockVisible)
    }

    @MainActor
    private static func testDockWidthClamping() {
        let workspace = NativeWorkspaceModel()
        workspace.updateWorkspaceDockWidth(450, maximum: 420)
        precondition(workspace.workspaceDockWidth == 420)

        workspace.updateWorkspaceDockWidth(100, maximum: 420)
        precondition(workspace.workspaceDockWidth == OptikaLayout.workspaceDockMinimumWidth)

        workspace.updateWorkspaceDockWidth(.nan, maximum: 420)
        precondition(workspace.workspaceDockWidth == OptikaLayout.workspaceDockMinimumWidth)
    }
}
