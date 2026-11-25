#include "camera/gentl/ConsumerBridge.h"

#include <cassert>
#include <cstdint>
#include <type_traits>
#include <utility>

int main(int argc, char** argv) {
    assert(argc == 2);
    static_assert(!std::is_copy_constructible_v<camera::gentl::Consumer>);
    static_assert(std::is_move_constructible_v<camera::gentl::Consumer>);
    static_assert(static_cast<std::uint32_t>(
        camera::gentl::Operation::GcGetInfo) == 0);
    static_assert(static_cast<std::uint32_t>(
        camera::gentl::Operation::DsGetBufferSegmentInfo) == 63);

    auto bridge = camera::gentl::ConsumerBridge::load(argv[1]);
    assert(bridge->bridgeAbiVersion() == 1);
    assert(bridge->genTLHeaderVersion() == 0x01060000);

    auto consumer = bridge->createProducer("mock.cti");
    bridge.reset(); // The Consumer must retain the bridge library.
    assert(consumer);
    assert(consumer.invoke(camera::gentl::Operation::GcInitLib) == 0);

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
    assert(rejected);

    return 0;
}
