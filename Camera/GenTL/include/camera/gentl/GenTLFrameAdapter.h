#pragma once

#include "camera/CameraFrames.h"
#include "camera/gentl/Producer.h"

#include <optional>

namespace camera::gentl {

// Converts a complete, image-bearing GenTL buffer into the Common frame
// contract when the metadata can be represented without losing layout facts.
// Unknown pixel formats remain Unknown while their raw identifier and bytes
// are preserved. Complete image payloads with validated image offset and row/
// trailing padding are projected without stripping or rearranging payload bytes.
// Non-image, incomplete, chunked, composite and unsupported multipart payloads
// remain outside the single-raster Common frame projection.
std::optional<camera::FramePtr>
makeCommonFrame(DataStreamFrame source);

} // namespace camera::gentl
