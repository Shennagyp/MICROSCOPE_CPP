#include "camera/gentl/GenTLFrameAdapter.h"

#include <limits>
#include <memory>
#include <string>

namespace camera::gentl {
namespace {

constexpr std::uint64_t pfncMono8 = UINT64_C(0x01080001);
constexpr std::uint64_t nanosecondsPerSecond = UINT64_C(1000000000);

bool isPfncNamespace(std::uint64_t value) noexcept {
    return value == GenTL::PIXELFORMAT_NAMESPACE_PFNC_16BIT ||
           value == GenTL::PIXELFORMAT_NAMESPACE_PFNC_32BIT;
}

} // namespace

std::optional<camera::FramePtr>
makeCommonFrame(const DataStreamFrame& source) {
    if (!source.imagePresent.value_or(false) ||
        source.incomplete ||
        source.containsChunkData.value_or(false) ||
        !source.payloadType ||
        *source.payloadType != GenTL::PAYLOAD_TYPE_IMAGE ||
        !source.width || !source.height ||
        *source.width == 0 || *source.height == 0 ||
        *source.width > std::numeric_limits<std::uint32_t>::max() ||
        *source.height > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }

    if (source.imageOffset.value_or(0) != 0 ||
        source.xPadding.value_or(0) != 0 ||
        source.yPadding.value_or(0) != 0) {
        return std::nullopt;
    }

    auto frame = std::make_shared<camera::Frame>();
    frame->width = static_cast<std::uint32_t>(*source.width);
    frame->height = static_cast<std::uint32_t>(*source.height);
    frame->pixels = source.payload;
    frame->sequenceNumber = source.frameId;
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

    // Mono8 is the first verified PFNC layout mapping. Do not infer stride
    // for other formats from payload size: packed, planar, multipart and
    // compressed formats need their own explicit layout rules.
    if (source.pixelFormatNamespace ==
            GenTL::PIXELFORMAT_NAMESPACE_PFNC_32BIT &&
        source.pixelFormat == pfncMono8 &&
        source.payload.size() ==
            static_cast<std::size_t>(frame->width) * frame->height) {
        frame->pixelFormat = camera::PixelFormat::Raw8;
        frame->strideBytes = frame->width;
        frame->significantBitsPerPixel = 8;
        frame->storageBitsPerPixel = 8;
    }

    return camera::FramePtr{std::move(frame)};
}

} // namespace camera::gentl
