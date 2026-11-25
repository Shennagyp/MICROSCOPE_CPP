#include <stdint.h>
#include <string.h>

static int context_token;

void* gentl_consumer_create(const char* path, char* error, uint32_t capacity) {
    if (!path || strcmp(path, "mock.cti") != 0) {
        if (error && capacity > 0) {
            const char* message = "mock rejected CTI path";
            const size_t length = strlen(message);
            const size_t copied = length < capacity - 1 ? length : capacity - 1;
            memcpy(error, message, copied);
            error[copied] = '\0';
        }
        return 0;
    }
    return &context_token;
}

void gentl_consumer_destroy(void* context) {
    (void)context;
}

uint32_t gentl_consumer_bridge_abi_version(void) {
    return 1;
}

uint32_t gentl_consumer_header_version(void) {
    return 0x01060000;
}

int32_t gentl_consumer_invoke(
    void* context,
    uint32_t operation,
    const uint64_t* arguments,
    uint32_t argument_count) {
    if (context != &context_token) return -1006;
    if (operation == 2 && argument_count == 0) return 0;
    if (operation == 0 && argument_count == 4 &&
        arguments && arguments[0] == 0x1234) return -11000;
    return -1009;
}
