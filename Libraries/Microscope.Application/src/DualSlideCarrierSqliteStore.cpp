#include "microscope/application/persistence/DualSlideCarrierSqliteStore.h"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace microscope::application::persistence {
namespace {

using Error = DualSlideCarrierStoreError;
using ErrorCode = DualSlideCarrierStoreErrorCode;

constexpr std::uint32_t kSchemaVersion = 1;
constexpr std::uint32_t kPayloadFormatVersion = 1;
constexpr std::size_t kMaximumPayloadBytes = 64 * 1024;
constexpr std::size_t kMaximumStringBytes = 4096;

[[nodiscard]] Error makeError(
    ErrorCode code,
    sqlite3* database,
    std::string detail = {}) {
    return Error{
        code,
        database == nullptr ? 0 : sqlite3_errcode(database),
        detail.empty() && database != nullptr ? sqlite3_errmsg(database)
                                               : std::move(detail)};
}

[[nodiscard]] bool execute(sqlite3* database, const char* sql) {
    char* message = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &message);
    sqlite3_free(message);
    return result == SQLITE_OK;
}

class Statement final {
public:
    Statement(sqlite3* database, const char* sql) : database_(database) {
        prepared_ = sqlite3_prepare_v2(database, sql, -1, &statement_, nullptr) ==
                    SQLITE_OK;
    }

    ~Statement() {
        if (statement_ != nullptr) {
            sqlite3_finalize(statement_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    [[nodiscard]] bool prepared() const noexcept { return prepared_; }
    [[nodiscard]] sqlite3_stmt* get() const noexcept { return statement_; }
    [[nodiscard]] int step() { return sqlite3_step(statement_); }

private:
    sqlite3* database_ = nullptr;
    sqlite3_stmt* statement_ = nullptr;
    bool prepared_ = false;
};

class Transaction final {
public:
    explicit Transaction(sqlite3* database) : database_(database) {
        active_ = execute(database_, "BEGIN IMMEDIATE");
    }

    ~Transaction() {
        if (active_) {
            const bool rolledBack = execute(database_, "ROLLBACK");
            (void)rolledBack;
        }
    }

    [[nodiscard]] bool active() const noexcept { return active_; }

    [[nodiscard]] bool commit() {
        if (!active_ || !execute(database_, "COMMIT")) {
            return false;
        }
        active_ = false;
        return true;
    }

private:
    sqlite3* database_ = nullptr;
    bool active_ = false;
};

[[nodiscard]] bool bindText(
    sqlite3_stmt* statement,
    int index,
    const std::string& value) {
    return sqlite3_bind_text(
               statement,
               index,
               value.data(),
               static_cast<int>(value.size()),
               SQLITE_TRANSIENT) == SQLITE_OK;
}

[[nodiscard]] bool bindOptionalText(
    sqlite3_stmt* statement,
    int index,
    const std::optional<std::string>& value) {
    return value ? bindText(statement, index, *value)
                 : sqlite3_bind_null(statement, index) == SQLITE_OK;
}

[[nodiscard]] bool bindOptionalRevision(
    sqlite3_stmt* statement,
    int index,
    const std::optional<std::uint64_t>& value) {
    if (!value) {
        return sqlite3_bind_null(statement, index) == SQLITE_OK;
    }
    if (*value == 0 ||
        *value > static_cast<std::uint64_t>(
                     std::numeric_limits<sqlite3_int64>::max())) {
        return false;
    }
    return sqlite3_bind_int64(
               statement, index, static_cast<sqlite3_int64>(*value)) ==
           SQLITE_OK;
}

[[nodiscard]] std::optional<std::string> columnOptionalText(
    sqlite3_stmt* statement,
    int column,
    bool& valid) {
    if (sqlite3_column_type(statement, column) == SQLITE_NULL) {
        return std::nullopt;
    }
    const auto* value = sqlite3_column_text(statement, column);
    const int byteCount = sqlite3_column_bytes(statement, column);
    if (value == nullptr || byteCount <= 0 ||
        static_cast<std::size_t>(byteCount) > kMaximumStringBytes) {
        valid = false;
        return std::nullopt;
    }
    return std::string(
        reinterpret_cast<const char*>(value),
        static_cast<std::size_t>(byteCount));
}

[[nodiscard]] std::optional<std::uint64_t> columnOptionalRevision(
    sqlite3_stmt* statement,
    int column,
    bool& valid) {
    if (sqlite3_column_type(statement, column) == SQLITE_NULL) {
        return std::nullopt;
    }
    const sqlite3_int64 value = sqlite3_column_int64(statement, column);
    if (value <= 0) {
        valid = false;
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(value);
}

[[nodiscard]] bool readCurrentRevision(
    sqlite3* database,
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentRevision,
    const core::EquipmentComponentId& stageComponentId,
    std::optional<std::uint64_t>& revision) {
    Statement statement(
        database,
        "SELECT revision FROM dual_slide_carrier_current "
        "WHERE microscope_id = ?1 AND equipment_revision = ?2 "
        "AND stage_component_id = ?3");
    if (!statement.prepared() ||
        !bindText(statement.get(), 1, microscopeId.value()) ||
        sqlite3_bind_int64(
            statement.get(), 2, static_cast<sqlite3_int64>(equipmentRevision)) != SQLITE_OK ||
        !bindText(statement.get(), 3, stageComponentId.value())) {
        return false;
    }
    const int result = statement.step();
    if (result == SQLITE_DONE) {
        revision.reset();
        return true;
    }
    if (result != SQLITE_ROW) {
        return false;
    }
    const sqlite3_int64 value = sqlite3_column_int64(statement.get(), 0);
    if (value <= 0 || statement.step() != SQLITE_DONE) {
        return false;
    }
    revision = static_cast<std::uint64_t>(value);
    return true;
}

[[nodiscard]] bool readSlideCurrentRevision(
    sqlite3* database,
    const core::SlideId& slideId,
    std::optional<std::uint64_t>& revision) {
    Statement statement(
        database,
        "SELECT revision FROM slide_current WHERE slide_id = ?1");
    if (!statement.prepared() ||
        !bindText(statement.get(), 1, slideId.value())) {
        return false;
    }
    const int result = statement.step();
    if (result == SQLITE_DONE) {
        revision.reset();
        return true;
    }
    if (result != SQLITE_ROW) {
        return false;
    }
    const sqlite3_int64 value = sqlite3_column_int64(statement.get(), 0);
    if (value <= 0 || statement.step() != SQLITE_DONE) {
        return false;
    }
    revision = static_cast<std::uint64_t>(value);
    return true;
}

[[nodiscard]] bool nearlyEqual(double left, double right) noexcept {
    if (!std::isfinite(left) || !std::isfinite(right)) {
        return false;
    }
    const double scale = std::max({1.0, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= scale * 1e-9;
}

[[nodiscard]] const char* unitName(core::AxisCoordinateUnit value) {
    switch (value) {
    case core::AxisCoordinateUnit::Micrometers:
        return "micrometers";
    case core::AxisCoordinateUnit::Millimeters:
        return "millimeters";
    default:
        return nullptr;
    }
}

[[nodiscard]] const char* directionName(
    core::CarrierHorizontalDirection value) {
    switch (value) {
    case core::CarrierHorizontalDirection::PositiveStageXFromLeftToRight:
        return "positive-stage-x-left-to-right";
    case core::CarrierHorizontalDirection::NegativeStageXFromLeftToRight:
        return "negative-stage-x-left-to-right";
    }
    return nullptr;
}

[[nodiscard]] const char* slotName(core::DualSlideSlot value) {
    switch (value) {
    case core::DualSlideSlot::Left:
        return "left";
    case core::DualSlideSlot::Right:
        return "right";
    }
    return nullptr;
}

[[nodiscard]] const char* cornerName(core::SlideCorner value) {
    switch (value) {
    case core::SlideCorner::TopLeft:
        return "top-left";
    case core::SlideCorner::TopRight:
        return "top-right";
    case core::SlideCorner::BottomRight:
        return "bottom-right";
    case core::SlideCorner::BottomLeft:
        return "bottom-left";
    }
    return nullptr;
}

[[nodiscard]] const char* observationSourceName(
    core::ObservationSource value) {
    switch (value) {
    case core::ObservationSource::HardwareReadback:
        return "hardware-readback";
    case core::ObservationSource::DeviceEvent:
        return "device-event";
    case core::ObservationSource::SdkStatus:
        return "sdk-status";
    case core::ObservationSource::OperatorConfirmation:
        return "operator-confirmation";
    case core::ObservationSource::CalibratedEstimate:
        return "calibrated-estimate";
    case core::ObservationSource::AdapterReport:
    case core::ObservationSource::Unknown:
        return nullptr;
    }
    return nullptr;
}

[[nodiscard]] std::optional<core::AxisCoordinateUnit> parseUnit(
    const std::string& value) {
    if (value == "micrometers") return core::AxisCoordinateUnit::Micrometers;
    if (value == "millimeters") return core::AxisCoordinateUnit::Millimeters;
    return std::nullopt;
}

[[nodiscard]] std::optional<core::CarrierHorizontalDirection> parseDirection(
    const std::string& value) {
    if (value == "positive-stage-x-left-to-right") {
        return core::CarrierHorizontalDirection::PositiveStageXFromLeftToRight;
    }
    if (value == "negative-stage-x-left-to-right") {
        return core::CarrierHorizontalDirection::NegativeStageXFromLeftToRight;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<core::DualSlideSlot> parseSlot(
    const std::string& value) {
    if (value == "left") return core::DualSlideSlot::Left;
    if (value == "right") return core::DualSlideSlot::Right;
    return std::nullopt;
}

[[nodiscard]] std::optional<core::SlideCorner> parseCorner(
    const std::string& value) {
    if (value == "top-left") return core::SlideCorner::TopLeft;
    if (value == "top-right") return core::SlideCorner::TopRight;
    if (value == "bottom-right") return core::SlideCorner::BottomRight;
    if (value == "bottom-left") return core::SlideCorner::BottomLeft;
    return std::nullopt;
}

[[nodiscard]] std::optional<core::ObservationSource> parseObservationSource(
    const std::string& value) {
    if (value == "hardware-readback") return core::ObservationSource::HardwareReadback;
    if (value == "device-event") return core::ObservationSource::DeviceEvent;
    if (value == "sdk-status") return core::ObservationSource::SdkStatus;
    if (value == "operator-confirmation") return core::ObservationSource::OperatorConfirmation;
    if (value == "calibrated-estimate") return core::ObservationSource::CalibratedEstimate;
    return std::nullopt;
}

class PayloadWriter final {
public:
    [[nodiscard]] bool writeU8(std::uint8_t value) {
        bytes_.push_back(value);
        return withinLimit();
    }

    [[nodiscard]] bool writeU64(std::uint64_t value) {
        for (int index = 0; index < 8; ++index) {
            bytes_.push_back(static_cast<std::uint8_t>(
                (value >> (index * 8)) & 0xffu));
        }
        return withinLimit();
    }

    [[nodiscard]] bool writeDouble(double value) {
        if (!std::isfinite(value)) return false;
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&bits, &value, sizeof(bits));
        return writeU64(bits);
    }

    [[nodiscard]] bool writeString(const std::string& value) {
        if (value.empty() || value.size() > kMaximumStringBytes ||
            value.find('\0') != std::string::npos) {
            return false;
        }
        if (!writeU64(static_cast<std::uint64_t>(value.size()))) return false;
        bytes_.insert(bytes_.end(), value.begin(), value.end());
        return withinLimit();
    }

    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept {
        return bytes_;
    }

private:
    [[nodiscard]] bool withinLimit() const noexcept {
        return bytes_.size() <= kMaximumPayloadBytes;
    }
    std::vector<std::uint8_t> bytes_;
};

class PayloadReader final {
public:
    PayloadReader(const void* data, std::size_t size)
        : data_(static_cast<const std::uint8_t*>(data)), size_(size) {}

    [[nodiscard]] bool readU8(std::uint8_t& value) {
        if (offset_ >= size_) return false;
        value = data_[offset_++];
        return true;
    }

    [[nodiscard]] bool readU64(std::uint64_t& value) {
        if (size_ - offset_ < 8) return false;
        value = 0;
        for (int index = 0; index < 8; ++index) {
            value |= static_cast<std::uint64_t>(data_[offset_++]) <<
                     (index * 8);
        }
        return true;
    }

    [[nodiscard]] bool readDouble(double& value) {
        std::uint64_t bits = 0;
        if (!readU64(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return std::isfinite(value);
    }

    [[nodiscard]] bool readString(std::string& value) {
        std::uint64_t length = 0;
        if (!readU64(length) || length == 0 ||
            length > kMaximumStringBytes ||
            length > static_cast<std::uint64_t>(size_ - offset_)) {
            return false;
        }
        value.assign(
            reinterpret_cast<const char*>(data_ + offset_),
            static_cast<std::size_t>(length));
        offset_ += static_cast<std::size_t>(length);
        return value.find('\0') == std::string::npos;
    }

    [[nodiscard]] bool finished() const noexcept {
        return offset_ == size_;
    }

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t offset_ = 0;
};

[[nodiscard]] bool writeRegistration(
    PayloadWriter& writer,
    const core::SlideRegistration& registration) {
    const char* slot = slotName(registration.slot());
    if (slot == nullptr ||
        !writer.writeString(registration.slideId().value()) ||
        !writer.writeString(registration.revision().value()) ||
        !writer.writeString(slot) ||
        !writer.writeDouble(registration.geometry().widthMicrometers()) ||
        !writer.writeDouble(registration.geometry().heightMicrometers()) ||
        !writer.writeDouble(
            registration.acceptedMaximumFitResidualMicrometers())) {
        return false;
    }

    const auto& corners = registration.cornerMeasurements();
    if (!writer.writeU64(corners.size())) return false;
    for (const auto& corner : corners) {
        const char* cornerTag = cornerName(corner.corner());
        const char* sourceTag = observationSourceName(corner.source());
        if (cornerTag == nullptr || sourceTag == nullptr ||
            !writer.writeString(cornerTag) ||
            !writer.writeDouble(
                corner.stagePosition().horizontalMicrometers) ||
            !writer.writeDouble(
                corner.stagePosition().transverseMicrometers) ||
            !writer.writeString(sourceTag) ||
            !writer.writeDouble(corner.uncertaintyMicrometers())) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool writeSlot(
    PayloadWriter& writer,
    const core::SlideSlotState& slot,
    core::DualSlideSlot expectedSlot) {
    const char* slotTag = slotName(slot.slot());
    if (slot.slot() != expectedSlot || slotTag == nullptr ||
        !writer.writeString(slotTag)) {
        return false;
    }

    if (!writer.writeU8(slot.slideId() ? 1u : 0u)) return false;
    if (slot.slideId() && !writer.writeString(slot.slideId()->value())) {
        return false;
    }

    if (!writer.writeU8(slot.registration() ? 1u : 0u)) return false;
    if (slot.registration() &&
        !writeRegistration(writer, *slot.registration())) {
        return false;
    }
    return true;
}

[[nodiscard]] std::optional<std::vector<std::uint8_t>> encodeCarrier(
    const core::DualSlideCarrierState& state) {
    PayloadWriter writer;
    const auto& specification = state.specification();
    const char* direction = directionName(specification.horizontalDirection());
    const char* horizontalUnit = unitName(specification.horizontalUnit());
    const char* transverseUnit = unitName(specification.transverseUnit());
    if (direction == nullptr || horizontalUnit == nullptr ||
        transverseUnit == nullptr ||
        !writer.writeString(specification.stageComponentId().value()) ||
        !writer.writeString(specification.horizontalAxisId().value()) ||
        !writer.writeString(specification.transverseAxisId().value()) ||
        !writer.writeString(direction) ||
        !writer.writeString(horizontalUnit) ||
        !writer.writeString(transverseUnit) ||
        !writeSlot(writer, state.left(), core::DualSlideSlot::Left) ||
        !writeSlot(writer, state.right(), core::DualSlideSlot::Right)) {
        return std::nullopt;
    }
    return writer.bytes();
}

template <typename TId>
[[nodiscard]] bool readStrongId(
    PayloadReader& reader,
    TId& id) {
    std::string value;
    if (!reader.readString(value)) return false;
    auto created = TId::create(std::move(value));
    if (!created) return false;
    id = std::move(created).value();
    return true;
}

[[nodiscard]] bool readRegistration(
    PayloadReader& reader,
    core::DualSlideSlot expectedSlot,
    const core::SlideId& expectedSlideId,
    core::SlideRegistration& registration) {
    core::SlideId slideId = expectedSlideId;
    core::CalibrationRevisionId revision =
        core::CalibrationRevisionId::create("invalid-placeholder").value();
    std::string slotTag;
    double width = 0.0;
    double height = 0.0;
    double maximumFitResidual = 0.0;
    if (!readStrongId(reader, slideId) ||
        !readStrongId(reader, revision) ||
        !reader.readString(slotTag) ||
        !reader.readDouble(width) ||
        !reader.readDouble(height) ||
        !reader.readDouble(maximumFitResidual)) {
        return false;
    }

    const auto decodedSlot = parseSlot(slotTag);
    if (!decodedSlot || *decodedSlot != expectedSlot ||
        slideId != expectedSlideId) {
        return false;
    }

    auto geometry = core::SlideGeometry::create(width, height);
    if (!geometry) return false;

    std::uint64_t cornerCount = 0;
    if (!reader.readU64(cornerCount) || cornerCount != 4) return false;

    std::array<std::optional<core::SlideCornerMeasurement>, 4> measured;
    for (std::size_t index = 0; index < 4; ++index) {
        std::string cornerTag;
        std::string sourceTag;
        core::StagePlanePoint point{};
        double uncertainty = 0.0;
        if (!reader.readString(cornerTag) ||
            !reader.readDouble(point.horizontalMicrometers) ||
            !reader.readDouble(point.transverseMicrometers) ||
            !reader.readString(sourceTag) ||
            !reader.readDouble(uncertainty)) {
            return false;
        }
        const auto corner = parseCorner(cornerTag);
        const auto source = parseObservationSource(sourceTag);
        if (!corner || !source) return false;
        std::size_t cornerIndex = 4;
        switch (*corner) {
        case core::SlideCorner::TopLeft: cornerIndex = 0; break;
        case core::SlideCorner::TopRight: cornerIndex = 1; break;
        case core::SlideCorner::BottomRight: cornerIndex = 2; break;
        case core::SlideCorner::BottomLeft: cornerIndex = 3; break;
        }
        if (cornerIndex >= measured.size() || measured[cornerIndex]) {
            return false;
        }
        auto measurement = core::SlideCornerMeasurement::create(
            *corner, point, *source, uncertainty);
        if (!measurement) return false;
        measured[cornerIndex] = std::move(measurement).value();
    }

    if (std::any_of(
            measured.begin(), measured.end(),
            [](const auto& item) { return !item.has_value(); })) {
        return false;
    }

    std::array<core::SlideCornerMeasurement, 4> corners{
        *measured[0], *measured[1], *measured[2], *measured[3]};
    auto created = core::SlideRegistration::create(
        std::move(slideId),
        std::move(revision),
        expectedSlot,
        std::move(geometry).value(),
        std::move(corners),
        maximumFitResidual);
    if (!created) return false;
    registration = std::move(created).value();
    return true;
}

[[nodiscard]] bool readSlot(
    PayloadReader& reader,
    core::DualSlideSlot expectedSlot,
    core::SlideSlotState& slot) {
    std::string slotTag;
    if (!reader.readString(slotTag)) return false;
    const auto decodedSlot = parseSlot(slotTag);
    if (!decodedSlot || *decodedSlot != expectedSlot) return false;

    std::uint8_t hasSlide = 0;
    if (!reader.readU8(hasSlide) || hasSlide > 1) return false;
    std::optional<core::SlideId> slideId;
    if (hasSlide != 0) {
        core::SlideId id =
            core::SlideId::create("invalid-placeholder").value();
        if (!readStrongId(reader, id)) return false;
        slideId = std::move(id);
    }

    std::uint8_t hasRegistration = 0;
    if (!reader.readU8(hasRegistration) || hasRegistration > 1) return false;
    std::optional<core::SlideRegistration> registration;
    if (hasRegistration != 0) {
        if (!slideId) return false;
        core::SlideRegistration decoded =
            core::SlideRegistration::create(
                core::SlideId::create("temporary").value(),
                core::CalibrationRevisionId::create("temporary").value(),
                expectedSlot,
                core::SlideGeometry::create(1.0, 1.0).value(),
                std::array<core::SlideCornerMeasurement, 4>{
                    core::SlideCornerMeasurement::create(
                        core::SlideCorner::TopLeft, {0.0, 0.0},
                        core::ObservationSource::HardwareReadback, 0.0).value(),
                    core::SlideCornerMeasurement::create(
                        core::SlideCorner::TopRight, {1.0, 0.0},
                        core::ObservationSource::HardwareReadback, 0.0).value(),
                    core::SlideCornerMeasurement::create(
                        core::SlideCorner::BottomRight, {1.0, 1.0},
                        core::ObservationSource::HardwareReadback, 0.0).value(),
                    core::SlideCornerMeasurement::create(
                        core::SlideCorner::BottomLeft, {0.0, 1.0},
                        core::ObservationSource::HardwareReadback, 0.0).value()},
                0.0).value();
        if (!readRegistration(reader, expectedSlot, *slideId, decoded)) {
            return false;
        }
        registration = std::move(decoded);
    }

    auto created = core::SlideSlotState::create(
        expectedSlot, std::move(slideId), std::move(registration));
    if (!created) return false;
    slot = std::move(created).value();
    return true;
}

[[nodiscard]] bool specMatches(
    const core::DualSlideCarrierSpecification& expected,
    const core::EquipmentComponentId& stageComponentId,
    const core::StageAxisId& horizontalAxisId,
    const core::StageAxisId& transverseAxisId,
    core::CarrierHorizontalDirection direction,
    core::AxisCoordinateUnit horizontalUnit,
    core::AxisCoordinateUnit transverseUnit) {
    return expected.stageComponentId() == stageComponentId &&
           expected.horizontalAxisId() == horizontalAxisId &&
           expected.transverseAxisId() == transverseAxisId &&
           expected.horizontalDirection() == direction &&
           expected.horizontalUnit() == horizontalUnit &&
           expected.transverseUnit() == transverseUnit;
}

[[nodiscard]] core::Result<
    core::DualSlideCarrierState,
    DualSlideCarrierStoreError> decodeCarrier(
        const void* data,
        std::size_t size,
        const core::DualSlideCarrierSpecification& installedSpecification,
        sqlite3* database) {
    PayloadReader reader(data, size);
    core::EquipmentComponentId stageComponentId =
        core::EquipmentComponentId::create("invalid-placeholder").value();
    core::StageAxisId horizontalAxisId =
        core::StageAxisId::create("invalid-placeholder").value();
    core::StageAxisId transverseAxisId =
        core::StageAxisId::create("invalid-placeholder").value();
    std::string directionTag;
    std::string horizontalUnitTag;
    std::string transverseUnitTag;
    if (!readStrongId(reader, stageComponentId) ||
        !readStrongId(reader, horizontalAxisId) ||
        !readStrongId(reader, transverseAxisId) ||
        !reader.readString(directionTag) ||
        !reader.readString(horizontalUnitTag) ||
        !reader.readString(transverseUnitTag)) {
        return core::Result<
            core::DualSlideCarrierState,
            DualSlideCarrierStoreError>::failure(makeError(
                ErrorCode::InvalidPersistedRecord,
                database,
                "The stored dual-slide carrier specification payload is malformed."));
    }

    const auto direction = parseDirection(directionTag);
    const auto horizontalUnit = parseUnit(horizontalUnitTag);
    const auto transverseUnit = parseUnit(transverseUnitTag);
    if (!direction || !horizontalUnit || !transverseUnit ||
        !specMatches(
            installedSpecification,
            stageComponentId,
            horizontalAxisId,
            transverseAxisId,
            *direction,
            *horizontalUnit,
            *transverseUnit)) {
        return core::Result<
            core::DualSlideCarrierState,
            DualSlideCarrierStoreError>::failure(makeError(
                ErrorCode::EquipmentBindingMismatch,
                database,
                "The persisted carrier registration belongs to a different installed stage/axis configuration."));
    }

    core::SlideSlotState left = core::SlideSlotState::create(
        core::DualSlideSlot::Left, std::nullopt).value();
    core::SlideSlotState right = core::SlideSlotState::create(
        core::DualSlideSlot::Right, std::nullopt).value();
    if (!readSlot(reader, core::DualSlideSlot::Left, left) ||
        !readSlot(reader, core::DualSlideSlot::Right, right) ||
        !reader.finished()) {
        return core::Result<
            core::DualSlideCarrierState,
            DualSlideCarrierStoreError>::failure(makeError(
                ErrorCode::InvalidPersistedRecord,
                database,
                "The stored dual-slide slot or four-corner registration payload is malformed."));
    }

    auto created = core::DualSlideCarrierState::create(
        installedSpecification, std::move(left), std::move(right));
    if (!created) {
        return core::Result<
            core::DualSlideCarrierState,
            DualSlideCarrierStoreError>::failure(makeError(
                ErrorCode::InvalidPersistedRecord,
                database,
                "The stored left/right carrier state violates the Core carrier contract."));
    }
    return core::Result<
        core::DualSlideCarrierState,
        DualSlideCarrierStoreError>::success(std::move(created).value());
}

[[nodiscard]] bool schemaTableExists(sqlite3* database, const char* name) {
    Statement statement(
        database,
        "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?1");
    if (!statement.prepared() || !bindText(statement.get(), 1, name)) {
        return false;
    }
    return statement.step() == SQLITE_ROW;
}

} // namespace

DualSlideCarrierSqliteStore::DualSlideCarrierSqliteStore(
    sqlite3* database) noexcept
    : database_(database) {}

DualSlideCarrierSqliteStore::~DualSlideCarrierSqliteStore() {
    if (database_ != nullptr) {
        sqlite3_close_v2(database_);
    }
}

core::Result<
    std::unique_ptr<DualSlideCarrierSqliteStore>,
    DualSlideCarrierStoreError>
DualSlideCarrierSqliteStore::open(const std::string& databasePath) {
    if (databasePath.empty()) {
        return core::Result<
            std::unique_ptr<DualSlideCarrierSqliteStore>,
            DualSlideCarrierStoreError>::failure(
                DualSlideCarrierStoreError{
                    ErrorCode::EmptyDatabasePath,
                    SQLITE_MISUSE,
                    "The dual-slide carrier store database path is empty."});
    }

    sqlite3* database = nullptr;
    const int opened = sqlite3_open_v2(
        databasePath.c_str(),
        &database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (opened != SQLITE_OK) {
        auto error = makeError(
            ErrorCode::DatabaseOpenFailed,
            database,
            "Could not open the dual-slide carrier SQLite database.");
        if (database != nullptr) sqlite3_close_v2(database);
        return core::Result<
            std::unique_ptr<DualSlideCarrierSqliteStore>,
            DualSlideCarrierStoreError>::failure(std::move(error));
    }

    sqlite3_busy_timeout(database, 5000);
    if (!execute(database, "PRAGMA foreign_keys = ON")) {
        auto error = makeError(
            ErrorCode::DatabaseOpenFailed,
            database,
            "Could not enable SQLite foreign-key enforcement.");
        sqlite3_close_v2(database);
        return core::Result<
            std::unique_ptr<DualSlideCarrierSqliteStore>,
            DualSlideCarrierStoreError>::failure(std::move(error));
    }

    if (!schemaTableExists(database, "slide_revision")) {
        auto error = makeError(
            ErrorCode::SampleSlideStoreSchemaRequired,
            database,
            "Open SampleSlideSqliteStore on this database before opening the dual-slide carrier store.");
        sqlite3_close_v2(database);
        return core::Result<
            std::unique_ptr<DualSlideCarrierSqliteStore>,
            DualSlideCarrierStoreError>::failure(std::move(error));
    }

    auto store = std::unique_ptr<DualSlideCarrierSqliteStore>(
        new DualSlideCarrierSqliteStore(database));
    const auto initialized = store->initializeSchema();
    if (!initialized) {
        return core::Result<
            std::unique_ptr<DualSlideCarrierSqliteStore>,
            DualSlideCarrierStoreError>::failure(initialized.error());
    }
    return core::Result<
        std::unique_ptr<DualSlideCarrierSqliteStore>,
        DualSlideCarrierStoreError>::success(std::move(store));
}

core::Status<DualSlideCarrierStoreError>
DualSlideCarrierSqliteStore::initializeSchema() {
    if (!execute(
            database_,
            "CREATE TABLE IF NOT EXISTS dual_slide_carrier_store_metadata ("
            "singleton INTEGER PRIMARY KEY CHECK (singleton = 1), "
            "schema_version INTEGER NOT NULL)")) {
        return core::Status<DualSlideCarrierStoreError>::failure(makeError(
            ErrorCode::SchemaMigrationFailed, database_));
    }

    Statement insertMetadata(
        database_,
        "INSERT OR IGNORE INTO dual_slide_carrier_store_metadata "
        "(singleton, schema_version) VALUES (1, 1)");
    if (!insertMetadata.prepared() ||
        insertMetadata.step() != SQLITE_DONE) {
        return core::Status<DualSlideCarrierStoreError>::failure(makeError(
            ErrorCode::SchemaMigrationFailed, database_));
    }

    Statement readMetadata(
        database_,
        "SELECT schema_version FROM dual_slide_carrier_store_metadata "
        "WHERE singleton = 1");
    if (!readMetadata.prepared() || readMetadata.step() != SQLITE_ROW) {
        return core::Status<DualSlideCarrierStoreError>::failure(makeError(
            ErrorCode::SchemaMigrationFailed, database_));
    }
    if (sqlite3_column_int64(readMetadata.get(), 0) != kSchemaVersion ||
        readMetadata.step() != SQLITE_DONE) {
        return core::Status<DualSlideCarrierStoreError>::failure(makeError(
            ErrorCode::UnsupportedSchemaVersion, database_));
    }

    const char* schema = R"SQL(
        CREATE TABLE IF NOT EXISTS dual_slide_carrier_revision (
            microscope_id TEXT NOT NULL,
            equipment_revision INTEGER NOT NULL CHECK (equipment_revision > 0),
            stage_component_id TEXT NOT NULL,
            revision INTEGER NOT NULL CHECK (revision > 0),
            left_slide_id TEXT,
            left_slide_revision INTEGER,
            right_slide_id TEXT,
            right_slide_revision INTEGER,
            payload_format_version INTEGER NOT NULL,
            payload BLOB NOT NULL,
            PRIMARY KEY (
                microscope_id,
                equipment_revision,
                stage_component_id,
                revision
            ),
            CHECK (
                (left_slide_id IS NULL AND left_slide_revision IS NULL) OR
                (left_slide_id IS NOT NULL AND left_slide_revision IS NOT NULL AND left_slide_revision > 0)
            ),
            CHECK (
                (right_slide_id IS NULL AND right_slide_revision IS NULL) OR
                (right_slide_id IS NOT NULL AND right_slide_revision IS NOT NULL AND right_slide_revision > 0)
            ),
            CHECK (left_slide_id IS NULL OR right_slide_id IS NULL OR left_slide_id <> right_slide_id),
            FOREIGN KEY (left_slide_id, left_slide_revision)
                REFERENCES slide_revision(slide_id, revision),
            FOREIGN KEY (right_slide_id, right_slide_revision)
                REFERENCES slide_revision(slide_id, revision)
        );

        CREATE TABLE IF NOT EXISTS dual_slide_carrier_current (
            microscope_id TEXT NOT NULL,
            equipment_revision INTEGER NOT NULL CHECK (equipment_revision > 0),
            stage_component_id TEXT NOT NULL,
            revision INTEGER NOT NULL CHECK (revision > 0),
            PRIMARY KEY (
                microscope_id,
                equipment_revision,
                stage_component_id
            ),
            FOREIGN KEY (
                microscope_id,
                equipment_revision,
                stage_component_id,
                revision
            ) REFERENCES dual_slide_carrier_revision(
                microscope_id,
                equipment_revision,
                stage_component_id,
                revision
            )
        );

        CREATE TRIGGER IF NOT EXISTS dual_slide_carrier_revision_sequential_insert
        BEFORE INSERT ON dual_slide_carrier_revision
        WHEN NEW.revision != COALESCE((
            SELECT revision FROM dual_slide_carrier_current
            WHERE microscope_id = NEW.microscope_id
              AND equipment_revision = NEW.equipment_revision
              AND stage_component_id = NEW.stage_component_id
        ), 0) + 1
        BEGIN SELECT RAISE(ABORT, 'Carrier revisions must be appended sequentially'); END;

        CREATE TRIGGER IF NOT EXISTS dual_slide_carrier_revision_no_update
        BEFORE UPDATE ON dual_slide_carrier_revision
        BEGIN SELECT RAISE(ABORT, 'Carrier revisions are immutable'); END;

        CREATE TRIGGER IF NOT EXISTS dual_slide_carrier_revision_no_delete
        BEFORE DELETE ON dual_slide_carrier_revision
        BEGIN SELECT RAISE(ABORT, 'Carrier revisions are immutable'); END;

        CREATE TRIGGER IF NOT EXISTS dual_slide_carrier_current_initial_revision
        BEFORE INSERT ON dual_slide_carrier_current
        WHEN NEW.revision != 1
        BEGIN SELECT RAISE(ABORT, 'Initial carrier revision must be one'); END;

        CREATE TRIGGER IF NOT EXISTS dual_slide_carrier_current_monotonic
        BEFORE UPDATE ON dual_slide_carrier_current
        WHEN NEW.revision != OLD.revision + 1
        BEGIN SELECT RAISE(ABORT, 'Carrier current revision must advance by one'); END;

        CREATE TRIGGER IF NOT EXISTS dual_slide_carrier_current_no_delete
        BEFORE DELETE ON dual_slide_carrier_current
        BEGIN SELECT RAISE(ABORT, 'Carrier current pointer cannot be deleted'); END;
    )SQL";

    if (!execute(database_, schema)) {
        return core::Status<DualSlideCarrierStoreError>::failure(makeError(
            ErrorCode::SchemaMigrationFailed, database_));
    }
    return core::Status<DualSlideCarrierStoreError>::success();
}

core::Status<DualSlideCarrierStoreError>
DualSlideCarrierSqliteStore::saveCurrentState(
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentDefinitionRevision,
    const core::DualSlideCarrierState& state,
    const std::vector<core::Slide>& currentSlides,
    std::uint64_t expectedCurrentRevision) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (equipmentDefinitionRevision == 0 ||
        equipmentDefinitionRevision > static_cast<std::uint64_t>(
                                          std::numeric_limits<sqlite3_int64>::max()) ||
        expectedCurrentRevision > static_cast<std::uint64_t>(
                                      std::numeric_limits<sqlite3_int64>::max()) ||
        expectedCurrentRevision == static_cast<std::uint64_t>(
                                       std::numeric_limits<sqlite3_int64>::max())) {
        return core::Status<DualSlideCarrierStoreError>::failure(makeError(
            ErrorCode::InvalidRevision, database_));
    }

    Transaction transaction(database_);
    if (!transaction.active()) {
        return core::Status<DualSlideCarrierStoreError>::failure(makeError(
            ErrorCode::SqliteOperationFailed, database_));
    }

    const auto& specification = state.specification();
    std::optional<std::uint64_t> actualCurrentRevision;
    if (!readCurrentRevision(
            database_,
            microscopeId,
            equipmentDefinitionRevision,
            specification.stageComponentId(),
            actualCurrentRevision)) {
        return core::Status<DualSlideCarrierStoreError>::failure(makeError(
            ErrorCode::SqliteOperationFailed, database_));
    }

    const std::uint64_t actual = actualCurrentRevision.value_or(0);
    if (actual != expectedCurrentRevision) {
        return core::Status<DualSlideCarrierStoreError>::failure(makeError(
            ErrorCode::RevisionConflict,
            database_,
            "The dual-slide carrier revision changed since it was loaded."));
    }

    const auto resolveSlotSlide = [&](
        const core::SlideSlotState& slot,
        std::optional<std::uint64_t>& slideRevision)
        -> std::optional<Error> {
        if (!slot.slideId()) {
            if (slot.registration()) {
                return makeError(
                    ErrorCode::InvalidPersistedRecord,
                    database_,
                    "An empty physical carrier slot cannot contain a Slide registration.");
            }
            slideRevision.reset();
            return std::nullopt;
        }

        const core::Slide* selectedSlide = nullptr;
        for (const auto& slide : currentSlides) {
            if (slide.id() != *slot.slideId()) continue;
            if (selectedSlide != nullptr) {
                return makeError(
                    ErrorCode::InvalidPersistedRecord,
                    database_,
                    "The current Slide list contains a duplicate SlideId.");
            }
            selectedSlide = &slide;
        }
        if (selectedSlide == nullptr) {
            return makeError(
                ErrorCode::SlideNotPersisted,
                database_,
                "Every occupied carrier slot must resolve to a current persisted Slide.");
        }

        std::optional<std::uint64_t> persistedCurrentSlideRevision;
        if (!readSlideCurrentRevision(
                database_, *slot.slideId(), persistedCurrentSlideRevision)) {
            return makeError(ErrorCode::SqliteOperationFailed, database_);
        }
        if (!persistedCurrentSlideRevision) {
            return makeError(
                ErrorCode::SlideNotPersisted,
                database_,
                "The carrier references a Slide that is absent from SampleSlideSqliteStore.");
        }
        if (*persistedCurrentSlideRevision != selectedSlide->revision()) {
            return makeError(
                ErrorCode::SlideRevisionMismatch,
                database_,
                "The supplied Slide is not its current persisted revision.");
        }

        if (slot.registration()) {
            const auto& registeredGeometry = slot.registration()->geometry();
            if (!nearlyEqual(
                    registeredGeometry.widthMicrometers(),
                    selectedSlide->geometry().widthMicrometers()) ||
                !nearlyEqual(
                    registeredGeometry.heightMicrometers(),
                    selectedSlide->geometry().heightMicrometers())) {
                return makeError(
                    ErrorCode::SlideGeometryMismatch,
                    database_,
                    "The four-corner registration geometry must match the physical Slide geometry.");
            }
        }

        slideRevision = selectedSlide->revision();
        return std::nullopt;
    };

    std::optional<std::uint64_t> leftSlideRevision;
    std::optional<std::uint64_t> rightSlideRevision;
    if (const auto error = resolveSlotSlide(state.left(), leftSlideRevision)) {
        return core::Status<DualSlideCarrierStoreError>::failure(*error);
    }
    if (const auto error = resolveSlotSlide(state.right(), rightSlideRevision)) {
        return core::Status<DualSlideCarrierStoreError>::failure(*error);
    }

    const auto payload = encodeCarrier(state);
    if (!payload || payload->size() > kMaximumPayloadBytes) {
        return core::Status<DualSlideCarrierStoreError>::failure(makeError(
            ErrorCode::PayloadTooLarge, database_));
    }

    const std::uint64_t nextRevision = expectedCurrentRevision + 1;
    Statement insertRevision(
        database_,
        "INSERT INTO dual_slide_carrier_revision ("
        "microscope_id, equipment_revision, stage_component_id, revision, "
        "left_slide_id, left_slide_revision, right_slide_id, right_slide_revision, "
        "payload_format_version, payload) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)");
    if (!insertRevision.prepared() ||
        !bindText(insertRevision.get(), 1, microscopeId.value()) ||
        sqlite3_bind_int64(
            insertRevision.get(), 2,
            static_cast<sqlite3_int64>(equipmentDefinitionRevision)) != SQLITE_OK ||
        !bindText(insertRevision.get(), 3, specification.stageComponentId().value()) ||
        sqlite3_bind_int64(
            insertRevision.get(), 4,
            static_cast<sqlite3_int64>(nextRevision)) != SQLITE_OK ||
        !bindOptionalText(
            insertRevision.get(), 5,
            state.left().slideId()
                ? std::optional<std::string>(state.left().slideId()->value())
                : std::nullopt) ||
        !bindOptionalRevision(insertRevision.get(), 6, leftSlideRevision) ||
        !bindOptionalText(
            insertRevision.get(), 7,
            state.right().slideId()
                ? std::optional<std::string>(state.right().slideId()->value())
                : std::nullopt) ||
        !bindOptionalRevision(insertRevision.get(), 8, rightSlideRevision) ||
        sqlite3_bind_int64(
            insertRevision.get(), 9, kPayloadFormatVersion) != SQLITE_OK ||
        sqlite3_bind_blob(
            insertRevision.get(),
            10,
            payload->data(),
            static_cast<int>(payload->size()),
            SQLITE_TRANSIENT) != SQLITE_OK ||
        insertRevision.step() != SQLITE_DONE) {
        return core::Status<DualSlideCarrierStoreError>::failure(makeError(
            ErrorCode::SqliteOperationFailed, database_));
    }

    if (actualCurrentRevision) {
        Statement updateCurrent(
            database_,
            "UPDATE dual_slide_carrier_current SET revision = ?1 "
            "WHERE microscope_id = ?2 AND equipment_revision = ?3 "
            "AND stage_component_id = ?4 AND revision = ?5");
        if (!updateCurrent.prepared() ||
            sqlite3_bind_int64(
                updateCurrent.get(), 1,
                static_cast<sqlite3_int64>(nextRevision)) != SQLITE_OK ||
            !bindText(updateCurrent.get(), 2, microscopeId.value()) ||
            sqlite3_bind_int64(
                updateCurrent.get(), 3,
                static_cast<sqlite3_int64>(equipmentDefinitionRevision)) != SQLITE_OK ||
            !bindText(updateCurrent.get(), 4, specification.stageComponentId().value()) ||
            sqlite3_bind_int64(
                updateCurrent.get(), 5,
                static_cast<sqlite3_int64>(expectedCurrentRevision)) != SQLITE_OK ||
            updateCurrent.step() != SQLITE_DONE ||
            sqlite3_changes(database_) != 1) {
            return core::Status<DualSlideCarrierStoreError>::failure(makeError(
                ErrorCode::SqliteOperationFailed, database_));
        }
    } else {
        Statement insertCurrent(
            database_,
            "INSERT INTO dual_slide_carrier_current ("
            "microscope_id, equipment_revision, stage_component_id, revision) "
            "VALUES (?1, ?2, ?3, ?4)");
        if (!insertCurrent.prepared() ||
            !bindText(insertCurrent.get(), 1, microscopeId.value()) ||
            sqlite3_bind_int64(
                insertCurrent.get(), 2,
                static_cast<sqlite3_int64>(equipmentDefinitionRevision)) != SQLITE_OK ||
            !bindText(insertCurrent.get(), 3, specification.stageComponentId().value()) ||
            sqlite3_bind_int64(
                insertCurrent.get(), 4,
                static_cast<sqlite3_int64>(nextRevision)) != SQLITE_OK ||
            insertCurrent.step() != SQLITE_DONE) {
            return core::Status<DualSlideCarrierStoreError>::failure(makeError(
                ErrorCode::SqliteOperationFailed, database_));
        }
    }

    if (!transaction.commit()) {
        return core::Status<DualSlideCarrierStoreError>::failure(makeError(
            ErrorCode::SqliteOperationFailed, database_));
    }
    return core::Status<DualSlideCarrierStoreError>::success();
}

core::Result<
    std::optional<PersistedDualSlideCarrierState>,
    DualSlideCarrierStoreError>
DualSlideCarrierSqliteStore::loadCurrentState(
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentDefinitionRevision,
    const core::DualSlideCarrierSpecification& installedSpecification) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (equipmentDefinitionRevision == 0 ||
        equipmentDefinitionRevision > static_cast<std::uint64_t>(
                                          std::numeric_limits<sqlite3_int64>::max())) {
        return core::Result<
            std::optional<PersistedDualSlideCarrierState>,
            DualSlideCarrierStoreError>::failure(makeError(
            ErrorCode::InvalidRevision, database_));
    }
    std::optional<std::uint64_t> current;
    if (!readCurrentRevision(
            database_,
            microscopeId,
            equipmentDefinitionRevision,
            installedSpecification.stageComponentId(),
            current)) {
        return core::Result<
            std::optional<PersistedDualSlideCarrierState>,
            DualSlideCarrierStoreError>::failure(makeError(
                ErrorCode::SqliteOperationFailed, database_));
    }
    if (!current) {
        return core::Result<
            std::optional<PersistedDualSlideCarrierState>,
            DualSlideCarrierStoreError>::success(std::nullopt);
    }
    return loadRevisionUnlocked(
        microscopeId,
        equipmentDefinitionRevision,
        installedSpecification,
        *current);
}

core::Result<
    std::optional<PersistedDualSlideCarrierState>,
    DualSlideCarrierStoreError>
DualSlideCarrierSqliteStore::loadRevision(
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentDefinitionRevision,
    const core::DualSlideCarrierSpecification& installedSpecification,
    std::uint64_t revision) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (equipmentDefinitionRevision == 0 || revision == 0 ||
        equipmentDefinitionRevision > static_cast<std::uint64_t>(
                                          std::numeric_limits<sqlite3_int64>::max()) ||
        revision > static_cast<std::uint64_t>(
                       std::numeric_limits<sqlite3_int64>::max())) {
        return core::Result<
            std::optional<PersistedDualSlideCarrierState>,
            DualSlideCarrierStoreError>::failure(makeError(
                ErrorCode::InvalidRevision, database_));
    }
    return loadRevisionUnlocked(
        microscopeId,
        equipmentDefinitionRevision,
        installedSpecification,
        revision);
}

core::Result<
    std::optional<PersistedDualSlideCarrierState>,
    DualSlideCarrierStoreError>
DualSlideCarrierSqliteStore::loadRevisionUnlocked(
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentDefinitionRevision,
    const core::DualSlideCarrierSpecification& installedSpecification,
    std::uint64_t revision) {
    Statement statement(
        database_,
        "SELECT left_slide_id, left_slide_revision, right_slide_id, "
        "right_slide_revision, payload_format_version, payload "
        "FROM dual_slide_carrier_revision "
        "WHERE microscope_id = ?1 AND equipment_revision = ?2 "
        "AND stage_component_id = ?3 AND revision = ?4");
    if (!statement.prepared() ||
        !bindText(statement.get(), 1, microscopeId.value()) ||
        sqlite3_bind_int64(
            statement.get(), 2,
            static_cast<sqlite3_int64>(equipmentDefinitionRevision)) != SQLITE_OK ||
        !bindText(statement.get(), 3, installedSpecification.stageComponentId().value()) ||
        sqlite3_bind_int64(
            statement.get(), 4, static_cast<sqlite3_int64>(revision)) != SQLITE_OK) {
        return core::Result<
            std::optional<PersistedDualSlideCarrierState>,
            DualSlideCarrierStoreError>::failure(makeError(
                ErrorCode::SqliteOperationFailed, database_));
    }

    const int step = statement.step();
    if (step == SQLITE_DONE) {
        return core::Result<
            std::optional<PersistedDualSlideCarrierState>,
            DualSlideCarrierStoreError>::success(std::nullopt);
    }
    if (step != SQLITE_ROW) {
        return core::Result<
            std::optional<PersistedDualSlideCarrierState>,
            DualSlideCarrierStoreError>::failure(makeError(
                ErrorCode::SqliteOperationFailed, database_));
    }

    bool validColumns = true;
    const auto leftId = columnOptionalText(statement.get(), 0, validColumns);
    const auto leftRevision = columnOptionalRevision(statement.get(), 1, validColumns);
    const auto rightId = columnOptionalText(statement.get(), 2, validColumns);
    const auto rightRevision = columnOptionalRevision(statement.get(), 3, validColumns);
    const sqlite3_int64 format = sqlite3_column_int64(statement.get(), 4);
    const auto* blob = sqlite3_column_blob(statement.get(), 5);
    const int blobBytes = sqlite3_column_bytes(statement.get(), 5);
    if (!validColumns ||
        leftId.has_value() != leftRevision.has_value() ||
        rightId.has_value() != rightRevision.has_value() ||
        (leftId && rightId && *leftId == *rightId) ||
        format != kPayloadFormatVersion ||
        blob == nullptr || blobBytes <= 0 ||
        static_cast<std::size_t>(blobBytes) > kMaximumPayloadBytes) {
        return core::Result<
            std::optional<PersistedDualSlideCarrierState>,
            DualSlideCarrierStoreError>::failure(makeError(
                ErrorCode::InvalidPersistedRecord,
                database_,
                "The stored carrier record columns, payload version or length are invalid."));
    }

    // SQLite column pointers are statement-owned and become invalid at the next
    // step/reset/finalize. Own the bounded payload before advancing to DONE.
    std::vector<std::uint8_t> payload(
        static_cast<std::size_t>(blobBytes));
    std::memcpy(payload.data(), blob, payload.size());
    if (statement.step() != SQLITE_DONE) {
        return core::Result<
            std::optional<PersistedDualSlideCarrierState>,
            DualSlideCarrierStoreError>::failure(makeError(
                ErrorCode::InvalidPersistedRecord,
                database_,
                "The carrier revision query returned an unexpected additional row or SQLite error."));
    }

    auto decoded = decodeCarrier(
        payload.data(),
        payload.size(),
        installedSpecification,
        database_);
    if (!decoded) {
        return core::Result<
            std::optional<PersistedDualSlideCarrierState>,
            DualSlideCarrierStoreError>::failure(decoded.error());
    }

    const auto& leftSlideId = decoded.value().left().slideId();
    const auto& rightSlideId = decoded.value().right().slideId();
    if (leftSlideId.has_value() != leftId.has_value() ||
        rightSlideId.has_value() != rightId.has_value() ||
        (leftSlideId && leftId && leftSlideId->value() != *leftId) ||
        (rightSlideId && rightId && rightSlideId->value() != *rightId)) {
        return core::Result<
            std::optional<PersistedDualSlideCarrierState>,
            DualSlideCarrierStoreError>::failure(makeError(
                ErrorCode::InvalidPersistedRecord,
                database_,
                "The carrier payload slot IDs do not match its referential-integrity columns."));
    }

    return core::Result<
        std::optional<PersistedDualSlideCarrierState>,
        DualSlideCarrierStoreError>::success(
            PersistedDualSlideCarrierState{
                revision,
                std::move(decoded).value(),
                leftRevision,
                rightRevision});
}

} // namespace microscope::application::persistence
