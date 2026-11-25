#include "camera/gentl/GenTL_v1_6.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

static int context_token;
static uint8_t register_memory[0x10000];
static const char mock_xml[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" \
    "<RegisterDescription\n" \
    "  ModelName=\"MockGenApiCamera\"\n" \
    "  VendorName=\"MockVendor\"\n" \
    "  ToolTip=\"GenApi integration test fixture\"\n" \
    "  StandardNameSpace=\"None\"\n" \
    "  SchemaMajorVersion=\"1\"\n" \
    "  SchemaMinorVersion=\"1\"\n" \
    "  SchemaSubMinorVersion=\"0\"\n" \
    "  MajorVersion=\"1\"\n" \
    "  MinorVersion=\"0\"\n" \
    "  SubMinorVersion=\"0\"\n" \
    "  ProductGuid=\"1F3C6A72-7842-4edd-9130-E2E90A2058BA\"\n" \
    "  VersionGuid=\"7645D2A1-A41E-4ac6-B486-1531FB7BECE6\"\n" \
    "  xmlns=\"http://www.genicam.org/GenApi/Version_1_1\"\n" \
    "  xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n" \
    "  <Category Name=\"Root\">\n" \
    "    <ToolTip>Root feature category</ToolTip>\n" \
    "    <pFeature>Gain</pFeature>\n" \
    "    <pFeature>AcquisitionStart</pFeature>\n" \
    "  </Category>\n" \
    "  <Integer Name=\"Gain\">\n" \
    "    <pValue>GainReg</pValue>\n" \
    "    <Min>0</Min>\n" \
    "    <Max>255</Max>\n" \
    "    <Inc>1</Inc>\n" \
    "    <Unit>dB</Unit>\n" \
    "  </Integer>\n" \
    "  <IntReg Name=\"GainReg\">\n" \
    "    <Address>0x1000</Address>\n" \
    "    <Length>2</Length>\n" \
    "    <AccessMode>RW</AccessMode>\n" \
    "    <pPort>Device</pPort>\n" \
    "    <Sign>Unsigned</Sign>\n" \
    "    <Endianess>BigEndian</Endianess>\n" \
    "  </IntReg>\n" \
    "  <Command Name=\"AcquisitionStart\">\n" \
    "    <pValue>CommandReg</pValue>\n" \
    "    <CommandValue>1</CommandValue>\n" \
    "  </Command>\n" \
    "  <IntReg Name=\"CommandReg\">\n" \
    "    <Address>0x1002</Address>\n" \
    "    <Length>2</Length>\n" \
    "    <AccessMode>RW</AccessMode>\n" \
    "    <pPort>Device</pPort>\n" \
    "    <Sign>Unsigned</Sign>\n" \
    "    <Endianess>BigEndian</Endianess>\n" \
    "  </IntReg>\n" \
    "  <Port Name=\"Device\">\n" \
    "    <ToolTip>Default device port</ToolTip>\n" \
    "  </Port>\n" \
    "</RegisterDescription>\n";
static uint8_t* stream_memory[8];
static BUFFER_HANDLE stream_handles[8];
static size_t stream_buffer_count;
static BUFFER_HANDLE queued_buffers[8];
static size_t queued_buffer_count;

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
        if (arguments[1] <= sizeof(register_memory) &&
            *size <= sizeof(register_memory) - (size_t)arguments[1]) {
            memcpy(bytes, register_memory + (size_t)arguments[1], *size);
        } else {
            for (size_t i = 0; i < *size; ++i) bytes[i] = (uint8_t)(0xA0 + i);
        }
        return GC_ERR_SUCCESS;
    }
    case 5: {
        if (argument_count != 4 || arguments[0] != 0x4444)
            return GC_ERR_INVALID_HANDLE;
        const uint64_t address = arguments[1];
        const uint8_t* bytes = (const uint8_t*)pointer(arguments[2]);
        const size_t* size = (const size_t*)pointer(arguments[3]);
        if (!size || (!bytes && *size != 0)) return GC_ERR_INVALID_PARAMETER;
        if (address <= sizeof(register_memory) &&
            *size <= sizeof(register_memory) - (size_t)address) {
            memcpy(register_memory + (size_t)address, bytes, *size);
        }
        return GC_ERR_SUCCESS;
    }
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
        if (argument_count != 3 || arguments[2] != UINT64_C(2000000000))
            return GC_ERR_INVALID_PARAMETER;
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

    case 8: // GCRegisterEvent
        if (argument_count != 3 || arguments[0] != 0x5555 ||
            arguments[1] != EVENT_NEW_BUFFER) return GC_ERR_INVALID_PARAMETER;
        *(EVENT_HANDLE*)pointer(arguments[2]) = (EVENT_HANDLE)(uintptr_t)0x6666;
        return GC_ERR_SUCCESS;
    case 9: // GCUnregisterEvent
        return argument_count == 2 && arguments[0] == 0x5555 &&
            arguments[1] == EVENT_NEW_BUFFER ? GC_ERR_SUCCESS : GC_ERR_INVALID_PARAMETER;
    case 10: { // EventGetData
        if (argument_count != 4 || arguments[0] != 0x6666)
            return GC_ERR_INVALID_HANDLE;
        size_t* size = (size_t*)pointer(arguments[2]);
        if (!size || !pointer(arguments[1])) return GC_ERR_INVALID_PARAMETER;
        if (queued_buffer_count == 0) return GC_ERR_TIMEOUT;
        if (*size < sizeof(EVENT_NEW_BUFFER_DATA)) {
            *size = sizeof(EVENT_NEW_BUFFER_DATA);
            return GC_ERR_BUFFER_TOO_SMALL;
        }
        const BUFFER_HANDLE handle = queued_buffers[0];
        for (size_t i = 1; i < queued_buffer_count; ++i)
            queued_buffers[i - 1] = queued_buffers[i];
        --queued_buffer_count;
        size_t index = 0;
        while (index < stream_buffer_count && stream_handles[index] != handle) ++index;
        if (index == stream_buffer_count) return GC_ERR_INVALID_BUFFER;
        for (size_t i = 0; i < 16; ++i) stream_memory[index][i] = (uint8_t)(0x30 + i);
        EVENT_NEW_BUFFER_DATA data = {handle, 0};
        memcpy(pointer(arguments[1]), &data, sizeof(data));
        *size = sizeof(data);
        return GC_ERR_SUCCESS;
    }
    case 14: // EventKill
        return argument_count == 1 && arguments[0] == 0x6666
            ? GC_ERR_SUCCESS : GC_ERR_INVALID_HANDLE;
    case 33: // DevOpenDataStream
        if (argument_count != 3 || arguments[0] != 0x3333 ||
            strcmp((const char*)pointer(arguments[1]), "stream0") != 0)
            return GC_ERR_INVALID_PARAMETER;
        *(DS_HANDLE*)pointer(arguments[2]) = (DS_HANDLE)(uintptr_t)0x5555;
        return GC_ERR_SUCCESS;
    case 37: { // DSAllocAndAnnounceBuffer
        if (argument_count != 4 || arguments[0] != 0x5555 ||
            arguments[1] != 64 || stream_buffer_count >= 8)
            return GC_ERR_INVALID_PARAMETER;
        const size_t index = stream_buffer_count++;
        stream_memory[index] = (uint8_t*)malloc(64);
        if (!stream_memory[index]) return GC_ERR_OUT_OF_MEMORY;
        memset(stream_memory[index], 0, 64);
        stream_handles[index] = (BUFFER_HANDLE)(uintptr_t)(0x7000 + index);
        *(BUFFER_HANDLE*)pointer(arguments[3]) = stream_handles[index];
        return GC_ERR_SUCCESS;
    }
    case 38: // DSFlushQueue
        if (argument_count != 2 || arguments[0] != 0x5555)
            return GC_ERR_INVALID_PARAMETER;
        queued_buffer_count = 0;
        return GC_ERR_SUCCESS;
    case 39: // DSStartAcquisition
        return argument_count == 3 && arguments[0] == 0x5555
            ? GC_ERR_SUCCESS : GC_ERR_INVALID_PARAMETER;
    case 40: // DSStopAcquisition
        return argument_count == 2 && arguments[0] == 0x5555
            ? GC_ERR_SUCCESS : GC_ERR_INVALID_PARAMETER;
    case 41: { // DSGetInfo
        if (argument_count != 5 || arguments[0] != 0x5555)
            return GC_ERR_INVALID_HANDLE;
        const int32_t command = (int32_t)arguments[1];
        const size_t value = command == STREAM_INFO_PAYLOAD_SIZE ? 64 :
            command == STREAM_INFO_BUF_ANNOUNCE_MIN ? 2 : 0;
        if (value == 0) return GC_ERR_NOT_AVAILABLE;
        return write_scalar((INFO_DATATYPE*)pointer(arguments[2]),
            pointer(arguments[3]), (size_t*)pointer(arguments[4]),
            INFO_DATATYPE_SIZET, &value, sizeof(value));
    }
    case 43: // DSClose
        if (argument_count != 1 || arguments[0] != 0x5555)
            return GC_ERR_INVALID_PARAMETER;
        return getenv("MOCK_GENTL_FAIL_DS_CLOSE")
            ? GC_ERR_INVALID_PARAMETER : GC_ERR_SUCCESS;
    case 44: { // DSRevokeBuffer
        if (argument_count != 4 || arguments[0] != 0x5555)
            return GC_ERR_INVALID_PARAMETER;
        const BUFFER_HANDLE handle = (BUFFER_HANDLE)pointer(arguments[1]);
        size_t index = 0;
        while (index < stream_buffer_count && stream_handles[index] != handle) ++index;
        if (index == stream_buffer_count) return GC_ERR_INVALID_BUFFER;
        free(stream_memory[index]);
        stream_memory[index] = 0;
        *(void**)pointer(arguments[2]) = 0;
        *(void**)pointer(arguments[3]) = 0;
        return GC_ERR_SUCCESS;
    }
    case 45: { // DSQueueBuffer
        if (argument_count != 2 || arguments[0] != 0x5555 ||
            queued_buffer_count >= 8) return GC_ERR_INVALID_PARAMETER;
        const BUFFER_HANDLE handle = (BUFFER_HANDLE)pointer(arguments[1]);
        size_t index = 0;
        while (index < stream_buffer_count && stream_handles[index] != handle) ++index;
        if (index == stream_buffer_count || !stream_memory[index])
            return GC_ERR_INVALID_BUFFER;
        queued_buffers[queued_buffer_count++] = handle;
        return GC_ERR_SUCCESS;
    }
    case 46: { // DSGetBufferInfo
        if (argument_count != 6 || arguments[0] != 0x5555)
            return GC_ERR_INVALID_HANDLE;
        const BUFFER_HANDLE handle = (BUFFER_HANDLE)pointer(arguments[1]);
        size_t index = 0;
        while (index < stream_buffer_count && stream_handles[index] != handle) ++index;
        if (index == stream_buffer_count || !stream_memory[index])
            return GC_ERR_INVALID_BUFFER;
        const int32_t command = (int32_t)arguments[2];
        INFO_DATATYPE* type = (INFO_DATATYPE*)pointer(arguments[3]);
        void* output = pointer(arguments[4]);
        size_t* size = (size_t*)pointer(arguments[5]);
        if (!type || !size) return GC_ERR_INVALID_PARAMETER;
        switch (command) {
        case BUFFER_INFO_BASE: {
            void* value = stream_memory[index];
            return write_scalar(type, output, size, INFO_DATATYPE_PTR, &value, sizeof(value));
        }
        case BUFFER_INFO_SIZE_FILLED: {
            const size_t value = 16;
            return write_scalar(type, output, size, INFO_DATATYPE_SIZET, &value, sizeof(value));
        }
        case BUFFER_INFO_IS_INCOMPLETE:
        case BUFFER_INFO_IMAGEPRESENT: {
            const bool8_t value = command == BUFFER_INFO_IMAGEPRESENT;
            return write_scalar(type, output, size, INFO_DATATYPE_BOOL8, &value, sizeof(value));
        }
        case BUFFER_INFO_FRAMEID: {
            const uint64_t value = 42;
            return write_scalar(type, output, size, INFO_DATATYPE_UINT64, &value, sizeof(value));
        }
        case BUFFER_INFO_TIMESTAMP_NS: {
            const uint64_t value = 123456789;
            return write_scalar(type, output, size, INFO_DATATYPE_UINT64, &value, sizeof(value));
        }
        case BUFFER_INFO_PIXELFORMAT: {
            const uint64_t value = UINT64_C(0x01080001);
            return write_scalar(type, output, size, INFO_DATATYPE_UINT64, &value, sizeof(value));
        }
        case BUFFER_INFO_PIXELFORMAT_NAMESPACE: {
            const uint64_t value = PIXELFORMAT_NAMESPACE_PFNC_32BIT;
            return write_scalar(type, output, size, INFO_DATATYPE_UINT64, &value, sizeof(value));
        }
        case BUFFER_INFO_WIDTH:
        case BUFFER_INFO_HEIGHT: {
            const size_t value = 4;
            return write_scalar(type, output, size, INFO_DATATYPE_SIZET, &value, sizeof(value));
        }
        case BUFFER_INFO_DELIVERED_IMAGEHEIGHT: {
            const size_t value = 4;
            return write_scalar(type, output, size, INFO_DATATYPE_SIZET, &value, sizeof(value));
        }
        case BUFFER_INFO_IMAGEOFFSET:
        case BUFFER_INFO_XPADDING:
        case BUFFER_INFO_YPADDING: {
            const size_t value = 0;
            return write_scalar(type, output, size, INFO_DATATYPE_SIZET, &value, sizeof(value));
        }
        case BUFFER_INFO_PAYLOADTYPE: {
            const size_t value = PAYLOAD_TYPE_IMAGE;
            return write_scalar(type, output, size, INFO_DATATYPE_SIZET, &value, sizeof(value));
        }
        case BUFFER_INFO_CONTAINS_CHUNKDATA: {
            const bool8_t value = 0;
            return write_scalar(type, output, size, INFO_DATATYPE_BOOL8, &value, sizeof(value));
        }
        case BUFFER_INFO_DATA_LARGER_THAN_BUFFER:
        case BUFFER_INFO_IS_COMPOSITE: {
            const bool8_t value = 0;
            return write_scalar(type, output, size, INFO_DATATYPE_BOOL8, &value, sizeof(value));
        }
        default: return GC_ERR_NOT_AVAILABLE;
        }
    }
    default:
        return GC_ERR_NOT_IMPLEMENTED;
    }
}
