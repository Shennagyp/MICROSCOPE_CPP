#pragma once

#include "amcam.h"

#include <algorithm>
#include <optional>
#include <vector>

namespace amscope::detail {

std::optional<unsigned> rawPixelFormatBitDepth(int pixelFormat) {
    switch (pixelFormat) {
    case AMCAM_PIXELFORMAT_RAW8:
    case AMCAM_PIXELFORMAT_GMCY8: return 8U;
    case AMCAM_PIXELFORMAT_RAW10: return 10U;
    case AMCAM_PIXELFORMAT_RAW11: return 11U;
    case AMCAM_PIXELFORMAT_RAW12:
    case AMCAM_PIXELFORMAT_RAW12PACK:
    case AMCAM_PIXELFORMAT_GMCY12: return 12U;
    case AMCAM_PIXELFORMAT_RAW14: return 14U;
    case AMCAM_PIXELFORMAT_RAW16: return 16U;
    default: return std::nullopt;
    }
}

struct RawPixelFormatChoice {
    unsigned bitDepth = 0;
    int pixelFormat = -1;
    // -1 means select through AMCAM_OPTION_PIXEL_FORMAT. Values 0/1
    // represent the legacy AMCAM_OPTION_BITDEPTH modes (8-bit / maximum).
    int legacyBitDepthMode = -1;
};

std::vector<RawPixelFormatChoice> rawPixelFormatChoices(
    const std::vector<int>& supportedPixelFormats,
    unsigned maximumBitDepth) {
    // Prefer unpacked RAW12 when both representations are supported. The
    // public bit-depth feature represents significant precision, not packing.
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
    // The legacy option is a separate capability source. It guarantees
    // selectable 8-bit and camera-maximum modes even when the modern format
    // enumeration omits RAW8 or does not enumerate RAW formats at all.
    // Prefer exact pixel-format entries when the same precision is present.
    const auto addLegacyChoice = [&choices](unsigned depth, int mode) {
        const auto existing = std::find_if(
            choices.begin(), choices.end(),
            [depth](const RawPixelFormatChoice& choice) {
                return choice.bitDepth == depth;
            });
        if (existing == choices.end())
            choices.push_back({depth, -1, mode});
    };
    if (maximumBitDepth >= 8U && maximumBitDepth <= 16U) {
        addLegacyChoice(8U, 0);
        addLegacyChoice(maximumBitDepth, 1);
    }

    std::sort(choices.begin(), choices.end(),
              [](const RawPixelFormatChoice& left,
                 const RawPixelFormatChoice& right) {
                  return left.bitDepth < right.bitDepth;
              });
    return choices;
}

std::optional<RawPixelFormatChoice> rawPixelFormatForBitDepth(
    const std::vector<RawPixelFormatChoice>& choices,
    unsigned bitDepth) {
    const auto found = std::find_if(
        choices.begin(), choices.end(),
        [bitDepth](const RawPixelFormatChoice& choice) {
            return choice.bitDepth == bitDepth;
        });
    return found == choices.end()
        ? std::nullopt
        : std::optional<RawPixelFormatChoice>(*found);
}

std::optional<unsigned> rawBitDepthForLegacyMode(
    const std::vector<RawPixelFormatChoice>& choices,
    int legacyMode,
    unsigned maximumBitDepth) {
    if (legacyMode != 0 && legacyMode != 1)
        return std::nullopt;
    const unsigned selectedDepth = legacyMode == 0 ? 8U : maximumBitDepth;
    const auto found = std::find_if(
        choices.begin(), choices.end(),
        [selectedDepth](const RawPixelFormatChoice& choice) {
            return choice.bitDepth == selectedDepth;
        });
    return found == choices.end()
        ? std::nullopt
        : std::optional<unsigned>(found->bitDepth);
}

std::optional<unsigned> rawBitDepthForPixelFormat(
    const std::vector<RawPixelFormatChoice>& choices,
    const std::vector<int>& supportedPixelFormats,
    int pixelFormat) {
    // The selected format may be an alternate representation of a depth
    // (notably RAW12PACK) even when the bit-depth selector prefers another
    // canonical format for writes. Require the exact current format to be in
    // the live SDK list before accepting its precision.
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
    return supportedDepth == choices.end()
        ? std::nullopt
        : bitDepth;
}
} // namespace amscope::detail
