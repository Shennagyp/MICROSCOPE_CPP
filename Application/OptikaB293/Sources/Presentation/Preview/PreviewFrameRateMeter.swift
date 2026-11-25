import Foundation

/// Measures the rate at which fresh images are assigned to a preview pane.
/// It deliberately measures presentation delivery, not SDK pulls or sensor timing.
struct PreviewFrameRateMeter {
    private let sampleInterval: TimeInterval = 0.5
    private let staleInterval: TimeInterval = 1.0

    private(set) var framesPerSecond = 0.0
    private var sampleStartedAt: TimeInterval?
    private var lastFrameAt: TimeInterval?
    private var sampleFrameCount: UInt64 = 0

    mutating func recordFrame(at timestamp: TimeInterval) {
        guard timestamp.isFinite, timestamp >= 0 else { return }

        guard let lastFrameAt = lastFrameAt else {
            beginSample(at: timestamp)
            return
        }
        guard timestamp > lastFrameAt else { return }

        if timestamp - lastFrameAt > staleInterval {
            framesPerSecond = 0
            beginSample(at: timestamp)
            return
        }

        sampleFrameCount &+= 1
        let sampleStart = sampleStartedAt ?? timestamp
        let elapsed = timestamp - sampleStart
        guard elapsed >= sampleInterval else {
            self.lastFrameAt = timestamp
            return
        }

        // N frames spanning an interval contain N-1 frame intervals.
        let measuredRate = Double(sampleFrameCount - 1) / elapsed
        framesPerSecond = measuredRate.isFinite ? measuredRate : 0
        beginSample(at: timestamp)
    }

    mutating func refresh(at timestamp: TimeInterval) {
        guard timestamp.isFinite,
              let lastFrameAt = lastFrameAt,
              timestamp >= lastFrameAt else {
            return
        }

        guard timestamp - lastFrameAt > staleInterval else { return }
        framesPerSecond = 0
        self.lastFrameAt = nil
        sampleStartedAt = nil
        sampleFrameCount = 0
    }

    private mutating func beginSample(at timestamp: TimeInterval) {
        sampleStartedAt = timestamp
        lastFrameAt = timestamp
        sampleFrameCount = 1
    }
}
