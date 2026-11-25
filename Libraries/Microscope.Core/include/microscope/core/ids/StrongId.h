#pragma once

#include "microscope/core/result/Result.h"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace microscope::core {

struct IdentityError final {
    enum class Code {
        EmptyValue,
        WhitespaceOnlyValue,
        EmbeddedNull,
        ZeroGeneration
    };

    Code code;
    std::string message;
};

// Implemented in the Core library so every strong ID uses one validation rule.
[[nodiscard]] Status<IdentityError> validateOpaqueIdentifier(
    std::string_view value);

template <typename Tag>
class StrongId final {
public:
    StrongId() = delete;

    [[nodiscard]] static Result<StrongId, IdentityError> create(
        std::string value) {
        auto validation = validateOpaqueIdentifier(value);
        if (!validation) {
            return Result<StrongId, IdentityError>::failure(
                std::move(validation).error());
        }

        return Result<StrongId, IdentityError>::success(
            StrongId(std::move(value)));
    }

    [[nodiscard]] const std::string& value() const noexcept {
        return value_;
    }

    friend bool operator==(const StrongId& left, const StrongId& right) noexcept {
        return left.value_ == right.value_;
    }

    friend bool operator!=(const StrongId& left, const StrongId& right) noexcept {
        return !(left == right);
    }

    friend bool operator<(const StrongId& left, const StrongId& right) noexcept {
        return left.value_ < right.value_;
    }

private:
    explicit StrongId(std::string value)
        : value_(std::move(value)) {}

    std::string value_;
};

} // namespace microscope::core

namespace std {

template <typename Tag>
struct hash<microscope::core::StrongId<Tag>> {
    [[nodiscard]] std::size_t operator()(
        const microscope::core::StrongId<Tag>& id) const noexcept {
        return std::hash<std::string>{}(id.value());
    }
};

} // namespace std
