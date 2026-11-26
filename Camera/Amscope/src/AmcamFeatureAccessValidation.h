#pragma once

#ifndef AMCAM_HRESULT_ERRORCODE_NEEDED
#define AMCAM_HRESULT_ERRORCODE_NEEDED
#endif
#include "amcam.h"
#include "camera/CameraFeatureAccess.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace amscope::detail {

// Classifies the first PixelFormatSupport query before its count is consumed.
// E_NOTIMPL is an explicit per-model unsupported result in the vendor SDK;
// other HRESULT failures are operational errors and must retain their code.
inline std::optional<camera::CameraFeatureAccessError>
pixelFormatEnumerationError(
    std::string feature, HRESULT result, int count) {
    using Error = camera::CameraFeatureAccessError;
    if (FAILED(result)) {
        if (result == E_NOTIMPL) {
            return Error{
                Error::Code::FeatureUnavailable,
                std::move(feature),
                "The camera SDK does not implement pixel-format enumeration.",
                static_cast<std::int32_t>(result),
                std::nullopt,
                std::nullopt
            };
        }
        return Error{
            Error::Code::BackendFailure,
            std::move(feature),
            "Amcam_get_PixelFormatSupport(count) failed.",
            static_cast<std::int32_t>(result),
            std::nullopt,
            std::nullopt
        };
    }

    if (count == 0) {
        return Error{
            Error::Code::FeatureUnavailable,
            std::move(feature),
            "The camera reports no enumerable pixel formats.",
            0,
            std::nullopt,
            std::nullopt
        };
    }

    if (count < 0 || count > 64) {
        return Error{
            Error::Code::BackendFailure,
            std::move(feature),
            "The SDK returned an invalid pixel-format count.",
            0,
            std::nullopt,
            std::nullopt
        };
    }

    return std::nullopt;
}

// A RAW pixel format represents significant precision independently of its
// packing/container representation. The supported-format list remains the
// authority for the exact formats accepted by this camera.
template <typename QueryCount, typename QueryEntry>
inline camera::CameraFeatureResult<std::vector<int>> enumeratePixelFormats(
    std::string feature,
    QueryCount&& queryCount,
    QueryEntry&& queryEntry) {
    int count = 0;
    const HRESULT countResult = queryCount(&count);
    if (const auto error =
            pixelFormatEnumerationError(feature, countResult, count)) {
        return *error;
    }

    std::vector<int> formats;
    formats.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        int code = -1;
        const HRESULT entryResult =
            queryEntry(static_cast<char>(index), &code);
        if (FAILED(entryResult)) {
            return camera::CameraFeatureAccessError{
                camera::CameraFeatureAccessError::Code::BackendFailure,
                std::move(feature),
                "Amcam_get_PixelFormatSupport(entry) failed.",
                static_cast<std::int32_t>(entryResult),
                std::nullopt,
                std::nullopt
            };
        }
        formats.push_back(code);
    }
    return formats;
}

inline std::optional<unsigned> rawPixelFormatBitDepth(int pixelFormat) {
    switch (pixelFormat) {
    case AMCAM_PIXELFORMAT_RAW8:
    case AMCAM_PIXELFORMAT_GMCY8:
        return 8U;
    case AMCAM_PIXELFORMAT_RAW10:
        return 10U;
    case AMCAM_PIXELFORMAT_RAW11:
        return 11U;
    case AMCAM_PIXELFORMAT_RAW12:
    case AMCAM_PIXELFORMAT_RAW12PACK:
    case AMCAM_PIXELFORMAT_GMCY12:
        return 12U;
    case AMCAM_PIXELFORMAT_RAW14:
        return 14U;
    case AMCAM_PIXELFORMAT_RAW16:
        return 16U;
    default:
        return std::nullopt;
    }
}

struct RawPixelFormatChoice {
    unsigned bitDepth = 0;
    int pixelFormat = -1;
};

inline std::vector<RawPixelFormatChoice> rawPixelFormatChoices(
    const std::vector<int>& supportedPixelFormats) {
    // Prefer unpacked RAW12 when multiple representations of the same
    // significant precision are available.
    constexpr RawPixelFormatChoice candidates[] = {
        {8U, AMCAM_PIXELFORMAT_RAW8},
        {8U, AMCAM_PIXELFORMAT_GMCY8},
        {10U, AMCAM_PIXELFORMAT_RAW10},
        {11U, AMCAM_PIXELFORMAT_RAW11},
        {12U, AMCAM_PIXELFORMAT_RAW12},
        {12U, AMCAM_PIXELFORMAT_RAW12PACK},
        {12U, AMCAM_PIXELFORMAT_GMCY12},
        {14U, AMCAM_PIXELFORMAT_RAW14},
        {16U, AMCAM_PIXELFORMAT_RAW16}
    };

    std::vector<RawPixelFormatChoice> choices;
    for (const auto& candidate : candidates) {
        if (std::find(supportedPixelFormats.begin(),
                      supportedPixelFormats.end(), candidate.pixelFormat) ==
            supportedPixelFormats.end()) {
            continue;
        }
        const auto existing = std::find_if(
            choices.begin(), choices.end(),
            [&candidate](const RawPixelFormatChoice& choice) {
                return choice.bitDepth == candidate.bitDepth;
            });
        if (existing == choices.end())
            choices.push_back(candidate);
    }
    std::sort(choices.begin(), choices.end(),
              [](const RawPixelFormatChoice& left,
                 const RawPixelFormatChoice& right) {
                  return left.bitDepth < right.bitDepth;
              });
    return choices;
}

inline std::optional<int> rawPixelFormatForBitDepth(
    const std::vector<RawPixelFormatChoice>& choices,
    unsigned bitDepth) {
    const auto found = std::find_if(
        choices.begin(), choices.end(),
        [bitDepth](const RawPixelFormatChoice& choice) {
            return choice.bitDepth == bitDepth;
        });
    return found == choices.end()
        ? std::nullopt
        : std::optional<int>(found->pixelFormat);
}

inline std::optional<unsigned> rawBitDepthForPixelFormat(
    const std::vector<RawPixelFormatChoice>& choices,
    const std::vector<int>& supportedPixelFormats,
    int pixelFormat) {
    // Require the exact current format to appear in the live SDK list. A
    // different supported representation of the same depth is not proof that
    // this camera currently selected that representation.
    if (std::find(supportedPixelFormats.begin(),
                  supportedPixelFormats.end(), pixelFormat) ==
        supportedPixelFormats.end()) {
        return std::nullopt;
    }
    const auto bitDepth = rawPixelFormatBitDepth(pixelFormat);
    if (!bitDepth)
        return std::nullopt;
    const auto supportedDepth = std::find_if(
        choices.begin(), choices.end(),
        [bitDepth](const RawPixelFormatChoice& choice) {
            return choice.bitDepth == *bitDepth;
        });
    return supportedDepth == choices.end() ? std::nullopt : bitDepth;
}

template <typename QueryFormats, typename ReadCurrentFormat>
inline camera::CameraFeatureResult<camera::CameraFeatureValue>
readRawOutputBitDepth(
    std::string feature,
    QueryFormats&& queryFormats,
    ReadCurrentFormat&& readCurrentFormat) {
    auto formatsResult = queryFormats();
    if (const auto* error =
            std::get_if<camera::CameraFeatureAccessError>(&formatsResult)) {
        return *error;
    }

    const auto& supportedFormats =
        std::get<std::vector<int>>(formatsResult);
    const auto choices = rawPixelFormatChoices(supportedFormats);
    if (choices.empty()) {
        return camera::CameraFeatureAccessError{
            camera::CameraFeatureAccessError::Code::FeatureUnavailable,
            std::move(feature),
            "The SDK exposes no supported RAW or sensor-mosaic precision formats.",
            0,
            std::nullopt,
            std::nullopt
        };
    }

    auto currentResult = readCurrentFormat();
    if (const auto* error =
            std::get_if<camera::CameraFeatureAccessError>(&currentResult)) {
        return *error;
    }
    const int currentFormat = std::get<int>(currentResult);
    const auto selectedDepth =
        rawBitDepthForPixelFormat(choices, supportedFormats, currentFormat);
    if (!selectedDepth) {
        return camera::CameraFeatureAccessError{
            camera::CameraFeatureAccessError::Code::FeatureNotReadable,
            std::move(feature),
            "The current pixel format is not a discrete RAW bit-depth format.",
            0,
            std::nullopt,
            std::nullopt
        };
    }
    return camera::CameraFeatureValue{
        static_cast<std::int64_t>(*selectedDepth)};
}

template <typename QueryFormats, typename WriteFormat, typename ReadFormat>
inline camera::CameraFeatureResult<std::monostate>
writeRawOutputBitDepth(
    std::string feature,
    std::int64_t requestedDepth,
    QueryFormats&& queryFormats,
    WriteFormat&& writeFormat,
    ReadFormat&& readFormat) {
    auto formatsResult = queryFormats();
    if (const auto* error =
            std::get_if<camera::CameraFeatureAccessError>(&formatsResult)) {
        return *error;
    }

    const auto& supportedFormats =
        std::get<std::vector<int>>(formatsResult);
    const auto choices = rawPixelFormatChoices(supportedFormats);
    if (choices.empty()) {
        return camera::CameraFeatureAccessError{
            camera::CameraFeatureAccessError::Code::FeatureUnavailable,
            std::move(feature),
            "The SDK exposes no supported RAW or sensor-mosaic precision formats.",
            0,
            std::nullopt,
            std::nullopt
        };
    }

    const auto requestedFormat =
        requestedDepth >= 8 && requestedDepth <= 16
            ? rawPixelFormatForBitDepth(
                  choices, static_cast<unsigned>(requestedDepth))
            : std::nullopt;
    if (!requestedFormat) {
        return camera::CameraFeatureAccessError{
            camera::CameraFeatureAccessError::Code::InvalidValue,
            std::move(feature),
            "The requested significant bit depth is not represented by a supported RAW or sensor-mosaic pixel format.",
            0,
            std::nullopt,
            std::nullopt
        };
    }

    auto writeResult = writeFormat(*requestedFormat);
    if (const auto* error =
            std::get_if<camera::CameraFeatureAccessError>(&writeResult)) {
        return *error;
    }

    auto readbackResult = readFormat();
    if (const auto* error =
            std::get_if<camera::CameraFeatureAccessError>(&readbackResult)) {
        return *error;
    }
    if (std::get<int>(readbackResult) != *requestedFormat) {
        return camera::CameraFeatureAccessError{
            camera::CameraFeatureAccessError::Code::BackendFailure,
            std::move(feature),
            "RAW pixel-format readback did not match the requested bit depth.",
            0,
            std::nullopt,
            std::nullopt
        };
    }
    return std::monostate{};
}

inline camera::CameraFeatureDescriptor makeRawOutputBitDepthDescriptor(
    bool hardwareSupported,
    bool readable,
    bool writable,
    bool pixelFormatProfileAllows,
    bool stopped,
    std::vector<std::int64_t> supportedBitDepths,
    std::optional<std::string> disabledReason) {
    using camera::CameraFeatureDescriptor;
    using camera::CameraFeatureKind;
    using camera::CameraFeatureGroup;

    // A profile denial is an actionable policy restriction, not a hardware
    // capability failure. Keep the reason explicit even when the underlying
    // pixel-format probe itself succeeded.
    if (hardwareSupported && !pixelFormatProfileAllows) {
        disabledReason =
            "Pixel-format selection is blocked by the active camera feature profile.";
    }

    return CameraFeatureDescriptor{
        std::string(camera::RawOutputBitDepthFeature),
        "Sensor Output Bit Depth",
        CameraFeatureKind::Integer,
        std::string("significant bits"),
        hardwareSupported && pixelFormatProfileAllows,
        readable,
        hardwareSupported && writable && pixelFormatProfileAllows && stopped,
        std::nullopt,
        std::nullopt,
        {},
        CameraFeatureGroup::Sensor,
        std::move(supportedBitDepths),
        std::move(disabledReason)
    };
}

} // namespace amscope::detail
