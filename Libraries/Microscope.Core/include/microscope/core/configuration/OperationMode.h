#pragma once

#include "microscope/core/configuration/Configuration.h"
#include "microscope/core/configuration/LightPathCalibration.h"

#include <cstdint>
#include <string>
#include <vector>

namespace microscope::core {

enum class OperationModeError {
    EmptyName,
    InvalidContrastMethod,
    ZeroRevision,
    NoLightPathSettings,
    LightPathSettingModeMismatch,
    LightPathSettingRevisionMismatch,
    LightPathSettingMicroscopeMismatch,
    LightPathSettingEquipmentRevisionMismatch,
    DuplicateLightPath
};

// An editable, versioned microscope mode. It contains desired configuration,
// including one calibrated irradiance setting per selected LightPath. It does
// not pin a camera identity: the selected LightPath's imaging route determines
// the connected camera endpoint, with manual camera choice handled at request
// time among those endpoints.
class OperationMode final {
public:
    [[nodiscard]] static Result<OperationMode, OperationModeError> create(
        MicroscopeId microscopeId,
        OperationModeId id,
        std::uint64_t revision,
        std::string name,
        ContrastMethod contrastMethod,
        std::vector<LightPathModeSetting> lightPathSettings);

    [[nodiscard]] const MicroscopeId& microscopeId() const noexcept;
    [[nodiscard]] const OperationModeId& id() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] ContrastMethod contrastMethod() const noexcept;
    [[nodiscard]] const std::vector<LightPathModeSetting>&
    lightPathSettings() const noexcept;

    [[nodiscard]] const LightPathModeSetting* findLightPathSetting(
        const LightPathId& lightPathId) const noexcept;

private:
    OperationMode(
        MicroscopeId microscopeId,
        OperationModeId id,
        std::uint64_t revision,
        std::string name,
        ContrastMethod contrastMethod,
        std::vector<LightPathModeSetting> lightPathSettings);

    MicroscopeId microscopeId_;
    OperationModeId id_;
    std::uint64_t revision_;
    std::string name_;
    ContrastMethod contrastMethod_;
    std::vector<LightPathModeSetting> lightPathSettings_;
};

} // namespace microscope::core
