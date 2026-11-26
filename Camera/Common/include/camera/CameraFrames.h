#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace camera {

// Describes the byte-buffer representation understood by a consumer. RAW8
// and RAW16 describe the sample storage container, not the sensor encoding.
enum class PixelFormat : std::uint8_t {
    Unknown,
    RGB24,
    Raw8,
    Raw16
};

// Byte order is meaningful for multi-byte sample containers. It is explicitly
// separate from significant bit depth and from the pixel-format identifier.
enum class PixelByteOrder : std::uint8_t {
    Unknown,
    NotApplicable,
    LittleEndian,
    BigEndian
};

// Sensor color-filter-array pattern is independent of the sample storage
// container. Bayer samples remain RAW8/RAW16 until an image-processing layer
// deliberately performs demosaicing.
enum class PixelColorFilterArray : std::uint8_t {
    Unknown,
    NotApplicable,
    RGGB,
    GRBG,
    GBRG,
    BGGR
};

// A format identifier is meaningful only inside its declared scheme.
// Examples: "amcam.fourcc" and "pfnc". The common layer never translates
// values between schemes unless an explicit, verified mapping exists.
struct PixelFormatIdentifier {
    std::string scheme;
    std::uint64_t value = 0;
};

// Timestamps are device/backend ticks. ticksPerSecond defines the unit;
// clockDomain is absent when the backend does not identify its clock origin.
struct FrameTimestamp {
    std::uint64_t ticks = 0;
    std::uint64_t ticksPerSecond = 0;
    std::optional<std::string> clockDomain;
};

// Exposure duration has its own timebase and is not inferred from the
// camera's configured exposure setting.
struct FrameExposure {
    std::uint64_t durationTicks = 0;
    std::uint64_t ticksPerSecond = 0;
};

// Backend-neutral frame payload and metadata. The pixel-format enum
// describes the buffer layout known to the consumer; pixelFormatIdentifier
// preserves the backend/standard's exact format identity when available.
// Significant source depth is independent of payload storage width.
struct Frame {
    virtual ~Frame() = default;

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t strideBytes = 0;
    PixelFormat pixelFormat = PixelFormat::Unknown;
    PixelByteOrder pixelByteOrder = PixelByteOrder::Unknown;
    PixelColorFilterArray pixelColorFilterArray =
        PixelColorFilterArray::Unknown;
    std::optional<PixelFormatIdentifier> pixelFormatIdentifier;
    std::uint8_t significantBitsPerPixel = 0;
    std::uint8_t storageBitsPerPixel = 0;
    bool isStill = false;

    // These are optional because a backend or camera may not provide them.
    // A present sequence value of zero is valid and is not a uniqueness key.
    std::optional<std::uint64_t> sequenceNumber;
    std::optional<FrameTimestamp> timestamp;
    std::optional<FrameExposure> exposure;

    std::vector<std::uint8_t> pixels;

    [[nodiscard]] const std::uint8_t* data() const noexcept {
        return pixels.empty() ? nullptr : pixels.data();
    }

    [[nodiscard]] std::size_t byteSize() const noexcept {
        return pixels.size();
    }
};

using FramePtr = std::shared_ptr<const Frame>;

} // namespace camera
