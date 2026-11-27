#include "microscope/core/configuration/OperationMode.h"

#include <utility>

namespace microscope::core {
namespace {

[[nodiscard]] bool isSelectableContrastMethod(ContrastMethod method) noexcept {
    switch (method) {
    case ContrastMethod::Brightfield:
    case ContrastMethod::PhaseContrast:
    case ContrastMethod::DifferentialInterferenceContrast:
    case ContrastMethod::PolarizedLight:
    case ContrastMethod::Fluorescence:
    case ContrastMethod::Darkfield:
    case ContrastMethod::Other:
        return true;
    case ContrastMethod::Unknown:
        return false;
    }
    return false;
}

} // namespace

OperationMode::OperationMode(
    MicroscopeId microscopeId,
    OperationModeId id,
    std::uint64_t revision,
    std::string name,
    ContrastMethod contrastMethod)
    : microscopeId_(std::move(microscopeId)),
      id_(std::move(id)),
      revision_(revision),
      name_(std::move(name)),
      contrastMethod_(contrastMethod) {}

Result<OperationMode, OperationModeError> OperationMode::create(
    MicroscopeId microscopeId,
    OperationModeId id,
    std::uint64_t revision,
    std::string name,
    ContrastMethod contrastMethod) {
    if (name.find(char{}) != std::string::npos) {
        return Result<OperationMode, OperationModeError>::failure(
            OperationModeError::InvalidName);
    }
    if (name.find_first_not_of(" \t\n\r\f\v") == std::string::npos) {
        return Result<OperationMode, OperationModeError>::failure(
            OperationModeError::EmptyName);
    }
    if (!isSelectableContrastMethod(contrastMethod)) {
        return Result<OperationMode, OperationModeError>::failure(
            OperationModeError::InvalidContrastMethod);
    }
    if (revision == 0) {
        return Result<OperationMode, OperationModeError>::failure(
            OperationModeError::ZeroRevision);
    }

    return Result<OperationMode, OperationModeError>::success(
        OperationMode(
            std::move(microscopeId), std::move(id), revision,
            std::move(name), contrastMethod));
}

const MicroscopeId& OperationMode::microscopeId() const noexcept {
    return microscopeId_;
}
const OperationModeId& OperationMode::id() const noexcept { return id_; }
std::uint64_t OperationMode::revision() const noexcept { return revision_; }
const std::string& OperationMode::name() const noexcept { return name_; }
ContrastMethod OperationMode::contrastMethod() const noexcept {
    return contrastMethod_;
}

} // namespace microscope::core
