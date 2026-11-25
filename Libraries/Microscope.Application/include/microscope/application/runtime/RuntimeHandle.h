#pragma once

#include "microscope/core/ids/Ids.h"
#include "microscope/core/runtime/OperationState.h"

#include <cstdint>

namespace microscope::application {

// Identifies one controlling runtime instance and the physical microscope it
// currently controls. Generation distinguishes runtime incarnations when a
// RuntimeId is reused. A handle is a value locator, not an authorization token;
// Application revalidates it against the active runtime/resource claim.
class RuntimeHandle final {
public:
    RuntimeHandle(
        core::MicroscopeId microscopeId,
        core::RuntimeId runtimeId,
        core::RuntimeGeneration generation);

    [[nodiscard]] const core::MicroscopeId& microscopeId() const noexcept;
    [[nodiscard]] const core::RuntimeId& runtimeId() const noexcept;
    [[nodiscard]] core::RuntimeGeneration generation() const noexcept;

    [[nodiscard]] bool targetsSameMicroscope(
        const RuntimeHandle& other) const noexcept;
    [[nodiscard]] bool identifiesSameRuntime(
        const RuntimeHandle& other) const noexcept;

private:
    core::MicroscopeId microscopeId_;
    core::RuntimeId runtimeId_;
    core::RuntimeGeneration generation_;
};

// An operation handle is scoped to the runtime that accepted the request.
// The operation ID alone is not sufficient to address a live operation.
// Like RuntimeHandle, this is a locator value, not an authorization token.
class OperationHandle final {
public:
    OperationHandle(RuntimeHandle runtime, core::OperationId operationId);

    [[nodiscard]] const RuntimeHandle& runtime() const noexcept;
    [[nodiscard]] const core::OperationId& operationId() const noexcept;
    [[nodiscard]] bool belongsTo(const RuntimeHandle& runtime) const noexcept;

private:
    RuntimeHandle runtime_;
    core::OperationId operationId_;
};

enum class RuntimeLifecycle {
    Initializing,
    Active,
    Closing,
    Closed,
    Faulted
};

// Value projection only. Application implementations publish a new revision;
// previously returned snapshots are not mutated.
struct RuntimeSnapshot final {
    RuntimeHandle handle;
    RuntimeLifecycle lifecycle = RuntimeLifecycle::Initializing;
    std::uint64_t revision = 0;
};

enum class OperationViewError {
    RuntimeIdentityMismatch,
    OperationIdentityMismatch
};

// A view can only be created when the Core snapshot and application handle
// identify the same microscope, runtime incarnation and operation.
class OperationView final {
public:
    [[nodiscard]] static core::Result<OperationView, OperationViewError> create(
        OperationHandle handle,
        core::OperationSnapshot snapshot);

    [[nodiscard]] const OperationHandle& handle() const noexcept;
    [[nodiscard]] const core::OperationSnapshot& snapshot() const noexcept;

private:
    OperationView(
        OperationHandle handle,
        core::OperationSnapshot snapshot);

    OperationHandle handle_;
    core::OperationSnapshot snapshot_;
};

} // namespace microscope::application
