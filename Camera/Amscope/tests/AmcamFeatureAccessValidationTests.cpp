#include "AmcamFeatureAccessValidation.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

} // namespace

int main() {
    using camera::CameraFeatureAccessError;
    using amscope::detail::pixelFormatEnumerationError;

    const auto unsupported =
        pixelFormatEnumerationError("RawOutputBitDepth", E_NOTIMPL, 0);
    require(unsupported.has_value() &&
                unsupported->code == CameraFeatureAccessError::Code::FeatureUnavailable &&
                unsupported->nativeCode == static_cast<std::int32_t>(E_NOTIMPL),
            "E_NOTIMPL must remain a model-level unsupported result.");

    const auto backendFailure =
        pixelFormatEnumerationError("RawOutputBitDepth", E_FAIL, 0);
    require(backendFailure.has_value() &&
                backendFailure->code == CameraFeatureAccessError::Code::BackendFailure &&
                backendFailure->nativeCode == static_cast<std::int32_t>(E_FAIL),
            "A non-E_NOTIMPL SDK failure must preserve its native code.");

    const auto empty =
        pixelFormatEnumerationError("RawOutputBitDepth", S_OK, 0);
    require(empty.has_value() &&
                empty->code == CameraFeatureAccessError::Code::FeatureUnavailable,
            "A successful empty enumeration must be unavailable, not a backend failure.");

    const auto negativeCount =
        pixelFormatEnumerationError("RawOutputBitDepth", S_OK, -1);
    require(negativeCount.has_value() &&
                negativeCount->code == CameraFeatureAccessError::Code::BackendFailure,
            "A negative SDK format count must be treated as invalid provider data.");

    const auto excessiveCount =
        pixelFormatEnumerationError("RawOutputBitDepth", S_OK, 65);
    require(excessiveCount.has_value() &&
                excessiveCount->code == CameraFeatureAccessError::Code::BackendFailure,
            "A count beyond the implementation bound must be treated as invalid provider data.");

    const auto valid =
        pixelFormatEnumerationError("RawOutputBitDepth", S_OK, 4);
    require(!valid.has_value(),
            "A successful in-range enumeration count must proceed to entry retrieval.");

    std::cout << "Amcam feature-access validation tests passed.\n";
    return EXIT_SUCCESS;
}
