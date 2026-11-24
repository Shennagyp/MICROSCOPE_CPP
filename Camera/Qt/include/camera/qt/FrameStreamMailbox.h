#pragma once

#include "camera/FrameStreamMailbox.h"

namespace camera::qt {

// Source-compatibility alias. The mailbox implementation is frontend-neutral
// and lives in Camera/Common so Qt and native macOS use the same semantics.
using FrameStreamMailbox = ::camera::FrameStreamMailbox;

} // namespace camera::qt
