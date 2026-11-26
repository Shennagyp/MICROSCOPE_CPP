#include "camera/gentl/GenTLProducerLibrary.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <new>

namespace {

struct NativeConsumerContext {
    std::shared_ptr<camera::gentl::GenTLProducerLibrary> producer;
};

void copyError(char* destination, std::uint32_t capacity,
               const char* message) noexcept {
    if (!destination || capacity == 0) return;
    const auto length = std::min<std::size_t>(
        std::strlen(message), static_cast<std::size_t>(capacity - 1));
    std::memcpy(destination, message, length);
    destination[length] = '\0';
}

} // namespace

#if defined(__GNUC__) || defined(__clang__)
#define CAMERA_GENTL_BRIDGE_EXPORT __attribute__((visibility("default")))
#else
#define CAMERA_GENTL_BRIDGE_EXPORT
#endif

extern "C" {

CAMERA_GENTL_BRIDGE_EXPORT void* gentl_consumer_create(
    const char* ctiPath, char* error, std::uint32_t errorCapacity) noexcept {
    try {
        if (!ctiPath || ctiPath[0] == '\0') {
            copyError(error, errorCapacity, "A CTI path is required.");
            return nullptr;
        }

        auto context = std::make_unique<NativeConsumerContext>();
        context->producer =
            camera::gentl::GenTLProducerLibrary::load(ctiPath);
        return context.release();
    } catch (const std::exception& exception) {
        copyError(error, errorCapacity, exception.what());
    } catch (...) {
        copyError(error, errorCapacity, "Unknown error loading the CTI Producer.");
    }
    return nullptr;
}

CAMERA_GENTL_BRIDGE_EXPORT void gentl_consumer_destroy(
    void* opaqueContext) noexcept {
    delete static_cast<NativeConsumerContext*>(opaqueContext);
}

CAMERA_GENTL_BRIDGE_EXPORT std::uint32_t
gentl_consumer_bridge_abi_version() noexcept {
    return 1;
}

CAMERA_GENTL_BRIDGE_EXPORT std::uint32_t
gentl_consumer_header_version() noexcept {
    return GC_GENTL_HEADER_VERSION;
}

CAMERA_GENTL_BRIDGE_EXPORT std::int32_t gentl_consumer_invoke(
    void* opaqueContext, std::uint32_t operation,
    const std::uint64_t* arguments, std::uint32_t argumentCount) noexcept {
    auto* context = static_cast<NativeConsumerContext*>(opaqueContext);
    if (!context || !context->producer) return GC_ERR_INVALID_HANDLE;
    return context->producer->invokeRaw(
        static_cast<camera::gentl::Operation>(operation),
        arguments, argumentCount);
}

} // extern "C"

#undef CAMERA_GENTL_BRIDGE_EXPORT
