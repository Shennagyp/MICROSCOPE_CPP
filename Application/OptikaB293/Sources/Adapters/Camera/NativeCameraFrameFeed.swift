import Foundation

/// The latest frame lease and dropped-frame counter observed for one UI notification.
struct NativeCameraFrameSnapshot {
    let lease: AmscopeFrameLease?
    let droppedFrames: UInt64
}

/// Narrow frame-consumption surface exposed to presentation.
/// Lifecycle and feature commands remain private to NativeCameraModel.
protocol NativeCameraFrameFeed: AnyObject {
    func setFrameAvailableHandler(_ handler: (() -> Void)?)
    func takeLatestFrameSnapshot() -> NativeCameraFrameSnapshot
}

extension AmscopeNativeCameraBridge: NativeCameraFrameFeed {
    func setFrameAvailableHandler(_ handler: (() -> Void)?) {
        onFrameAvailable = handler
    }

    func takeLatestFrameSnapshot() -> NativeCameraFrameSnapshot {
        let droppedFrames = self.droppedFrames
        let lease = takeLatestFrame()
        return NativeCameraFrameSnapshot(
            lease: lease,
            droppedFrames: droppedFrames
        )
    }
}
