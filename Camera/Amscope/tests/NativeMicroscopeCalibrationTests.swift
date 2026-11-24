import Foundation

@main
struct NativeMicroscopeCalibrationTests {
    static func main() {
        testMU503UnbinnedCalibration()
        testMU503TwoByTwoBinning()
        testMU503FourByFourBinning()
        testUnknownMU503ModeHasNoScale()
        testCustomCalibration()
        testInvalidOpticalInputsHaveNoScale()
        print("Native microscope calibration tests passed.")
    }

    private static func testMU503UnbinnedCalibration() {
        let calibration = makeCalibration(
            sensorPreset: .amscopeMU503,
            objective: 4,
            reduction: 0.5
        )
        let scale = calibration.micrometersPerSourcePixel(
            imageWidth: 2560,
            imageHeight: 1922
        )
        precondition(abs((scale ?? 0) - 1.1) < 0.000001)
    }

    private static func testMU503TwoByTwoBinning() {
        let calibration = makeCalibration(
            sensorPreset: .amscopeMU503,
            objective: 4,
            reduction: 0.5
        )
        let scale = calibration.micrometersPerSourcePixel(
            imageWidth: 1280,
            imageHeight: 960
        )
        precondition(abs((scale ?? 0) - 2.2) < 0.000001)
    }

    private static func testMU503FourByFourBinning() {
        let calibration = makeCalibration(
            sensorPreset: .amscopeMU503,
            objective: 4,
            reduction: 0.5
        )
        let scale = calibration.micrometersPerSourcePixel(
            imageWidth: 640,
            imageHeight: 480
        )
        precondition(abs((scale ?? 0) - 4.4) < 0.000001)
    }

    private static func testUnknownMU503ModeHasNoScale() {
        let calibration = makeCalibration(
            sensorPreset: .amscopeMU503,
            objective: 10,
            reduction: 0.5
        )
        precondition(
            calibration.micrometersPerSourcePixel(
                imageWidth: 1920,
                imageHeight: 1080
            ) == nil
        )
    }

    private static func testCustomCalibration() {
        let calibration = makeCalibration(
            sensorPreset: .custom,
            objective: 10,
            reduction: 0.5,
            customBinning: 2
        )
        let scale = calibration.micrometersPerSourcePixel(
            imageWidth: 1280,
            imageHeight: 720
        )
        precondition(abs((scale ?? 0) - 0.88) < 0.000001)
    }

    private static func testInvalidOpticalInputsHaveNoScale() {
        let calibration = makeCalibration(
            sensorPreset: .amscopeMU503,
            objective: 0,
            reduction: 0.5
        )
        precondition(
            calibration.micrometersPerSourcePixel(
                imageWidth: 2560,
                imageHeight: 1922
            ) == nil
        )
    }

    private static func makeCalibration(
        sensorPreset: NativeCameraSensorPreset,
        objective: Double,
        reduction: Double,
        customBinning: Double = 1
    ) -> NativeMicroscopeMeasurementCalibration {
        NativeMicroscopeMeasurementCalibration(
            sensorPixelPitchMicrometers: 2.2,
            objectiveMagnification: objective,
            reductionLensMagnification: reduction,
            sensorPreset: sensorPreset,
            customBinningFactor: customBinning
        )
    }
}
