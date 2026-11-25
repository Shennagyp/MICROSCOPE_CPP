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
    require(frame.strideBytes == 3, "Mono8 stride was not derived.");
    require(frame.significantBitsPerPixel == 8 &&
            frame.storageBitsPerPixel == 8,
            "Mono8 bit-depth metadata was not populated.");
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
