import Foundation

@main
struct NativeAutomaticResolutionPolicyTests {
    static func main() {
        testLowestResolutionMeetingTargetIsSelected()
        testHighestResolutionIsFallbackWhenTargetCannotBeMet()
        testInvalidOpticalInputsAreRejected()
        testMalformedResolutionModesAreIgnored()
        testObjectiveExposureProfilesPersist()
        testLegacyConfigurationDefaultsToAutomaticExposure()
        print("Automatic resolution and objective exposure tests passed.")
    }

    private static let modes = [
        "AmcamResolutionIndex:960x540",
        "AmcamResolutionIndex:1920x1080",
        "AmcamResolutionIndex:3840x2160"
    ]

    private static func testLowestResolutionMeetingTargetIsSelected() {
        var configuration = MicroscopeOpticalConfiguration.optikaB293
        configuration.targetRayleighSamplingFactor = 1.4

        guard let choice = NativeAutomaticResolutionPolicy.choose(
            modeSymbols: modes,
            configuration: configuration
        ) else {
            preconditionFailure("A valid optical configuration and mode set must produce a choice.")
        }

        precondition(choice.symbol == "AmcamResolutionIndex:1920x1080")
        precondition(choice.meetsTarget)
        precondition(choice.minimumBandSamplingFactor >= configuration.targetRayleighSamplingFactor)
    }

    private static func testHighestResolutionIsFallbackWhenTargetCannotBeMet() {
        var configuration = MicroscopeOpticalConfiguration.optikaB293
        configuration.targetRayleighSamplingFactor = 4.0

        guard let choice = NativeAutomaticResolutionPolicy.choose(
            modeSymbols: modes,
            configuration: configuration
        ) else {
            preconditionFailure("A valid configuration should choose a fallback mode.")
        }

        precondition(choice.symbol == "AmcamResolutionIndex:3840x2160")
        precondition(!choice.meetsTarget)
    }

    private static func testInvalidOpticalInputsAreRejected() {
        var invalidObjective = MicroscopeOpticalConfiguration.optikaB293
        invalidObjective.objectives[0].numericalAperture = 0
        precondition(NativeAutomaticResolutionPolicy.choose(
            modeSymbols: modes,
            configuration: invalidObjective
        ) == nil)

        var invalidPixelPitch = MicroscopeOpticalConfiguration.optikaB293
        invalidPixelPitch.cameraPixelPitchMicrometers = .nan
        precondition(NativeAutomaticResolutionPolicy.choose(
            modeSymbols: modes,
            configuration: invalidPixelPitch
        ) == nil)

        var invalidTarget = MicroscopeOpticalConfiguration.optikaB293
        invalidTarget.targetRayleighSamplingFactor = 0
        precondition(NativeAutomaticResolutionPolicy.choose(
            modeSymbols: modes,
            configuration: invalidTarget
        ) == nil)
    }

    private static func testObjectiveExposureProfilesPersist() {
        var configuration = MicroscopeOpticalConfiguration.optikaB293
        let profile = ObjectiveExposureProfile(
            automaticExposureEnabled: false,
            manualExposureTimeMicroseconds: 12_500
        )
        configuration.objectiveExposureProfiles["40x"] = profile

        let encoded = try! JSONEncoder().encode(configuration)
        let decoded = try! JSONDecoder().decode(
            MicroscopeOpticalConfiguration.self,
            from: encoded
        )

        precondition(decoded == configuration)
        precondition(decoded.exposureProfile(for: "40x") == profile)
        precondition(
            decoded.exposureProfile(for: "10x").automaticExposureEnabled
        )
    }

    private static func testLegacyConfigurationDefaultsToAutomaticExposure() {
        let encoded = try! JSONEncoder().encode(
            MicroscopeOpticalConfiguration.optikaB293
        )
        var legacyObject = try! JSONSerialization.jsonObject(
            with: encoded
        ) as! [String: Any]
        legacyObject.removeValue(forKey: "objectiveExposureProfiles")

        let legacyData = try! JSONSerialization.data(withJSONObject: legacyObject)
        let decoded = try! JSONDecoder().decode(
            MicroscopeOpticalConfiguration.self,
            from: legacyData
        )

        precondition(decoded.objectiveExposureProfiles.isEmpty)
        precondition(
            decoded.exposureProfile(for: "4x") ==
                ObjectiveExposureProfile.automatic
        )
    }

    private static func testMalformedResolutionModesAreIgnored() {
        let onlyMalformed = [
            "960x540",
            "AmcamResolutionIndex:0x1080",
            "AmcamResolutionIndex:1920x",
            "AmcamResolutionIndex:abc"
        ]
        precondition(NativeAutomaticResolutionPolicy.choose(
            modeSymbols: onlyMalformed,
            configuration: .optikaB293
        ) == nil)

        let mixed = onlyMalformed + ["AmcamResolutionIndex:3840x2160"]
        let choice = NativeAutomaticResolutionPolicy.choose(
            modeSymbols: mixed,
            configuration: .optikaB293
        )
        precondition(choice?.symbol == "AmcamResolutionIndex:3840x2160")
    }
}
