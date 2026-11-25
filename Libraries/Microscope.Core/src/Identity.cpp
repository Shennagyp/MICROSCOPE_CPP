#include "microscope/core/ids/StrongId.h"

namespace microscope::core {

Status<IdentityError> validateOpaqueIdentifier(std::string_view value) {
    if (value.empty()) {
        return Status<IdentityError>::failure(
            IdentityError{
                IdentityError::Code::EmptyValue,
                "An opaque identifier cannot be empty."});
    }

    return Status<IdentityError>::success();
}

} // namespace microscope::core
