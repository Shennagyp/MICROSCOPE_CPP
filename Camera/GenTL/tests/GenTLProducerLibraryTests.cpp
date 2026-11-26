#include "camera/gentl/GenTLProducerLibrary.h"

#include <cstdint>
#include <filesystem>
#include <type_traits>

int main(int argc, char** argv) {
    if (argc != 2) return 1;

    static_assert(!std::is_copy_constructible_v<camera::gentl::GenTLProducerLibrary>);
    static_assert(std::is_move_constructible_v<camera::gentl::Operation>);

    auto producerLibrary =
        camera::gentl::GenTLProducerLibrary::load(argv[1]);
    if (!producerLibrary || producerLibrary->path() !=
            std::filesystem::canonical(argv[1])) return 2;

    if (producerLibrary->invoke(camera::gentl::Operation::gcInitLib) !=
        GenTL::GC_ERR_SUCCESS) return 3;

    GenTL::TL_HANDLE system = nullptr;
    if (producerLibrary->invoke(
            camera::gentl::Operation::tlOpen, &system) !=
            GenTL::GC_ERR_SUCCESS ||
        system != reinterpret_cast<GenTL::TL_HANDLE>(
            static_cast<std::uintptr_t>(0x1111))) return 4;

    if (producerLibrary->invoke(
            camera::gentl::Operation::tlClose, system) !=
        GenTL::GC_ERR_SUCCESS) return 5;

    if (producerLibrary->invoke(camera::gentl::Operation::gcCloseLib) !=
        GenTL::GC_ERR_SUCCESS) return 6;

    if (producerLibrary->invokeRaw(
            camera::gentl::Operation::tlOpen, nullptr, 1) !=
        GenTL::GC_ERR_INVALID_PARAMETER) return 7;

    // The Consumer must keep the loaded CTI alive independently of the
    // caller's GenTLProducerLibrary shared_ptr.
    auto retainedLibrary =
        camera::gentl::GenTLProducerLibrary::load(argv[1]);
    auto consumer = retainedLibrary->createConsumer();
    retainedLibrary.reset();
    if (!consumer) return 8;
    if (consumer.invoke(camera::gentl::Operation::gcInitLib) !=
        GenTL::GC_ERR_SUCCESS) return 9;
    GenTL::TL_HANDLE retainedSystem = nullptr;
    if (consumer.invoke(camera::gentl::Operation::tlOpen, &retainedSystem) !=
            GenTL::GC_ERR_SUCCESS ||
        !retainedSystem) return 10;
    if (consumer.invoke(camera::gentl::Operation::tlClose, retainedSystem) !=
        GenTL::GC_ERR_SUCCESS) return 11;
    if (consumer.invoke(camera::gentl::Operation::gcCloseLib) !=
        GenTL::GC_ERR_SUCCESS) return 12;

    return 0;
}
