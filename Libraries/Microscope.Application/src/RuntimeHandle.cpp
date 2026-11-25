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

bool OperationView::isConsistent() const noexcept {
    return handle.operationId() == snapshot.operationId;
}

} // namespace microscope::application
