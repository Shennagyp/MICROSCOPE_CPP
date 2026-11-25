#include "camera/gentl/ConsumerBridge.h"

#include <cassert>
#include <cstdint>
#include <type_traits>
#include <stdexcept>
#include <utility>

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    static_assert(!std::is_copy_constructible_v<camera::gentl::Consumer>);
    static_assert(std::is_move_constructible_v<camera::gentl::Consumer>);
    static_assert(static_cast<std::uint32_t>(
        camera::gentl::Operation::gcGetInfo) == 0);
    static_assert(static_cast<std::uint32_t>(
        camera::gentl::Operation::dsGetBufferSegmentInfo) == 63);

    auto bridge = camera::gentl::ConsumerBridge::load(argv[1]);
    if (bridge->bridgeAbiVersion() != 1) return 2;
    if (bridge->genTLHeaderVersion() != 0x01060000) return 3;

    auto consumer = bridge->createProducer("mock.cti");
    bridge.reset(); // The Consumer must retain the bridge library.
    if (!consumer) return 4;
    if (consumer.invoke(camera::gentl::Operation::gcInitLib) != 0) return 5;

    const std::uint64_t infoArguments[] = {
        0x1234,
        0x1000,
        0x2000,
        0x3000
    };
    assert(consumer.invokeRaw(
        camera::gentl::Operation::GcGetInfo,
        infoArguments, 4) == -11000);

    bool rejected = false;
    try {
        auto invalid = camera::gentl::ConsumerBridge::load(argv[1])
            ->createProducer("missing.cti");
        (void)invalid;
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    if (!rejected) return 7;

    return 0;
}
