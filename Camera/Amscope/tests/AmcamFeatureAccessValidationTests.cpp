#include "AmcamFeatureAccessValidation.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using camera::CameraFeatureAccessError;
    using amscope::detail::pixelFormatEnumerationError;

    const auto unsupported =
        pixelFormatEnumerationError("RawOutputBitDepth", E_NOTIMPL, 0);
    require(unsupported.has_value() &&
                unsupported->code == CameraFeatureAccessError::Code::FeatureUnavailable &&
                unsupported->nativeCode == static_cast<std::int32_t>(E_NOTIMPL),
            "E_NOTIMPL must remain a model-level unsupported result.");

    const auto backendFailure =
        pixelFormatEnumerationError("RawOutputBitDepth", E_FAIL, 0);
    require(backendFailure.has_value() &&
                backendFailure->code == CameraFeatureAccessError::Code::BackendFailure &&
                backendFailure->nativeCode == static_cast<std::int32_t>(E_FAIL),
            "A non-E_NOTIMPL SDK failure must preserve its native code.");

    const auto empty =
        pixelFormatEnumerationError("RawOutputBitDepth", S_OK, 0);
    require(empty.has_value() &&
                empty->code == CameraFeatureAccessError::Code::FeatureUnavailable,
            "A successful empty enumeration must be unavailable, not a backend failure.");

    const auto negativeCount =
        pixelFormatEnumerationError("RawOutputBitDepth", S_OK, -1);
    require(negativeCount.has_value() &&
                negativeCount->code == CameraFeatureAccessError::Code::BackendFailure,
            "A negative SDK format count must be treated as invalid provider data.");

    const auto excessiveCount =
        pixelFormatEnumerationError("RawOutputBitDepth", S_OK, 65);
    require(excessiveCount.has_value() &&
                excessiveCount->code == CameraFeatureAccessError::Code::BackendFailure,
            "A count beyond the implementation bound must be treated as invalid provider data.");

    const auto valid =
        pixelFormatEnumerationError("RawOutputBitDepth", S_OK, 4);
    require(!valid.has_value(),
            "A successful in-range enumeration count must proceed to entry retrieval.");

    using namespace amscope::detail;
    const std::vector<int> formats{
        AMCAM_PIXELFORMAT_GMCY12,
        AMCAM_PIXELFORMAT_RAW12PACK,
        AMCAM_PIXELFORMAT_RAW10,
        AMCAM_PIXELFORMAT_RAW8,
        AMCAM_PIXELFORMAT_RAW12,
        AMCAM_PIXELFORMAT_RAW14,
        AMCAM_PIXELFORMAT_RAW16,
        0x7fffffff
    };
    const auto choices = rawPixelFormatChoices(formats);
    require(choices.size() == 5,
            "Supported pixel formats must collapse to one selectable choice per significant depth.");
    require(choices[0].bitDepth == 8 &&
                choices[0].pixelFormat == AMCAM_PIXELFORMAT_RAW8 &&
                choices[1].bitDepth == 10 &&
                choices[2].bitDepth == 12 &&
                choices[2].pixelFormat == AMCAM_PIXELFORMAT_RAW12 &&
                choices[3].bitDepth == 14 &&
                choices[4].bitDepth == 16,
            "RAW depth choices must be ordered and prefer unpacked canonical formats.");
    require(rawPixelFormatBitDepth(AMCAM_PIXELFORMAT_GMCY12) == 12U &&
                !rawPixelFormatBitDepth(0x7fffffff).has_value(),
            "Pixel-format precision mapping must accept supported RAW/mosaic formats only.");
    require(rawPixelFormatForBitDepth(choices, 12U) ==
                AMCAM_PIXELFORMAT_RAW12 &&
                !rawPixelFormatForBitDepth(choices, 13U).has_value(),
            "A requested bit depth must resolve only to a supported canonical pixel format.");

    const std::vector<int> alternateOnly{
        AMCAM_PIXELFORMAT_RAW12PACK, AMCAM_PIXELFORMAT_RAW8
    };
    const auto alternateChoices = rawPixelFormatChoices(alternateOnly);
    require(rawBitDepthForPixelFormat(
                alternateChoices, alternateOnly, AMCAM_PIXELFORMAT_RAW12PACK) == 12U &&
                !rawBitDepthForPixelFormat(
                    alternateChoices, alternateOnly, AMCAM_PIXELFORMAT_RAW12).has_value(),
            "Readback accepts an alternate representation only when that exact format is in the SDK list.");

    const auto availableDescriptor = makeRawOutputBitDepthDescriptor(
        true, true, true, true, true, {8, 10, 12}, std::nullopt);
    require(availableDescriptor.name == camera::RawOutputBitDepthFeature &&
                availableDescriptor.kind == camera::CameraFeatureKind::Integer &&
                availableDescriptor.unit == std::optional<std::string>("significant bits") &&
                availableDescriptor.isAvailable &&
                availableDescriptor.isReadable &&
                availableDescriptor.isWritable &&
                availableDescriptor.discreteIntegerValues ==
                    std::vector<std::int64_t>({8, 10, 12}),
            "RAW bit-depth descriptor must expose the exact supported discrete choices.");

    const auto profileDeniedDescriptor = makeRawOutputBitDepthDescriptor(
        true, true, true, false, true, {8, 10, 12}, std::nullopt);
    require(!profileDeniedDescriptor.isAvailable &&
                profileDeniedDescriptor.isReadable &&
                !profileDeniedDescriptor.isWritable &&
                profileDeniedDescriptor.disabledReason ==
                    std::optional<std::string>(
                        "Pixel-format selection is blocked by the active camera feature profile."),
            "Pixel-format model policy must gate availability and writes without falsifying hardware read capability, and explain the policy denial.");

    const auto streamingDescriptor = makeRawOutputBitDepthDescriptor(
        true, true, true, true, false, {8, 10, 12}, std::nullopt);
    require(streamingDescriptor.isAvailable &&
                streamingDescriptor.isReadable &&
                !streamingDescriptor.isWritable,
            "RAW bit-depth selection must remain read-only while streaming.");

    std::cout << "Amcam feature-access validation tests passed.\n";
    return EXIT_SUCCESS;
}
