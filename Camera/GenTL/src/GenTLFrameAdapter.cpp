#include "camera/gentl/GenTLFrameAdapter.h"

#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace camera::gentl {
namespace {

constexpr std::uint64_t pfnc32Mono8 = UINT64_C(0x01080001);
constexpr std::uint64_t pfnc32Mono10 = UINT64_C(0x01100003);
constexpr std::uint64_t pfnc32Mono12 = UINT64_C(0x01100005);
constexpr std::uint64_t pfnc32Mono14 = UINT64_C(0x01100025);
constexpr std::uint64_t pfnc32Mono16 = UINT64_C(0x01100007);
constexpr std::uint64_t pfnc32Rgb8 = UINT64_C(0x02180014);
constexpr std::uint64_t pfnc16Mono8 = UINT64_C(0x0101);
constexpr std::uint64_t pfnc16Mono16 = UINT64_C(0x0105);
constexpr std::uint64_t pfnc16Rgb8 = UINT64_C(0x0401);
constexpr std::uint64_t nanosecondsPerSecond = UINT64_C(1000000000);

bool isPfncNamespace(std::uint64_t value) noexcept {
    return value == GenTL::PIXELFORMAT_NAMESPACE_PFNC_16BIT ||
           value == GenTL::PIXELFORMAT_NAMESPACE_PFNC_32BIT;
}

bool isPfnc32(const DataStreamFrame& source, std::uint64_t format) noexcept {
    return source.pixelFormatNamespace ==
               GenTL::PIXELFORMAT_NAMESPACE_PFNC_32BIT &&
           source.pixelFormat == format;
}

bool isPfnc16(const DataStreamFrame& source, std::uint64_t format) noexcept {
    return source.pixelFormatNamespace ==
               GenTL::PIXELFORMAT_NAMESPACE_PFNC_16BIT &&
           source.pixelFormat == format;
}

camera::PixelByteOrder pixelByteOrder(
    const std::optional<GenTL::PIXELENDIANNESS_ID>& source) noexcept {
    if (!source) return camera::PixelByteOrder::Unknown;
    switch (*source) {
    case GenTL::PIXELENDIANNESS_LITTLE:
        return camera::PixelByteOrder::LittleEndian;
    case GenTL::PIXELENDIANNESS_BIG:
        return camera::PixelByteOrder::BigEndian;
    default:
        return camera::PixelByteOrder::Unknown;
    }
}

bool payloadSizeMatches(
    std::uint32_t width, std::uint32_t height,
    std::size_t bytesPerPixel, std::size_t actualSize) noexcept {
    const auto maximum = std::numeric_limits<std::size_t>::max();
    const auto w = static_cast<std::size_t>(width);
    const auto h = static_cast<std::size_t>(height);
    if (w == 0 || h == 0 || w > maximum / h) return false;
    const auto pixelCount = w * h;
    if (bytesPerPixel == 0 || pixelCount > maximum / bytesPerPixel)
        return false;
    return actualSize == pixelCount * bytesPerPixel;
}

bool strideFits(std::uint32_t width, std::size_t bytesPerPixel) noexcept {
    return bytesPerPixel != 0 &&
        static_cast<std::uint64_t>(width) <=
            std::numeric_limits<std::uint32_t>::max() / bytesPerPixel;
}

} // namespace

std::optional<camera::FramePtr>
makeCommonFrame(DataStreamFrame source) {
    if (source.payload.empty() ||
        !source.imagePresent.value_or(false) ||
        source.incomplete ||
        !source.containsChunkData ||
        *source.containsChunkData ||
        !source.dataLargerThanBuffer ||
        *source.dataLargerThanBuffer ||
        !source.isComposite ||
        *source.isComposite ||
        !source.deliveredImageHeight ||
        !source.height ||
        *source.deliveredImageHeight != *source.height ||
        !source.payloadType ||
        *source.payloadType != GenTL::PAYLOAD_TYPE_IMAGE ||
        !source.width || !source.height ||
        *source.width == 0 || *source.height == 0 ||
        *source.width > std::numeric_limits<std::uint32_t>::max() ||
        *source.height > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }

    if (!source.imageOffset || *source.imageOffset != 0 ||
        !source.xPadding || *source.xPadding != 0 ||
        !source.yPadding || *source.yPadding != 0) {
        return std::nullopt;
    }

    auto frame = std::make_shared<camera::Frame>();
    frame->width = static_cast<std::uint32_t>(*source.width);
    frame->height = static_cast<std::uint32_t>(*source.height);
    frame->pixels = std::move(source.payload);
    frame->sequenceNumber = source.frameId;
    frame->pixelByteOrder = pixelByteOrder(source.pixelEndianness);
    if (source.timestampNanoseconds) {
        frame->timestamp = camera::FrameTimestamp{
            *source.timestampNanoseconds, nanosecondsPerSecond, std::nullopt};
    }

    if (source.pixelFormat) {
        std::string scheme = "gentl.namespace.unknown";
        if (source.pixelFormatNamespace) {
            if (isPfncNamespace(*source.pixelFormatNamespace)) {
                scheme = "pfnc";
            } else {
                scheme = "gentl.namespace." +
                    std::to_string(*source.pixelFormatNamespace);
            }
        }
        frame->pixelFormatIdentifier = camera::PixelFormatIdentifier{
            std::move(scheme), *source.pixelFormat};
    }

    // Map only layouts whose component order, storage width, row stride and
    // byte order are explicit. Packed, planar, Bayer and compressed formats
    // remain opaque until their exact layout contracts are implemented.
    const bool mono8 =
        isPfnc32(source, pfnc32Mono8) ||
        isPfnc16(source, pfnc16Mono8);
    if (mono8 && payloadSizeMatches(frame->width, frame->height, 1,
                                    frame->pixels.size()) &&
        strideFits(frame->width, 1)) {
        frame->pixelFormat = camera::PixelFormat::Raw8;
        frame->pixelByteOrder = camera::PixelByteOrder::NotApplicable;
        frame->strideBytes = frame->width;
        frame->significantBitsPerPixel = 8;
        frame->storageBitsPerPixel = 8;
    }

    const bool rgb8 =
        isPfnc32(source, pfnc32Rgb8) ||
        isPfnc16(source, pfnc16Rgb8);
    if (rgb8 && payloadSizeMatches(frame->width, frame->height, 3,
                                   frame->pixels.size()) &&
        strideFits(frame->width, 3)) {
        frame->pixelFormat = camera::PixelFormat::RGB24;
        frame->pixelByteOrder = camera::PixelByteOrder::NotApplicable;
        frame->strideBytes = frame->width * 3;
        frame->significantBitsPerPixel = 24;
        frame->storageBitsPerPixel = 24;
    }

    std::uint8_t monoSignificantBits = 0;
    if (isPfnc32(source, pfnc32Mono10)) monoSignificantBits = 10;
    else if (isPfnc32(source, pfnc32Mono12)) monoSignificantBits = 12;
    else if (isPfnc32(source, pfnc32Mono14)) monoSignificantBits = 14;
    else if (isPfnc32(source, pfnc32Mono16) ||
             isPfnc16(source, pfnc16Mono16)) monoSignificantBits = 16;

    const auto byteOrder = pixelByteOrder(source.pixelEndianness);
    if (monoSignificantBits != 0 &&
        byteOrder != camera::PixelByteOrder::Unknown &&
        payloadSizeMatches(frame->width, frame->height, 2,
                           frame->pixels.size()) &&
        strideFits(frame->width, 2)) {
        frame->pixelFormat = camera::PixelFormat::Raw16;
        frame->pixelByteOrder = byteOrder;
        frame->strideBytes = frame->width * 2;
        frame->significantBitsPerPixel = monoSignificantBits;
        frame->storageBitsPerPixel = 16;
    }

    return camera::FramePtr{std::move(frame)};
}

} // namespace camera::gentl
