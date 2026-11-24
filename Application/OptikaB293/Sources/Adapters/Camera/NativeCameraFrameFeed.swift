import Foundation

/// Read-only frame-consumption surface exposed to presentation.
/// Lifecycle and feature commands remain private to NativeCameraModel.
protocol NativeCameraFrameFeed: AnyObject {
    var droppedFrames: UInt64 { get }
    var framesPulled: UInt64 { get }

    func setFrameAvailableHandler(_ handler: @escaping () -> Void)
    func takeLatestFrame() -> AmscopeFrameLease?
}

extension AmscopeNativeCameraBridge: NativeCameraFrameFeed {
    func setFrameAvailableHandler(_ handler: @escaping () -> Void) {
        onFrameAvailable = handler
    }
}
