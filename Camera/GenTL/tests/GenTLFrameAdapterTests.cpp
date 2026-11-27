#include "camera/gentl/GenTLFrameAdapter.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

camera::gentl::DataStreamFrame validMono8Frame() {
    camera::gentl::DataStreamFrame frame;
    frame.payload = {1, 2, 3, 4, 5, 6};
    frame.frameId = 17;
    frame.timestampNanoseconds = 123456789;
    frame.pixelFormat = UINT64_C(0x01080001);
    frame.pixelFormatNamespace = GenTL::PIXELFORMAT_NAMESPACE_PFNC_32BIT;
    frame.pixelEndianness = GenTL::PIXELENDIANNESS_LITTLE;
    frame.width = 3;
    frame.height = 2;
    frame.deliveredImageHeight = 2;
    frame.imageOffset = 0;
    frame.xPadding = 0;
    frame.yPadding = 0;
    frame.payloadType = GenTL::PAYLOAD_TYPE_IMAGE;
    frame.imagePresent = true;
    frame.containsChunkData = false;
    frame.dataLargerThanBuffer = false;
    frame.isComposite = false;
    return frame;
}

void testMono8Projection() {
    const auto source = validMono8Frame();
    const auto result = camera::gentl::makeCommonFrame(source);
    require(result.has_value(), "Valid Mono8 image was not projected.");
    const auto& frame = **result;
    require(frame.width == 3 && frame.height == 2, "Dimensions were not preserved.");
    require(frame.pixels == source.payload, "Payload bytes were not preserved.");
    require(frame.sequenceNumber == source.frameId, "Frame ID was not preserved.");
    require(frame.timestamp.has_value(), "Timestamp was not preserved.");
    require(frame.timestamp->ticksPerSecond == UINT64_C(1000000000),
            "GenTL nanosecond timebase was not preserved.");
    require(frame.pixelFormat == camera::PixelFormat::Raw8,
            "PFNC Mono8 was not mapped to Raw8.");
    require(frame.pixelByteOrder == camera::PixelByteOrder::NotApplicable,
            "Mono8 byte order should be not applicable.");
    require(frame.strideBytes == 3, "Mono8 stride was not derived.");
    require(frame.significantBitsPerPixel == 8 &&
            frame.storageBitsPerPixel == 8,
            "Mono8 bit-depth metadata was not populated.");
}


void testPackedMono10pAndMono12pPreserveByteLayout() {
    struct FormatCase {
        std::uint64_t format;
        std::uint8_t bits;
        camera::PixelPacking packing;
        std::uint32_t expectedStride;
    };
    const FormatCase cases[] = {
        {UINT64_C(0x010A0046), 10, camera::PixelPacking::PFNC10p, 4},
        {UINT64_C(0x010C0047), 12, camera::PixelPacking::PFNC12p, 5},
        {UINT64_C(0x010E0104), 14, camera::PixelPacking::PFNC14p, 6}
    };
    for (const auto& test : cases) {
        auto source = validMono8Frame();
        source.width = 3;
        source.height = 2;
        source.deliveredImageHeight = 2;
        source.pixelFormat = test.format;
        source.pixelEndianness = GenTL::PIXELENDIANNESS_UNKNOWN;
        source.payload.resize(test.expectedStride * source.height);
        for (std::size_t i = 0; i < source.payload.size(); ++i)
            source.payload[i] = static_cast<std::uint8_t>(i * 17 + 3);
        const auto original = source.payload;
        const auto result = camera::gentl::makeCommonFrame(std::move(source));
        require(result.has_value(), "Packed monochrome image was rejected.");
        const auto& frame = **result;
        require(frame.pixelPacking == test.packing,
                "Packed PFNC layout identity was not preserved.");
        require(frame.pixelFormat == camera::PixelFormat::Raw8,
                "Packed byte payload was not represented as raw bytes.");
        require(frame.pixelByteOrder == camera::PixelByteOrder::NotApplicable,
                "Packed samples must not claim byte endianness.");
        require(frame.significantBitsPerPixel == test.bits &&
                    frame.storageBitsPerPixel == test.bits,
                "Packed sample bit depth was not preserved.");
        require(frame.strideBytes == test.expectedStride,
                "Packed row stride was not computed correctly.");
        require(frame.pixels == original,
                "Packed payload bytes were modified or unpacked.");
    }
}

void testPackedMono14pPreservesOffsetAndPadding() {
    auto source = validMono8Frame();
    source.width = 3;
    source.height = 2;
    source.deliveredImageHeight = 2;
    source.pixelFormat = UINT64_C(0x010E0104);
    source.pixelEndianness = GenTL::PIXELENDIANNESS_UNKNOWN;
    source.imageOffset = 2;
    source.xPadding = 2;
    source.yPadding = 3;
    source.payload.resize(2 + (6 + 2) * 2 + 3);
    for (std::size_t i = 0; i < source.payload.size(); ++i)
        source.payload[i] = static_cast<std::uint8_t>(0xA0 + i);
    const auto original = source.payload;

    const auto result = camera::gentl::makeCommonFrame(std::move(source));
    require(result.has_value(), "Padded Mono14p image was rejected.");
    const auto& frame = **result;
    require(frame.pixelPacking == camera::PixelPacking::PFNC14p &&
                frame.significantBitsPerPixel == 14 &&
                frame.storageBitsPerPixel == 14,
            "Padded Mono14p representation metadata was not preserved.");
    require(frame.imageOffsetBytes == 2 && frame.rowPaddingBytes == 2 &&
                frame.trailingPaddingBytes == 3 && frame.strideBytes == 8,
            "Padded Mono14p layout offsets/stride were not preserved.");
    require(frame.imageData() == frame.pixels.data() + 2,
            "Mono14p image start did not retain the no-copy payload offset.");
    require(frame.pixels == original,
            "Mono14p padding or packed payload bytes were modified.");
}

void testPackedMonochromeRejectsIncorrectPayloadLength() {
    auto source = validMono8Frame();
    source.pixelFormat = UINT64_C(0x010E0104);
    source.payload.resize(11);
    const auto result = camera::gentl::makeCommonFrame(source);
    require(result.has_value(), "Opaque packed frame should remain available.");
    require((*result)->pixelPacking == camera::PixelPacking::Unknown &&
                (*result)->strideBytes == 0,
            "Invalid packed 14-bit payload length was assigned a false row layout.");
}

void testMono16ProjectionUsesReportedByteOrder() {
    auto source = validMono8Frame();
    source.pixelFormat = UINT64_C(0x01100007);
    source.payload.assign(12, 0x2A);
    source.pixelEndianness = GenTL::PIXELENDIANNESS_BIG;

    const auto result = camera::gentl::makeCommonFrame(source);
    require(result.has_value(), "Valid PFNC Mono16 image was not projected.");
    const auto& frame = **result;
    require(frame.pixelFormat == camera::PixelFormat::Raw16,
            "PFNC Mono16 was not mapped to Raw16.");
    require(frame.pixelByteOrder == camera::PixelByteOrder::BigEndian,
            "Mono16 byte order was not preserved.");
    require(frame.strideBytes == 6,
            "Mono16 row stride was not derived from 16-bit storage.");
    require(frame.significantBitsPerPixel == 16 &&
                frame.storageBitsPerPixel == 16,
            "Mono16 bit-depth metadata was not populated.");
}

void testUnpackedMono10Mono12Mono14Projection() {
    struct FormatCase {
        std::uint64_t format;
        std::uint8_t significantBits;
    };
    const FormatCase formats[] = {
        {UINT64_C(0x01100003), 10},
        {UINT64_C(0x01100005), 12},
        {UINT64_C(0x01100025), 14}
    };

    for (const auto& format : formats) {
        auto source = validMono8Frame();
        source.pixelFormat = format.format;
        source.payload.assign(12, 0x31);
        source.pixelEndianness = GenTL::PIXELENDIANNESS_LITTLE;

        const auto result = camera::gentl::makeCommonFrame(source);
        require(result.has_value(), "Unpacked Mono10/12/14 frame was rejected.");
        const auto& frame = **result;
        require(frame.pixelFormat == camera::PixelFormat::Raw16,
                "Unpacked Mono10/12/14 was not mapped to Raw16.");
        require(frame.pixelByteOrder == camera::PixelByteOrder::LittleEndian,
                "Unpacked Mono10/12/14 byte order was not preserved.");
        require(frame.strideBytes == 6 && frame.storageBitsPerPixel == 16,
                "Unpacked Mono10/12/14 storage layout was incorrect.");
        require(frame.significantBitsPerPixel == format.significantBits,
                "Unpacked Mono significant bit depth was incorrect.");
    }
}

void testBayerMosaicFormatsPreservePatternAndRawSamples() {
    struct FormatCase {
        std::uint64_t format;
        camera::PixelColorFilterArray pattern;
        std::uint8_t significantBits;
    };
    const FormatCase formats[] = {
        {UINT64_C(0x01080008), camera::PixelColorFilterArray::GRBG, 8},
        {UINT64_C(0x0110000C), camera::PixelColorFilterArray::GRBG, 10},
        {UINT64_C(0x01100010), camera::PixelColorFilterArray::GRBG, 12},
        {UINT64_C(0x01100109), camera::PixelColorFilterArray::GRBG, 14},
        {UINT64_C(0x0110002E), camera::PixelColorFilterArray::GRBG, 16},
        {UINT64_C(0x01080009), camera::PixelColorFilterArray::RGGB, 8},
        {UINT64_C(0x0110000D), camera::PixelColorFilterArray::RGGB, 10},
        {UINT64_C(0x01100011), camera::PixelColorFilterArray::RGGB, 12},
        {UINT64_C(0x0110010A), camera::PixelColorFilterArray::RGGB, 14},
        {UINT64_C(0x0110002F), camera::PixelColorFilterArray::RGGB, 16},
        {UINT64_C(0x0108000A), camera::PixelColorFilterArray::GBRG, 8},
        {UINT64_C(0x0110000E), camera::PixelColorFilterArray::GBRG, 10},
        {UINT64_C(0x01100012), camera::PixelColorFilterArray::GBRG, 12},
        {UINT64_C(0x0110010B), camera::PixelColorFilterArray::GBRG, 14},
        {UINT64_C(0x01100030), camera::PixelColorFilterArray::GBRG, 16},
        {UINT64_C(0x0108000B), camera::PixelColorFilterArray::BGGR, 8},
        {UINT64_C(0x0110000F), camera::PixelColorFilterArray::BGGR, 10},
        {UINT64_C(0x01100013), camera::PixelColorFilterArray::BGGR, 12},
        {UINT64_C(0x0110010C), camera::PixelColorFilterArray::BGGR, 14},
        {UINT64_C(0x01100031), camera::PixelColorFilterArray::BGGR, 16}
    };

    for (const auto& format : formats) {
        auto source = validMono8Frame();
        source.pixelFormat = format.format;
        source.payload.assign(format.significantBits == 8 ? 6 : 12, 0x5A);
        source.pixelEndianness = GenTL::PIXELENDIANNESS_BIG;

        const auto result = camera::gentl::makeCommonFrame(std::move(source));
        require(result.has_value(), "A supported Bayer payload was rejected.");
        const auto& frame = **result;
        require(frame.pixelColorFilterArray == format.pattern,
                "The Bayer mosaic pattern was not preserved.");
        require(frame.significantBitsPerPixel == format.significantBits,
                "The Bayer significant bit depth was not preserved.");
        require(frame.storageBitsPerPixel ==
                    (format.significantBits == 8 ? 8 : 16),
                "The Bayer sample storage width was incorrect.");
        require(frame.pixelFormat ==
                    (format.significantBits == 8
                        ? camera::PixelFormat::Raw8
                        : camera::PixelFormat::Raw16),
                "Bayer samples were not retained in the expected RAW container.");
        require(frame.strideBytes ==
                    (format.significantBits == 8 ? 3U : 6U),
                "Bayer row stride was incorrect.");
        require(frame.pixelByteOrder ==
                    (format.significantBits == 8
                        ? camera::PixelByteOrder::NotApplicable
                        : camera::PixelByteOrder::BigEndian),
                "Bayer byte order was not preserved.");
        require(frame.pixels == std::vector<std::uint8_t>(
                    format.significantBits == 8 ? 6 : 12, 0x5A),
                "Bayer payload samples were modified.");
    }
}

void testPackedBayer10pAnd12pPreservePatternAndByteLayout() {
    struct FormatCase {
        std::uint64_t format;
        camera::PixelColorFilterArray pattern;
        std::uint8_t bits;
        std::uint32_t stride;
        std::size_t payloadSize;
    };
    const FormatCase cases[] = {
        {UINT64_C(0x010A0056), camera::PixelColorFilterArray::GRBG, 10, 4, 8},
        {UINT64_C(0x010C0057), camera::PixelColorFilterArray::GRBG, 12, 5, 10},
        {UINT64_C(0x010E0105), camera::PixelColorFilterArray::GRBG, 14, 6, 12},
        {UINT64_C(0x010A0058), camera::PixelColorFilterArray::RGGB, 10, 4, 8},
        {UINT64_C(0x010C0059), camera::PixelColorFilterArray::RGGB, 12, 5, 10},
        {UINT64_C(0x010E0106), camera::PixelColorFilterArray::RGGB, 14, 6, 12},
        {UINT64_C(0x010A0054), camera::PixelColorFilterArray::GBRG, 10, 4, 8},
        {UINT64_C(0x010C0055), camera::PixelColorFilterArray::GBRG, 12, 5, 10},
        {UINT64_C(0x010E0107), camera::PixelColorFilterArray::GBRG, 14, 6, 12},
        {UINT64_C(0x010A0052), camera::PixelColorFilterArray::BGGR, 10, 4, 8},
        {UINT64_C(0x010C0053), camera::PixelColorFilterArray::BGGR, 12, 5, 10},
        {UINT64_C(0x010E0108), camera::PixelColorFilterArray::BGGR, 14, 6, 12}
    };
    for (const auto& test : cases) {
        auto source = validMono8Frame();
        source.pixelFormat = test.format;
        source.pixelEndianness = GenTL::PIXELENDIANNESS_UNKNOWN;
        source.payload.resize(test.payloadSize);
        for (std::size_t i = 0; i < source.payload.size(); ++i)
            source.payload[i] = static_cast<std::uint8_t>(i * 13 + 7);
        const auto original = source.payload;
        const auto result = camera::gentl::makeCommonFrame(std::move(source));
        require(result.has_value(), "Packed Bayer frame was rejected.");
        const auto& frame = **result;
        require(frame.pixelColorFilterArray == test.pattern,
                "Packed Bayer mosaic pattern was not preserved.");
        require(frame.pixelFormat == camera::PixelFormat::Raw8,
                "Packed Bayer bytes were not represented as raw byte storage.");
        require(frame.pixelPacking ==
                    (test.bits == 10 ? camera::PixelPacking::PFNC10p :
                     test.bits == 12 ? camera::PixelPacking::PFNC12p :
                                       camera::PixelPacking::PFNC14p),
                "Packed Bayer PFNC packing identity was not preserved.");
        require(frame.pixelByteOrder == camera::PixelByteOrder::NotApplicable,
                "Packed Bayer samples must not claim byte endianness.");
        require(frame.significantBitsPerPixel == test.bits &&
                    frame.storageBitsPerPixel == test.bits,
                "Packed Bayer sample bit depth was not preserved.");
        require(frame.strideBytes == test.stride,
                "Packed Bayer row stride was incorrect.");
        require(frame.pixels == original,
                "Packed Bayer payload bytes were modified or unpacked.");
    }
}

void testBayer16WithoutKnownByteOrderPreservesOnlyMosaicMetadata() {
    auto source = validMono8Frame();
    source.pixelFormat = UINT64_C(0x0110002F); // PFNC BayerRG16
    source.payload.assign(12, 0x44);
    source.pixelEndianness = GenTL::PIXELENDIANNESS_UNKNOWN;

    const auto result = camera::gentl::makeCommonFrame(source);
    require(result.has_value(), "Unknown-endian Bayer16 payload was discarded.");
    require((*result)->pixelColorFilterArray ==
                camera::PixelColorFilterArray::RGGB,
            "Bayer mosaic identity was lost when byte order was unavailable.");
    require((*result)->pixelFormat == camera::PixelFormat::Unknown &&
                (*result)->strideBytes == 0,
            "Bayer16 storage layout was inferred without a known byte order.");
}

void testMono16WithoutKnownByteOrderRemainsOpaque() {
    auto source = validMono8Frame();
    source.pixelFormat = UINT64_C(0x01100007);
    source.payload.assign(12, 0x44);
    source.pixelEndianness = GenTL::PIXELENDIANNESS_UNKNOWN;

    const auto result = camera::gentl::makeCommonFrame(source);
    require(result.has_value(), "Unknown-endian Mono16 payload should be preserved.");
    require((*result)->pixelFormat == camera::PixelFormat::Unknown,
            "Mono16 was mapped without a known byte order.");
    require((*result)->pixelByteOrder == camera::PixelByteOrder::Unknown,
            "Unknown Mono16 byte order was fabricated.");
    require((*result)->strideBytes == 0,
            "Mono16 stride was inferred without a safe layout.");
}

void testRgb8Projection() {
    auto source = validMono8Frame();
    source.pixelFormat = UINT64_C(0x02180014);
    source.payload.assign(18, 0x7F);
    source.pixelEndianness = GenTL::PIXELENDIANNESS_UNKNOWN;

    const auto result = camera::gentl::makeCommonFrame(source);
    require(result.has_value(), "Valid PFNC RGB8 image was not projected.");
    const auto& frame = **result;
    require(frame.pixelFormat == camera::PixelFormat::RGB24,
            "PFNC RGB8 was not mapped to RGB24.");
    require(frame.pixelByteOrder == camera::PixelByteOrder::NotApplicable,
            "RGB8 byte order should be not applicable.");
    require(frame.strideBytes == 9,
            "RGB8 row stride was not derived.");
    require(frame.significantBitsPerPixel == 24 &&
                frame.storageBitsPerPixel == 24,
            "RGB8 bit-depth metadata was not populated.");
}

void testPfnc16NamespaceMappings() {
    auto mono8 = validMono8Frame();
    mono8.pixelFormat = UINT64_C(0x0101);
    mono8.pixelFormatNamespace = GenTL::PIXELFORMAT_NAMESPACE_PFNC_16BIT;
    auto mono8Result = camera::gentl::makeCommonFrame(mono8);
    require(mono8Result && (**mono8Result).pixelFormat == camera::PixelFormat::Raw8,
            "PFNC 16-bit namespace Mono8 was not mapped.");

    auto mono16 = validMono8Frame();
    mono16.pixelFormat = UINT64_C(0x0105);
    mono16.pixelFormatNamespace = GenTL::PIXELFORMAT_NAMESPACE_PFNC_16BIT;
    mono16.payload.assign(12, 0x55);
    auto mono16Result = camera::gentl::makeCommonFrame(mono16);
    require(mono16Result &&
                (**mono16Result).pixelFormat == camera::PixelFormat::Raw16,
            "PFNC 16-bit namespace Mono16 was not mapped.");

    auto rgb8 = validMono8Frame();
    rgb8.pixelFormat = UINT64_C(0x0401);
    rgb8.pixelFormatNamespace = GenTL::PIXELFORMAT_NAMESPACE_PFNC_16BIT;
    rgb8.payload.assign(18, 0x66);
    auto rgb8Result = camera::gentl::makeCommonFrame(rgb8);
    require(rgb8Result &&
                (**rgb8Result).pixelFormat == camera::PixelFormat::RGB24,
            "PFNC 16-bit namespace RGB8 was not mapped.");
}


void testRvalueProjectionTransfersPayload() {
    auto source = validMono8Frame();
    const auto expectedPayload = source.payload;
    const auto result = camera::gentl::makeCommonFrame(std::move(source));
    require(result.has_value(), "Rvalue Mono8 projection was rejected.");
    require((*result)->pixels == expectedPayload,
            "Rvalue projection did not transfer the original payload.");
}

void testUnknownPixelFormatPreservesIdentity() {
    auto source = validMono8Frame();
    source.pixelFormat = UINT64_C(0xDEADBEEF);
    const auto result = camera::gentl::makeCommonFrame(source);
    require(result.has_value(), "Unknown pixel format should retain a generic frame.");
    require((*result)->pixelFormat == camera::PixelFormat::Unknown,
            "Unknown format was incorrectly classified.");
    require((*result)->pixelFormatIdentifier.has_value(),
            "Unknown raw pixel-format identity was lost.");
    require((*result)->pixelFormatIdentifier->value == UINT64_C(0xDEADBEEF),
            "Unknown pixel-format value was changed.");
    require((*result)->pixelByteOrder == camera::PixelByteOrder::LittleEndian,
            "Unknown format lost its reported GenTL pixel endianness.");
}

void testImageOffsetAndPaddingArePreservedWithoutCopying() {
    auto source = validMono8Frame();
    source.payload = {0xEE, 1, 2, 3, 0xA1, 4, 5, 6, 0xA2, 0xFD};
    source.imageOffset = 1;
    source.xPadding = 1;
    source.yPadding = 1;
    const auto original = source.payload;
    const auto result = camera::gentl::makeCommonFrame(std::move(source));
    require(result.has_value(), "Valid padded Mono8 image was rejected.");
    const auto& frame = **result;
    require(frame.imageOffsetBytes == 1 && frame.rowPaddingBytes == 1 &&
                frame.trailingPaddingBytes == 1,
            "Image offset/padding metadata was not preserved.");
    require(frame.strideBytes == 4, "Row stride omitted X padding.");
    require(frame.imageData() == frame.pixels.data() + 1,
            "imageData() ignored the payload image offset.");
    require(frame.pixels == original,
            "Payload bytes were stripped, reordered, or copied into a new layout.");

    source = validMono8Frame();
    source.imageOffset = source.payload.size() + 1;
    require(!camera::gentl::makeCommonFrame(source),
            "Image offset beyond payload must be rejected.");
    source = validMono8Frame();
    source.xPadding = 1;
    const auto opaqueRowPadded = camera::gentl::makeCommonFrame(source);
    require(opaqueRowPadded.has_value() &&
                (*opaqueRowPadded)->pixelFormat == camera::PixelFormat::Unknown &&
                (*opaqueRowPadded)->strideBytes == 0 &&
                (*opaqueRowPadded)->rowPaddingBytes == 1,
            "Inconsistent row padding must remain opaque, not be misinterpreted.");
    source = validMono8Frame();
    source.yPadding = 7;
    require(!camera::gentl::makeCommonFrame(source),
            "Trailing padding beyond payload must be rejected.");

    source = validMono8Frame();
    source.containsChunkData = true;
    require(!camera::gentl::makeCommonFrame(source),
            "Chunk-bearing buffers must not be projected.");

    source = validMono8Frame();
    source.dataLargerThanBuffer = true;
    require(!camera::gentl::makeCommonFrame(source),
            "Truncated buffers must not be projected.");

    source = validMono8Frame();
    source.isComposite = true;
    require(!camera::gentl::makeCommonFrame(source),
            "Composite buffers must not be projected.");

    source = validMono8Frame();
    source.deliveredImageHeight = 1;
    require(!camera::gentl::makeCommonFrame(source),
            "Delivered image height mismatch must not be projected.");
}

void testMissingLayoutMetadataIsRejected() {
    auto source = validMono8Frame();
    source.imageOffset.reset();
    require(!camera::gentl::makeCommonFrame(source),
            "Unknown image offset must not be treated as zero.");

    source = validMono8Frame();
    source.xPadding.reset();
    require(!camera::gentl::makeCommonFrame(source),
            "Unknown X padding must not be treated as zero.");

    source = validMono8Frame();
    source.yPadding.reset();
    require(!camera::gentl::makeCommonFrame(source),
            "Unknown Y padding must not be treated as zero.");

    source = validMono8Frame();
    source.containsChunkData.reset();
    require(!camera::gentl::makeCommonFrame(source),
            "Unknown chunk-data status must not be treated as absent.");

    source = validMono8Frame();
    source.dataLargerThanBuffer.reset();
    require(!camera::gentl::makeCommonFrame(source),
            "Unknown buffer-size status must not be treated as safe.");

    source = validMono8Frame();
    source.isComposite.reset();
    require(!camera::gentl::makeCommonFrame(source),
            "Unknown composite status must not be treated as a simple image.");

    source = validMono8Frame();
    source.deliveredImageHeight.reset();
    require(!camera::gentl::makeCommonFrame(source),
            "Unknown delivered image height must not be inferred.");
}


void testEmptyPayloadIsRejectedForEveryPixelFormat() {
    auto source = validMono8Frame();
    source.payload.clear();
    require(!camera::gentl::makeCommonFrame(source),
            "An empty Mono8 image payload must not be projected.");

    source = validMono8Frame();
    source.pixelFormat = UINT64_C(0xDEADBEEF);
    source.payload.clear();
    require(!camera::gentl::makeCommonFrame(source),
            "An empty unknown-format payload must not be projected.");
}

void testNonImageAndIncompleteBuffersAreRejected() {
    auto source = validMono8Frame();
    source.payloadType = GenTL::PAYLOAD_TYPE_RAW_DATA;
    require(!camera::gentl::makeCommonFrame(source),
            "Non-image payload was projected.");

    source = validMono8Frame();
    source.incomplete = true;
    require(!camera::gentl::makeCommonFrame(source),
            "Incomplete payload was projected.");

    source = validMono8Frame();
    source.imagePresent = false;
    require(!camera::gentl::makeCommonFrame(source),
            "Buffer without an image was projected.");
}

} // namespace

int main() {
    try {
        testMono8Projection();
        testRvalueProjectionTransfersPayload();
        testPackedMono10pAndMono12pPreserveByteLayout();
        testPackedMono14pPreservesOffsetAndPadding();
        testPackedMonochromeRejectsIncorrectPayloadLength();
        testMono16ProjectionUsesReportedByteOrder();
        testUnpackedMono10Mono12Mono14Projection();
        testBayerMosaicFormatsPreservePatternAndRawSamples();
        testPackedBayer10pAnd12pPreservePatternAndByteLayout();
        testBayer16WithoutKnownByteOrderPreservesOnlyMosaicMetadata();
        testMono16WithoutKnownByteOrderRemainsOpaque();
        testRgb8Projection();
        testPfnc16NamespaceMappings();
        testUnknownPixelFormatPreservesIdentity();
        testImageOffsetAndPaddingArePreservedWithoutCopying();
        testMissingLayoutMetadataIsRejected();
        testEmptyPayloadIsRejectedForEveryPixelFormat();
        testNonImageAndIncompleteBuffersAreRejected();
        std::cout << "All GenTL frame adapter tests passed.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
