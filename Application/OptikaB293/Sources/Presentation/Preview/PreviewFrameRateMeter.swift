import Foundation

/// Measures individual frame-arrival intervals at a presentation boundary.
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

        // N individual arrivals spanning an interval contain N-1 intervals.
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
        reset()
    }

    mutating func reset() {
        framesPerSecond = 0
        sampleStartedAt = nil
        lastFrameAt = nil
        sampleFrameCount = 0
    }

    private mutating func beginSample(at timestamp: TimeInterval) {
        sampleStartedAt = timestamp
        lastFrameAt = timestamp
        sampleFrameCount = 1
    }
}

/// Estimates throughput from a monotonically increasing acquisition counter.
/// Unlike frame-arrival sampling, the counter delta is the number of frames
/// produced during the measured wall-clock interval, so FPS = delta / elapsed.
struct FrameRateCounterMeter {
    private let sampleInterval: TimeInterval = 0.5
    private let staleInterval: TimeInterval = 1.0

    private(set) var framesPerSecond = 0.0
    private var baselineCount: UInt64?
    private var baselineAt: TimeInterval?
    private var lastProgressAt: TimeInterval?

    mutating func record(_ cumulativeCount: UInt64, at timestamp: TimeInterval) {
        guard timestamp.isFinite, timestamp >= 0 else { return }

        guard let previousCount = baselineCount,
              let baselineAt = baselineAt else {
            baselineCount = cumulativeCount
            self.baselineAt = timestamp
            return
        }

        guard cumulativeCount >= previousCount else {
            // The driver resets its pull counter on a new acquisition run.
            reset()
            baselineCount = cumulativeCount
            self.baselineAt = timestamp
            return
        }

        let delta = cumulativeCount - previousCount
        guard delta > 0 else { return }

        // Rebase after idle time even when no frame has ever progressed in
        // this session (for example, the preview view mounted before camera start).
        let lastProgress = lastProgressAt ?? baselineAt
        if timestamp - lastProgress > staleInterval {
            framesPerSecond = 0
            self.baselineCount = cumulativeCount
            self.baselineAt = timestamp
            self.lastProgressAt = timestamp
            return
        }

        lastProgressAt = timestamp
        let elapsed = timestamp - baselineAt
        guard elapsed >= sampleInterval else { return }

        let measuredRate = Double(delta) / elapsed
        framesPerSecond = measuredRate.isFinite ? measuredRate : 0
        self.baselineCount = cumulativeCount
        self.baselineAt = timestamp
    }

    mutating func refresh(at timestamp: TimeInterval) {
        guard timestamp.isFinite,
              let lastProgressAt = lastProgressAt,
              timestamp >= lastProgressAt else {
            return
        }
        guard timestamp - lastProgressAt > staleInterval else { return }
        reset()
    }

    mutating func reset() {
        framesPerSecond = 0
        baselineCount = nil
        baselineAt = nil
        lastProgressAt = nil
    }
}
