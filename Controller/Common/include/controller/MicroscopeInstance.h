#pragma once

#include "controller/stage/Stage.h"

#include <utility>

namespace microscope::controller {

// A live physical microscope instance. Application acquisition and slide
// lifecycle remain outside this controller aggregate.
class MicroscopeInstance final {
public:
    MicroscopeInstance(core::MicroscopeId id,core::DeviceId stageDeviceId,Stage stage)
        :id_(std::move(id)),stageDeviceId_(std::move(stageDeviceId)),
         stage_(std::move(stage)){}

    const core::MicroscopeId& id() const noexcept { return id_; }
    const core::DeviceId& stageDeviceId() const noexcept { return stageDeviceId_; }

    Stage& stage() noexcept { return stage_; }
    const Stage& stage() const noexcept { return stage_; }

private:
    core::MicroscopeId id_;
    core::DeviceId stageDeviceId_;
    Stage stage_;
};

} // namespace microscope::controller
