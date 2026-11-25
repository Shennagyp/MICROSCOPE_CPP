import Foundation

/// The latest frame lease and rate-source counter observed for one UI notification.
/// framesPulled is used only to derive INPUT FPS, never displayed as a total.
struct NativeCameraFrameSnapshot {
    let lease: AmscopeFrameLease?
    let framesPulled: UInt64
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
        let framesPulled = self.framesPulled
        let droppedFrames = self.droppedFrames
        let lease = takeLatestFrame()
        return NativeCameraFrameSnapshot(
            lease: lease,
            framesPulled: framesPulled,
            droppedFrames: droppedFrames
        )
    }
}
