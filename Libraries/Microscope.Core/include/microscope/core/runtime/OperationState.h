#pragma once

#include "microscope/core/ids/Ids.h"
#include "microscope/core/result/Result.h"

#include <cstdint>
#include <utility>

namespace microscope::core {

enum class OperationStatus {
    Accepted,
    Running,
    AwaitingOperator,
    Verifying,
    Succeeded,
    Failed,
    PartiallyCompleted,
    CancellationRequested,
    Cancelled
};

enum class StepStatus {
    Pending,
    Dispatched,
    AdapterAccepted,
    DeviceAcknowledged,
    Executing,
    AwaitingOperator,
    AwaitingObservation,
    Verifying,
    Verified,
    Failed,
    TimedOut,
    Skipped
};

enum class OperationSnapshotError {
    InvalidStatus,
    ZeroRevision
};

// Immutable operation value. Only Core runtime code creates new revisions;
// consumers receive read-only accessors and cannot mutate a published snapshot.
class OperationSnapshot final {
public:
    [[nodiscard]] static Result<
        OperationSnapshot,
        OperationSnapshotError> create(
            MicroscopeId microscopeId,
            RuntimeId runtimeId,
            RuntimeGeneration runtimeGeneration,
            OperationId operationId,
            OperationStatus status,
            std::uint64_t revision) {
        switch (status) {
        case OperationStatus::Accepted:
        case OperationStatus::Running:
        case OperationStatus::AwaitingOperator:
        case OperationStatus::Verifying:
        case OperationStatus::Succeeded:
        case OperationStatus::Failed:
        case OperationStatus::PartiallyCompleted:
        case OperationStatus::CancellationRequested:
        case OperationStatus::Cancelled:
            break;
        default:
            return Result<
                OperationSnapshot,
                OperationSnapshotError>::failure(
                    OperationSnapshotError::InvalidStatus);
        }

        if (revision == 0) {
            return Result<
                OperationSnapshot,
                OperationSnapshotError>::failure(
                    OperationSnapshotError::ZeroRevision);
        }

        return Result<
            OperationSnapshot,
            OperationSnapshotError>::success(
                OperationSnapshot(
                    std::move(microscopeId),
                    std::move(runtimeId),
                    runtimeGeneration,
                    std::move(operationId),
                    status,
                    revision));
    }

    [[nodiscard]] const MicroscopeId& microscopeId() const noexcept {
        return microscopeId_;
    }

    [[nodiscard]] const RuntimeId& runtimeId() const noexcept {
        return runtimeId_;
    }

    [[nodiscard]] RuntimeGeneration runtimeGeneration() const noexcept {
        return runtimeGeneration_;
    }

    [[nodiscard]] const OperationId& operationId() const noexcept {
        return operationId_;
    }

    [[nodiscard]] OperationStatus status() const noexcept {
        return status_;
    }

    [[nodiscard]] std::uint64_t revision() const noexcept {
        return revision_;
    }

private:
    OperationSnapshot(
        MicroscopeId microscopeId,
        RuntimeId runtimeId,
        RuntimeGeneration runtimeGeneration,
        OperationId operationId,
        OperationStatus status,
        std::uint64_t revision)
        : microscopeId_(std::move(microscopeId)),
          runtimeId_(std::move(runtimeId)),
          runtimeGeneration_(runtimeGeneration),
          operationId_(std::move(operationId)),
          status_(status),
          revision_(revision) {}

    MicroscopeId microscopeId_;
    RuntimeId runtimeId_;
    RuntimeGeneration runtimeGeneration_;
    OperationId operationId_;
    OperationStatus status_;
    std::uint64_t revision_;
};

} // namespace microscope::core
