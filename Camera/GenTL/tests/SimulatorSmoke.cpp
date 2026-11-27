#include "camera/gentl/Producer.h"
#include "camera/gentl/GenTLFrameAdapter.h"

#include <charconv>
#include <exception>
#include <iostream>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <vector>

namespace {

void verifyTwoDeviceIsolation(
    const camera::gentl::Producer& producer,
    const std::vector<camera::gentl::DeviceDescriptor>& devices) {
    if (devices.size() < 2) {
        throw std::runtime_error(
            "The two-device simulator mode requires two discovered devices.");
    }
    if (devices[0].deviceId == devices[1].deviceId ||
        devices[0].serialNumber == devices[1].serialNumber) {
        throw std::runtime_error(
            "The simulator returned duplicate identities for its two devices.");
    }

    auto firstDevice = producer.openDevice(devices[0]);
    auto secondDevice = producer.openDevice(devices[1]);

    const std::vector<std::uint8_t> firstRegister{0x31, 0x41};
    const std::vector<std::uint8_t> secondRegister{0x52, 0x62};
    firstDevice.writePort(0x1000, firstRegister);
    secondDevice.writePort(0x1000, secondRegister);
    if (firstDevice.readPort(0x1000, 2) != firstRegister ||
        secondDevice.readPort(0x1000, 2) != secondRegister) {
        throw std::runtime_error(
            "Device register state leaked between the two device ports.");
    }

    const auto firstStreamIds = firstDevice.dataStreamIds();
    const auto secondStreamIds = secondDevice.dataStreamIds();
    if (firstStreamIds.empty() || secondStreamIds.empty()) {
        throw std::runtime_error("One simulated device has no data stream.");
    }
    auto firstStream = firstDevice.openDataStream(firstStreamIds.front(), 3);
    auto secondStream = secondDevice.openDataStream(secondStreamIds.front(), 3);
    firstStream.start();
    secondStream.start();

    std::uint64_t firstTimestamp = 0;
    std::uint64_t secondTimestamp = 0;
    for (std::size_t frameIndex = 0; frameIndex < 3; ++frameIndex) {
        const auto first = firstStream.nextFrame(5000);
        const auto second = secondStream.nextFrame(5000);
        if (!first || !second || first->incomplete || second->incomplete) {
            throw std::runtime_error(
                "One of the independent simulated streams failed to deliver a complete frame.");
        }
        const auto expectedId = static_cast<std::uint64_t>(frameIndex + 1);
        const auto expectedByte = static_cast<std::uint8_t>(0x30 + frameIndex);
        if (first->frameId != expectedId || second->frameId != expectedId ||
            first->payload.empty() || second->payload.empty() ||
            first->payload.front() != expectedByte ||
            second->payload.front() != expectedByte ||
            !first->timestampNanoseconds || !second->timestampNanoseconds ||
            (frameIndex > 0 &&
             (*first->timestampNanoseconds <= firstTimestamp ||
              *second->timestampNanoseconds <= secondTimestamp))) {
            throw std::runtime_error(
                "The two streams did not maintain independent frame sequences and timestamps.");
        }
        firstTimestamp = *first->timestampNanoseconds;
        secondTimestamp = *second->timestampNanoseconds;
    }

    // Stop one stream while the other remains active. Its event
    // unregistration and queue flush must not cancel the peer stream.
    firstStream.stop();
    const auto survivingSecondFrame = secondStream.nextFrame(5000);
    if (!survivingSecondFrame || survivingSecondFrame->incomplete ||
        survivingSecondFrame->frameId != 4 ||
        !survivingSecondFrame->timestampNanoseconds ||
        *survivingSecondFrame->timestampNanoseconds <= secondTimestamp ||
        survivingSecondFrame->payload.empty() ||
        survivingSecondFrame->payload.front() != 0x33) {
        throw std::runtime_error(
            "Stopping the first device disrupted the second device's active stream.");
    }
    secondStream.stop();
    firstDevice.close();
    secondDevice.close();
    std::cout << "Two-device isolation passed: distinct identities, independent "
                 "port registers, separate stream events/buffer queues, and "
                 "interleaved frame sequences.\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) {
        std::cerr
            << "Usage: camera_gentl_simulator_smoke "
            << "<vendor-producer.cti> [device-index] "
            << "[--verify-two-device-isolation|--sequence-gap|--sequence-duplicate|"
               "--sequence-out-of-order|--sequence-ambiguous|"
               "--incomplete-frame|--empty-frame|--sustained-acquisition|--multipart|"
               "--queue-starvation-recovery|--reopen-cycles|--interruption-recovery]\n";
        return 64;
    }

    std::size_t selectedDeviceIndex = 0;
    bool verifyMultiDeviceIsolation = false;
    std::string sequenceMode;
    std::string deliveryMode;
    bool sustainedMode = false;
    bool multipartMode = false;
    bool queueStarvationMode = false;
    bool reopenCyclesMode = false;
    bool interruptionMode = false;
    if (argc >= 3) {
        const std::string option = argv[2];
        if (option == "--verify-two-device-isolation") {
            verifyMultiDeviceIsolation = true;
        } else {
            const auto result = std::from_chars(
                option.data(), option.data() + option.size(), selectedDeviceIndex);
            if (result.ec != std::errc{} ||
                result.ptr != option.data() + option.size()) {
                std::cerr << "The device index must be a non-negative integer.\n";
                return 65;
            }
        }
    }
    if (argc == 4) {
        const std::string option = argv[3];
        if (option == "--verify-two-device-isolation") {
            verifyMultiDeviceIsolation = true;
        } else if (option == "--sequence-gap") {
            sequenceMode = "gap";
        } else if (option == "--sequence-duplicate") {
            sequenceMode = "duplicate";
        } else if (option == "--sequence-out-of-order") {
            sequenceMode = "out-of-order";
            setenv("MOCK_GENTL_SIMULATOR_TLTYPE", "GEV", 1);
        } else if (option == "--sequence-ambiguous") {
            sequenceMode = "ambiguous";
        } else if (option == "--incomplete-frame") {
            deliveryMode = "incomplete";
        } else if (option == "--empty-frame") {
            deliveryMode = "empty";
        } else if (option == "--sustained-acquisition") {
            sustainedMode = true;
        } else if (option == "--multipart") {
            multipartMode = true;
            setenv("MOCK_GENTL_SIMULATOR_MULTIPART", "1", 1);
        } else if (option == "--queue-starvation-recovery") {
            queueStarvationMode = true;
            setenv("MOCK_GENTL_SIMULATOR_QUEUE_STARVATION", "1", 1);
        } else if (option == "--reopen-cycles") {
            reopenCyclesMode = true;
        } else if (option == "--interruption-recovery") {
            interruptionMode = true;
            setenv("MOCK_GENTL_SIMULATOR_INTERRUPT_AFTER", "2", 1);
        } else {
            std::cerr << "Unknown simulator smoke option.\n";
            return 67;
        }
    }
    if (!sequenceMode.empty())
        setenv("MOCK_GENTL_SIMULATOR_SEQUENCE_MODE", sequenceMode.c_str(), 1);
    if (!deliveryMode.empty())
        setenv("MOCK_GENTL_SIMULATOR_DELIVERY_MODE", deliveryMode.c_str(), 1);

    try {
        auto producer = camera::gentl::Producer::open(argv[1]);

        std::cout << "CTI: " << producer.ctiPath() << '\n';
        const auto devices = producer.discover(5000);
        if (devices.empty()) {
            std::cerr << "The CTI Producer discovered no devices.\n";
            return 2;
        }
        if (verifyMultiDeviceIsolation) {
            verifyTwoDeviceIsolation(producer, devices);
        }
        if (selectedDeviceIndex >= devices.size()) {
            std::cerr << "Requested device index " << selectedDeviceIndex
                      << " is outside the discovered device range [0, "
                      << (devices.size() - 1) << "].\n";
            return 66;
        }

        const auto& descriptor = devices[selectedDeviceIndex];
        std::cout << "Discovered " << devices.size()
                  << " device(s); testing device index "
                  << selectedDeviceIndex << ":\n"
                  << "  interface: " << descriptor.interfaceId << '\n'
                  << "  device ID: " << descriptor.deviceId << '\n'
                  << "  display:   " << descriptor.displayName << '\n'
                  << "  vendor:    " << descriptor.vendor << '\n'
                  << "  model:     " << descriptor.model << '\n'
                  << "  serial:    " << descriptor.serialNumber << '\n';

        auto device = producer.openDevice(descriptor);
        const auto xml = device.genApiXml();
        if (xml.empty()) {
            std::cerr << "The device returned an empty GenApi XML document.\n";
            return 3;
        }
        std::cout << "GenApi XML bytes: " << xml.size() << '\n';

        const auto streamIds = device.dataStreamIds();
        if (streamIds.empty()) {
            std::cerr << "The device exposes no data streams.\n";
            return 4;
        }

        {
            auto stream = device.openDataStream(streamIds.front(), 4);
            // The ordinary smoke injects two Producer underruns and one
            // host wait timeout. The sustained case intentionally injects
            // neither and exercises repeated acquisition-buffer recycling.
            const char* injectedUnderruns =
                (sustainedMode || queueStarvationMode || interruptionMode) ? "0" : "2";
            if (setenv("MOCK_GENTL_SIMULATOR_UNDERRUNS", injectedUnderruns, 1) != 0)
                throw std::runtime_error("Could not configure simulator underruns.");
            stream.start();
            if (!sustainedMode && !interruptionMode) {
                if (setenv("MOCK_GENTL_SIMULATOR_FORCE_WAIT_TIMEOUT", "1", 1) != 0)
                    throw std::runtime_error("Could not configure simulator timeout.");
                const auto forcedTimeout = stream.nextFrame(1);
                unsetenv("MOCK_GENTL_SIMULATOR_FORCE_WAIT_TIMEOUT");
                if (forcedTimeout)
                    throw std::runtime_error("The simulator failed to inject the wait timeout.");
            }

            const std::size_t requiredFrames =
                sustainedMode ? 4096 :
                interruptionMode ? 2 : (multipartMode ? 1 : 5);
            const std::uint64_t expectedUnderruns =
                queueStarvationMode ? 1 :
                ((sustainedMode || interruptionMode) ? 0 : 2);
            const std::uint64_t expectedHostTimeouts =
                interruptionMode ? 1 :
                (queueStarvationMode ? 2 : (sustainedMode ? 0 : 1));

            if (queueStarvationMode) {
                const auto depletedWait = stream.nextFrame(1);
                if (depletedWait)
                    throw std::runtime_error("A depleted Producer queue unexpectedly delivered a frame.");
                const auto depleted = stream.statistics();
                if (depleted.producerBufferUnderruns != 1 ||
                    depleted.announcedBuffers != 4 ||
                    depleted.queuedBuffers != 0 ||
                    depleted.buffersAwaitingDelivery != 0 ||
                    depleted.integrity.observedBuffers != 0 ||
                    depleted.integrity.frameWaitTimeouts != 2 ||
                    depleted.integrity.bufferStarvationEvents != 1) {
                    throw std::runtime_error(
                        "Producer queue depletion was not distinguished from host timeout.");
                }
            }
            const std::size_t sequenceOffset = verifyMultiDeviceIsolation ? 3 : 0;
            std::uint64_t previousFrameId = 0;
            std::uint64_t previousTimestampNanoseconds = 0;
            for (std::size_t index = 0; index < requiredFrames; ++index) {
                const auto frame = stream.nextFrame(5000);
                if (!frame) {
                    throw std::runtime_error(
                        "Timed out waiting for simulated frame " +
                        std::to_string(index + 1) + ".");
                }
                const bool expectedIncomplete =
                    deliveryMode == "incomplete" && index == 2;
                const bool expectedEmpty =
                    deliveryMode == "empty" && index == 2;
                if (frame->incomplete != expectedIncomplete) {
                    throw std::runtime_error(
                        "The Producer incomplete-buffer status did not match the injected case.");
                }
                if (frame->payload.size() !=
                    (multipartMode ? 64U : (expectedEmpty ? 0U : 16U))) {
                    throw std::runtime_error(
                        "The deterministic simulator returned an unexpected filled payload size.");
                }
                if (!frame->frameId || !frame->timestampNanoseconds ||
                    (sequenceMode.empty() &&
                     *frame->frameId != index + sequenceOffset + 1) ||
                    (index > 0 &&
                     ((sequenceMode.empty() &&
                       *frame->frameId != previousFrameId + 1) ||
                      *frame->timestampNanoseconds <= previousTimestampNanoseconds))) {
                    throw std::runtime_error(
                        "Frame IDs or timestamps were not reported consistently.");
                }
                if (!expectedEmpty && frame->payload.front() !=
                    static_cast<std::uint8_t>(0x30 + index)) {
                    throw std::runtime_error(
                        "Frame payload did not advance with the simulated delivery sequence.");
                }
                previousFrameId = *frame->frameId;
                previousTimestampNanoseconds = *frame->timestampNanoseconds;
                if (frame->imagePresent && !*frame->imagePresent) {
                    throw std::runtime_error(
                        "The completed buffer does not contain an image.");
                }

                const auto commonFrame =
                    camera::gentl::makeCommonFrame(*frame);
                if (multipartMode) {
                    if (!commonFrame || !(*commonFrame)->isMultipart ||
                        (*commonFrame)->pixelFormat != camera::PixelFormat::Unknown ||
                        (*commonFrame)->parts.size() != 2 ||
                        (*commonFrame)->pixels.size() != 64) {
                        throw std::runtime_error(
                            "Multipart payload was not preserved as a composite Common frame.");
                    }
                    const auto& first = (*commonFrame)->parts[0];
                    const auto& second = (*commonFrame)->parts[1];
                    if (first.offsetBytes != 8 || first.byteSize != 16 ||
                        first.dataType != GenTL::PART_DATATYPE_2D_IMAGE ||
                        first.dataFormat != UINT64_C(0x01080001) ||
                        first.dataFormatNamespace != GenTL::PIXELFORMAT_NAMESPACE_PFNC_32BIT ||
                        first.width != 4 || first.height != 4 ||
                        first.sourceId != 7 || first.regionId != 3 ||
                        first.dataPurposeId != 1 ||
                        second.offsetBytes != 40 || second.byteSize != 8 ||
                        second.dataType != GenTL::PART_DATATYPE_UNKNOWN ||
                        (*commonFrame)->partData(first) == nullptr ||
                        (*commonFrame)->partData(second) == nullptr ||
                        (*commonFrame)->partData(first)[0] != frame->payload[8] ||
                        (*commonFrame)->partData(second)[0] != frame->payload[40]) {
                        throw std::runtime_error(
                            "Multipart part metadata or offset-backed byte views are invalid.");
                    }
                } else if ((expectedIncomplete || expectedEmpty) == commonFrame.has_value()) {
                    throw std::runtime_error(
                        "Incomplete/empty payload acceptance did not match the adapter contract.");
                }

                std::cout << "Frame " << (index + 1)
                          << ": bytes=" << frame->payload.size()
                          << " common-projection="
                          << (commonFrame ? "mapped" : "opaque-or-unsupported");
                if (frame->width) std::cout << " width=" << *frame->width;
                if (frame->height) std::cout << " height=" << *frame->height;
                if (frame->pixelFormat) {
                    std::cout << " pixel-format=0x" << std::hex
                              << *frame->pixelFormat << std::dec;
                }
                if (frame->frameId) {
                    std::cout << " frame-id=" << *frame->frameId;
                }
                std::cout << '\n';
            }

            if (interruptionMode) {
                const auto interruptedWait = stream.nextFrame(1);
                if (interruptedWait)
                    throw std::runtime_error("The interrupted stream unexpectedly delivered a frame.");
            }

            const auto statistics = stream.statistics();
            const std::string expectedTransportType =
                sequenceMode == "out-of-order" ? "GEV" : "MockGenTL";
            std::cout << "Statistics diagnostic: tltype="
                      << statistics.transportType.value_or("<absent>")
                      << " delivered=" << statistics.producerDeliveredBuffers.value_or(UINT64_MAX)
                      << " underruns=" << statistics.producerBufferUnderruns.value_or(UINT64_MAX)
                      << " started=" << statistics.producerFramesStarted.value_or(UINT64_MAX)
                      << " announced=" << statistics.announcedBuffers.value_or(SIZE_MAX)
                      << " queued=" << statistics.queuedBuffers.value_or(SIZE_MAX)
                      << " awaiting=" << statistics.buffersAwaitingDelivery.value_or(SIZE_MAX)
                      << " observed=" << statistics.integrity.observedBuffers
                      << " complete=" << statistics.integrity.completeImages
                      << " incomplete=" << statistics.integrity.incompleteBuffers
                      << " gaps=" << statistics.integrity.sequenceGaps
                      << " missing=" << statistics.integrity.missingFrames
                      << " timeouts=" << statistics.integrity.frameWaitTimeouts
                      << " starvation=" << statistics.integrity.bufferStarvationEvents
                      << '\n';
            if (statistics.transportType !=
                    std::optional<std::string>(expectedTransportType) ||
                statistics.producerDeliveredBuffers != requiredFrames ||
                statistics.producerBufferUnderruns != expectedUnderruns ||
                statistics.producerFramesStarted != requiredFrames ||
                statistics.announcedBuffers != 4 ||
                statistics.queuedBuffers != 4 ||
                statistics.buffersAwaitingDelivery != 0 ||
                statistics.integrity.observedBuffers != requiredFrames ||
                statistics.integrity.completeImages !=
                    (multipartMode ? 1U : requiredFrames -
                    ((deliveryMode == "incomplete" || deliveryMode == "empty") ? 1U : 0U)) ||
                statistics.integrity.nonImagePayloads != 0 ||
                statistics.integrity.incompleteBuffers !=
                    (deliveryMode == "incomplete" ? 1U : 0U) ||
                statistics.integrity.invalidPayloads !=
                    (deliveryMode == "empty" ? 1U : 0U) ||
                statistics.integrity.sequenceGaps != (sequenceMode == "gap" ? 1u : 0u) ||
                statistics.integrity.missingFrames != (sequenceMode == "gap" ? 2u : 0u) ||
                statistics.integrity.duplicateFrameIds !=
                    (sequenceMode == "duplicate" ? 1u : 0u) ||
                statistics.integrity.outOfOrderFrameIds !=
                    (sequenceMode == "out-of-order" ? 1u : 0u) ||
                statistics.integrity.ambiguousSequenceTransitions !=
                    (sequenceMode == "ambiguous" ? 1u : 0u) ||
                statistics.integrity.frameWaitTimeouts != expectedHostTimeouts ||
                statistics.integrity.bufferStarvationEvents != expectedUnderruns) {
                throw std::runtime_error(
                    "Acquisition statistics did not preserve the producer, "
                    "sequence and host-timeout distinctions.");
            }
            std::cout << "Acquisition integrity passed: delivered="
                      << *statistics.producerDeliveredBuffers
                      << " underruns=" << *statistics.producerBufferUnderruns
                      << " host-timeouts=" << statistics.integrity.frameWaitTimeouts
                      << " missing-frames=" << statistics.integrity.missingFrames
                      << " queued=" << *statistics.queuedBuffers << '\n';
            unsetenv("MOCK_GENTL_SIMULATOR_UNDERRUNS");
            unsetenv("MOCK_GENTL_SIMULATOR_SEQUENCE_MODE");
            unsetenv("MOCK_GENTL_SIMULATOR_TLTYPE");
            unsetenv("MOCK_GENTL_SIMULATOR_DELIVERY_MODE");
            unsetenv("MOCK_GENTL_SIMULATOR_MULTIPART");
            unsetenv("MOCK_GENTL_SIMULATOR_QUEUE_STARVATION");
            unsetenv("MOCK_GENTL_SIMULATOR_INTERRUPT_AFTER");
            stream.stop();

            if (interruptionMode) {
                auto restarted = device.openDataStream(streamIds.front(), 4);
                restarted.start();
                auto lastFrameId = previousFrameId;
                auto lastTimestamp = previousTimestampNanoseconds;
                for (std::size_t frameIndex = 0; frameIndex < 2; ++frameIndex) {
                    const auto frame = restarted.nextFrame(5000);
                    if (!frame || frame->incomplete || !frame->frameId ||
                        *frame->frameId != lastFrameId + 1 ||
                        !frame->timestampNanoseconds ||
                        *frame->timestampNanoseconds <= lastTimestamp ||
                        frame->payload.size() != 16 ||
                        frame->payload.front() !=
                            static_cast<std::uint8_t>(0x30 + frameIndex)) {
                        throw std::runtime_error(
                            "Stream interruption recovery did not resume with monotonic frames.");
                    }
                    lastFrameId = *frame->frameId;
                    lastTimestamp = *frame->timestampNanoseconds;
                }
                const auto restartedStats = restarted.statistics();
                if (restartedStats.producerDeliveredBuffers != 2 ||
                    restartedStats.producerFramesStarted != 2 ||
                    restartedStats.producerBufferUnderruns != 0 ||
                    restartedStats.announcedBuffers != 4 ||
                    restartedStats.queuedBuffers != 4 ||
                    restartedStats.buffersAwaitingDelivery != 0 ||
                    restartedStats.integrity.completeImages != 2 ||
                    restartedStats.integrity.frameWaitTimeouts != 0 ||
                    restartedStats.integrity.bufferStarvationEvents != 0) {
                    throw std::runtime_error(
                        "Restarted acquisition did not return to a clean state.");
                }
                restarted.stop();
            }

            if (reopenCyclesMode) {
                if (stream || stream.nextFrame(1))
                    throw std::runtime_error("Stopped stream remained active.");
                auto lastFrameId = previousFrameId;
                auto lastTimestamp = previousTimestampNanoseconds;
                for (std::size_t cycle = 0; cycle < 3; ++cycle) {
                    auto reopened = device.openDataStream(streamIds.front(), 4);
                    reopened.start();
                    for (std::size_t frameIndex = 0; frameIndex < 2; ++frameIndex) {
                        const auto frame = reopened.nextFrame(5000);
                        if (!frame || frame->incomplete || !frame->frameId ||
                            *frame->frameId != lastFrameId + 1 ||
                            !frame->timestampNanoseconds ||
                            *frame->timestampNanoseconds <= lastTimestamp ||
                            frame->payload.size() != 16 ||
                            frame->payload.front() !=
                                static_cast<std::uint8_t>(0x30 + frameIndex)) {
                            throw std::runtime_error(
                                "Reopened stream did not resume with valid monotonic frames.");
                        }
                        lastFrameId = *frame->frameId;
                        lastTimestamp = *frame->timestampNanoseconds;
                    }
                    const auto reopenedStats = reopened.statistics();
                    if (reopenedStats.producerDeliveredBuffers != 2 ||
                        reopenedStats.producerFramesStarted != 2 ||
                        reopenedStats.announcedBuffers != 4 ||
                        reopenedStats.queuedBuffers != 4 ||
                        reopenedStats.buffersAwaitingDelivery != 0 ||
                        reopenedStats.integrity.completeImages != 2 ||
                        reopenedStats.integrity.frameWaitTimeouts != 0 ||
                        reopenedStats.integrity.bufferStarvationEvents != 0) {
                        throw std::runtime_error(
                            "Reopened stream did not restore a clean acquisition state.");
                    }
                    reopened.stop();
                    if (reopened || reopened.nextFrame(1))
                        throw std::runtime_error("A stopped reopened stream remained active.");
                }
            }
        }

        device.close();
        std::cout << "Simulator GenTL smoke passed: discovery, device open, "
                     "GenApi XML, five complete image buffers, stream stop "
                     "and device close.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Simulator GenTL smoke failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
