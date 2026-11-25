#pragma once

#include "microscope/core/ids/StrongId.h"

#include <cstdint>

namespace microscope::core {

namespace detail {
struct MicroscopeIdTag final {};
struct RuntimeIdTag final {};
struct DeviceIdTag final {};
struct SampleIdTag final {};
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
using SampleId = StrongId<detail::SampleIdTag>;
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

class RuntimeGeneration final {
public:
    RuntimeGeneration() = delete;

    [[nodiscard]] static Result<RuntimeGeneration, IdentityError> create(
        std::uint64_t value) {
        if (value == 0) {
            return Result<RuntimeGeneration, IdentityError>::failure(
                IdentityError{
                    IdentityError::Code::ZeroGeneration,
                    "Runtime generation must be greater than zero."});
        }

        return Result<RuntimeGeneration, IdentityError>::success(
            RuntimeGeneration(value));
    }

    [[nodiscard]] std::uint64_t value() const noexcept {
        return value_;
    }

    friend bool operator==(
        RuntimeGeneration left, RuntimeGeneration right) noexcept {
        return left.value_ == right.value_;
    }

    friend bool operator!=(
        RuntimeGeneration left, RuntimeGeneration right) noexcept {
        return !(left == right);
    }

    friend bool operator<(
        RuntimeGeneration left, RuntimeGeneration right) noexcept {
        return left.value_ < right.value_;
    }

private:
    explicit RuntimeGeneration(std::uint64_t value) noexcept
        : value_(value) {}

    std::uint64_t value_;
};

} // namespace microscope::core
