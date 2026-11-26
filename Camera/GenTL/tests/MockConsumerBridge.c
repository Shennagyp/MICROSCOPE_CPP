#include "camera/gentl/GenTL_v1_6.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>

static int context_token;
static uint8_t register_memory[0x10000];
static size_t command_poll_count = 0;
static int fails_at_address(const char* variable, uint64_t address) {
    const char* configured = getenv(variable);
    if (!configured || !*configured) return 0;
    char* end = 0;
    const unsigned long long parsed = strtoull(configured, &end, 0);
    return end != configured && *end == '\0' && parsed == address;
}

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
    "    <pFeature>FeatureEnabled</pFeature>\n" \
    "    <pFeature>TriggerMode</pFeature>\n" \
    "    <pFeature>Temperature</pFeature>\n" \
    "    <pFeature>DeviceLabel</pFeature>\n" \
    "    <pFeature>AcquisitionControl</pFeature>\n" \
    "  </Category>\n" \
    "  <Category Name=\"AcquisitionControl\">\n" \
    "    <pFeature>ExposureTime</pFeature>\n" \
    "    <pFeature>TriggerControl</pFeature>\n" \
    "    <pFeature>AcquisitionStart</pFeature>\n" \
    "    <pFeature>AcquisitionStop</pFeature>\n" \
    "  </Category>\n" \
    "  <Category Name=\"TriggerControl\">\n" \
    "    <pFeature>TriggerDelay</pFeature>\n" \
    "  </Category>\n" \
    "  <Float Name=\"ExposureTime\">\n" \
    "    <Value>2500.0</Value>\n" \
    "    <Min>10.0</Min>\n" \
    "    <Max>1000000.0</Max>\n" \
    "    <Inc>0.5</Inc>\n" \
    "    <Unit>us</Unit>\n" \
    "  </Float>\n" \
    "  <Integer Name=\"TriggerDelay\">\n" \
    "    <Value>7</Value>\n" \
    "    <Min>0</Min>\n" \
    "    <Max>100</Max>\n" \
    "    <Inc>1</Inc>\n" \
    "  </Integer>\n" \
    "  <Integer Name=\"Gain\">\n" \
    "    <pIsAvailable>FeatureEnabled</pIsAvailable>\n" \
    "    <pValue>GainReg</pValue>\n" \
    "    <Min>0</Min>\n" \
    "    <Max>255</Max>\n" \
    "    <Inc>1</Inc>\n" \
    "    <Unit>dB</Unit>\n" \
    "  </Integer>\n" \
    "  <Float Name=\"Temperature\">\n" \
    "    <Value>36.5</Value>\n" \
    "    <Min>0</Min>\n" \
    "    <Max>100</Max>\n" \
    "    <Inc>0.1</Inc>\n" \
    "    <Unit>degC</Unit>\n" \
    "  </Float>\n" \
    "  <String Name=\"DeviceLabel\">\n" \
    "    <Value>Mock GenTL</Value>\n" \
    "  </String>\n" \
    "  <IntReg Name=\"GainReg\">\n" \
    "    <Address>0x1000</Address>\n" \
    "    <Length>2</Length>\n" \
    "    <AccessMode>RW</AccessMode>\n" \
    "    <pPort>Device</pPort>\n" \
    "    <Sign>Unsigned</Sign>\n" \
    "    <Endianess>BigEndian</Endianess>\n" \
    "  </IntReg>\n" \
    "  <Boolean Name=\"FeatureEnabled\">\n" \
    "    <pValue>EnableReg</pValue>\n" \
    "    <OnValue>1</OnValue>\n" \
    "    <OffValue>0</OffValue>\n" \
    "  </Boolean>\n" \
    "  <IntReg Name=\"EnableReg\">\n" \
    "    <Address>0x1006</Address>\n" \
    "    <Length>2</Length>\n" \
    "    <AccessMode>RW</AccessMode>\n" \
    "    <pPort>Device</pPort>\n" \
    "    <Sign>Unsigned</Sign>\n" \
    "    <Endianess>BigEndian</Endianess>\n" \
    "  </IntReg>\n" \
    "  <Enumeration Name=\"TriggerMode\">\n" \
    "    <EnumEntry Name=\"Off\">\n" \
    "      <Value>0</Value>\n" \
    "      <Symbolic>Off</Symbolic>\n" \
    "    </EnumEntry>\n" \
    "    <EnumEntry Name=\"On\">\n" \
    "      <Value>1</Value>\n" \
    "      <Symbolic>On</Symbolic>\n" \
    "    </EnumEntry>\n" \
    "    <pValue>TriggerModeReg</pValue>\n" \
    "  </Enumeration>\n" \
    "  <IntReg Name=\"TriggerModeReg\">\n" \
    "    <Address>0x1004</Address>\n" \
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
    "  <Command Name=\"AcquisitionStop\">\n" \
    "    <pValue>StopCommandReg</pValue>\n" \
    "    <CommandValue>0</CommandValue>\n" \
    "  </Command>\n" \
    "  <IntReg Name=\"StopCommandReg\">\n" \
    "    <Address>0x1008</Address>\n" \
    "    <Length>2</Length>\n" \
    "    <AccessMode>RW</AccessMode>\n" \
    "    <pPort>Device</pPort>\n" \
    "    <Sign>Unsigned</Sign>\n" \
    "    <Endianess>BigEndian</Endianess>\n" \
    "  </IntReg>\n" \
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
static uint8_t* stream_memory[16];
static BUFFER_HANDLE stream_handles[16];
static size_t stream_buffer_count;
static BUFFER_HANDLE queued_buffers[16];
static size_t queued_buffer_count;
static atomic_int event_wait_entered = 0;
static atomic_int event_unregistered = 0;

int gentl_mock_event_wait_entered(void) {
    return atomic_load_explicit(&event_wait_entered, memory_order_acquire);
}

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
        if (fails_at_address("MOCK_GENTL_FAIL_PORT_READ_ADDRESS", arguments[1]))
            return GC_ERR_IO;
        if (arguments[1] == 0x1002 && *size == 2) {
            if (command_poll_count > 0)
                memset(register_memory + 0x1002, 0, 2);
            ++command_poll_count;
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
        if (fails_at_address("MOCK_GENTL_FAIL_PORT_WRITE_ADDRESS", address))
            return GC_ERR_IO;
        if (address == 0x1002 && *size == 2) command_poll_count = 0;
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
        atomic_store_explicit(&event_wait_entered, 0, memory_order_release);
        atomic_store_explicit(&event_unregistered, 0, memory_order_release);
        *(EVENT_HANDLE*)pointer(arguments[2]) = (EVENT_HANDLE)(uintptr_t)0x6666;
        return GC_ERR_SUCCESS;
    case 9: // GCUnregisterEvent
        if (argument_count != 2 || arguments[0] != 0x5555 ||
            arguments[1] != EVENT_NEW_BUFFER) return GC_ERR_INVALID_PARAMETER;
        if (getenv("MOCK_GENTL_FAIL_EVENT_UNREGISTER"))
            return GC_ERR_INVALID_PARAMETER;
        atomic_store_explicit(&event_unregistered, 1, memory_order_release);
        return GC_ERR_SUCCESS;
    case 10: { // EventGetData
        if (argument_count != 4 || arguments[0] != 0x6666)
            return GC_ERR_INVALID_HANDLE;
        size_t* size = (size_t*)pointer(arguments[2]);
        if (!size || !pointer(arguments[1])) return GC_ERR_INVALID_PARAMETER;
        if (arguments[3] > UINT64_C(1000000000)) {
            atomic_store_explicit(&event_wait_entered, 1, memory_order_release);
            while (!atomic_load_explicit(&event_unregistered, memory_order_acquire)) {
                /* Deterministic pending-wait fixture: unregister must abort this call. */
            }
            return GC_ERR_ABORT;
        }
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
        if (argument_count != 1 || arguments[0] != 0x6666)
            return GC_ERR_INVALID_HANDLE;
        atomic_store_explicit(&event_unregistered, 1, memory_order_release);
        return GC_ERR_SUCCESS;
    case 33: // DevOpenDataStream
        if (argument_count != 3 || arguments[0] != 0x3333 ||
            strcmp((const char*)pointer(arguments[1]), "stream0") != 0)
            return GC_ERR_INVALID_PARAMETER;
        *(DS_HANDLE*)pointer(arguments[2]) = (DS_HANDLE)(uintptr_t)0x5555;
        return GC_ERR_SUCCESS;
    case 37: { // DSAllocAndAnnounceBuffer
        if (argument_count != 4 || arguments[0] != 0x5555 ||
            arguments[1] != 64 || stream_buffer_count >= 16)
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
            queued_buffer_count >= 16) return GC_ERR_INVALID_PARAMETER;
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
        case BUFFER_INFO_PIXEL_ENDIANNESS: {
            const PIXELENDIANNESS_ID value = PIXELENDIANNESS_LITTLE;
            return write_scalar(type, output, size, INFO_DATATYPE_INT32, &value, sizeof(value));
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


// The same fixture also exports the standard GenTL C symbols. These wrappers
// route into the existing deterministic mock state machine so the native C++
// CTI loader can be tested without an intermediary bridge.
static GC_ERROR mock_genTL_call(
    uint32_t operation, const uint64_t* arguments, uint32_t argument_count) {
    return (GC_ERROR)gentl_consumer_invoke(
        &context_token, operation, arguments, argument_count);
}

#define MOCK_ENCODE(value) ((uint64_t)(uintptr_t)(value))
#define MOCK_CALL0(operation) \
    return mock_genTL_call((operation), 0, 0)
#define MOCK_CALL1(operation, a) do { \
    const uint64_t args[] = {MOCK_ENCODE(a)}; \
    return mock_genTL_call((operation), args, 1); \
} while (0)
#define MOCK_CALL2(operation, a, b) do { \
    const uint64_t args[] = {MOCK_ENCODE(a), MOCK_ENCODE(b)}; \
    return mock_genTL_call((operation), args, 2); \
} while (0)
#define MOCK_CALL3(operation, a, b, c) do { \
    const uint64_t args[] = {MOCK_ENCODE(a), MOCK_ENCODE(b), MOCK_ENCODE(c)}; \
    return mock_genTL_call((operation), args, 3); \
} while (0)
#define MOCK_CALL4(operation, a, b, c, d) do { \
    const uint64_t args[] = {MOCK_ENCODE(a), MOCK_ENCODE(b), MOCK_ENCODE(c), MOCK_ENCODE(d)}; \
    return mock_genTL_call((operation), args, 4); \
} while (0)
#define MOCK_CALL5(operation, a, b, c, d, e) do { \
    const uint64_t args[] = {MOCK_ENCODE(a), MOCK_ENCODE(b), MOCK_ENCODE(c), MOCK_ENCODE(d), MOCK_ENCODE(e)}; \
    return mock_genTL_call((operation), args, 5); \
} while (0)
#define MOCK_CALL6(operation, a, b, c, d, e, f) do { \
    const uint64_t args[] = {MOCK_ENCODE(a), MOCK_ENCODE(b), MOCK_ENCODE(c), MOCK_ENCODE(d), MOCK_ENCODE(e), MOCK_ENCODE(f)}; \
    return mock_genTL_call((operation), args, 6); \
} while (0)

GC_ERROR GC_CALLTYPE GCGetInfo(
    TL_INFO_CMD command, INFO_DATATYPE* type, void* buffer, size_t* size) {
    MOCK_CALL4(0, command, type, buffer, size);
}
GC_ERROR GC_CALLTYPE GCInitLib(void) { MOCK_CALL0(2); }
GC_ERROR GC_CALLTYPE GCCloseLib(void) { MOCK_CALL0(3); }
GC_ERROR GC_CALLTYPE GCReadPort(
    PORT_HANDLE port, uint64_t address, void* buffer, size_t* size) {
    MOCK_CALL4(4, port, address, buffer, size);
}
GC_ERROR GC_CALLTYPE GCWritePort(
    PORT_HANDLE port, uint64_t address, const void* buffer, size_t* size) {
    MOCK_CALL4(5, port, address, buffer, size);
}
GC_ERROR GC_CALLTYPE GCGetPortURL(
    PORT_HANDLE port, char* url, size_t* size) {
    MOCK_CALL3(6, port, url, size);
}
GC_ERROR GC_CALLTYPE GCRegisterEvent(
    EVENTSRC_HANDLE source, EVENT_TYPE event, EVENT_HANDLE* handle) {
    MOCK_CALL3(8, source, event, handle);
}
GC_ERROR GC_CALLTYPE GCUnregisterEvent(
    EVENTSRC_HANDLE source, EVENT_TYPE event) {
    MOCK_CALL2(9, source, event);
}
GC_ERROR GC_CALLTYPE EventGetData(
    EVENT_HANDLE event, void* buffer, size_t* size, uint64_t timeout) {
    MOCK_CALL4(10, event, buffer, size, timeout);
}
GC_ERROR GC_CALLTYPE EventKill(EVENT_HANDLE event) { MOCK_CALL1(14, event); }
GC_ERROR GC_CALLTYPE TLOpen(TL_HANDLE* handle) { MOCK_CALL1(15, handle); }
GC_ERROR GC_CALLTYPE TLClose(TL_HANDLE handle) { MOCK_CALL1(16, handle); }
GC_ERROR GC_CALLTYPE TLGetNumInterfaces(
    TL_HANDLE tl, uint32_t* count) { MOCK_CALL2(18, tl, count); }
GC_ERROR GC_CALLTYPE TLGetInterfaceID(
    TL_HANDLE tl, uint32_t index, char* id, size_t* size) {
    MOCK_CALL4(19, tl, index, id, size);
}
GC_ERROR GC_CALLTYPE TLOpenInterface(
    TL_HANDLE tl, const char* id, IF_HANDLE* handle) {
    MOCK_CALL3(21, tl, id, handle);
}
GC_ERROR GC_CALLTYPE TLUpdateInterfaceList(
    TL_HANDLE tl, bool8_t* changed, uint64_t timeout) {
    MOCK_CALL3(22, tl, changed, timeout);
}
GC_ERROR GC_CALLTYPE IFClose(IF_HANDLE handle) { MOCK_CALL1(23, handle); }
GC_ERROR GC_CALLTYPE IFGetInfo(
    IF_HANDLE iface, INTERFACE_INFO_CMD command, INFO_DATATYPE* type,
    void* buffer, size_t* size) {
    MOCK_CALL5(24, iface, command, type, buffer, size);
}
GC_ERROR GC_CALLTYPE IFGetNumDevices(
    IF_HANDLE iface, uint32_t* count) { MOCK_CALL2(25, iface, count); }
GC_ERROR GC_CALLTYPE IFGetDeviceID(
    IF_HANDLE iface, uint32_t index, char* id, size_t* size) {
    MOCK_CALL4(26, iface, index, id, size);
}
GC_ERROR GC_CALLTYPE IFUpdateDeviceList(
    IF_HANDLE iface, bool8_t* changed, uint64_t timeout) {
    MOCK_CALL3(27, iface, changed, timeout);
}
GC_ERROR GC_CALLTYPE IFGetDeviceInfo(
    IF_HANDLE iface, const char* id, DEVICE_INFO_CMD command,
    INFO_DATATYPE* type, void* buffer, size_t* size) {
    MOCK_CALL6(28, iface, id, command, type, buffer, size);
}
GC_ERROR GC_CALLTYPE IFOpenDevice(
    IF_HANDLE iface, const char* id, DEVICE_ACCESS_FLAGS access,
    DEV_HANDLE* handle) {
    MOCK_CALL4(29, iface, id, access, handle);
}
GC_ERROR GC_CALLTYPE DevGetPort(
    DEV_HANDLE device, PORT_HANDLE* port) { MOCK_CALL2(30, device, port); }
GC_ERROR GC_CALLTYPE DevGetNumDataStreams(
    DEV_HANDLE device, uint32_t* count) { MOCK_CALL2(31, device, count); }
GC_ERROR GC_CALLTYPE DevGetDataStreamID(
    DEV_HANDLE device, uint32_t index, char* id, size_t* size) {
    MOCK_CALL4(32, device, index, id, size);
}
GC_ERROR GC_CALLTYPE DevOpenDataStream(
    DEV_HANDLE device, const char* id, DS_HANDLE* stream) {
    MOCK_CALL3(33, device, id, stream);
}
GC_ERROR GC_CALLTYPE DevClose(DEV_HANDLE device) { MOCK_CALL1(35, device); }
GC_ERROR GC_CALLTYPE DSAllocAndAnnounceBuffer(
    DS_HANDLE stream, size_t size, void* privateData, BUFFER_HANDLE* buffer) {
    MOCK_CALL4(37, stream, size, privateData, buffer);
}
GC_ERROR GC_CALLTYPE DSFlushQueue(
    DS_HANDLE stream, ACQ_QUEUE_TYPE operation) {
    MOCK_CALL2(38, stream, operation);
}
GC_ERROR GC_CALLTYPE DSStartAcquisition(
    DS_HANDLE stream, ACQ_START_FLAGS flags, uint64_t count) {
    MOCK_CALL3(39, stream, flags, count);
}
GC_ERROR GC_CALLTYPE DSStopAcquisition(
    DS_HANDLE stream, ACQ_STOP_FLAGS flags) {
    MOCK_CALL2(40, stream, flags);
}
GC_ERROR GC_CALLTYPE DSGetInfo(
    DS_HANDLE stream, STREAM_INFO_CMD command, INFO_DATATYPE* type,
    void* buffer, size_t* size) {
    MOCK_CALL5(41, stream, command, type, buffer, size);
}
GC_ERROR GC_CALLTYPE DSClose(DS_HANDLE stream) { MOCK_CALL1(43, stream); }
GC_ERROR GC_CALLTYPE DSRevokeBuffer(
    DS_HANDLE stream, BUFFER_HANDLE buffer, void** memory, void** privateData) {
    MOCK_CALL4(44, stream, buffer, memory, privateData);
}
GC_ERROR GC_CALLTYPE DSQueueBuffer(
    DS_HANDLE stream, BUFFER_HANDLE buffer) {
    MOCK_CALL2(45, stream, buffer);
}
GC_ERROR GC_CALLTYPE DSGetBufferInfo(
    DS_HANDLE stream, BUFFER_HANDLE buffer, BUFFER_INFO_CMD command,
    INFO_DATATYPE* type, void* value, size_t* size) {
    MOCK_CALL6(46, stream, buffer, command, type, value, size);
}
GC_ERROR GC_CALLTYPE GCGetNumPortURLs(
    PORT_HANDLE port, uint32_t* count) { MOCK_CALL2(47, port, count); }
GC_ERROR GC_CALLTYPE GCGetPortURLInfo(
    PORT_HANDLE port, uint32_t index, URL_INFO_CMD command,
    INFO_DATATYPE* type, void* value, size_t* size) {
    MOCK_CALL6(48, port, index, command, type, value, size);
}

#undef MOCK_CALL0
#undef MOCK_CALL1
#undef MOCK_CALL2
#undef MOCK_CALL3
#undef MOCK_CALL4
#undef MOCK_CALL5
#undef MOCK_CALL6
#undef MOCK_ENCODE
