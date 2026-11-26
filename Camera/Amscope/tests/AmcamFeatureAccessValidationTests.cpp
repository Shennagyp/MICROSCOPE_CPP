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
    using camera::CameraFeatureResult;
    using camera::CameraFeatureValue;
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

    const auto enumerated = enumeratePixelFormats(
        "RawOutputBitDepth",
        [](int* count) { *count = 2; return S_OK; },
        [](char index, int* code) {
            *code = index == 0 ? AMCAM_PIXELFORMAT_RAW8
                               : AMCAM_PIXELFORMAT_RAW12;
            return S_OK;
        });
    require(std::holds_alternative<std::vector<int>>(enumerated) &&
                std::get<std::vector<int>>(enumerated) ==
                    std::vector<int>({AMCAM_PIXELFORMAT_RAW8,
                                      AMCAM_PIXELFORMAT_RAW12}),
            "Pixel-format enumeration helper must return the exact ordered SDK list.");

    const auto entryFailure = enumeratePixelFormats(
        "RawOutputBitDepth",
        [](int* count) { *count = 2; return S_OK; },
        [](char index, int* code) {
            if (index == 0) { *code = AMCAM_PIXELFORMAT_RAW8; return S_OK; }
            return E_FAIL;
        });
    require(std::holds_alternative<CameraFeatureAccessError>(entryFailure) &&
                std::get<CameraFeatureAccessError>(entryFailure).code ==
                    CameraFeatureAccessError::Code::BackendFailure &&
                std::get<CameraFeatureAccessError>(entryFailure).nativeCode ==
                    static_cast<std::int32_t>(E_FAIL),
            "A failed format-list entry must preserve its native backend error.");

    const std::vector<int> rawFormats{
        AMCAM_PIXELFORMAT_RAW8, AMCAM_PIXELFORMAT_RAW12PACK,
        AMCAM_PIXELFORMAT_RAW12
    };
    const auto queryRawFormats = [&]()
        -> CameraFeatureResult<std::vector<int>> { return rawFormats; };
    const auto readPacked12 = []() -> CameraFeatureResult<int> {
        return AMCAM_PIXELFORMAT_RAW12PACK;
    };
    const auto readDepth = readRawOutputBitDepth(
        "RawOutputBitDepth", queryRawFormats, readPacked12);
    require(std::holds_alternative<CameraFeatureValue>(readDepth) &&
                std::get<std::int64_t>(
                    std::get<CameraFeatureValue>(readDepth)) == 12,
            "RAW bit-depth read must map an enumerated packed format to significant precision.");

    const auto readNonRaw = []() -> CameraFeatureResult<int> {
        return 0x7fffffff;
    };
    const auto unreadableDepth = readRawOutputBitDepth(
        "RawOutputBitDepth", queryRawFormats, readNonRaw);
    require(std::holds_alternative<CameraFeatureAccessError>(unreadableDepth) &&
                std::get<CameraFeatureAccessError>(unreadableDepth).code ==
                    CameraFeatureAccessError::Code::FeatureNotReadable,
            "RAW bit-depth read must reject a current format outside the live RAW list.");

    const auto readFailure = []() -> CameraFeatureResult<int> {
        return CameraFeatureAccessError{
            CameraFeatureAccessError::Code::BackendFailure,
            "RawOutputBitDepth", "read failed", static_cast<std::int32_t>(E_FAIL),
            std::nullopt, std::nullopt};
    };
    const auto failedRead = readRawOutputBitDepth(
        "RawOutputBitDepth", queryRawFormats, readFailure);
    require(std::holds_alternative<CameraFeatureAccessError>(failedRead) &&
                std::get<CameraFeatureAccessError>(failedRead).nativeCode ==
                    static_cast<std::int32_t>(E_FAIL),
            "RAW bit-depth read must propagate current-format SDK failures.");

    int writtenFormat = -1;
    const auto writeFormat = [&](int format)
        -> CameraFeatureResult<std::monostate> {
        writtenFormat = format;
        return std::monostate{};
    };
    const auto readRaw12 = []() -> CameraFeatureResult<int> {
        return AMCAM_PIXELFORMAT_RAW12;
    };
    const auto writtenDepth = writeRawOutputBitDepth(
        "RawOutputBitDepth", 12, queryRawFormats, writeFormat, readRaw12);
    require(std::holds_alternative<std::monostate>(writtenDepth) &&
                writtenFormat == AMCAM_PIXELFORMAT_RAW12,
            "RAW bit-depth write must select a supported canonical format and verify readback.");

    writtenFormat = -1;
    const auto invalidDepth = writeRawOutputBitDepth(
        "RawOutputBitDepth", 13, queryRawFormats, writeFormat, readRaw12);
    require(std::holds_alternative<CameraFeatureAccessError>(invalidDepth) &&
                std::get<CameraFeatureAccessError>(invalidDepth).code ==
                    CameraFeatureAccessError::Code::InvalidValue &&
                writtenFormat == -1,
            "RAW bit-depth write must reject unsupported precision without calling the SDK writer.");

    const auto writeFailure = [](int)
        -> CameraFeatureResult<std::monostate> {
        return CameraFeatureAccessError{
            CameraFeatureAccessError::Code::BackendFailure,
            "RawOutputBitDepth", "write failed", static_cast<std::int32_t>(E_FAIL),
            std::nullopt, std::nullopt};
    };
    const auto failedWrite = writeRawOutputBitDepth(
        "RawOutputBitDepth", 12, queryRawFormats, writeFailure, readRaw12);
    require(std::holds_alternative<CameraFeatureAccessError>(failedWrite) &&
                std::get<CameraFeatureAccessError>(failedWrite).nativeCode ==
                    static_cast<std::int32_t>(E_FAIL),
            "RAW bit-depth write must preserve a failed SDK set operation.");

    const auto mismatchedReadback = []() -> CameraFeatureResult<int> {
        return AMCAM_PIXELFORMAT_RAW8;
    };
    const auto mismatch = writeRawOutputBitDepth(
        "RawOutputBitDepth", 12, queryRawFormats, writeFormat,
        mismatchedReadback);
    require(std::holds_alternative<CameraFeatureAccessError>(mismatch) &&
                std::get<CameraFeatureAccessError>(mismatch).code ==
                    CameraFeatureAccessError::Code::BackendFailure,
            "RAW bit-depth write must reject a successful but mismatched readback.");

    std::cout << "Amcam feature-access validation tests passed.\n";
    return EXIT_SUCCESS;
}
