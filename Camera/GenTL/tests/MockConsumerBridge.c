#include "camera/gentl/GenTL_v1_6.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>

static int context_token;
static const char mock_xml[] = "<RegisterDescription/>";

static void* pointer(uint64_t value) {
    return (void*)(uintptr_t)value;
}

static int32_t write_string(char* destination, size_t* size, const char* value) {
    if (!size) return GC_ERR_INVALID_PARAMETER;
    const size_t required = strlen(value) + 1;
    if (!destination || *size < required) {
        *size = required;
        return GC_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(destination, value, required);
    *size = required;
    return GC_ERR_SUCCESS;
}

static int32_t write_info_string(
    INFO_DATATYPE* type, void* destination, size_t* size, const char* value) {
    if (type) *type = INFO_DATATYPE_STRING;
    return write_string((char*)destination, size, value);
}

static int32_t write_scalar(
    INFO_DATATYPE* type, void* destination, size_t* size,
    INFO_DATATYPE value_type, const void* value, size_t value_size) {
    if (!type || !size) return GC_ERR_INVALID_PARAMETER;
    *type = value_type;
    if (!destination || *size < value_size) {
        *size = value_size;
        return GC_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(destination, value, value_size);
    *size = value_size;
    return GC_ERR_SUCCESS;
}

void* gentl_consumer_create(const char* path, char* error, uint32_t capacity) {
    if (!path || strcmp(strrchr(path, '/') ? strrchr(path, '/') + 1 : path, "mock.cti") != 0) {
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

void gentl_consumer_destroy(void* context) { (void)context; }
uint32_t gentl_consumer_bridge_abi_version(void) { return 1; }
uint32_t gentl_consumer_header_version(void) { return GC_GENTL_HEADER_VERSION; }

int32_t gentl_consumer_invoke(
    void* context, uint32_t operation,
    const uint64_t* arguments, uint32_t argument_count) {
    if (context != &context_token) return GC_ERR_INVALID_HANDLE;
    if (argument_count != 0 && !arguments) return GC_ERR_INVALID_PARAMETER;

    switch (operation) {
    case 0:
        return argument_count == 4 && arguments[0] == 0x1234
            ? -11000 : GC_ERR_INVALID_PARAMETER;
    case 2:
    case 3:
    case 16:
    case 23:
    case 35:
        return argument_count == ((operation == 2 || operation == 3) ? 0u : 1u)
            ? GC_ERR_SUCCESS : GC_ERR_INVALID_PARAMETER;
    case 4: {
        if (argument_count != 4 || arguments[0] != 0x4444) return GC_ERR_INVALID_HANDLE;
        size_t* size = (size_t*)pointer(arguments[3]);
        uint8_t* bytes = (uint8_t*)pointer(arguments[2]);
        if (!size || (!bytes && *size != 0)) return GC_ERR_INVALID_PARAMETER;
        if (arguments[1] == 0x2000) {
            const size_t available = sizeof(mock_xml) - 1;
            if (*size > available) return GC_ERR_INVALID_PARAMETER;
            memcpy(bytes, mock_xml, *size);
            return GC_ERR_SUCCESS;
        }
        for (size_t i = 0; i < *size; ++i) bytes[i] = (uint8_t)(0xA0 + i);
        return GC_ERR_SUCCESS;
    }
    case 5:
        return argument_count == 4 && arguments[0] == 0x4444
            ? GC_ERR_SUCCESS : GC_ERR_INVALID_HANDLE;
    case 6:
        if (argument_count != 3 || arguments[0] != 0x4444) return GC_ERR_INVALID_HANDLE;
        return write_string((char*)pointer(arguments[1]),
                            (size_t*)pointer(arguments[2]), "Mock://DevicePort");
    case 15:
        if (argument_count != 1) return GC_ERR_INVALID_PARAMETER;
        *(TL_HANDLE*)pointer(arguments[0]) = (TL_HANDLE)(uintptr_t)0x1111;
        return GC_ERR_SUCCESS;
    case 18:
        if (argument_count != 2 || arguments[0] != 0x1111) return GC_ERR_INVALID_HANDLE;
        *(uint32_t*)pointer(arguments[1]) = 1;
        return GC_ERR_SUCCESS;
    case 19:
        if (argument_count != 4 || arguments[0] != 0x1111) return GC_ERR_INVALID_HANDLE;
        return write_string((char*)pointer(arguments[2]),
                            (size_t*)pointer(arguments[3]), "iface0");
    case 21:
        if (argument_count != 3 || arguments[0] != 0x1111) return GC_ERR_INVALID_HANDLE;
        *(IF_HANDLE*)pointer(arguments[2]) = (IF_HANDLE)(uintptr_t)0x2222;
        return GC_ERR_SUCCESS;
    case 22:
    case 27:
        if (argument_count != 3) return GC_ERR_INVALID_PARAMETER;
        *(bool8_t*)pointer(arguments[1]) = 1;
        return GC_ERR_SUCCESS;
    case 24:
        if (argument_count != 5 || arguments[0] != 0x2222) return GC_ERR_INVALID_HANDLE;
        if (arguments[1] != INTERFACE_INFO_DISPLAYNAME) return GC_ERR_NOT_AVAILABLE;
        return write_info_string((INFO_DATATYPE*)pointer(arguments[2]),
                                 pointer(arguments[3]), (size_t*)pointer(arguments[4]),
                                 "Mock Interface");
    case 25:
        if (argument_count != 2 || arguments[0] != 0x2222) return GC_ERR_INVALID_HANDLE;
        *(uint32_t*)pointer(arguments[1]) = 1;
        return GC_ERR_SUCCESS;
    case 26:
        if (argument_count != 4 || arguments[0] != 0x2222) return GC_ERR_INVALID_HANDLE;
        return write_string((char*)pointer(arguments[2]),
                            (size_t*)pointer(arguments[3]), "device0");
    case 28: {
        if (argument_count != 6 || arguments[0] != 0x2222) return GC_ERR_INVALID_HANDLE;
        const char* value = 0;
        switch ((DEVICE_INFO_CMD)arguments[2]) {
        case DEVICE_INFO_DISPLAYNAME: value = "Mock Camera"; break;
        case DEVICE_INFO_VENDOR: value = "Mock Vendor"; break;
        case DEVICE_INFO_MODEL: value = "Mock Model"; break;
        case DEVICE_INFO_SERIAL_NUMBER: value = "MOCK-0001"; break;
        default: return GC_ERR_NOT_AVAILABLE;
        }
        return write_info_string((INFO_DATATYPE*)pointer(arguments[3]),
                                 pointer(arguments[4]), (size_t*)pointer(arguments[5]),
                                 value);
    }
    case 29:
        if (argument_count != 4 || arguments[0] != 0x2222) return GC_ERR_INVALID_HANDLE;
        *(DEV_HANDLE*)pointer(arguments[3]) = (DEV_HANDLE)(uintptr_t)0x3333;
        return GC_ERR_SUCCESS;
    case 30:
        if (argument_count != 2 || arguments[0] != 0x3333) return GC_ERR_INVALID_HANDLE;
        *(PORT_HANDLE*)pointer(arguments[1]) = (PORT_HANDLE)(uintptr_t)0x4444;
        return GC_ERR_SUCCESS;
    case 31:
        if (argument_count != 2 || arguments[0] != 0x3333) return GC_ERR_INVALID_HANDLE;
        *(uint32_t*)pointer(arguments[1]) = 1;
        return GC_ERR_SUCCESS;
    case 32:
        if (argument_count != 4 || arguments[0] != 0x3333) return GC_ERR_INVALID_HANDLE;
        return write_string((char*)pointer(arguments[2]),
                            (size_t*)pointer(arguments[3]), "stream0");
    case 47: // GCGetNumPortURLs
        if (argument_count != 2 || arguments[0] != 0x4444) return GC_ERR_INVALID_HANDLE;
        *(uint32_t*)pointer(arguments[1]) = 1;
        return GC_ERR_SUCCESS;
    case 48: { // GCGetPortURLInfo
        if (argument_count != 6 || arguments[0] != 0x4444 || arguments[1] != 0)
            return GC_ERR_INVALID_HANDLE;
        const uint32_t command = (uint32_t)arguments[2];
        if (command == URL_INFO_SCHEME) {
            const int32_t value = URL_SCHEME_LOCAL;
            return write_scalar((INFO_DATATYPE*)pointer(arguments[3]),
                pointer(arguments[4]), (size_t*)pointer(arguments[5]),
                INFO_DATATYPE_INT32, &value, sizeof(value));
        }
        if (command == URL_INFO_FILE_REGISTER_ADDRESS) {
            const uint64_t value = 0x2000;
            return write_scalar((INFO_DATATYPE*)pointer(arguments[3]),
                pointer(arguments[4]), (size_t*)pointer(arguments[5]),
                INFO_DATATYPE_UINT64, &value, sizeof(value));
        }
        if (command == URL_INFO_FILE_SIZE) {
            const uint64_t value = sizeof(mock_xml) - 1;
            return write_scalar((INFO_DATATYPE*)pointer(arguments[3]),
                pointer(arguments[4]), (size_t*)pointer(arguments[5]),
                INFO_DATATYPE_UINT64, &value, sizeof(value));
        }
        if (command == URL_INFO_URL) {
            return write_info_string((INFO_DATATYPE*)pointer(arguments[3]),
                pointer(arguments[4]), (size_t*)pointer(arguments[5]),
                "Local:0x2000");
        }
        return GC_ERR_NOT_AVAILABLE;
    }
    default:
        return GC_ERR_NOT_IMPLEMENTED;
    }
}
