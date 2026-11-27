import Foundation

@main
struct PreviewFrameRateMeterTests {
    static func main() {
        testMeasuresPresentedFramesPerSecond()
        testRateRemainsZeroUntilFirstMeasurementWindow()
        testRateResetsAfterAStall()
        testMeasurementRestartsAfterAStall()
        testNonMonotonicAndInvalidTimestampsAreIgnored()
        testCounterMeterMeasuresCoalescedPullDeltas()
        testCounterMeterResetsAfterCounterRestart()
        testCounterMeterResetsAfterNoProgress()
        testCounterMeterIgnoresPreAcquisitionIdleTime()
        print("Preview frame-rate meter tests passed.")
    }

    private static func testMeasuresPresentedFramesPerSecond() {
        var meter = PreviewFrameRateMeter()
        for frame in 0...5 {
            meter.recordFrame(at: Double(frame) * 0.1)
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

    private static func testCounterMeterMeasuresCoalescedPullDeltas() {
        var meter = FrameRateCounterMeter()
        meter.record(0, at: 0.0)
        meter.record(12, at: 0.48)
        precondition(abs(meter.framesPerSecond - 25.0) < 0.000001)
    }

    private static func testCounterMeterResetsAfterCounterRestart() {
        var meter = FrameRateCounterMeter()
        meter.record(100, at: 1.0)
        meter.record(112, at: 1.48)
        precondition(abs(meter.framesPerSecond - 25.0) < 0.000001)

        meter.record(0, at: 1.6)
        precondition(meter.framesPerSecond == 0)
    }

    private static func testCounterMeterResetsAfterNoProgress() {
        var meter = FrameRateCounterMeter()
        meter.record(0, at: 0.0)
        meter.record(12, at: 0.52)
        precondition(abs(meter.framesPerSecond - (12.0 / 0.52)) < 0.000001)

        meter.refresh(at: 1.53)
        precondition(meter.framesPerSecond == 0)
    }

    private static func testCounterMeterIgnoresPreAcquisitionIdleTime() {
        var meter = FrameRateCounterMeter()
        meter.record(0, at: 0.0)

        // The UI existed for ten seconds before the camera began delivering.
        meter.record(1, at: 10.0)
        precondition(meter.framesPerSecond == 0)

        meter.record(14, at: 10.52)
        precondition(abs(meter.framesPerSecond - (13.0 / 0.52)) < 0.000001)
    }
}
