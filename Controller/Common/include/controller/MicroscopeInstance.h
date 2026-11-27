#pragma once

#include "controller/stage/Stage.h"

#include <utility>

namespace microscope::controller {

// A live physical microscope instance. Application acquisition and slide
// lifecycle remain outside this controller aggregate.
class MicroscopeInstance final {
public:
    MicroscopeInstance(core::MicroscopeId id, Stage stage)
        : id_(std::move(id)), stage_(std::move(stage)) {}

    const core::MicroscopeId& id() const noexcept { return id_; }

    Stage& stage() noexcept { return stage_; }
    const Stage& stage() const noexcept { return stage_; }

private:
    core::MicroscopeId id_;
    Stage stage_;
};

} // namespace microscope::controller
