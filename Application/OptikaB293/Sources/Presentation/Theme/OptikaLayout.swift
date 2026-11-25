import CoreGraphics

enum OptikaLayout {
    static let minimumWindowWidth: CGFloat = 960
    static let minimumWindowHeight: CGFloat = 640
    static let defaultWindowWidth: CGFloat = 1440
    static let defaultWindowHeight: CGFloat = 900
    static let workspaceDockMinimumWidth: CGFloat = 280
    static let workspaceDockDefaultWidth: CGFloat = 350
    static let workspaceDockMaximumWidth: CGFloat = 520
    static let workspaceDockResizeDividerWidth: CGFloat = 6
    static let dockRailWidth: CGFloat = 26
    static let dockTabHeight: CGFloat = 104
    static let slideMinimumWidth: CGFloat = 240
    static let dualPreviewMinimumWidth: CGFloat = slideMinimumWidth * 2 + 4
    static let microscopeControlsMinimumWidth: CGFloat = 435
    static let compactCornerRadius: CGFloat = 4
    static let panelCornerRadius: CGFloat = 8
    static let overlayCornerRadius: CGFloat = 7
}
