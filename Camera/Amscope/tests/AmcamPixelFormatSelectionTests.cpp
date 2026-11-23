#include "AmcamPixelFormatSelection.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <vector>

namespace {

using amscope::detail::rawBitDepthForLegacyMode;
using amscope::detail::rawBitDepthForPixelFormat;
using amscope::detail::rawPixelFormatChoices;
using amscope::detail::rawPixelFormatForBitDepth;

void expectValues(
    const std::vector<amscope::detail::RawPixelFormatChoice>& choices,
    const std::vector<unsigned>& expected) {
    assert(choices.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
        assert(choices[i].bitDepth == expected[i]);
}

void testEnumeratedDepthsMergeWithLegacyModes() {
    const std::vector<int> formats{
        AMCAM_PIXELFORMAT_RAW10,
        AMCAM_PIXELFORMAT_RAW12
    };
    const auto choices = rawPixelFormatChoices(formats, 12);
    expectValues(choices, {8, 10, 12});

    const auto eight = rawPixelFormatForBitDepth(choices, 8);
    assert(eight && eight->legacyBitDepthMode == 0);
    const auto ten = rawPixelFormatForBitDepth(choices, 10);
    assert(ten && ten->pixelFormat == AMCAM_PIXELFORMAT_RAW10);
    assert(ten->legacyBitDepthMode == -1);
    const auto twelve = rawPixelFormatForBitDepth(choices, 12);
    assert(twelve && twelve->pixelFormat == AMCAM_PIXELFORMAT_RAW12);
    assert(twelve->legacyBitDepthMode == -1);
}

void testLegacyModesRemainAvailableWithoutFormatEnumeration() {
    const auto choices = rawPixelFormatChoices({}, 12);
    expectValues(choices, {8, 12});
    assert(rawBitDepthForLegacyMode(choices, 0, 12) == 8U);
    assert(rawBitDepthForLegacyMode(choices, 1, 12) == 12U);
    assert(!rawBitDepthForLegacyMode(choices, 2, 12));
}

void testExactFormatTakesPrecedenceOverLegacyMaximum() {
    const std::vector<int> formats{
        AMCAM_PIXELFORMAT_RAW12PACK
    };
    const auto choices = rawPixelFormatChoices(formats, 12);
    expectValues(choices, {8, 12});
    const auto twelve = rawPixelFormatForBitDepth(choices, 12);
    assert(twelve && twelve->pixelFormat == AMCAM_PIXELFORMAT_RAW12PACK);
    assert(twelve->legacyBitDepthMode == -1);
    assert(rawBitDepthForPixelFormat(
        choices, formats, AMCAM_PIXELFORMAT_RAW12PACK) == 12U);
}

void testNonRawFormatsAreNotSensorPrecisionChoices() {
    const std::vector<int> formats{
        AMCAM_PIXELFORMAT_RGB888,
        AMCAM_PIXELFORMAT_YUV444
    };
    const auto choices = rawPixelFormatChoices(formats, 12);
    expectValues(choices, {8, 12});
    assert(!rawBitDepthForPixelFormat(
        choices, formats, AMCAM_PIXELFORMAT_RGB888));
}

void testFixedEightBitCameraHasNoFalseSecondChoice() {
    const std::vector<int> formats{AMCAM_PIXELFORMAT_RAW8};
    const auto choices = rawPixelFormatChoices(formats, 8);
    expectValues(choices, {8});
    assert(!rawPixelFormatForBitDepth(choices, 12));
}

void testIntermediateEnumeratedPrecisionsArePreserved() {
    const std::vector<int> formats{
        AMCAM_PIXELFORMAT_RAW10,
        AMCAM_PIXELFORMAT_RAW11,
        AMCAM_PIXELFORMAT_RAW12,
        AMCAM_PIXELFORMAT_RAW14
    };
    const auto choices = rawPixelFormatChoices(formats, 14);
    expectValues(choices, {8, 10, 11, 12, 14});
}

} // namespace

int main() {
    testEnumeratedDepthsMergeWithLegacyModes();
    testLegacyModesRemainAvailableWithoutFormatEnumeration();
    testExactFormatTakesPrecedenceOverLegacyMaximum();
    testNonRawFormatsAreNotSensorPrecisionChoices();
    testFixedEightBitCameraHasNoFalseSecondChoice();
    testIntermediateEnumeratedPrecisionsArePreserved();
    return 0;
}
