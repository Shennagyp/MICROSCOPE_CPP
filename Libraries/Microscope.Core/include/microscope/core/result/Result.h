#pragma once

#include <utility>
#include <variant>

namespace microscope::core {

// A value-or-error result for normal contract outcomes.
// T and E are selected by the boundary; neither is converted to a string.
template <typename T, typename E>
class Result final {
public:
    [[nodiscard]] static Result success(T value) {
        return Result(std::in_place_index<0>, std::move(value));
    }

    [[nodiscard]] static Result failure(E error) {
        return Result(std::in_place_index<1>, std::move(error));
    }

    [[nodiscard]] bool hasValue() const noexcept {
        return storage_.index() == 0;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return hasValue();
    }

    [[nodiscard]] T& value() & {
        return std::get<0>(storage_);
    }

    [[nodiscard]] const T& value() const & {
        return std::get<0>(storage_);
    }

    [[nodiscard]] T&& value() && {
        return std::get<0>(std::move(storage_));
    }

    [[nodiscard]] E& error() & {
        return std::get<1>(storage_);
    }

    [[nodiscard]] const E& error() const & {
        return std::get<1>(storage_);
    }

    [[nodiscard]] E&& error() && {
        return std::get<1>(std::move(storage_));
    }

private:
    template <std::size_t Index, typename Value>
    explicit Result(std::in_place_index_t<Index> index, Value&& value)
        : storage_(index, std::forward<Value>(value)) {}

    std::variant<T, E> storage_;
};

// Status is the no-value counterpart to Result<T, E>.
template <typename E>
class Status final {
public:
    [[nodiscard]] static Status success() {
        return Status(std::in_place_index<0>);
    }

    [[nodiscard]] static Status failure(E error) {
        return Status(std::in_place_index<1>, std::move(error));
    }

    [[nodiscard]] bool isSuccess() const noexcept {
        return storage_.index() == 0;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return isSuccess();
    }

    [[nodiscard]] E& error() & {
        return std::get<1>(storage_);
    }

    [[nodiscard]] const E& error() const & {
        return std::get<1>(storage_);
    }

private:
    explicit Status(std::in_place_index_t<0>)
        : storage_(std::in_place_index<0>) {}

    explicit Status(std::in_place_index_t<1>, E error)
        : storage_(std::in_place_index<1>, std::move(error)) {}

    std::variant<std::monostate, E> storage_;
};

} // namespace microscope::core
