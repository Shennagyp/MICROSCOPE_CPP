#pragma once

#include "microscope/core/configuration/Configuration.h"

#include <cstdint>
#include <string>

namespace microscope::core {

enum class OperationModeError {
    EmptyName,
    InvalidName,
    InvalidContrastMethod,
    ZeroRevision
};

// A microscope-scoped, revisioned operating intent. It defines the contrast
// method only. LightPath selections, calibrated settings and camera discovery
// belong to separate hardware/configuration services.
class OperationMode final {
public:
    [[nodiscard]] static Result<OperationMode, OperationModeError> create(
        MicroscopeId microscopeId,
        OperationModeId id,
        std::uint64_t revision,
        std::string name,
        ContrastMethod contrastMethod);

    [[nodiscard]] const MicroscopeId& microscopeId() const noexcept;
    [[nodiscard]] const OperationModeId& id() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] ContrastMethod contrastMethod() const noexcept;

private:
    OperationMode(
        MicroscopeId microscopeId,
        OperationModeId id,
        std::uint64_t revision,
        std::string name,
        ContrastMethod contrastMethod);

    MicroscopeId microscopeId_;
    OperationModeId id_;
    std::uint64_t revision_;
    std::string name_;
    ContrastMethod contrastMethod_;
};

} // namespace microscope::core
