import Foundation

@main
struct PreviewFrameRateMeterTests {
    static func main() {
        testMeasuresPresentedFramesPerSecond()
        testMeasuresCoalescedInputFrameDeltas()
        testResetClearsPreviousSession()
        testRateRemainsZeroUntilFirstMeasurementWindow()
        testRateResetsAfterAStall()
        testMeasurementRestartsAfterAStall()
        testNonMonotonicAndInvalidTimestampsAreIgnored()
        print("Preview frame-rate meter tests passed.")
    }

    private static func testMeasuresPresentedFramesPerSecond() {
        var meter = PreviewFrameRateMeter()
        for frame in 0...5 {
            meter.recordFrame(at: Double(frame) * 0.1)
        }
        precondition(abs(meter.framesPerSecond - 10.0) < 0.000001)
    }

    private static func testMeasuresCoalescedInputFrameDeltas() {
        var meter = PreviewFrameRateMeter()
        meter.recordFrames(1, at: 0.0)
        meter.recordFrames(2, at: 0.1)
        meter.recordFrames(1, at: 0.2)
        meter.recordFrames(1, at: 0.3)
        meter.recordFrames(1, at: 0.5)
        precondition(abs(meter.framesPerSecond - 10.0) < 0.000001)
    }

    private static func testResetClearsPreviousSession() {
        var meter = PreviewFrameRateMeter()
        for frame in 0...5 {
            meter.recordFrame(at: Double(frame) * 0.1)
        }
        precondition(meter.framesPerSecond > 0)
        meter.reset()
        precondition(meter.framesPerSecond == 0)

        for frame in 0...5 {
            meter.recordFrame(at: 2.0 + Double(frame) * 0.1)
        }
        precondition(abs(meter.framesPerSecond - 10.0) < 0.000001)
    }

    private static func testRateRemainsZeroUntilFirstMeasurementWindow() {
        var meter = PreviewFrameRateMeter()
        meter.recordFrame(at: 10.0)
        meter.recordFrame(at: 10.1)
        meter.recordFrame(at: 10.2)
        precondition(meter.framesPerSecond == 0)
    }

    private static func testRateResetsAfterAStall() {
        var meter = PreviewFrameRateMeter()
        for frame in 0...5 {
            meter.recordFrame(at: Double(frame) * 0.1)
        }
        precondition(meter.framesPerSecond > 0)

        meter.refresh(at: 1.51)
        precondition(meter.framesPerSecond == 0)
    }

    private static func testMeasurementRestartsAfterAStall() {
        var meter = PreviewFrameRateMeter()
        for frame in 0...5 {
            meter.recordFrame(at: Double(frame) * 0.1)
        }
        meter.refresh(at: 1.51)

        for frame in 0...5 {
            meter.recordFrame(at: 2.0 + Double(frame) * 0.1)
        }
        precondition(abs(meter.framesPerSecond - 10.0) < 0.000001)
    }

    private static func testNonMonotonicAndInvalidTimestampsAreIgnored() {
        var meter = PreviewFrameRateMeter()
        meter.recordFrame(at: 5.0)
        meter.recordFrame(at: 4.9)
        meter.recordFrame(at: .nan)
        meter.recordFrame(at: .infinity)
        meter.recordFrame(at: -1.0)
        precondition(meter.framesPerSecond == 0)

        for frame in 1...5 {
            meter.recordFrame(at: 5.0 + Double(frame) * 0.1)
        }
        precondition(abs(meter.framesPerSecond - 10.0) < 0.000001)
    }
}
