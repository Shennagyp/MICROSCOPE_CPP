#include "microscope/core/ids/StrongId.h"


namespace microscope::core {

Status<IdentityError> validateOpaqueIdentifier(std::string_view value) {
    if (value.empty()) {
        return Status<IdentityError>::failure(
            IdentityError{
                IdentityError::Code::EmptyValue,
                "An opaque identifier cannot be empty."});
    }

    if (value.find('\0') != std::string_view::npos) {
        return Status<IdentityError>::failure(
            IdentityError{
                IdentityError::Code::EmbeddedNull,
                "An opaque identifier cannot contain an embedded NUL byte."});
    }

    const auto nonWhitespace = value.find_first_not_of(" \t\n\r\f\v");
    if (nonWhitespace == std::string_view::npos) {
        return Status<IdentityError>::failure(
            IdentityError{
                IdentityError::Code::WhitespaceOnlyValue,
                "An opaque identifier cannot consist only of whitespace."});
    }

    return Status<IdentityError>::success();
}

} // namespace microscope::core
