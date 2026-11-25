#include "camera/gentl/GenApiDeviceAccess.h"
#include "camera/gentl/Producer.h"

#include <Base/GCException.h>

#include <algorithm>
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

        auto device = producer.openDevice(devices.front());
        camera::gentl::GenApiDeviceAccess access(std::move(device));

        auto featureResult = access.features().featureDescriptors();
        auto* features =
            std::get_if<std::vector<camera::CameraFeatureDescriptor>>(&featureResult);
        require(features != nullptr, "GenApi feature enumeration failed.");

        const auto gain = std::find_if(features->begin(), features->end(),
            [](const auto& item) { return item.name == "Gain"; });
        require(gain != features->end(), "Root feature Gain was not exposed.");
        require(gain->kind == camera::CameraFeatureKind::Integer,
                "Gain was not mapped as an Integer feature.");
        require(gain->unit == std::optional<std::string>{"dB"},
                "Gain unit was not preserved.");
        require(std::none_of(features->begin(), features->end(),
            [](const auto& item) {
                return item.name == "GainReg" || item.name == "CommandReg";
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

        auto commandResult = access.commands().commandDescriptors();
        auto* commands =
            std::get_if<std::vector<camera::CameraCommandDescriptor>>(&commandResult);
        require(commands != nullptr, "GenApi command enumeration failed.");
        require(std::any_of(commands->begin(), commands->end(),
            [](const auto& item) { return item.name == "AcquisitionStart"; }),
            "Root command AcquisitionStart was not exposed.");
        require(std::none_of(commands->begin(), commands->end(),
            [](const auto& item) { return item.name == "CommandReg"; }),
            "Internal command register leaked into the command list.");

        auto hiddenCommand = access.commands().executeCommand("CommandReg");
        auto* hiddenCommandError =
            std::get_if<camera::CameraCommandAccessError>(&hiddenCommand);
        require(hiddenCommandError &&
                    hiddenCommandError->code ==
                        camera::CameraCommandAccessError::Code::CommandNotFound,
                "Internal GenApi register was directly executable as a command.");

        auto executeResult = access.commands().executeCommand("AcquisitionStart");
        require(std::holds_alternative<std::monostate>(executeResult),
                "GenApi command issue failed.");
        auto doneResult = access.commands().isCommandDone("AcquisitionStart");
        auto* isDone = std::get_if<bool>(&doneResult);
        require(isDone && !*isDone,
                "GenApi command completion polling did not preserve the active state.");

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
