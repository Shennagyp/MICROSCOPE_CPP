#include "camera/gentl/ConsumerBridge.h"

#include <array>
#include <dlfcn.h>
#include <stdexcept>
#include <string>

namespace camera::gentl {
namespace {

template <typename Function>
Function resolve(void* library, const char* symbol) {
    dlerror();
    auto* address = dlsym(library, symbol);
    if (const char* error = dlerror()) {
        throw std::runtime_error(
            std::string("GenTL bridge is missing '") + symbol + "': " + error);
    }
    return reinterpret_cast<Function>(address);
}

constexpr std::uint32_t expectedBridgeAbi = 1;
constexpr std::uint32_t expectedGenTLHeader = 0x01060000;

} // namespace

ConsumerBridge::ConsumerBridge(
    void* libraryHandle,
    CreateFunction create,
    DestroyFunction destroy,
    VersionFunction bridgeVersion,
    VersionFunction headerVersion,
    InvokeFunction invoke) noexcept
    : libraryHandle_(libraryHandle),
      create_(create),
      destroy_(destroy),
      bridgeVersion_(bridgeVersion),
      headerVersion_(headerVersion),
      invoke_(invoke) {}

std::shared_ptr<ConsumerBridge> ConsumerBridge::load(
    const std::filesystem::path& libraryPath) {
    const auto path = libraryPath.string();
    void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        const char* error = dlerror();
        throw std::runtime_error(
            "Could not load GenTL Consumer bridge '" + path + "': " +
            (error ? error : "unknown dynamic-loader error"));
    }

    try {
        const auto create = resolve<CreateFunction>(library, "gentl_consumer_create");
        const auto destroy = resolve<DestroyFunction>(library, "gentl_consumer_destroy");
        const auto bridgeVersion =
            resolve<VersionFunction>(library, "gentl_consumer_bridge_abi_version");
        const auto headerVersion =
            resolve<VersionFunction>(library, "gentl_consumer_header_version");
        const auto invoke = resolve<InvokeFunction>(library, "gentl_consumer_invoke");

        auto result = std::shared_ptr<ConsumerBridge>(new ConsumerBridge(
            library, create, destroy, bridgeVersion, headerVersion, invoke));

        if (result->bridgeAbiVersion() != expectedBridgeAbi) {
            throw std::runtime_error("Unsupported GenTL Consumer bridge ABI version.");
        }
        if (result->genTLHeaderVersion() != expectedGenTLHeader) {
            throw std::runtime_error("GenTL Consumer bridge was not built against GenTL 1.6.");
        }
        return result;
    } catch (...) {
        dlclose(library);
        throw;
    }
}

ConsumerBridge::~ConsumerBridge() {
    if (libraryHandle_) {
        dlclose(libraryHandle_);
    }
}

Consumer ConsumerBridge::createProducer(
    const std::filesystem::path& ctiPath) const {
    const auto path = ctiPath.string();
    std::array<char, 1024> error{};
    void* context = create_(path.c_str(), error.data(),
                            static_cast<std::uint32_t>(error.size()));
    if (!context) {
        const std::string message = error[0] != '\0'
            ? std::string(error.data())
            : std::string("The GenTL Consumer rejected the CTI Producer.");
        throw std::runtime_error(message);
    }
    return Consumer(shared_from_this(), context);
}

std::uint32_t ConsumerBridge::bridgeAbiVersion() const noexcept {
    return bridgeVersion_();
}

std::uint32_t ConsumerBridge::genTLHeaderVersion() const noexcept {
    return headerVersion_();
}

Consumer::Consumer(
    std::shared_ptr<const ConsumerBridge> owner,
    void* context) noexcept
    : owner_(std::move(owner)), context_(context) {}

Consumer::~Consumer() {
    reset();
}

Consumer::Consumer(Consumer&& other) noexcept
    : owner_(std::move(other.owner_)),
      context_(std::exchange(other.context_, nullptr)) {}

Consumer& Consumer::operator=(Consumer&& other) noexcept {
    if (this != &other) {
        reset();
        owner_ = std::move(other.owner_);
        context_ = std::exchange(other.context_, nullptr);
    }
    return *this;
}

void Consumer::reset() noexcept {
    if (context_ && owner_) {
        owner_->destroy_(context_);
    }
    context_ = nullptr;
    owner_.reset();
}

Consumer::operator bool() const noexcept {
    return context_ != nullptr && owner_ != nullptr;
}

std::int32_t Consumer::invokeRaw(
    Operation operation,
    const std::uint64_t* arguments,
    std::uint32_t argumentCount) const noexcept {
    if (!context_ || !owner_) {
        return -1006; // GC_ERR_INVALID_HANDLE
    }
    if (argumentCount != 0 && !arguments) {
        return -1009; // GC_ERR_INVALID_PARAMETER
    }
    return owner_->invoke_(
        context_, static_cast<std::uint32_t>(operation),
        arguments, argumentCount);
}

} // namespace camera::gentl
