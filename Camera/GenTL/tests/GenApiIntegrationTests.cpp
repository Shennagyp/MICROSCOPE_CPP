#include "camera/gentl/GenApiDeviceAccess.h"
#include "camera/gentl/Producer.h"

#include <Base/GCException.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

const camera::CameraFeatureDescriptor* findFeature(
    const std::vector<camera::CameraFeatureDescriptor>& features,
    const std::string& name) {
    const auto found = std::find_if(features.begin(), features.end(),
        [&](const auto& item) { return item.name == name; });
    return found == features.end() ? nullptr : &*found;
}

const camera::CameraCommandDescriptor* findCommand(
    const std::vector<camera::CameraCommandDescriptor>& commands,
    const std::string& name) {
    const auto found = std::find_if(commands.begin(), commands.end(),
        [&](const auto& item) { return item.name == name; });
    return found == commands.end() ? nullptr : &*found;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;

    const auto directory =
        std::filesystem::temp_directory_path() / "camera-gentl-genapi-tests";
    std::filesystem::create_directories(directory);
    const auto ctiPath = directory / "mock.cti";
    {
        std::ofstream file(ctiPath);
        file << "test fixture";
    }

    try {
        auto bridge = camera::gentl::ConsumerBridge::load(argv[1]);
        auto producer = camera::gentl::Producer::open(bridge, ctiPath);
        const auto devices = producer.discover();
        require(devices.size() == 1, "GenApi fixture device was not discovered.");

        {
            auto device = producer.openDevice(devices.front());
            camera::gentl::GenApiDeviceAccess access(std::move(device));

            auto featureResult = access.features().featureDescriptors();
            auto* features =
                std::get_if<std::vector<camera::CameraFeatureDescriptor>>(&featureResult);
            require(features != nullptr, "GenApi feature enumeration failed.");

            const auto* gain = findFeature(*features, "Gain");
            require(gain != nullptr, "Root feature Gain was not exposed.");
            require(gain->kind == camera::CameraFeatureKind::Integer,
                    "Gain was not mapped as an Integer feature.");
            require(gain->unit == std::optional<std::string>{"dB"},
                    "Gain unit was not preserved.");
            require(!gain->isAvailable,
                    "Gain should be unavailable until FeatureEnabled is set.");

            const auto* exposureTime = findFeature(*features, "ExposureTime");
            require(exposureTime &&
                        exposureTime->kind == camera::CameraFeatureKind::FloatingPoint &&
                        exposureTime->unit == std::optional<std::string>{"us"},
                    "Feature nested under AcquisitionControl was not exposed.");
            const auto* triggerDelay = findFeature(*features, "TriggerDelay");
            require(triggerDelay &&
                        triggerDelay->kind == camera::CameraFeatureKind::Integer,
                    "Feature nested two category levels below Root was not exposed.");
            auto nestedDelayRead = access.features().readFeature("TriggerDelay");
            auto* nestedDelayValue =
                std::get_if<camera::CameraFeatureValue>(&nestedDelayRead);
            require(nestedDelayValue &&
                        std::get_if<std::int64_t>(nestedDelayValue) &&
                        *std::get_if<std::int64_t>(nestedDelayValue) == 7,
                    "Nested GenApi feature direct lookup failed.");

            const auto* temperature = findFeature(*features, "Temperature");
            require(temperature &&
                        temperature->kind == camera::CameraFeatureKind::FloatingPoint &&
                        temperature->unit == std::optional<std::string>{"degC"},
                    "Temperature was not mapped as a floating-point feature.");
            const auto* deviceLabel = findFeature(*features, "DeviceLabel");
            require(deviceLabel &&
                        deviceLabel->kind == camera::CameraFeatureKind::String,
                    "DeviceLabel was not mapped as a string feature.");

            const auto* triggerMode = findFeature(*features, "TriggerMode");
            require(triggerMode != nullptr &&
                        triggerMode->kind == camera::CameraFeatureKind::Enumeration,
                    "TriggerMode was not mapped as an Enumeration feature.");
            require(triggerMode->enumerationValues ==
                        std::vector<std::string>{"Off", "On"},
                    "Available GenApi enumeration symbols were not preserved.");

            require(std::none_of(features->begin(), features->end(),
                [](const auto& item) {
                    return item.name == "GainReg" ||
                           item.name == "EnableReg" ||
                           item.name == "TriggerModeReg" ||
                           item.name == "CommandReg";
                }), "Internal register nodes leaked into the public feature list.");

            auto hiddenRegisterRead = access.features().readFeature("GainReg");
            auto* hiddenReadError =
                std::get_if<camera::CameraFeatureAccessError>(&hiddenRegisterRead);
            require(hiddenReadError &&
                        hiddenReadError->code ==
                            camera::CameraFeatureAccessError::Code::FeatureNotFound,
                    "Internal GenApi register was directly readable as a feature.");

            const camera::CameraFeatureValue hiddenRegisterWriteValue{
                std::int64_t{7}};
            auto hiddenRegisterWrite =
                access.features().writeFeature("GainReg", hiddenRegisterWriteValue);
            auto* hiddenWriteError =
                std::get_if<camera::CameraFeatureAccessError>(&hiddenRegisterWrite);
            require(hiddenWriteError &&
                        hiddenWriteError->code ==
                            camera::CameraFeatureAccessError::Code::FeatureNotFound,
                    "Internal GenApi register was directly writable as a feature.");

            auto unavailableGain = access.features().readFeature("Gain");
            auto* unavailableError =
                std::get_if<camera::CameraFeatureAccessError>(&unavailableGain);
            require(unavailableError &&
                        unavailableError->code ==
                            camera::CameraFeatureAccessError::Code::FeatureUnavailable,
                    "GenApi pIsAvailable state was not enforced.");

            auto enabledRead = access.features().readFeature("FeatureEnabled");
            auto* enabledValue = std::get_if<camera::CameraFeatureValue>(&enabledRead);
            require(enabledValue != nullptr, "FeatureEnabled read failed.");
            auto* initiallyEnabled = std::get_if<bool>(enabledValue);
            require(initiallyEnabled && !*initiallyEnabled,
                    "FeatureEnabled initial value was not false.");

            const camera::CameraFeatureValue enableFeature{true};
            auto enableResult =
                access.features().writeFeature("FeatureEnabled", enableFeature);
            require(std::holds_alternative<std::monostate>(enableResult),
                    "FeatureEnabled write failed.");

            featureResult = access.features().featureDescriptors();
            features =
                std::get_if<std::vector<camera::CameraFeatureDescriptor>>(&featureResult);
            require(features != nullptr, "GenApi feature refresh failed.");
            gain = findFeature(*features, "Gain");
            require(gain && gain->isAvailable,
                    "Gain did not become available after its selector condition changed.");

            auto initialRead = access.features().readFeature("Gain");
            auto* initialValue = std::get_if<camera::CameraFeatureValue>(&initialRead);
            require(initialValue != nullptr, "GenApi Gain read failed.");
            auto* initialGain = std::get_if<std::int64_t>(initialValue);
            require(initialGain && *initialGain == 0,
                    "GenApi Gain initial register value was not zero.");

            const camera::CameraFeatureValue requestedGain{std::int64_t{42}};
            auto writeResult = access.features().writeFeature("Gain", requestedGain);
            require(std::holds_alternative<std::monostate>(writeResult),
                    "GenApi Gain write failed.");

            auto readBack = access.features().readFeature("Gain");
            auto* readBackValue = std::get_if<camera::CameraFeatureValue>(&readBack);
            require(readBackValue != nullptr, "GenApi Gain readback failed.");
            auto* readBackGain = std::get_if<std::int64_t>(readBackValue);
            require(readBackGain && *readBackGain == 42,
                    "GenApi Gain write/readback did not preserve the requested value.");

            const camera::CameraFeatureValue requestedExposure{5000.5};
            auto exposureWrite =
                access.features().writeFeature("ExposureTime", requestedExposure);
            require(std::holds_alternative<std::monostate>(exposureWrite),
                    "ExposureTime floating-point write failed.");
            auto exposureReadBack =
                access.features().readFeature("ExposureTime");
            auto* exposureReadBackValue =
                std::get_if<camera::CameraFeatureValue>(&exposureReadBack);
            require(exposureReadBackValue &&
                        std::get_if<double>(exposureReadBackValue) &&
                        std::abs(*std::get_if<double>(exposureReadBackValue) - 5000.5) < 1e-9,
                    "ExposureTime floating-point write/readback failed.");

            const camera::CameraFeatureValue invalidExposure{5000.25};
            auto invalidExposureWrite =
                access.features().writeFeature("ExposureTime", invalidExposure);
            auto* invalidExposureError =
                std::get_if<camera::CameraFeatureAccessError>(&invalidExposureWrite);
            require(invalidExposureError &&
                        invalidExposureError->code ==
                            camera::CameraFeatureAccessError::Code::InvalidValue,
                    "ExposureTime increment validation did not reject an invalid step.");

            auto temperatureRead =
                access.features().readFeature("Temperature");
            auto* temperatureValue =
                std::get_if<camera::CameraFeatureValue>(&temperatureRead);
            require(temperatureValue != nullptr, "Temperature read failed.");
            auto* temperatureNumber = std::get_if<double>(temperatureValue);
            require(temperatureNumber &&
                        std::abs(*temperatureNumber - 36.5) < 1e-9,
                    "Temperature value was not preserved.");

            auto labelRead = access.features().readFeature("DeviceLabel");
            auto* labelValue = std::get_if<camera::CameraFeatureValue>(&labelRead);
            require(labelValue != nullptr, "DeviceLabel read failed.");
            auto* label = std::get_if<std::string>(labelValue);
            require(label && *label == "Mock GenTL",
                    "DeviceLabel string value was not preserved.");

            const camera::CameraFeatureValue requestedLabel{
                std::string{"Updated GenTL"}};
            auto labelWrite =
                access.features().writeFeature("DeviceLabel", requestedLabel);
            require(std::holds_alternative<std::monostate>(labelWrite),
                    "DeviceLabel string write failed.");
            auto labelReadBack = access.features().readFeature("DeviceLabel");
            auto* labelReadBackValue =
                std::get_if<camera::CameraFeatureValue>(&labelReadBack);
            require(labelReadBackValue &&
                        std::get_if<std::string>(labelReadBackValue) &&
                        *std::get_if<std::string>(labelReadBackValue) == "Updated GenTL",
                    "DeviceLabel string write/readback failed.");

            auto initialEnum = access.features().readFeature("TriggerMode");
            auto* initialEnumValue =
                std::get_if<camera::CameraFeatureValue>(&initialEnum);
            require(initialEnumValue != nullptr, "TriggerMode initial read failed.");
            auto* initialSymbol =
                std::get_if<camera::FeatureEnumeration>(initialEnumValue);
            require(initialSymbol && initialSymbol->symbol == "Off",
                    "TriggerMode initial enum symbol was not Off.");

            const camera::CameraFeatureValue requestedMode{
                camera::FeatureEnumeration{"On"}};
            auto enumWrite =
                access.features().writeFeature("TriggerMode", requestedMode);
            require(std::holds_alternative<std::monostate>(enumWrite),
                    "TriggerMode enum write failed.");
            auto enumReadBack = access.features().readFeature("TriggerMode");
            auto* enumReadBackValue =
                std::get_if<camera::CameraFeatureValue>(&enumReadBack);
            require(enumReadBackValue != nullptr, "TriggerMode readback failed.");
            auto* enumReadBackSymbol =
                std::get_if<camera::FeatureEnumeration>(enumReadBackValue);
            require(enumReadBackSymbol && enumReadBackSymbol->symbol == "On",
                    "TriggerMode enum write/readback did not preserve the symbol.");

            auto commandResult = access.commands().commandDescriptors();
            auto* commands =
                std::get_if<std::vector<camera::CameraCommandDescriptor>>(&commandResult);
            require(commands != nullptr, "GenApi command enumeration failed.");
            require(findCommand(*commands, "AcquisitionStop") != nullptr,
                    "Command nested under AcquisitionControl was not exposed.");
            require(findCommand(*commands, "AcquisitionStart") != nullptr,
                    "Root command AcquisitionStart was not exposed.");
            require(findCommand(*commands, "CommandReg") == nullptr,
                    "Internal command register leaked into the command list.");

            auto hiddenCommand = access.commands().executeCommand("CommandReg");
            auto* hiddenCommandError =
                std::get_if<camera::CameraCommandAccessError>(&hiddenCommand);
            require(hiddenCommandError &&
                        hiddenCommandError->code ==
                            camera::CameraCommandAccessError::Code::CommandNotFound,
                    "Internal GenApi register was directly executable as a command.");

            auto executeResult =
                access.commands().executeCommand("AcquisitionStart");
            require(std::holds_alternative<std::monostate>(executeResult),
                    "GenApi command issue failed.");
            auto nestedExecute =
                access.commands().executeCommand("AcquisitionStop");
            require(std::holds_alternative<std::monostate>(nestedExecute),
                    "Nested GenApi command lookup/execution failed.");

            auto doneResult =
                access.commands().isCommandDone("AcquisitionStart");
            auto* isDone = std::get_if<bool>(&doneResult);
            require(isDone && !*isDone,
                    "GenApi command completion polling did not preserve the active state.");

            auto completedResult =
                access.commands().isCommandDone("AcquisitionStart");
            auto* isCompleted = std::get_if<bool>(&completedResult);
            require(isCompleted && *isCompleted,
                    "GenApi command completion did not transition to done.");
        }

        {
            auto readOnlyDevice = producer.openDevice(
                devices.front(), GenTL::DEVICE_ACCESS_READONLY);
            camera::gentl::GenApiDeviceAccess readOnly(std::move(readOnlyDevice));

            auto featureResult = readOnly.features().featureDescriptors();
            auto* features =
                std::get_if<std::vector<camera::CameraFeatureDescriptor>>(&featureResult);
            require(features != nullptr, "Read-only GenApi feature enumeration failed.");
            const auto* gain = findFeature(*features, "Gain");
            require(gain && gain->isAvailable && gain->isReadable &&
                        !gain->isWritable,
                    "GenTL READONLY access was not mapped to GenApi RO.");

            const camera::CameraFeatureValue requestedGain{std::int64_t{7}};
            auto writeResult =
                readOnly.features().writeFeature("Gain", requestedGain);
            auto* writeError =
                std::get_if<camera::CameraFeatureAccessError>(&writeResult);
            require(writeError &&
                        writeError->code ==
                            camera::CameraFeatureAccessError::Code::FeatureNotWritable,
                    "Read-only GenApi port accepted a feature write.");

            auto commandResult = readOnly.commands().commandDescriptors();
            auto* commands =
                std::get_if<std::vector<camera::CameraCommandDescriptor>>(&commandResult);
            require(commands != nullptr, "Read-only GenApi command enumeration failed.");
            const auto* acquisitionStart =
                findCommand(*commands, "AcquisitionStart");
            require(acquisitionStart && !acquisitionStart->isExecutable,
                    "Read-only GenApi command was incorrectly marked executable.");

            auto executeResult =
                readOnly.commands().executeCommand("AcquisitionStart");
            auto* commandError =
                std::get_if<camera::CameraCommandAccessError>(&executeResult);
            require(commandError &&
                        commandError->code ==
                            camera::CameraCommandAccessError::Code::CommandNotExecutable,
                    "Read-only GenApi port accepted a command.");
        }

        std::cout << "GenApi feature and command integration tests passed.\n";
        return EXIT_SUCCESS;
    } catch (const GenICam::GenericException& error) {
        std::cerr << "GenApi runtime error: " << error.GetDescription() << '\n';
        return EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
