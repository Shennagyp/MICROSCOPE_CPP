#pragma once

#include "microscope/core/ids/StrongId.h"

#include <cstdint>

namespace microscope::core {

namespace detail {
struct MicroscopeIdTag final {};
struct RuntimeIdTag final {};
struct RuntimeGenerationTag final {};
struct DeviceSessionGenerationTag final {};
struct DeviceIdTag final {};
struct EquipmentComponentIdTag final {};
struct OpticalPortIdTag final {};
struct OpticalConnectionIdTag final {};
struct OpticalPathIdTag final {};
struct LightPathIdTag final {};
struct OperationModeIdTag final {};
struct CameraModeIdTag final {};
struct StageAxisIdTag final {};
struct ConfigurationProfileIdTag final {};
struct ConfigurationTransitionStepIdTag final {};
struct OpticalCompatibilityIdTag final {};
struct IlluminationChannelIdTag final {};
struct SampleTypeIdTag final {};
struct SampleIdTag final {};
struct SlideTypeIdTag final {};
struct SlideIdTag final {};
struct TileIdTag final {};
struct AcquisitionPlanIdTag final {};
struct AcquisitionRunIdTag final {};
struct ImageFrameIdTag final {};
struct DerivedImageIdTag final {};
struct OperationIdTag final {};
struct StepIdTag final {};
struct CommandIdTag final {};
struct CalibrationRevisionIdTag final {};
} // namespace detail

using MicroscopeId = StrongId<detail::MicroscopeIdTag>;
using RuntimeId = StrongId<detail::RuntimeIdTag>;
using DeviceId = StrongId<detail::DeviceIdTag>;
using EquipmentComponentId = StrongId<detail::EquipmentComponentIdTag>;
using OpticalPortId = StrongId<detail::OpticalPortIdTag>;
using OpticalConnectionId = StrongId<detail::OpticalConnectionIdTag>;
using OpticalPathId = StrongId<detail::OpticalPathIdTag>;
using LightPathId = StrongId<detail::LightPathIdTag>;
using OperationModeId = StrongId<detail::OperationModeIdTag>;
using CameraModeId = StrongId<detail::CameraModeIdTag>;
using StageAxisId = StrongId<detail::StageAxisIdTag>;
using ConfigurationProfileId = StrongId<detail::ConfigurationProfileIdTag>;
using ConfigurationTransitionStepId =
    StrongId<detail::ConfigurationTransitionStepIdTag>;
using OpticalCompatibilityId = StrongId<detail::OpticalCompatibilityIdTag>;
using IlluminationChannelId = StrongId<detail::IlluminationChannelIdTag>;
using SampleTypeId = StrongId<detail::SampleTypeIdTag>;
using SampleId = StrongId<detail::SampleIdTag>;
using SlideTypeId = StrongId<detail::SlideTypeIdTag>;
using SlideId = StrongId<detail::SlideIdTag>;
using TileId = StrongId<detail::TileIdTag>;
using AcquisitionPlanId = StrongId<detail::AcquisitionPlanIdTag>;
using AcquisitionRunId = StrongId<detail::AcquisitionRunIdTag>;
using ImageFrameId = StrongId<detail::ImageFrameIdTag>;
using DerivedImageId = StrongId<detail::DerivedImageIdTag>;
using OperationId = StrongId<detail::OperationIdTag>;
using StepId = StrongId<detail::StepIdTag>;
using CommandId = StrongId<detail::CommandIdTag>;
using CalibrationRevisionId = StrongId<detail::CalibrationRevisionIdTag>;

template <typename Tag>
class PositiveGeneration final {
public:
    PositiveGeneration() = delete;

    [[nodiscard]] static Result<PositiveGeneration, IdentityError> create(
        std::uint64_t value) {
        if (value == 0) {
            return Result<PositiveGeneration, IdentityError>::failure(
                IdentityError{
                    IdentityError::Code::ZeroGeneration,
                    "Generation must be greater than zero."});
        }

        return Result<PositiveGeneration, IdentityError>::success(
            PositiveGeneration(value));
    }

    [[nodiscard]] std::uint64_t value() const noexcept {
        return value_;
    }

    friend bool operator==(
        PositiveGeneration left, PositiveGeneration right) noexcept {
        return left.value_ == right.value_;
    }

    friend bool operator!=(
        PositiveGeneration left, PositiveGeneration right) noexcept {
        return !(left == right);
    }

    friend bool operator<(
        PositiveGeneration left, PositiveGeneration right) noexcept {
        return left.value_ < right.value_;
    }

private:
    explicit PositiveGeneration(std::uint64_t value) noexcept
        : value_(value) {}

    std::uint64_t value_;
};

using RuntimeGeneration = PositiveGeneration<detail::RuntimeGenerationTag>;
using DeviceSessionGeneration =
    PositiveGeneration<detail::DeviceSessionGenerationTag>;

} // namespace microscope::core
