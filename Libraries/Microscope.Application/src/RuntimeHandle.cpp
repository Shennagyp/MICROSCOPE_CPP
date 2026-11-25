#include "microscope/application/runtime/RuntimeHandle.h"

#include <utility>

namespace microscope::application {

RuntimeHandle::RuntimeHandle(
    core::MicroscopeId microscopeId,
    core::RuntimeId runtimeId,
    core::RuntimeGeneration generation)
    : microscopeId_(std::move(microscopeId)),
      runtimeId_(std::move(runtimeId)),
      generation_(generation) {}

const core::MicroscopeId& RuntimeHandle::microscopeId() const noexcept {
    return microscopeId_;
}

const core::RuntimeId& RuntimeHandle::runtimeId() const noexcept {
    return runtimeId_;
}

core::RuntimeGeneration RuntimeHandle::generation() const noexcept {
    return generation_;
}

bool RuntimeHandle::targetsSameMicroscope(
    const RuntimeHandle& other) const noexcept {
    return microscopeId_ == other.microscopeId_;
}

bool RuntimeHandle::identifiesSameRuntime(
    const RuntimeHandle& other) const noexcept {
    return microscopeId_ == other.microscopeId_ &&
           runtimeId_ == other.runtimeId_ &&
           generation_ == other.generation_;
}

OperationHandle::OperationHandle(
    RuntimeHandle runtime,
    core::OperationId operationId)
    : runtime_(std::move(runtime)),
      operationId_(std::move(operationId)) {}

const RuntimeHandle& OperationHandle::runtime() const noexcept {
    return runtime_;
}

const core::OperationId& OperationHandle::operationId() const noexcept {
    return operationId_;
}

bool OperationHandle::belongsTo(const RuntimeHandle& runtime) const noexcept {
    return runtime_.identifiesSameRuntime(runtime);
}

RuntimeSnapshot::RuntimeSnapshot(
    RuntimeHandle handle,
    RuntimeLifecycle lifecycle,
    std::uint64_t revision)
    : handle_(std::move(handle)),
      lifecycle_(lifecycle),
      revision_(revision) {}

core::Result<RuntimeSnapshot, RuntimeSnapshotError> RuntimeSnapshot::create(
    RuntimeHandle handle,
    RuntimeLifecycle lifecycle,
    std::uint64_t revision) {
    switch (lifecycle) {
    case RuntimeLifecycle::Initializing:
    case RuntimeLifecycle::Active:
    case RuntimeLifecycle::Closing:
    case RuntimeLifecycle::Closed:
    case RuntimeLifecycle::Faulted:
        break;
    default:
        return core::Result<RuntimeSnapshot, RuntimeSnapshotError>::failure(
            RuntimeSnapshotError::InvalidLifecycle);
    }

    if (revision == 0) {
        return core::Result<RuntimeSnapshot, RuntimeSnapshotError>::failure(
            RuntimeSnapshotError::ZeroRevision);
    }

    return core::Result<RuntimeSnapshot, RuntimeSnapshotError>::success(
        RuntimeSnapshot(std::move(handle), lifecycle, revision));
}

const RuntimeHandle& RuntimeSnapshot::handle() const noexcept {
    return handle_;
}

RuntimeLifecycle RuntimeSnapshot::lifecycle() const noexcept {
    return lifecycle_;
}

std::uint64_t RuntimeSnapshot::revision() const noexcept {
    return revision_;
}

OperationView::OperationView(
    OperationHandle handle,
    core::OperationSnapshot snapshot)
    : handle_(std::move(handle)),
      snapshot_(std::move(snapshot)) {}

core::Result<OperationView, OperationViewError> OperationView::create(
    OperationHandle handle,
    core::OperationSnapshot snapshot) {
    const auto& runtime = handle.runtime();
    if (runtime.microscopeId() != snapshot.microscopeId() ||
        runtime.runtimeId() != snapshot.runtimeId() ||
        runtime.generation() != snapshot.runtimeGeneration()) {
        return core::Result<OperationView, OperationViewError>::failure(
            OperationViewError::RuntimeIdentityMismatch);
    }

    if (handle.operationId() != snapshot.operationId()) {
        return core::Result<OperationView, OperationViewError>::failure(
            OperationViewError::OperationIdentityMismatch);
    }

    return core::Result<OperationView, OperationViewError>::success(
        OperationView(std::move(handle), std::move(snapshot)));
}

const OperationHandle& OperationView::handle() const noexcept {
    return handle_;
}

const core::OperationSnapshot& OperationView::snapshot() const noexcept {
    return snapshot_;
}

} // namespace microscope::application
