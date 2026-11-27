#include "camera/gentl/GenApiRegisterAccess.h"
#include "GenApiFeatureTree.h"
#include <Base/GCException.h>
#include <GenApi/IRegister.h>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace camera::gentl {
namespace {
using Error = camera::CameraRegisterAccessError;
using ErrorCode = Error::Code;
Error makeError(ErrorCode code, const std::string& name, const std::string& message) {
    return Error{code, name, message};
}
std::optional<std::size_t> registerLength(GenApi::IRegister& reg) {
    const auto length = reg.GetLength(true);
    if (length < 0 || static_cast<std::uint64_t>(length) >
            std::numeric_limits<std::size_t>::max()) return std::nullopt;
    return static_cast<std::size_t>(length);
}
} // namespace

GenApiRegisterAccess::GenApiRegisterAccess(
    std::shared_ptr<GenApiNodeMap> nodeMap, std::size_t maximumTransferBytes)
    : nodeMap_(std::move(nodeMap)), maximumTransferBytes_(maximumTransferBytes) {
    if (!nodeMap_) throw std::invalid_argument("GenApi register access requires a node map.");
    if (maximumTransferBytes_ == 0)
        throw std::invalid_argument("GenApi register transfer limit must be nonzero.");
}

camera::CameraRegisterResult<std::vector<camera::CameraRegisterDescriptor>>
GenApiRegisterAccess::registerDescriptors() {
    try {
        return nodeMap_->withNodeMap([](auto& nodeMap)
            -> camera::CameraRegisterResult<std::vector<camera::CameraRegisterDescriptor>> {
            std::vector<camera::CameraRegisterDescriptor> result;
            for (auto* node : detail::rootFeatureNodes(nodeMap)) {
                auto* reg = dynamic_cast<GenApi::IRegister*>(node);
                if (!reg || !GenApi::IsImplemented(node)) continue;
                camera::CameraRegisterDescriptor d;
                d.name = node->GetName().c_str();
                d.displayName = node->GetDisplayName().c_str();
                if (d.displayName.empty()) d.displayName = d.name;
                d.isAvailable = GenApi::IsAvailable(node);
                d.isReadable = GenApi::IsReadable(node);
                d.isWritable = GenApi::IsWritable(node);
                try {
                    const auto length = registerLength(*reg);
                    if (length) d.sizeBytes = *length;
                } catch (const GenICam::GenericException&) {
                    // A dynamic length may depend on selector state.
                }
                result.push_back(std::move(d));
            }
            return result;
        });
    } catch (const GenICam::GenericException& e) {
        return makeError(ErrorCode::BackendFailure, {},
            std::string("GenApi register enumeration failed: ") + e.GetDescription());
    } catch (const std::exception& e) {
        return makeError(ErrorCode::BackendFailure, {},
            std::string("GenApi register enumeration failed: ") + e.what());
    } catch (...) {
        return makeError(ErrorCode::BackendFailure, {},
                         "GenApi register enumeration failed with an unknown error.");
    }
}

camera::CameraRegisterResult<std::vector<std::uint8_t>>
GenApiRegisterAccess::readRegister(std::string_view name) {
    const std::string key(name);
    try {
        return nodeMap_->withNodeMap([&](auto& map)
            -> camera::CameraRegisterResult<std::vector<std::uint8_t>> {
            auto* node = detail::findRootFeatureNode(map, key);
            if (!node) return makeError(ErrorCode::RegisterNotFound, key, "Register not found.");
            auto* reg = dynamic_cast<GenApi::IRegister*>(node);
            if (!reg) return makeError(ErrorCode::Unsupported, key, "Node is not a public IRegister.");
            if (!GenApi::IsAvailable(node))
                return makeError(ErrorCode::RegisterUnavailable, key, "Register is unavailable.");
            if (!GenApi::IsReadable(node))
                return makeError(ErrorCode::RegisterNotReadable, key, "Register is not readable.");
            const auto length = registerLength(*reg);
            if (!length || *length == 0)
                return makeError(ErrorCode::InvalidSize, key, "Register has an invalid byte length.");
            if (*length > maximumTransferBytes_)
                return makeError(ErrorCode::TransferLimitExceeded, key, "Register exceeds transfer limit.");
            if (*length > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
                return makeError(ErrorCode::InvalidSize, key, "Register length exceeds GenApi API range.");
            std::vector<std::uint8_t> bytes(*length);
            reg->Get(bytes.data(), static_cast<std::int64_t>(*length), true, true);
            return bytes;
        });
    } catch (const GenICam::GenericException& e) {
        return makeError(ErrorCode::BackendFailure, key,
            std::string("GenApi register read failed: ") + e.GetDescription());
    } catch (const std::exception& e) {
        return makeError(ErrorCode::BackendFailure, key,
            std::string("GenApi register read failed: ") + e.what());
    } catch (...) {
        return makeError(ErrorCode::BackendFailure, key, "GenApi register read failed.");
    }
}

camera::CameraRegisterResult<std::monostate>
GenApiRegisterAccess::writeRegister(
    std::string_view name, const std::vector<std::uint8_t>& bytes) {
    const std::string key(name);
    try {
        return nodeMap_->withNodeMap([&](auto& map)
            -> camera::CameraRegisterResult<std::monostate> {
            auto* node = detail::findRootFeatureNode(map, key);
            if (!node) return makeError(ErrorCode::RegisterNotFound, key, "Register not found.");
            auto* reg = dynamic_cast<GenApi::IRegister*>(node);
            if (!reg) return makeError(ErrorCode::Unsupported, key, "Node is not a public IRegister.");
            if (!GenApi::IsAvailable(node))
                return makeError(ErrorCode::RegisterUnavailable, key, "Register is unavailable.");
            if (!GenApi::IsWritable(node))
                return makeError(ErrorCode::RegisterNotWritable, key, "Register is not writable.");
            const auto length = registerLength(*reg);
            if (!length || *length == 0)
                return makeError(ErrorCode::InvalidSize, key, "Register has an invalid byte length.");
            if (*length > maximumTransferBytes_)
                return makeError(ErrorCode::TransferLimitExceeded, key, "Register exceeds transfer limit.");
            if (*length > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
                return makeError(ErrorCode::InvalidSize, key, "Register length exceeds GenApi API range.");
            if (bytes.size() != *length)
                return makeError(ErrorCode::InvalidSize, key, "Write length must exactly match register length.");
            reg->Set(bytes.data(), static_cast<std::int64_t>(bytes.size()), true);
            return std::monostate{};
        });
    } catch (const GenICam::GenericException& e) {
        return makeError(ErrorCode::BackendFailure, key,
            std::string("GenApi register write failed: ") + e.GetDescription());
    } catch (const std::exception& e) {
        return makeError(ErrorCode::BackendFailure, key,
            std::string("GenApi register write failed: ") + e.what());
    } catch (...) {
        return makeError(ErrorCode::BackendFailure, key, "GenApi register write failed.");
    }
}
} // namespace camera::gentl
