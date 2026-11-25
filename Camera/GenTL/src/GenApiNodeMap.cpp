#include "camera/gentl/GenApiNodeMap.h"

#include <Base/GCException.h>
#include <Base/GCString.h>

#include <limits>
#include <stdexcept>
#include <string>

namespace camera::gentl {

GenApiPort::GenApiPort(std::shared_ptr<DeviceSession> device) noexcept
    : device_(std::move(device)) {}

GenApi::EAccessMode GenApiPort::GetAccessMode() const {
    if (!device_ || !*device_) return GenApi::NA;
    return device_->accessFlags() == GenTL::DEVICE_ACCESS_READONLY
        ? GenApi::RO
        : GenApi::RW;
}

void GenApiPort::Read(
    void* buffer, std::int64_t address, std::int64_t length) {
    if (!device_ || !*device_) {
        throw RUNTIME_EXCEPTION("GenTL device port is not open.");
    }
    if (address < 0 || length < 0 ||
        static_cast<std::uint64_t>(length) >
            std::numeric_limits<std::size_t>::max() ||
        (length != 0 && !buffer)) {
        throw RUNTIME_EXCEPTION("Invalid GenApi port read range or destination.");
    }
    try {
        device_->readPortInto(
            static_cast<std::uint64_t>(address),
            buffer,
            static_cast<std::size_t>(length));
    } catch (const std::exception& error) {
        throw RUNTIME_EXCEPTION(error.what());
    }
}

void GenApiPort::Write(
    const void* buffer, std::int64_t address, std::int64_t length) {
    if (!device_ || !*device_) {
        throw RUNTIME_EXCEPTION("GenTL device port is not open.");
    }
    if (address < 0 || length < 0 ||
        static_cast<std::uint64_t>(length) >
            std::numeric_limits<std::size_t>::max() ||
        (length != 0 && !buffer)) {
        throw RUNTIME_EXCEPTION("Invalid GenApi port write range or source.");
    }
    try {
        device_->writePortFrom(
            static_cast<std::uint64_t>(address),
            buffer,
            static_cast<std::size_t>(length));
    } catch (const std::exception& error) {
        throw RUNTIME_EXCEPTION(error.what());
    }
}

GenApiNodeMap::GenApiNodeMap(std::shared_ptr<DeviceSession> device)
    : device_(std::move(device)),
      port_(device_) {
    if (!device_ || !*device_) {
        throw std::invalid_argument(
            "GenApi node-map construction requires an open GenTL device session.");
    }

    const auto xml = device_->genApiXml();
    const GenICam::gcstring xmlText(
        reinterpret_cast<const char*>(xml.data()), xml.size());

    nodeMap_._LoadXMLFromString(xmlText);
    if (!nodeMap_._Connect(&port_)) {
        nodeMap_._Destroy();
        throw RUNTIME_EXCEPTION("GenApi could not connect the device port.");
    }
}

} // namespace camera::gentl
