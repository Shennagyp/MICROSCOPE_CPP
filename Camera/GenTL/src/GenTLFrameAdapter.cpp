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
constexpr std::uint64_t pfnc32Mono10p = UINT64_C(0x010A0046);
constexpr std::uint64_t pfnc32Mono12p = UINT64_C(0x010C0047);
constexpr std::uint64_t pfnc32Mono14p = UINT64_C(0x010E0104);
constexpr std::uint64_t pfnc32Rgb8 = UINT64_C(0x02180014);
constexpr std::uint64_t pfnc16Mono8 = UINT64_C(0x0101);
constexpr std::uint64_t pfnc16Mono16 = UINT64_C(0x0105);
constexpr std::uint64_t pfnc16Rgb8 = UINT64_C(0x0401);
constexpr std::uint64_t nanosecondsPerSecond = UINT64_C(1000000000);

struct BayerFormat final {
    std::uint64_t pixelFormat;
    camera::PixelColorFilterArray pattern;
    std::uint8_t significantBits;
    bool packed = false;
};

// PFNC 32-bit format identifiers. Unpacked 10/12/14/16-bit variants use
// 16-bit containers; 10p/12p variants retain their packed byte layout.
constexpr BayerFormat bayerFormats[] = {
    {UINT64_C(0x01080008), camera::PixelColorFilterArray::GRBG, 8},
    {UINT64_C(0x0110000C), camera::PixelColorFilterArray::GRBG, 10},
    {UINT64_C(0x01100010), camera::PixelColorFilterArray::GRBG, 12},
    {UINT64_C(0x01100109), camera::PixelColorFilterArray::GRBG, 14},
    {UINT64_C(0x0110002E), camera::PixelColorFilterArray::GRBG, 16},
    {UINT64_C(0x010A0056), camera::PixelColorFilterArray::GRBG, 10, true},
    {UINT64_C(0x010C0057), camera::PixelColorFilterArray::GRBG, 12, true},
    {UINT64_C(0x010E0105), camera::PixelColorFilterArray::GRBG, 14, true},
    {UINT64_C(0x01080009), camera::PixelColorFilterArray::RGGB, 8},
    {UINT64_C(0x0110000D), camera::PixelColorFilterArray::RGGB, 10},
    {UINT64_C(0x01100011), camera::PixelColorFilterArray::RGGB, 12},
    {UINT64_C(0x0110010A), camera::PixelColorFilterArray::RGGB, 14},
    {UINT64_C(0x0110002F), camera::PixelColorFilterArray::RGGB, 16},
    {UINT64_C(0x010A0058), camera::PixelColorFilterArray::RGGB, 10, true},
    {UINT64_C(0x010C0059), camera::PixelColorFilterArray::RGGB, 12, true},
    {UINT64_C(0x010E0106), camera::PixelColorFilterArray::RGGB, 14, true},
    {UINT64_C(0x0108000A), camera::PixelColorFilterArray::GBRG, 8},
    {UINT64_C(0x0110000E), camera::PixelColorFilterArray::GBRG, 10},
    {UINT64_C(0x01100012), camera::PixelColorFilterArray::GBRG, 12},
    {UINT64_C(0x0110010B), camera::PixelColorFilterArray::GBRG, 14},
    {UINT64_C(0x01100030), camera::PixelColorFilterArray::GBRG, 16},
    {UINT64_C(0x010A0054), camera::PixelColorFilterArray::GBRG, 10, true},
    {UINT64_C(0x010C0055), camera::PixelColorFilterArray::GBRG, 12, true},
    {UINT64_C(0x010E0107), camera::PixelColorFilterArray::GBRG, 14, true},
    {UINT64_C(0x0108000B), camera::PixelColorFilterArray::BGGR, 8},
    {UINT64_C(0x0110000F), camera::PixelColorFilterArray::BGGR, 10},
    {UINT64_C(0x01100013), camera::PixelColorFilterArray::BGGR, 12},
    {UINT64_C(0x0110010C), camera::PixelColorFilterArray::BGGR, 14},
    {UINT64_C(0x01100031), camera::PixelColorFilterArray::BGGR, 16},
    {UINT64_C(0x010A0052), camera::PixelColorFilterArray::BGGR, 10, true},
    {UINT64_C(0x010C0053), camera::PixelColorFilterArray::BGGR, 12, true},
    {UINT64_C(0x010E0108), camera::PixelColorFilterArray::BGGR, 14, true}
};

std::optional<BayerFormat> bayerFormat(
    const DataStreamFrame& source) noexcept {
    if (source.pixelFormatNamespace !=
        GenTL::PIXELFORMAT_NAMESPACE_PFNC_32BIT || !source.pixelFormat) {
        return std::nullopt;
    }

    for (const auto& candidate : bayerFormats) {
        if (candidate.pixelFormat == *source.pixelFormat)
            return candidate;
    }
    return std::nullopt;
}

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

bool imageLayoutFits(
    std::size_t activeRowBytes, std::size_t height,
    std::size_t imageOffset, std::size_t rowPadding,
    std::size_t trailingPadding, std::size_t payloadSize,
    std::uint32_t& strideBytes) noexcept {
    const auto maximum = std::numeric_limits<std::size_t>::max();
    if (activeRowBytes == 0 || height == 0 ||
        rowPadding > maximum - activeRowBytes) return false;
    const auto rowStride = activeRowBytes + rowPadding;
    if (rowStride > std::numeric_limits<std::uint32_t>::max() ||
        rowStride > maximum / height) return false;
    const auto imageBytes = rowStride * height;
    if (imageOffset > payloadSize ||
        imageBytes > payloadSize - imageOffset ||
        trailingPadding != payloadSize - imageOffset - imageBytes) return false;
    strideBytes = static_cast<std::uint32_t>(rowStride);
    return true;
}

bool rowBytesForPixels(std::uint32_t width, std::size_t bytesPerPixel,
                       std::size_t& rowBytes) noexcept {
    if (width == 0 || bytesPerPixel == 0 ||
        static_cast<std::size_t>(width) >
            std::numeric_limits<std::size_t>::max() / bytesPerPixel)
        return false;
    rowBytes = static_cast<std::size_t>(width) * bytesPerPixel;
    return true;
}

bool packedRowBytes(std::uint32_t width, std::uint8_t bitsPerPixel,
                    std::size_t& rowBytes) noexcept {
    if (width == 0 || bitsPerPixel == 0) return false;
    const auto maximum = std::numeric_limits<std::size_t>::max();
    const auto w = static_cast<std::size_t>(width);
    if (w > (maximum - 7) / bitsPerPixel) return false;
    rowBytes = (w * bitsPerPixel + 7) / 8;
    return rowBytes != 0;
}

} // namespace

std::optional<camera::FramePtr>
makeCommonFrame(DataStreamFrame source) {
    const bool multipartPayload =
        source.payloadType == GenTL::PAYLOAD_TYPE_MULTI_PART ||
        source.isComposite.value_or(false);
    if (multipartPayload) {
        if (source.payload.empty() || source.incomplete ||
            source.dataLargerThanBuffer.value_or(false))
            return std::nullopt;

        auto frame = std::make_shared<camera::Frame>();
        frame->isMultipart = true;
        frame->pixels = std::move(source.payload);
        frame->sequenceNumber = source.frameId;
        if (source.timestampNanoseconds) {
            frame->timestamp = camera::FrameTimestamp{
                *source.timestampNanoseconds, nanosecondsPerSecond, std::nullopt};
        }

        frame->parts.reserve(source.parts.size());
        for (const auto& sourcePart : source.parts) {
            if (sourcePart.offsetBytes > frame->pixels.size() ||
                sourcePart.byteSize > frame->pixels.size() - sourcePart.offsetBytes)
                return std::nullopt;
            camera::FramePart part;
            part.offsetBytes = sourcePart.offsetBytes;
            part.byteSize = sourcePart.byteSize;
            part.dataType = sourcePart.dataType;
            part.dataFormat = sourcePart.dataFormat;
            part.dataFormatNamespace = sourcePart.dataFormatNamespace;
            part.width = sourcePart.width;
            part.height = sourcePart.height;
            part.xOffset = sourcePart.xOffset;
            part.yOffset = sourcePart.yOffset;
            part.xPadding = sourcePart.xPadding;
            part.deliveredImageHeight = sourcePart.deliveredImageHeight;
            part.sourceId = sourcePart.sourceId;
            part.regionId = sourcePart.regionId;
            part.dataPurposeId = sourcePart.dataPurposeId;
            frame->parts.push_back(std::move(part));
        }
        return camera::FramePtr{std::move(frame)};
    }

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

    if (!source.imageOffset || !source.xPadding || !source.yPadding ||
        *source.imageOffset > source.payload.size() ||
        *source.yPadding > source.payload.size() - *source.imageOffset)
        return std::nullopt;

    auto frame = std::make_shared<camera::Frame>();
    frame->width = static_cast<std::uint32_t>(*source.width);
    frame->height = static_cast<std::uint32_t>(*source.height);
    frame->imageOffsetBytes = *source.imageOffset;
    frame->rowPaddingBytes = *source.xPadding;
    frame->trailingPaddingBytes = *source.yPadding;
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

    // Map Bayer sample storage and mosaic pattern independently. This
    // preserves raw sensor samples for downstream processing; this adapter
    // never demosaics or changes sample values.
    if (const auto bayer = bayerFormat(source)) {
        frame->pixelColorFilterArray = bayer->pattern;
        if (bayer->packed) {
            std::size_t activeRowBytes = 0;
            std::uint32_t packedStride = 0;
            if (packedRowBytes(frame->width, bayer->significantBits, activeRowBytes) &&
                imageLayoutFits(activeRowBytes, frame->height, frame->imageOffsetBytes,
                    frame->rowPaddingBytes, frame->trailingPaddingBytes,
                    frame->pixels.size(), packedStride)) {
                frame->pixelFormat = camera::PixelFormat::Raw8;
                frame->pixelPacking =
                    bayer->significantBits == 10 ? camera::PixelPacking::PFNC10p :
                    bayer->significantBits == 12 ? camera::PixelPacking::PFNC12p :
                                                   camera::PixelPacking::PFNC14p;
                frame->pixelByteOrder = camera::PixelByteOrder::NotApplicable;
                frame->strideBytes = packedStride;
                frame->significantBitsPerPixel = bayer->significantBits;
                frame->storageBitsPerPixel = bayer->significantBits;
            }
        } else {
            const std::size_t bytesPerPixel =
                bayer->significantBits == 8 ? 1 : 2;
            const auto reportedByteOrder = pixelByteOrder(source.pixelEndianness);
            std::size_t activeRowBytes = 0;
            std::uint32_t rowStride = 0;
            const bool byteOrderIsSafe = bytesPerPixel == 1 ||
                reportedByteOrder != camera::PixelByteOrder::Unknown;
            if (byteOrderIsSafe &&
                rowBytesForPixels(frame->width, bytesPerPixel, activeRowBytes) &&
                imageLayoutFits(activeRowBytes, frame->height, frame->imageOffsetBytes,
                    frame->rowPaddingBytes, frame->trailingPaddingBytes,
                    frame->pixels.size(), rowStride)) {
                frame->pixelFormat = bytesPerPixel == 1
                    ? camera::PixelFormat::Raw8
                    : camera::PixelFormat::Raw16;
                frame->pixelPacking = camera::PixelPacking::NotPacked;
                frame->pixelByteOrder = bytesPerPixel == 1
                    ? camera::PixelByteOrder::NotApplicable
                    : reportedByteOrder;
                frame->strideBytes = rowStride;
                frame->significantBitsPerPixel = bayer->significantBits;
                frame->storageBitsPerPixel = static_cast<std::uint8_t>(
                    bytesPerPixel * 8);
            }
        }
    }

    // PFNC Mono10p/Mono12p/Mono14p retain their packed wire representation.
    // Do not unpack or reorder samples in the camera backend. Each row is
    // byte-aligned; the final byte may contain unused high bits for widths
    // that do not fill a complete byte.
    std::uint8_t packedSignificantBits = 0;
    camera::PixelPacking packedKind = camera::PixelPacking::Unknown;
    if (isPfnc32(source, pfnc32Mono10p)) {
        packedSignificantBits = 10;
        packedKind = camera::PixelPacking::PFNC10p;
    } else if (isPfnc32(source, pfnc32Mono12p)) {
        packedSignificantBits = 12;
        packedKind = camera::PixelPacking::PFNC12p;
    } else if (isPfnc32(source, pfnc32Mono14p)) {
        packedSignificantBits = 14;
        packedKind = camera::PixelPacking::PFNC14p;
    }
    if (packedSignificantBits != 0) {
        std::size_t activeRowBytes = 0;
        std::uint32_t packedStride = 0;
        if (packedRowBytes(frame->width, packedSignificantBits, activeRowBytes) &&
            imageLayoutFits(activeRowBytes, frame->height, frame->imageOffsetBytes,
                frame->rowPaddingBytes, frame->trailingPaddingBytes,
                frame->pixels.size(), packedStride)) {
            frame->pixelFormat = camera::PixelFormat::Raw8;
            frame->pixelByteOrder = camera::PixelByteOrder::NotApplicable;
            frame->pixelColorFilterArray =
                camera::PixelColorFilterArray::NotApplicable;
            frame->pixelPacking = packedKind;
            frame->strideBytes = packedStride;
            frame->significantBitsPerPixel = packedSignificantBits;
            frame->storageBitsPerPixel = packedSignificantBits;
        }
    }

    // Map only layouts whose component order, storage width, row stride and
    // byte order are explicit. Other packed, planar and compressed formats
    // remain opaque until their exact layout contracts are implemented.
    const bool mono8 =
        isPfnc32(source, pfnc32Mono8) ||
        isPfnc16(source, pfnc16Mono8);
    std::size_t mono8RowBytes = 0;
    std::uint32_t mono8Stride = 0;
    if (mono8 && rowBytesForPixels(frame->width, 1, mono8RowBytes) &&
        imageLayoutFits(mono8RowBytes, frame->height, frame->imageOffsetBytes,
            frame->rowPaddingBytes, frame->trailingPaddingBytes,
            frame->pixels.size(), mono8Stride)) {
        frame->pixelFormat = camera::PixelFormat::Raw8;
        frame->pixelByteOrder = camera::PixelByteOrder::NotApplicable;
        frame->pixelColorFilterArray =
            camera::PixelColorFilterArray::NotApplicable;
        frame->pixelPacking = camera::PixelPacking::NotPacked;
        frame->strideBytes = mono8Stride;
        frame->significantBitsPerPixel = 8;
        frame->storageBitsPerPixel = 8;
    }

    const bool rgb8 =
        isPfnc32(source, pfnc32Rgb8) ||
        isPfnc16(source, pfnc16Rgb8);
    std::size_t rgb8RowBytes = 0;
    std::uint32_t rgb8Stride = 0;
    if (rgb8 && rowBytesForPixels(frame->width, 3, rgb8RowBytes) &&
        imageLayoutFits(rgb8RowBytes, frame->height, frame->imageOffsetBytes,
            frame->rowPaddingBytes, frame->trailingPaddingBytes,
            frame->pixels.size(), rgb8Stride)) {
        frame->pixelFormat = camera::PixelFormat::RGB24;
        frame->pixelByteOrder = camera::PixelByteOrder::NotApplicable;
        frame->pixelColorFilterArray =
            camera::PixelColorFilterArray::NotApplicable;
        frame->pixelPacking = camera::PixelPacking::NotPacked;
        frame->strideBytes = rgb8Stride;
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
    std::size_t mono16RowBytes = 0;
    std::uint32_t mono16Stride = 0;
    if (monoSignificantBits != 0 &&
        byteOrder != camera::PixelByteOrder::Unknown &&
        rowBytesForPixels(frame->width, 2, mono16RowBytes) &&
        imageLayoutFits(mono16RowBytes, frame->height, frame->imageOffsetBytes,
            frame->rowPaddingBytes, frame->trailingPaddingBytes,
            frame->pixels.size(), mono16Stride)) {
        frame->pixelFormat = camera::PixelFormat::Raw16;
        frame->pixelByteOrder = byteOrder;
        frame->pixelColorFilterArray =
            camera::PixelColorFilterArray::NotApplicable;
        frame->pixelPacking = camera::PixelPacking::NotPacked;
        frame->strideBytes = mono16Stride;
        frame->significantBitsPerPixel = monoSignificantBits;
        frame->storageBitsPerPixel = 16;
    }

    return camera::FramePtr{std::move(frame)};
}

} // namespace camera::gentl
