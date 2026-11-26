#include "microscope/core/configuration/OperationMode.h"

#include <set>
#include <utility>

namespace microscope::core {
namespace {

[[nodiscard]] bool isKnownContrastMethod(ContrastMethod method) noexcept {
    switch (method) {
    case ContrastMethod::Brightfield:
    case ContrastMethod::PhaseContrast:
    case ContrastMethod::DifferentialInterferenceContrast:
    case ContrastMethod::PolarizedLight:
    case ContrastMethod::Fluorescence:
    case ContrastMethod::Darkfield:
    case ContrastMethod::Other:
    case ContrastMethod::Unknown:
        return true;
    }
    return false;
}

} // namespace

OperationMode::OperationMode(
    MicroscopeId microscopeId,
    OperationModeId id,
    std::uint64_t revision,
    std::string name,
    ContrastMethod contrastMethod,
    std::vector<LightPathModeSetting> lightPathSettings)
    : microscopeId_(std::move(microscopeId)),
      id_(std::move(id)),
      revision_(revision),
      name_(std::move(name)),
      contrastMethod_(contrastMethod),
      lightPathSettings_(std::move(lightPathSettings)) {}

Result<OperationMode, OperationModeError> OperationMode::create(
    MicroscopeId microscopeId,
    OperationModeId id,
    std::uint64_t revision,
    std::string name,
    ContrastMethod contrastMethod,
    std::vector<LightPathModeSetting> lightPathSettings) {
    if (name.find('\0') != std::string::npos) {
        return Result<OperationMode, OperationModeError>::failure(
            OperationModeError::InvalidName);
    }
    if (name.find_first_not_of(" \t\n\r\f\v") == std::string::npos) {
        return Result<OperationMode, OperationModeError>::failure(
            OperationModeError::EmptyName);
    }
    if (!isKnownContrastMethod(contrastMethod)) {
        return Result<OperationMode, OperationModeError>::failure(
            OperationModeError::InvalidContrastMethod);
    }
    if (revision == 0) {
        return Result<OperationMode, OperationModeError>::failure(
            OperationModeError::ZeroRevision);
    }
    if (lightPathSettings.empty()) {
        return Result<OperationMode, OperationModeError>::failure(
            OperationModeError::NoLightPathSettings);
    }

    std::set<LightPathId> lightPathIds;
    const auto equipmentRevision =
        lightPathSettings.front().equipmentDefinitionRevision();
    for (const auto& setting : lightPathSettings) {
        if (setting.operationModeId() != id) {
            return Result<OperationMode, OperationModeError>::failure(
                OperationModeError::LightPathSettingModeMismatch);
        }
        if (setting.operationModeRevision() != revision) {
            return Result<OperationMode, OperationModeError>::failure(
                OperationModeError::LightPathSettingRevisionMismatch);
        }
        if (setting.microscopeId() != microscopeId) {
            return Result<OperationMode, OperationModeError>::failure(
                OperationModeError::LightPathSettingMicroscopeMismatch);
        }
        if (setting.equipmentDefinitionRevision() != equipmentRevision) {
            return Result<OperationMode, OperationModeError>::failure(
                OperationModeError::LightPathSettingEquipmentRevisionMismatch);
        }
        if (!lightPathIds.insert(setting.lightPathId()).second) {
            return Result<OperationMode, OperationModeError>::failure(
                OperationModeError::DuplicateLightPath);
        }
    }

    return Result<OperationMode, OperationModeError>::success(
        OperationMode(
            std::move(microscopeId),
            std::move(id),
            revision,
            std::move(name),
            contrastMethod,
            std::move(lightPathSettings)));
}

const MicroscopeId& OperationMode::microscopeId() const noexcept {
    return microscopeId_;
}

const OperationModeId& OperationMode::id() const noexcept {
    return id_;
}

std::uint64_t OperationMode::revision() const noexcept {
    return revision_;
}

const std::string& OperationMode::name() const noexcept {
    return name_;
}

ContrastMethod OperationMode::contrastMethod() const noexcept {
    return contrastMethod_;
}

const std::vector<LightPathModeSetting>&
OperationMode::lightPathSettings() const noexcept {
    return lightPathSettings_;
}

const LightPathModeSetting* OperationMode::findLightPathSetting(
    const LightPathId& lightPathId) const noexcept {
    for (const auto& setting : lightPathSettings_) {
        if (setting.lightPathId() == lightPathId) {
            return &setting;
        }
    }
    return nullptr;
}

} // namespace microscope::core
