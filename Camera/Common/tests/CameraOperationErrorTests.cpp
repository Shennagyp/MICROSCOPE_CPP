#include "camera/CameraOperationError.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <variant>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void testOperationErrorPreservesBackendIdentity() {
    using namespace camera;

    CameraOperationError error;
    error.code = CameraErrorCode::TransportFailure;
    error.operation = "camera.open";
    error.message = "The transport rejected the open request";
    error.native = CameraNativeError{"amcam.hresult", -2147467259};

    CameraOperationResult<int> result{error};
    require(std::holds_alternative<CameraOperationError>(result),
            "operation result carries a typed error alternative");

    const auto& actual = std::get<CameraOperationError>(result);
    require(actual.code == CameraErrorCode::TransportFailure,
            "common error category is preserved");
    require(actual.operation == "camera.open",
            "operation context is preserved");
    require(actual.native.has_value(),
            "native backend detail remains optional and explicit");
    require(actual.native->domain == "amcam.hresult" &&
            actual.native->code == -2147467259,
            "native code is interpreted only within its declared domain");
}

void testSuccessfulOperationResult() {
    using namespace camera;

    CameraOperationResult<int> result{42};
    require(std::holds_alternative<int>(result),
            "operation result carries the successful value");
    require(std::get<int>(result) == 42,
            "successful value is preserved");
}

void testNativeDetailsCanBeAbsent() {
    using namespace camera;

    CameraOperationError error;
    error.code = CameraErrorCode::Timeout;
    error.operation = "camera.capture";
    error.message = "The capture deadline elapsed";

    require(!error.native.has_value(),
            "common errors do not require a fabricated native code");
}

} // namespace

int main() {
    testOperationErrorPreservesBackendIdentity();
    testSuccessfulOperationResult();
    testNativeDetailsCanBeAbsent();
    std::cout << "All common camera operation error tests passed.\n";
    return EXIT_SUCCESS;
}
