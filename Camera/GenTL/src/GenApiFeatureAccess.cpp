#include "camera/gentl/GenApiFeatureAccess.h"

#include "camera/CameraFeatureValidation.h"
#include "GenApiFeatureTree.h"

#include <Base/GCException.h>
#include <Base/GCString.h>
#include <GenApi/IBoolean.h>
#include <GenApi/IEnumeration.h>
#include <GenApi/IEnumEntry.h>
#include <GenApi/IFloat.h>
#include <GenApi/IInteger.h>
#include <GenApi/INode.h>
#include <GenApi/IString.h>

#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace camera::gentl {
namespace {

using Error = camera::CameraFeatureAccessError;
using ErrorCode = Error::Code;
using Descriptor = camera::CameraFeatureDescriptor;
using Value = camera::CameraFeatureValue;

Error makeError(ErrorCode code, const std::string& feature,
                const std::string& message) {
    return Error{code, feature, message, 0, std::nullopt, std::nullopt};
}

std::optional<Descriptor> describeNode(GenApi::INode* node) {
    if (!node || !GenApi::IsImplemented(node)) return std::nullopt;

    Descriptor descriptor;
    descriptor.name = node->GetName().c_str();
    descriptor.displayName = node->GetDisplayName().c_str();
    if (descriptor.displayName.empty()) descriptor.displayName = descriptor.name;

    descriptor.isAvailable = GenApi::IsAvailable(node);
    descriptor.isReadable = GenApi::IsReadable(node);
    descriptor.isWritable = GenApi::IsWritable(node);

    switch (node->GetPrincipalInterfaceType()) {
    case GenApi::intfIBoolean:
        descriptor.kind = camera::CameraFeatureKind::Boolean;
        break;

    case GenApi::intfIInteger: {
        descriptor.kind = camera::CameraFeatureKind::Integer;
        if (auto* integer = dynamic_cast<GenApi::IInteger*>(node)) {
            try {
                camera::CameraIntegerRange range;
                range.minimum = integer->GetMin();
                range.maximum = integer->GetMax();
                try {
                    const auto increment = integer->GetInc();
                    if (increment > 0) range.increment = increment;
                } catch (const GenICam::GenericException&) {
                    // A Producer may expose no fixed increment. Preserve the
                    // exact min/max range and leave increment unspecified.
                }
                descriptor.numericRange = range;
            } catch (const GenICam::GenericException&) {
                // Range metadata is optional; actual writes remain validated
                // by GenApi and the device.
            }
            try {
                const auto unit = integer->GetUnit();
                if (unit.length() != 0) descriptor.unit = unit.c_str();
            } catch (const GenICam::GenericException&) {
            }
        }
        break;
    }

    case GenApi::intfIFloat: {
        descriptor.kind = camera::CameraFeatureKind::FloatingPoint;
        if (auto* floating = dynamic_cast<GenApi::IFloat*>(node)) {
            try {
                camera::CameraFloatingPointRange range;
                range.minimum = floating->GetMin();
                range.maximum = floating->GetMax();
                try {
                    const auto increment = floating->GetInc();
                    if (std::isfinite(increment) && increment > 0.0)
                        range.increment = increment;
                } catch (const GenICam::GenericException&) {
                }
                descriptor.numericRange = range;
            } catch (const GenICam::GenericException&) {
            }
            try {
                const auto unit = floating->GetUnit();
                if (!unit.empty()) descriptor.unit = unit.c_str();
            } catch (const GenICam::GenericException&) {
            }
        }
        break;
    }

    case GenApi::intfIString:
        descriptor.kind = camera::CameraFeatureKind::String;
        break;

    case GenApi::intfIEnumeration: {
        descriptor.kind = camera::CameraFeatureKind::Enumeration;
        if (auto* enumeration = dynamic_cast<GenApi::IEnumeration*>(node)) {
            GenApi::NodeList_t entries;
            enumeration->GetEntries(entries);
            for (auto* entryNode : entries) {
                auto* entry = dynamic_cast<GenApi::IEnumEntry*>(entryNode);
                if (!entry || !GenApi::IsImplemented(entryNode) ||
                    !GenApi::IsAvailable(entryNode)) {
                    continue;
                }
                descriptor.enumerationValues.emplace_back(
                    entry->GetSymbolic().c_str());
            }
        }
        break;
    }

    default:
        return std::nullopt;
    }

    if (!descriptor.isAvailable) {
        descriptor.disabledReason =
            "GenApi node is not available in the current device state.";
    } else if (!descriptor.isReadable && !descriptor.isWritable) {
        descriptor.disabledReason =
            "GenApi node is neither readable nor writable.";
    } else if (!descriptor.isWritable) {
        descriptor.disabledReason = "GenApi node is read-only.";
    } else if (!descriptor.isReadable) {
        descriptor.disabledReason = "GenApi node is write-only.";
    }
    return descriptor;
}

camera::CameraFeatureResult<Value> readNode(
    GenApi::INode* node, const std::string& name) {
    if (!node) {
        return makeError(ErrorCode::FeatureNotFound, name,
                         "GenApi feature was not found.");
    }
    if (!GenApi::IsAvailable(node)) {
        return makeError(ErrorCode::FeatureUnavailable, name,
                         "GenApi feature is not available.");
    }
    if (!GenApi::IsReadable(node)) {
        return makeError(ErrorCode::FeatureNotReadable, name,
                         "GenApi feature is not readable.");
    }

    switch (node->GetPrincipalInterfaceType()) {
    case GenApi::intfIBoolean:
        if (auto* feature = dynamic_cast<GenApi::IBoolean*>(node))
            return Value{feature->GetValue()};
        break;

    case GenApi::intfIInteger:
        if (auto* feature = dynamic_cast<GenApi::IInteger*>(node))
            return Value{static_cast<std::int64_t>(feature->GetValue())};
        break;

    case GenApi::intfIFloat:
        if (auto* feature = dynamic_cast<GenApi::IFloat*>(node))
            return Value{feature->GetValue()};
        break;

    case GenApi::intfIString:
        if (auto* feature = dynamic_cast<GenApi::IString*>(node))
            return Value{std::string(feature->GetValue().c_str())};
        break;

    case GenApi::intfIEnumeration:
        if (auto* feature = dynamic_cast<GenApi::IEnumeration*>(node)) {
            auto* entry = feature->GetCurrentEntry();
            if (!entry) {
                return makeError(ErrorCode::BackendFailure, name,
                                 "GenApi enumeration has no current entry.");
            }
            return Value{camera::FeatureEnumeration{
                entry->GetSymbolic().c_str()}};
        }
        break;

    default:
        return makeError(ErrorCode::Unsupported, name,
                         "GenApi node type is not a supported camera feature.");
    }

    return makeError(ErrorCode::Unsupported, name,
                     "GenApi node does not expose its declared feature interface.");
}

bool writeEnumeration(GenApi::IEnumeration& enumeration,
                       const camera::FeatureEnumeration& requested) {
    GenApi::NodeList_t entries;
    enumeration.GetEntries(entries);
    for (auto* entryNode : entries) {
        auto* entry = dynamic_cast<GenApi::IEnumEntry*>(entryNode);
        if (!entry || !GenApi::IsImplemented(entryNode) ||
            !GenApi::IsAvailable(entryNode)) {
            continue;
        }
        if (requested.symbol == entry->GetSymbolic().c_str()) {
            enumeration.SetIntValue(entry->GetValue(), true);
            return true;
        }
    }
    return false;
}

} // namespace

GenApiFeatureAccess::GenApiFeatureAccess(DeviceSession device)
    : GenApiFeatureAccess(std::make_shared<GenApiNodeMap>(
          std::make_shared<DeviceSession>(std::move(device)))) {}

GenApiFeatureAccess::GenApiFeatureAccess(
    std::shared_ptr<GenApiNodeMap> nodeMap)
    : nodeMap_(std::move(nodeMap)) {
    if (!nodeMap_) throw std::invalid_argument("GenApi feature access requires a node map.");
}

camera::CameraFeatureResult<std::vector<Descriptor>>
GenApiFeatureAccess::featureDescriptors() {
    try {
        return nodeMap_->withNodeMap([](auto& nodeMap)
            -> camera::CameraFeatureResult<std::vector<Descriptor>> {
            const auto nodes = detail::rootFeatureNodes(nodeMap);
            std::vector<Descriptor> descriptors;
            descriptors.reserve(nodes.size());
            for (auto* node : nodes) {
                auto descriptor = describeNode(node);
                if (descriptor) descriptors.push_back(std::move(*descriptor));
            }
            return descriptors;
        });
    } catch (const GenICam::GenericException& error) {
        return makeError(ErrorCode::BackendFailure, {},
                         std::string("GenApi descriptor enumeration failed: ") +
                             error.GetDescription());
    } catch (const std::exception& error) {
        return makeError(ErrorCode::BackendFailure, {},
                         std::string("GenApi descriptor enumeration failed: ") +
                             error.what());
    } catch (...) {
        return makeError(ErrorCode::BackendFailure, {},
                         "GenApi descriptor enumeration failed with an unknown error.");
    }
}

camera::CameraFeatureResult<Value>
GenApiFeatureAccess::readFeature(std::string_view name) {
    const std::string featureName(name);
    try {
        return nodeMap_->withNodeMap([&](auto& nodeMap)
            -> camera::CameraFeatureResult<Value> {
            auto* node = detail::findRootFeatureNode(nodeMap, featureName);
            return readNode(node, featureName);
        });
    } catch (const GenICam::GenericException& error) {
        return makeError(ErrorCode::BackendFailure, featureName,
                         std::string("GenApi feature read failed: ") +
                             error.GetDescription());
    } catch (const std::exception& error) {
        return makeError(ErrorCode::BackendFailure, featureName,
                         std::string("GenApi feature read failed: ") + error.what());
    } catch (...) {
        return makeError(ErrorCode::BackendFailure, featureName,
                         "GenApi feature read failed with an unknown error.");
    }
}

camera::CameraFeatureResult<std::monostate>
GenApiFeatureAccess::writeFeature(
    std::string_view name, const Value& value) {
    const std::string featureName(name);
    try {
        return nodeMap_->withNodeMap([&](auto& nodeMap)
            -> camera::CameraFeatureResult<std::monostate> {
            auto* node = detail::findRootFeatureNode(nodeMap, featureName);
            if (!node) {
                return makeError(ErrorCode::FeatureNotFound, featureName,
                                 "GenApi feature was not found.");
            }

            auto descriptor = describeNode(node);
            if (!descriptor) {
                return makeError(ErrorCode::Unsupported, featureName,
                                 "GenApi node type is not a supported camera feature.");
            }

            auto validation = camera::validateFeatureWrite(*descriptor, value);
            if (const auto* error =
                    std::get_if<camera::CameraFeatureAccessError>(&validation)) {
                return *error;
            }

            switch (node->GetPrincipalInterfaceType()) {
            case GenApi::intfIBoolean: {
                auto* feature = dynamic_cast<GenApi::IBoolean*>(node);
                if (!feature) break;
                feature->SetValue(std::get<bool>(value), true);
                return std::monostate{};
            }

            case GenApi::intfIInteger: {
                auto* feature = dynamic_cast<GenApi::IInteger*>(node);
                if (!feature) break;
                feature->SetValue(std::get<std::int64_t>(value), true);
                return std::monostate{};
            }

            case GenApi::intfIFloat: {
                auto* feature = dynamic_cast<GenApi::IFloat*>(node);
                if (!feature) break;
                feature->SetValue(std::get<double>(value), true);
                return std::monostate{};
            }

            case GenApi::intfIString: {
                auto* feature = dynamic_cast<GenApi::IString*>(node);
                if (!feature) break;
                feature->SetValue(
                    GenICam::gcstring(std::get<std::string>(value).c_str()), true);
                return std::monostate{};
            }

            case GenApi::intfIEnumeration: {
                auto* feature = dynamic_cast<GenApi::IEnumeration*>(node);
                if (!feature) break;
                if (!writeEnumeration(
                        *feature,
                        std::get<camera::FeatureEnumeration>(value))) {
                    return makeError(ErrorCode::InvalidValue, featureName,
                                     "Enumeration symbol is no longer available.");
                }
                return std::monostate{};
            }

            default:
                return makeError(ErrorCode::Unsupported, featureName,
                                 "GenApi node type is not a supported camera feature.");
            }
            return makeError(ErrorCode::BackendFailure, featureName,
                             "GenApi node does not expose its declared feature interface.");
        });
    } catch (const GenICam::GenericException& error) {
        return makeError(ErrorCode::BackendFailure, featureName,
                         std::string("GenApi feature write failed: ") +
                             error.GetDescription());
    } catch (const std::exception& error) {
        return makeError(ErrorCode::BackendFailure, featureName,
                         std::string("GenApi feature write failed: ") + error.what());
    } catch (...) {
        return makeError(ErrorCode::BackendFailure, featureName,
                         "GenApi feature write failed with an unknown error.");
    }
}

} // namespace camera::gentl
