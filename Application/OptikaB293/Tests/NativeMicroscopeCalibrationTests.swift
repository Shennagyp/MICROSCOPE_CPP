import Foundation

@main
struct NativeMicroscopeCalibrationTests {
    static func main() {
        testNativeOutputPixelScale()
        testBinnedOutputPixelScale()
        testCustomSensorSpecification()
        testDifferentObjectivesAndReductionLenses()
        testInvalidOpticalInputsHaveNoScale()
        print("Native microscope calibration tests passed.")
    }

    private static func testNativeOutputPixelScale() {
        let calibration = makeCalibration(
            pixelPitch: 2.2,
            outputFactor: 1,
            objective: 4,
            reduction: 0.5
        )
        precondition(abs((calibration.micrometersPerSourcePixel ?? 0) - 1.1) < 0.000001)
    }

    private static func testBinnedOutputPixelScale() {
        let calibration = makeCalibration(
            pixelPitch: 2.2,
            outputFactor: 2,
            objective: 4,
            reduction: 0.5
        )
        precondition(abs((calibration.micrometersPerSourcePixel ?? 0) - 2.2) < 0.000001)
    }

    private static func testCustomSensorSpecification() {
        let calibration = makeCalibration(
            pixelPitch: 3.45,
            outputFactor: 2,
            objective: 10,
            reduction: 0.5
        )
        precondition(abs((calibration.micrometersPerSourcePixel ?? 0) - 1.38) < 0.000001)
    }

    private static func testDifferentObjectivesAndReductionLenses() {
        let calibration = makeCalibration(
            pixelPitch: 2.4,
            outputFactor: 1,
            objective: 40,
            reduction: 0.35
        )
        let expected = 2.4 / (40 * 0.35)
        precondition(
            abs((calibration.micrometersPerSourcePixel ?? 0) - expected) < 0.000001
        )
    }

    private static func testInvalidOpticalInputsHaveNoScale() {
        let invalidValues: [(Double, Double, Double, Double)] = [
            (0, 1, 4, 0.5),
            (2.2, 0, 4, 0.5),
            (2.2, 1, 0, 0.5),
            (2.2, 1, 4, 0),
            (.nan, 1, 4, 0.5),
            (2.2, .infinity, 4, 0.5)
        ]

        for (pitch, outputFactor, objective, reduction) in invalidValues {
            let calibration = makeCalibration(
                pixelPitch: pitch,
                outputFactor: outputFactor,
                objective: objective,
                reduction: reduction
            )
            precondition(
                calibration.micrometersPerSourcePixel == nil,
                "Invalid optical specifications must not produce a scale."
            )
        }
    }

    private static func makeCalibration(
        pixelPitch: Double,
        outputFactor: Double,
        objective: Double,
        reduction: Double
    ) -> NativeMicroscopeMeasurementCalibration {
        NativeMicroscopeMeasurementCalibration(
            sensorPixelPitchMicrometers: pixelPitch,
            outputPixelFactor: outputFactor,
            objectiveMagnification: objective,
            reductionLensMagnification: reduction
        )
    }
}
