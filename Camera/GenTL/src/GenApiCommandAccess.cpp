#include "camera/gentl/GenApiCommandAccess.h"

#include <Base/GCException.h>
#include <GenApi/ICommand.h>
#include <GenApi/INode.h>

#include <exception>
#include <string>
#include <utility>

namespace camera::gentl {
namespace {

using Error = camera::CameraCommandAccessError;
using ErrorCode = Error::Code;
using Descriptor = camera::CameraCommandDescriptor;

Error makeError(ErrorCode code, const std::string& command,
                const std::string& message) {
    return Error{code, command, message, 0};
}

std::optional<Descriptor> describeCommand(GenApi::INode* node) {
    if (!node || !GenApi::IsImplemented(node) ||
        node->GetPrincipalInterfaceType() != GenApi::intfICommand) {
        return std::nullopt;
    }

    Descriptor descriptor;
    descriptor.name = node->GetName().c_str();
    descriptor.displayName = node->GetDisplayName().c_str();
    if (descriptor.displayName.empty()) descriptor.displayName = descriptor.name;
    descriptor.isAvailable = GenApi::IsAvailable(node);
    descriptor.isExecutable = descriptor.isAvailable && GenApi::IsWritable(node);

    if (!descriptor.isAvailable) {
        descriptor.disabledReason = "GenApi command is not available in the current device state.";
    } else if (!descriptor.isExecutable) {
        descriptor.disabledReason = "GenApi command is not executable in the current device state.";
    }
    return descriptor;
}

} // namespace

GenApiCommandAccess::GenApiCommandAccess(DeviceSession device)
    : GenApiCommandAccess(std::make_shared<GenApiNodeMap>(
          std::make_shared<DeviceSession>(std::move(device)))) {}

GenApiCommandAccess::GenApiCommandAccess(
    std::shared_ptr<GenApiNodeMap> nodeMap)
    : nodeMap_(std::move(nodeMap)) {
    if (!nodeMap_) throw std::invalid_argument("GenApi command access requires a node map.");
}

camera::CameraCommandResult<std::vector<Descriptor>>
GenApiCommandAccess::commandDescriptors() {
    try {
        return nodeMap_->withNodeMap([](auto& nodeMap)
            -> camera::CameraCommandResult<std::vector<Descriptor>> {
            GenApi::NodeList_t nodes;
            nodeMap._GetNodes(nodes);
            std::vector<Descriptor> descriptors;
            descriptors.reserve(nodes.size());
            for (auto* node : nodes) {
                auto descriptor = describeCommand(node);
                if (descriptor) descriptors.push_back(std::move(*descriptor));
            }
            return descriptors;
        });
    } catch (const GenICam::GenericException& error) {
        return makeError(ErrorCode::BackendFailure, {},
            std::string("GenApi command enumeration failed: ") + error.GetDescription());
    } catch (const std::exception& error) {
        return makeError(ErrorCode::BackendFailure, {},
            std::string("GenApi command enumeration failed: ") + error.what());
    } catch (...) {
        return makeError(ErrorCode::BackendFailure, {},
            "GenApi command enumeration failed with an unknown error.");
    }
}

camera::CameraCommandResult<std::monostate>
GenApiCommandAccess::executeCommand(std::string_view name) {
    const std::string commandName(name);
    try {
        return nodeMap_->withNodeMap([&](auto& nodeMap)
            -> camera::CameraCommandResult<std::monostate> {
            auto* node = nodeMap._GetNode(GenICam::gcstring(commandName.c_str()));
            if (!node || node->GetPrincipalInterfaceType() != GenApi::intfICommand)
                return makeError(ErrorCode::CommandNotFound, commandName,
                                 "GenApi command was not found.");
            if (!GenApi::IsAvailable(node))
                return makeError(ErrorCode::CommandUnavailable, commandName,
                                 "GenApi command is not available.");
            if (!GenApi::IsWritable(node))
                return makeError(ErrorCode::CommandNotExecutable, commandName,
                                 "GenApi command is not executable.");

            auto* command = dynamic_cast<GenApi::ICommand*>(node);
            if (!command)
                return makeError(ErrorCode::Unsupported, commandName,
                                 "GenApi node does not expose ICommand.");
            command->Execute(true);
            return std::monostate{};
        });
    } catch (const GenICam::GenericException& error) {
        return makeError(ErrorCode::BackendFailure, commandName,
            std::string("GenApi command execution failed: ") + error.GetDescription());
    } catch (const std::exception& error) {
        return makeError(ErrorCode::BackendFailure, commandName,
            std::string("GenApi command execution failed: ") + error.what());
    } catch (...) {
        return makeError(ErrorCode::BackendFailure, commandName,
                         "GenApi command execution failed with an unknown error.");
    }
}

camera::CameraCommandResult<bool>
GenApiCommandAccess::isCommandDone(std::string_view name) {
    const std::string commandName(name);
    try {
        return nodeMap_->withNodeMap([&](auto& nodeMap)
            -> camera::CameraCommandResult<bool> {
            auto* node = nodeMap._GetNode(GenICam::gcstring(commandName.c_str()));
            if (!node || node->GetPrincipalInterfaceType() != GenApi::intfICommand)
                return makeError(ErrorCode::CommandNotFound, commandName,
                                 "GenApi command was not found.");
            if (!GenApi::IsAvailable(node))
                return makeError(ErrorCode::CommandUnavailable, commandName,
                                 "GenApi command is not available.");
            auto* command = dynamic_cast<GenApi::ICommand*>(node);
            if (!command)
                return makeError(ErrorCode::Unsupported, commandName,
                                 "GenApi node does not expose ICommand.");
            return command->IsDone();
        });
    } catch (const GenICam::GenericException& error) {
        return makeError(ErrorCode::BackendFailure, commandName,
            std::string("GenApi command status query failed: ") + error.GetDescription());
    } catch (const std::exception& error) {
        return makeError(ErrorCode::BackendFailure, commandName,
            std::string("GenApi command status query failed: ") + error.what());
    } catch (...) {
        return makeError(ErrorCode::BackendFailure, commandName,
                         "GenApi command status query failed with an unknown error.");
    }
}

} // namespace camera::gentl
