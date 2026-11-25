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

void testUnsafeLayoutIsRejected() {
    auto source = validMono8Frame();
    source.imageOffset = 1;
    require(!camera::gentl::makeCommonFrame(source),
            "Non-zero image offset must not be projected.");

    source = validMono8Frame();
    source.xPadding = 1;
    require(!camera::gentl::makeCommonFrame(source),
            "Non-zero X padding must not be projected.");

    source = validMono8Frame();
    source.yPadding = 1;
    require(!camera::gentl::makeCommonFrame(source),
            "Non-zero Y padding must not be projected.");

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
        testMono16ProjectionUsesReportedByteOrder();
        testUnpackedMono10Mono12Mono14Projection();
        testMono16WithoutKnownByteOrderRemainsOpaque();
        testRgb8Projection();
        testPfnc16NamespaceMappings();
        testUnknownPixelFormatPreservesIdentity();
        testUnsafeLayoutIsRejected();
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
