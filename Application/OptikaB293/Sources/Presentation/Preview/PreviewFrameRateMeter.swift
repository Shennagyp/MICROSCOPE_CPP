import Foundation

/// Measures a frame rate from timestamped frame arrivals.
/// recordFrames(_:) accepts a delta count so coalesced notifications still
/// account for every successful SDK pull between notifications.
struct PreviewFrameRateMeter {
    private let sampleInterval: TimeInterval = 0.5
    private let staleInterval: TimeInterval = 1.0

    private(set) var framesPerSecond = 0.0
    private var sampleStartedAt: TimeInterval?
    private var lastFrameAt: TimeInterval?
    private var sampleFrameCount: UInt64 = 0

    mutating func recordFrame(at timestamp: TimeInterval) {
        recordFrames(1, at: timestamp)
    }

    mutating func recordFrames(_ count: UInt64, at timestamp: TimeInterval) {
        guard count > 0, timestamp.isFinite, timestamp >= 0 else { return }

        guard let lastFrameAt = lastFrameAt else {
            beginSample(at: timestamp, frameCount: count)
            return
        }
        guard timestamp > lastFrameAt else { return }

        if timestamp - lastFrameAt > staleInterval {
            framesPerSecond = 0
            beginSample(at: timestamp, frameCount: count)
            return
        }

        sampleFrameCount &+= count
        let sampleStart = sampleStartedAt ?? timestamp
        let elapsed = timestamp - sampleStart
        self.lastFrameAt = timestamp
        guard elapsed >= sampleInterval else { return }

        // N arrivals spanning an interval contain N-1 frame intervals.
        let measuredRate = Double(sampleFrameCount > 0 ? sampleFrameCount - 1 : 0) / elapsed
        framesPerSecond = measuredRate.isFinite ? measuredRate : 0
        beginSample(at: timestamp, frameCount: 1)
    }

    mutating func refresh(at timestamp: TimeInterval) {
        guard timestamp.isFinite,
              let lastFrameAt = lastFrameAt,
              timestamp >= lastFrameAt else {
            return
        }
        guard timestamp - lastFrameAt > staleInterval else { return }
        reset()
    }

    mutating func reset() {
        framesPerSecond = 0
        sampleStartedAt = nil
        lastFrameAt = nil
        sampleFrameCount = 0
    }

    private mutating func beginSample(at timestamp: TimeInterval, frameCount: UInt64) {
        sampleStartedAt = timestamp
        lastFrameAt = timestamp
        sampleFrameCount = frameCount
    }
}
