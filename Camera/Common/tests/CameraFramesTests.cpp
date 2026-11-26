#include "camera/CameraFrames.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <type_traits>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct BackendFrame final : camera::Frame {
    std::uint32_t nativeFlags = 0;
};

} // namespace

int main() {
    static_assert(std::is_same_v<
        camera::FramePtr, std::shared_ptr<const camera::Frame>>);

    camera::Frame frame;
    require(frame.pixelFormat == camera::PixelFormat::Unknown,
            "A default frame does not claim an encoding or buffer layout");
    require(frame.pixelColorFilterArray ==
                camera::PixelColorFilterArray::Unknown,
            "A default frame does not invent a sensor color-filter-array pattern");
    require(!frame.pixelFormatIdentifier,
            "A default frame has no invented pixel-format identity");
    require(!frame.sequenceNumber && !frame.timestamp && !frame.exposure,
            "Unavailable frame metadata is represented explicitly");

    frame.width = 640;
    frame.height = 480;
    frame.strideBytes = 1920;
    frame.pixelFormat = camera::PixelFormat::RGB24;
    frame.significantBitsPerPixel = 24;
    frame.storageBitsPerPixel = 24;
    frame.pixels.resize(1920U * 480U);

    require(frame.data() == frame.pixels.data(),
            "The common frame exposes its owned payload without a copy");
    require(frame.byteSize() == 1920U * 480U,
            "The common frame reports its actual payload byte count");
    require(frame.width == 640 && frame.height == 480 &&
                frame.strideBytes == 1920,
            "Image dimensions and row stride remain independent values");

    frame.pixelFormat = camera::PixelFormat::Raw8;
    frame.pixelFormatIdentifier =
        camera::PixelFormatIdentifier{"amcam.fourcc", 0x42474752U};
    frame.significantBitsPerPixel = 10;
    frame.storageBitsPerPixel = 8;
    frame.pixelColorFilterArray = camera::PixelColorFilterArray::RGGB;
    require(frame.pixelColorFilterArray == camera::PixelColorFilterArray::RGGB,
            "Sensor mosaic identity is independent from RAW sample storage");
    require(frame.pixelFormatIdentifier->scheme == "amcam.fourcc" &&
                frame.pixelFormatIdentifier->value == 0x42474752U,
            "Opaque pixel-format values retain their originating scheme");
    require(frame.significantBitsPerPixel == 10 &&
                frame.storageBitsPerPixel == 8,
            "Significant source depth is independent of storage width");

    // Zero is a valid value whenever the producer marks the metadata present.
    frame.sequenceNumber = 0;
    frame.timestamp = camera::FrameTimestamp{0, 1000000ULL, std::nullopt};
    frame.exposure = camera::FrameExposure{0, 1000000ULL};
    require(frame.sequenceNumber && *frame.sequenceNumber == 0,
            "A reported zero sequence is not treated as unavailable");
    require(frame.timestamp && frame.timestamp->ticks == 0 &&
                frame.timestamp->ticksPerSecond == 1000000ULL &&
                !frame.timestamp->clockDomain,
            "Timestamp value, timebase, and unknown clock origin are represented separately");
    require(frame.exposure && frame.exposure->durationTicks == 0 &&
                frame.exposure->ticksPerSecond == 1000000ULL,
            "Exposure sample preserves its unit and zero value");

    camera::FramePtr lease = std::make_shared<BackendFrame>();
    const auto backendLease =
        std::dynamic_pointer_cast<const BackendFrame>(lease);
    require(backendLease != nullptr,
            "A backend can retain typed metadata without polluting the common frame model");

    std::cout << "All common camera frame model tests passed.\n";
    return EXIT_SUCCESS;
}
