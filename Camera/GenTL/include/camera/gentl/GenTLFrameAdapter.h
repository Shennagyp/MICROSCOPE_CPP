#pragma once

#include "camera/CameraFrames.h"
#include "camera/gentl/Producer.h"

#include <optional>

namespace camera::gentl {

// Converts a complete, image-bearing GenTL buffer into the Common frame
// contract when the metadata can be represented without losing layout facts.
// Unknown pixel formats remain Unknown while their raw identifier and bytes
// are preserved. Non-image, incomplete, chunked, or offset/padded buffers are
// not projected into the simpler Common frame type.
std::optional<camera::FramePtr>
makeCommonFrame(const DataStreamFrame& source);

} // namespace camera::gentl
