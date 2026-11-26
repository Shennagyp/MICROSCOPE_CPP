#pragma once

#include "camera/gentl/ConsumerBridge.h"
#include "camera/gentl/GenTL_v1_6.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <type_traits>
#include <utility>

namespace camera::gentl {

// Loads a vendor CTI Producer and dispatches directly to its standard GenTL
// C exports. This is the native C++ Consumer boundary; no intermediary C ABI
// bridge or application-specific runtime is required.
class GenTLProducerLibrary final {
public:
    static std::shared_ptr<GenTLProducerLibrary> load(
        const std::filesystem::path& ctiPath);

    ~GenTLProducerLibrary();

    GenTLProducerLibrary(const GenTLProducerLibrary&) = delete;
    GenTLProducerLibrary& operator=(const GenTLProducerLibrary&) = delete;

    const std::filesystem::path& path() const noexcept;

    std::int32_t invokeRaw(
        Operation operation,
        const std::uint64_t* arguments,
        std::uint32_t argumentCount) const noexcept;

    template <typename... Args>
    std::int32_t invoke(Operation operation, Args... arguments) const noexcept {
        const std::array<std::uint64_t, sizeof...(Args)> encoded{{encode(arguments)...}};
        return invokeRaw(operation, encoded.data(),
                         static_cast<std::uint32_t>(sizeof...(Args)));
    }

private:
    struct Functions;

    GenTLProducerLibrary(
        void* libraryHandle,
        std::filesystem::path path,
        Functions functions) noexcept;

    template <typename T>
    static std::uint64_t encode(T value) noexcept {
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return 0;
        } else if constexpr (std::is_pointer_v<T>) {
            return static_cast<std::uint64_t>(
                reinterpret_cast<std::uintptr_t>(value));
        } else if constexpr (std::is_enum_v<T>) {
            return static_cast<std::uint64_t>(
                static_cast<std::underlying_type_t<T>>(value));
        } else {
            return static_cast<std::uint64_t>(value);
        }
    }

    void* libraryHandle_ = nullptr;
    std::filesystem::path path_;
    std::unique_ptr<Functions> functions_;
};

} // namespace camera::gentl
