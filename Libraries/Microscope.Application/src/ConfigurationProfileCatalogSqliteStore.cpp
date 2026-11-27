#include "microscope/application/persistence/ConfigurationProfileCatalogSqliteStore.h"

#include <sqlite3.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace microscope::application::persistence {
namespace {

using Error = ConfigurationProfileCatalogStoreError;
using ErrorCode = ConfigurationProfileCatalogStoreErrorCode;

constexpr sqlite3_int64 kSchemaVersion = 1;
constexpr std::uint32_t kPayloadFormatVersion = 1;
constexpr std::uint32_t kPayloadMagic = 0x5043464dU; // "MFCP" in little endian
constexpr std::size_t kMaximumPayloadBytes = 4U * 1024U * 1024U;
constexpr std::uint32_t kMaximumCatalogProfiles = 4096;
constexpr std::uint32_t kMaximumCollectionItems = 16384;
constexpr std::uint32_t kMaximumStringBytes = 4096;
constexpr std::uint64_t kMaximumSqliteInteger =
    static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max());

Error makeError(
    ErrorCode code,
    sqlite3* database,
    std::string detail = {}) {
    if (detail.empty() && database != nullptr) detail = sqlite3_errmsg(database);
    return Error{code, database == nullptr ? 0 : sqlite3_errcode(database),
        std::move(detail)};
}

bool execute(sqlite3* database, const char* sql) {
    char* message = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &message);
    sqlite3_free(message);
    return result == SQLITE_OK;
}

class Statement final {
public:
    Statement(sqlite3* database, const char* sql) : database_(database) {
        prepared_ = sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr) == SQLITE_OK;
    }

    ~Statement() {
        if (statement_ != nullptr) sqlite3_finalize(statement_);
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    bool prepared() const noexcept { return prepared_; }
    sqlite3_stmt* get() const noexcept { return statement_; }
    int step() { return sqlite3_step(statement_); }

    bool bindText(int index, const std::string& value) {
        if (value.empty() || value.find('\0') != std::string::npos ||
            value.size() > kMaximumStringBytes ||
            value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        return sqlite3_bind_text(statement_, index, value.data(),
            static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
    }

    bool bindInt64(int index, std::uint64_t value) {
        if (value > kMaximumSqliteInteger) return false;
        return sqlite3_bind_int64(statement_, index,
            static_cast<sqlite3_int64>(value)) == SQLITE_OK;
    }

    bool bindInt(int index, int value) {
        return sqlite3_bind_int(statement_, index, value) == SQLITE_OK;
    }

    bool bindBlob(int index, const std::vector<std::uint8_t>& value) {
        if (value.empty() || value.size() > kMaximumPayloadBytes ||
            value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return false;
        }
        return sqlite3_bind_blob(statement_, index, value.data(),
            static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
    }

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

    bool active() const noexcept { return active_; }

    bool commit() {
        if (!active_ || !execute(database_, "COMMIT")) return false;
        active_ = false;
        return true;
    }

private:
    sqlite3* database_ = nullptr;
    bool active_ = false;
};

// Canonical little-endian codec. No native object representation or enum ABI
// is written to storage. Every enum receives an explicit stable tag below.
class BinaryWriter final {
public:
    void u8(std::uint8_t value) {
        if (bytes_.size() >= kMaximumPayloadBytes) {
            valid_ = false;
            return;
        }
        bytes_.push_back(value);
    }

    void u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }

    void u64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }

    void real(double value) {
        static_assert(sizeof(double) == sizeof(std::uint64_t),
            "The profile codec requires 64-bit doubles.");
        static_assert(std::numeric_limits<double>::is_iec559,
            "The profile codec requires IEEE-754 doubles.");
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u64(bits);
    }

    void string(const std::string& value) {
        if (value.empty() || value.size() > kMaximumStringBytes ||
            value.find('\0') != std::string::npos ||
            value.size() > std::numeric_limits<std::uint32_t>::max() ||
            bytes_.size() > kMaximumPayloadBytes - 4U ||
            value.size() > kMaximumPayloadBytes - bytes_.size() - 4U) {
            valid_ = false;
            return;
        }
        u32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
        if (bytes_.size() > kMaximumPayloadBytes) valid_ = false;
    }

    void invalidate() noexcept { valid_ = false; }
    bool valid() const noexcept { return valid_ && bytes_.size() <= kMaximumPayloadBytes; }
    const std::vector<std::uint8_t>& bytes() const noexcept { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
    bool valid_ = true;
};

class BinaryReader final {
public:
    BinaryReader(const void* data, std::size_t size)
        : data_(static_cast<const std::uint8_t*>(data)), size_(size) {}

    bool u8(std::uint8_t& value) {
        if (remaining() < 1) return false;
        value = data_[offset_++];
        return true;
    }

    bool u32(std::uint32_t& value) {
        if (remaining() < 4) return false;
        value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            value |= static_cast<std::uint32_t>(data_[offset_++]) << shift;
        }
        return true;
    }

    bool u64(std::uint64_t& value) {
        if (remaining() < 8) return false;
        value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8) {
            value |= static_cast<std::uint64_t>(data_[offset_++]) << shift;
        }
        return true;
    }

    bool real(double& value) {
        std::uint64_t bits = 0;
        if (!u64(bits)) return false;
        std::memcpy(&value, &bits, sizeof(value));
        return std::isfinite(value);
    }

    bool string(std::string& value) {
        std::uint32_t count = 0;
        if (!u32(count) || count == 0 || count > kMaximumStringBytes ||
            remaining() < count) return false;
        const auto* first = reinterpret_cast<const char*>(data_ + offset_);
        value.assign(first, count);
        offset_ += count;
        return value.find('\0') == std::string::npos;
    }

    bool finished() const noexcept { return offset_ == size_; }

private:
    std::size_t remaining() const noexcept {
        return offset_ <= size_ ? size_ - offset_ : 0;
    }

    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t offset_ = 0;
};

int contrastTag(core::ContrastMethod value) {
    switch (value) {
    case core::ContrastMethod::Brightfield: return 0;
    case core::ContrastMethod::PhaseContrast: return 1;
    case core::ContrastMethod::DifferentialInterferenceContrast: return 2;
    case core::ContrastMethod::PolarizedLight: return 3;
    case core::ContrastMethod::Fluorescence: return 4;
    case core::ContrastMethod::Darkfield: return 5;
    case core::ContrastMethod::Other: return 6;
    case core::ContrastMethod::Unknown: return -1;
    }
    return -1;
}

std::optional<core::ContrastMethod> parseContrast(std::uint8_t tag) {
    switch (tag) {
    case 0: return core::ContrastMethod::Brightfield;
    case 1: return core::ContrastMethod::PhaseContrast;
    case 2: return core::ContrastMethod::DifferentialInterferenceContrast;
    case 3: return core::ContrastMethod::PolarizedLight;
    case 4: return core::ContrastMethod::Fluorescence;
    case 5: return core::ContrastMethod::Darkfield;
    case 6: return core::ContrastMethod::Other;
    default: return std::nullopt;
    }
}

int opticalFunctionTag(core::OpticalElementFunction value) {
    switch (value) {
    case core::OpticalElementFunction::BeamSplitter: return 0;
    case core::OpticalElementFunction::PhasePlate: return 1;
    case core::OpticalElementFunction::PhaseAnnulus: return 2;
    case core::OpticalElementFunction::Polarizer: return 3;
    case core::OpticalElementFunction::Analyzer: return 4;
    case core::OpticalElementFunction::DifferentialInterferencePrism: return 5;
    case core::OpticalElementFunction::ExcitationFilter: return 6;
    case core::OpticalElementFunction::EmissionFilter: return 7;
    case core::OpticalElementFunction::DichroicMirror: return 8;
    case core::OpticalElementFunction::NeutralDensityFilter: return 9;
    case core::OpticalElementFunction::DarkfieldStop: return 10;
    case core::OpticalElementFunction::Other: return 11;
    }
    return -1;
}

std::optional<core::OpticalElementFunction> parseOpticalFunction(std::uint8_t tag) {
    switch (tag) {
    case 0: return core::OpticalElementFunction::BeamSplitter;
    case 1: return core::OpticalElementFunction::PhasePlate;
    case 2: return core::OpticalElementFunction::PhaseAnnulus;
    case 3: return core::OpticalElementFunction::Polarizer;
    case 4: return core::OpticalElementFunction::Analyzer;
    case 5: return core::OpticalElementFunction::DifferentialInterferencePrism;
    case 6: return core::OpticalElementFunction::ExcitationFilter;
    case 7: return core::OpticalElementFunction::EmissionFilter;
    case 8: return core::OpticalElementFunction::DichroicMirror;
    case 9: return core::OpticalElementFunction::NeutralDensityFilter;
    case 10: return core::OpticalElementFunction::DarkfieldStop;
    case 11: return core::OpticalElementFunction::Other;
    default: return std::nullopt;
    }
}

int pathRoleTag(core::OpticalPathRole value) {
    switch (value) {
    case core::OpticalPathRole::Imaging: return 0;
    case core::OpticalPathRole::Illumination: return 1;
    case core::OpticalPathRole::Other: return 2;
    }
    return -1;
}

std::optional<core::OpticalPathRole> parsePathRole(std::uint8_t tag) {
    switch (tag) {
    case 0: return core::OpticalPathRole::Imaging;
    case 1: return core::OpticalPathRole::Illumination;
    case 2: return core::OpticalPathRole::Other;
    default: return std::nullopt;
    }
}

int intensityUnitTag(core::IlluminationIntensityUnit value) {
    switch (value) {
    case core::IlluminationIntensityUnit::Percent: return 0;
    case core::IlluminationIntensityUnit::Milliamperes: return 1;
    case core::IlluminationIntensityUnit::Milliwatts: return 2;
    case core::IlluminationIntensityUnit::Relative: return 3;
    case core::IlluminationIntensityUnit::DeviceDefined: return 4;
    }
    return -1;
}

std::optional<core::IlluminationIntensityUnit> parseIntensityUnit(std::uint8_t tag) {
    switch (tag) {
    case 0: return core::IlluminationIntensityUnit::Percent;
    case 1: return core::IlluminationIntensityUnit::Milliamperes;
    case 2: return core::IlluminationIntensityUnit::Milliwatts;
    case 3: return core::IlluminationIntensityUnit::Relative;
    case 4: return core::IlluminationIntensityUnit::DeviceDefined;
    default: return std::nullopt;
    }
}

template <typename Id>
void writeId(BinaryWriter& writer, const Id& value) {
    writer.string(value.value());
}

template <typename Id>
bool readId(BinaryReader& reader, std::optional<Id>& value) {
    std::string raw;
    if (!reader.string(raw)) return false;
    auto created = Id::create(std::move(raw));
    if (!created) return false;
    value = std::move(created).value();
    return true;
}

template <typename Id>
void writeIdVector(BinaryWriter& writer, const std::vector<Id>& values) {
    if (values.size() > kMaximumCollectionItems) {
        writer.invalidate();
        return;
    }
    writer.u32(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) {
        if (!writer.valid()) break;
        writeId(writer, value);
    }
}

template <typename Id>
bool readIdVector(BinaryReader& reader, std::vector<Id>& values) {
    std::uint32_t count = 0;
    if (!reader.u32(count) || count > kMaximumCollectionItems) return false;
    values.clear();
    values.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::optional<Id> id;
        if (!readId(reader, id) || !id) return false;
        values.push_back(std::move(*id));
    }
    return true;
}

void writeProfile(BinaryWriter& writer, const core::ConfigurationProfile& profile) {
    writeId(writer, profile.id());
    const int contrast = contrastTag(profile.contrastMethod());
    writer.u8(contrast < 0 ? 0xffU : static_cast<std::uint8_t>(contrast));
    writeIdVector(writer, profile.compatibleObjectiveIds());
    writeIdVector(writer, profile.compatibleCondenserIds());
    writeIdVector(writer, profile.compatibleOpticalPathIds());
    writeIdVector(writer, profile.compatibleCameraIds());
    writeIdVector(writer, profile.compatibleIlluminationIds());
    writeIdVector(writer, profile.requiredPathComponentIds());

    const auto& requirements = profile.requiredOpticalFunctions();
    if (requirements.size() > kMaximumCollectionItems) {
        writer.invalidate();
    } else {
        writer.u32(static_cast<std::uint32_t>(requirements.size()));
        for (const auto& requirement : requirements) {
            if (!writer.valid()) break;
            const int function = opticalFunctionTag(requirement.function());
            writer.u8(function < 0 ? 0xffU : static_cast<std::uint8_t>(function));
            writer.u8(requirement.compatibilityId() ? 1U : 0U);
            if (requirement.compatibilityId()) writeId(writer, *requirement.compatibilityId());
            writer.u32(requirement.minimumComponentCount());
            writer.u8(requirement.requiredPathRole() ? 1U : 0U);
            if (requirement.requiredPathRole()) {
                const int role = pathRoleTag(*requirement.requiredPathRole());
                writer.u8(role < 0 ? 0xffU : static_cast<std::uint8_t>(role));
            }
        }
    }

    writer.u8(profile.condenserApertureTarget() ? 1U : 0U);
    if (profile.condenserApertureTarget()) writer.real(profile.condenserApertureTarget()->value());

    writer.u8(profile.illuminationIntensityTarget() ? 1U : 0U);
    if (profile.illuminationIntensityTarget()) {
        writer.real(profile.illuminationIntensityTarget()->value());
        const int unit = intensityUnitTag(profile.illuminationIntensityTarget()->unit());
        writer.u8(unit < 0 ? 0xffU : static_cast<std::uint8_t>(unit));
    }

    writer.u8(profile.exposurePreset() ? 1U : 0U);
    if (profile.exposurePreset()) writer.real(profile.exposurePreset()->exposureTimeMicroseconds());
}

bool readProfile(BinaryReader& reader, std::optional<core::ConfigurationProfile>& output) {
    std::optional<core::ConfigurationProfileId> id;
    std::uint8_t contrastTagValue = 0;
    if (!readId(reader, id) || !id || !reader.u8(contrastTagValue)) return false;
    const auto contrast = parseContrast(contrastTagValue);
    if (!contrast) return false;

    std::vector<core::EquipmentComponentId> objectives, condensers, cameras,
        illuminations, requiredComponents;
    std::vector<core::OpticalPathId> opticalPaths;
    if (!readIdVector(reader, objectives) ||
        !readIdVector(reader, condensers) ||
        !readIdVector(reader, opticalPaths) ||
        !readIdVector(reader, cameras) ||
        !readIdVector(reader, illuminations) ||
        !readIdVector(reader, requiredComponents)) return false;

    std::uint32_t requirementCount = 0;
    if (!reader.u32(requirementCount) || requirementCount > kMaximumCollectionItems) return false;
    std::vector<core::OpticalFunctionRequirement> requirements;
    requirements.reserve(requirementCount);
    for (std::uint32_t i = 0; i < requirementCount; ++i) {
        std::uint8_t functionTagValue = 0, hasCompatibility = 0;
        std::uint32_t minimumCount = 0;
        std::uint8_t hasPathRole = 0;
        if (!reader.u8(functionTagValue) || !reader.u8(hasCompatibility) ||
            hasCompatibility > 1U) return false;
        const auto function = parseOpticalFunction(functionTagValue);
        if (!function) return false;

        std::optional<core::OpticalCompatibilityId> compatibility;
        if (hasCompatibility != 0U &&
            (!readId(reader, compatibility) || !compatibility)) return false;
        if (!reader.u32(minimumCount) || !reader.u8(hasPathRole) ||
            hasPathRole > 1U) return false;

        std::optional<core::OpticalPathRole> pathRole;
        if (hasPathRole != 0U) {
            std::uint8_t roleTagValue = 0;
            if (!reader.u8(roleTagValue)) return false;
            pathRole = parsePathRole(roleTagValue);
            if (!pathRole) return false;
        }

        auto created = core::OpticalFunctionRequirement::create(
            *function, std::move(compatibility), minimumCount, pathRole);
        if (!created) return false;
        requirements.push_back(std::move(created).value());
    }

    std::uint8_t hasAperture = 0;
    if (!reader.u8(hasAperture) || hasAperture > 1U) return false;
    std::optional<core::NonNegativeNumericalAperture> aperture;
    if (hasAperture != 0U) {
        double value = 0.0;
        if (!reader.real(value)) return false;
        auto created = core::NonNegativeNumericalAperture::create(value);
        if (!created) return false;
        aperture = std::move(created).value();
    }

    std::uint8_t hasIntensity = 0;
    if (!reader.u8(hasIntensity) || hasIntensity > 1U) return false;
    std::optional<core::IlluminationIntensitySetting> intensity;
    if (hasIntensity != 0U) {
        double value = 0.0;
        std::uint8_t unitTagValue = 0;
        if (!reader.real(value) || !reader.u8(unitTagValue)) return false;
        const auto unit = parseIntensityUnit(unitTagValue);
        if (!unit) return false;
        auto created = core::IlluminationIntensitySetting::create(value, *unit);
        if (!created) return false;
        intensity = std::move(created).value();
    }

    std::uint8_t hasExposure = 0;
    if (!reader.u8(hasExposure) || hasExposure > 1U) return false;
    std::optional<core::ExposurePreset> exposure;
    if (hasExposure != 0U) {
        double value = 0.0;
        if (!reader.real(value)) return false;
        auto positive = core::PositiveMicroseconds::create(value);
        if (!positive) return false;
        exposure = core::ExposurePreset::create(std::move(positive).value());
    }

    auto created = core::ConfigurationProfile::create(
        std::move(*id), *contrast,
        std::move(objectives), std::move(condensers), std::move(opticalPaths),
        std::move(cameras), std::move(illuminations),
        std::move(requiredComponents), std::move(requirements),
        std::move(aperture), std::move(intensity), std::move(exposure));
    if (!created) return false;
    output = std::move(created).value();
    return true;
}

bool encodeCatalog(
    const core::ConfigurationProfileCatalog& catalog,
    std::vector<std::uint8_t>& payload) {
    if (catalog.profiles().empty() ||
        catalog.profiles().size() > kMaximumCatalogProfiles) return false;
    BinaryWriter writer;
    writer.u32(kPayloadMagic);
    writer.u32(static_cast<std::uint32_t>(catalog.profiles().size()));
    for (const auto& profile : catalog.profiles()) {
        if (!writer.valid()) break;
        const auto withinLimits = [](const auto& values) {
            return values.size() <= kMaximumCollectionItems;
        };
        if (!withinLimits(profile.compatibleObjectiveIds()) ||
            !withinLimits(profile.compatibleCondenserIds()) ||
            !withinLimits(profile.compatibleOpticalPathIds()) ||
            !withinLimits(profile.compatibleCameraIds()) ||
            !withinLimits(profile.compatibleIlluminationIds()) ||
            !withinLimits(profile.requiredPathComponentIds()) ||
            !withinLimits(profile.requiredOpticalFunctions())) {
            return false;
        }
        writeProfile(writer, profile);
    }
    if (!writer.valid()) return false;
    payload = writer.bytes();
    return !payload.empty() && payload.size() <= kMaximumPayloadBytes;
}

std::optional<core::ConfigurationProfileCatalog> decodeCatalog(
    const void* data,
    std::size_t size) {
    if (data == nullptr || size == 0 || size > kMaximumPayloadBytes) {
        return std::nullopt;
    }
    BinaryReader reader(data, size);
    std::uint32_t magic = 0, count = 0;
    if (!reader.u32(magic) || magic != kPayloadMagic ||
        !reader.u32(count) || count == 0 || count > kMaximumCatalogProfiles) {
        return std::nullopt;
    }

    std::vector<core::ConfigurationProfile> profiles;
    profiles.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::optional<core::ConfigurationProfile> profile;
        if (!readProfile(reader, profile) || !profile) return std::nullopt;
        profiles.push_back(std::move(*profile));
    }
    if (!reader.finished()) return std::nullopt;
    auto created = core::ConfigurationProfileCatalog::create(std::move(profiles));
    if (!created) return std::nullopt;
    return std::move(created).value();
}

bool validRevision(std::uint64_t value) noexcept {
    return value > 0 && value <= kMaximumSqliteInteger;
}

bool readCurrentRevision(
    sqlite3* database,
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentRevision,
    std::optional<std::uint64_t>& revision) {
    Statement statement(database,
        "SELECT revision FROM configuration_profile_catalog_current "
        "WHERE microscope_id = ?1 AND equipment_revision = ?2");
    if (!statement.prepared() ||
        !statement.bindText(1, microscopeId.value()) ||
        !statement.bindInt64(2, equipmentRevision)) return false;
    const int step = statement.step();
    if (step == SQLITE_DONE) {
        revision.reset();
        return true;
    }
    if (step != SQLITE_ROW) return false;
    if (sqlite3_column_type(statement.get(), 0) != SQLITE_INTEGER) return false;
    const sqlite3_int64 value = sqlite3_column_int64(statement.get(), 0);
    if (value <= 0 || statement.step() != SQLITE_DONE) return false;
    revision = static_cast<std::uint64_t>(value);
    return true;
}

} // namespace

ConfigurationProfileCatalogSqliteStore::ConfigurationProfileCatalogSqliteStore(
    sqlite3* database) noexcept : database_(database) {}

ConfigurationProfileCatalogSqliteStore::~ConfigurationProfileCatalogSqliteStore() {
    if (database_ != nullptr) sqlite3_close_v2(database_);
}

core::Result<std::unique_ptr<ConfigurationProfileCatalogSqliteStore>, Error>
ConfigurationProfileCatalogSqliteStore::open(const std::string& databasePath) {
    if (databasePath.empty()) {
        return core::Result<std::unique_ptr<ConfigurationProfileCatalogSqliteStore>, Error>::failure(
            Error{ErrorCode::EmptyDatabasePath, SQLITE_MISUSE,
                "The configuration-profile catalog database path is empty."});
    }
    sqlite3* database = nullptr;
    const int opened = sqlite3_open_v2(databasePath.c_str(), &database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (opened != SQLITE_OK) {
        auto error = makeError(ErrorCode::DatabaseOpenFailed, database,
            "Could not open the configuration-profile catalog database.");
        if (database != nullptr) sqlite3_close_v2(database);
        return core::Result<std::unique_ptr<ConfigurationProfileCatalogSqliteStore>, Error>::failure(
            std::move(error));
    }

    sqlite3_busy_timeout(database, 5000);
    if (!execute(database, "PRAGMA foreign_keys = ON")) {
        auto error = makeError(ErrorCode::DatabaseOpenFailed, database,
            "Could not enable SQLite foreign-key enforcement.");
        sqlite3_close_v2(database);
        return core::Result<std::unique_ptr<ConfigurationProfileCatalogSqliteStore>, Error>::failure(
            std::move(error));
    }

    auto store = std::unique_ptr<ConfigurationProfileCatalogSqliteStore>(
        new ConfigurationProfileCatalogSqliteStore(database));
    const auto initialized = store->initializeSchema();
    if (!initialized) {
        return core::Result<std::unique_ptr<ConfigurationProfileCatalogSqliteStore>, Error>::failure(
            initialized.error());
    }
    return core::Result<std::unique_ptr<ConfigurationProfileCatalogSqliteStore>, Error>::success(
        std::move(store));
}

core::Status<Error> ConfigurationProfileCatalogSqliteStore::initializeSchema() {
    const char* schema = R"SQL(
        CREATE TABLE IF NOT EXISTS configuration_profile_catalog_metadata (
            singleton INTEGER PRIMARY KEY CHECK (singleton = 1),
            schema_version INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS configuration_profile_catalog_revision (
            microscope_id TEXT NOT NULL,
            equipment_revision INTEGER NOT NULL CHECK (equipment_revision > 0),
            revision INTEGER NOT NULL CHECK (revision > 0),
            payload_format_version INTEGER NOT NULL,
            payload BLOB NOT NULL,
            PRIMARY KEY (microscope_id, equipment_revision, revision)
        );

        CREATE TABLE IF NOT EXISTS configuration_profile_catalog_current (
            microscope_id TEXT NOT NULL,
            equipment_revision INTEGER NOT NULL,
            revision INTEGER NOT NULL CHECK (revision > 0),
            PRIMARY KEY (microscope_id, equipment_revision),
            FOREIGN KEY (microscope_id, equipment_revision, revision)
                REFERENCES configuration_profile_catalog_revision (
                    microscope_id, equipment_revision, revision
                )
        );

        CREATE TRIGGER IF NOT EXISTS configuration_profile_catalog_revision_sequential_insert
        BEFORE INSERT ON configuration_profile_catalog_revision
        WHEN NEW.revision != COALESCE((
            SELECT revision FROM configuration_profile_catalog_current
            WHERE microscope_id = NEW.microscope_id
              AND equipment_revision = NEW.equipment_revision
        ), 0) + 1
        BEGIN SELECT RAISE(ABORT, 'Catalog revisions must be appended sequentially'); END;

        CREATE TRIGGER IF NOT EXISTS configuration_profile_catalog_revision_no_update
        BEFORE UPDATE ON configuration_profile_catalog_revision
        BEGIN SELECT RAISE(ABORT, 'Catalog revisions are immutable'); END;

        CREATE TRIGGER IF NOT EXISTS configuration_profile_catalog_revision_no_delete
        BEFORE DELETE ON configuration_profile_catalog_revision
        BEGIN SELECT RAISE(ABORT, 'Catalog revisions are immutable'); END;

        CREATE TRIGGER IF NOT EXISTS configuration_profile_catalog_current_initial
        BEFORE INSERT ON configuration_profile_catalog_current
        WHEN NEW.revision != 1
        BEGIN SELECT RAISE(ABORT, 'Initial catalog revision must be one'); END;

        CREATE TRIGGER IF NOT EXISTS configuration_profile_catalog_current_monotonic
        BEFORE UPDATE ON configuration_profile_catalog_current
        WHEN NEW.revision != OLD.revision + 1
        BEGIN SELECT RAISE(ABORT, 'Current catalog revision must advance by one'); END;

        CREATE TRIGGER IF NOT EXISTS configuration_profile_catalog_current_no_delete
        BEFORE DELETE ON configuration_profile_catalog_current
        BEGIN SELECT RAISE(ABORT, 'Catalog current pointer cannot be deleted'); END;
    )SQL";

    if (!execute(database_, schema)) {
        return core::Status<Error>::failure(
            makeError(ErrorCode::SchemaMigrationFailed, database_));
    }

    Statement insertMetadata(database_,
        "INSERT OR IGNORE INTO configuration_profile_catalog_metadata "
        "(singleton, schema_version) VALUES (1, 1)");
    if (!insertMetadata.prepared() || insertMetadata.step() != SQLITE_DONE) {
        return core::Status<Error>::failure(
            makeError(ErrorCode::SchemaMigrationFailed, database_));
    }
    Statement readMetadata(database_,
        "SELECT schema_version FROM configuration_profile_catalog_metadata WHERE singleton = 1");
    if (!readMetadata.prepared() || readMetadata.step() != SQLITE_ROW ||
        sqlite3_column_type(readMetadata.get(), 0) != SQLITE_INTEGER) {
        return core::Status<Error>::failure(
            makeError(ErrorCode::SchemaMigrationFailed, database_));
    }
    const auto version = sqlite3_column_int64(readMetadata.get(), 0);
    if (readMetadata.step() != SQLITE_DONE) {
        return core::Status<Error>::failure(
            makeError(ErrorCode::SchemaMigrationFailed, database_));
    }
    if (version != kSchemaVersion) {
        return core::Status<Error>::failure(
            makeError(ErrorCode::UnsupportedSchemaVersion, database_,
                "The configuration-profile catalog schema version is unsupported."));
    }
    return core::Status<Error>::success();
}

core::Result<std::uint64_t, Error>
ConfigurationProfileCatalogSqliteStore::saveCurrentCatalog(
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentDefinitionRevision,
    const core::ConfigurationProfileCatalog& catalog,
    std::uint64_t expectedCurrentRevision) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validRevision(equipmentDefinitionRevision) ||
        expectedCurrentRevision >= kMaximumSqliteInteger) {
        return core::Result<std::uint64_t, Error>::failure(
            makeError(ErrorCode::InvalidRevision, database_));
    }

    std::vector<std::uint8_t> payload;
    if (!encodeCatalog(catalog, payload)) {
        return core::Result<std::uint64_t, Error>::failure(
            makeError(ErrorCode::PayloadTooLarge, database_,
                "The configuration-profile catalog is empty or exceeds the payload limits."));
    }

    Transaction transaction(database_);
    if (!transaction.active()) {
        return core::Result<std::uint64_t, Error>::failure(
            makeError(ErrorCode::SqliteOperationFailed, database_));
    }
    std::optional<std::uint64_t> actualCurrent;
    if (!readCurrentRevision(database_, microscopeId,
            equipmentDefinitionRevision, actualCurrent)) {
        return core::Result<std::uint64_t, Error>::failure(
            makeError(ErrorCode::SqliteOperationFailed, database_));
    }
    if (actualCurrent.value_or(0) != expectedCurrentRevision) {
        return core::Result<std::uint64_t, Error>::failure(
            makeError(ErrorCode::RevisionConflict, database_,
                "The configuration-profile catalog changed since it was loaded."));
    }

    const std::uint64_t nextRevision = expectedCurrentRevision + 1U;
    Statement insertRevision(database_,
        "INSERT INTO configuration_profile_catalog_revision ("
        "microscope_id, equipment_revision, revision, payload_format_version, payload) "
        "VALUES (?1, ?2, ?3, ?4, ?5)");
    if (!insertRevision.prepared() ||
        !insertRevision.bindText(1, microscopeId.value()) ||
        !insertRevision.bindInt64(2, equipmentDefinitionRevision) ||
        !insertRevision.bindInt64(3, nextRevision) ||
        !insertRevision.bindInt(4, static_cast<int>(kPayloadFormatVersion)) ||
        !insertRevision.bindBlob(5, payload) ||
        insertRevision.step() != SQLITE_DONE) {
        return core::Result<std::uint64_t, Error>::failure(
            makeError(ErrorCode::SqliteOperationFailed, database_));
    }

    if (actualCurrent) {
        Statement updateCurrent(database_,
            "UPDATE configuration_profile_catalog_current SET revision = ?1 "
            "WHERE microscope_id = ?2 AND equipment_revision = ?3 AND revision = ?4");
        if (!updateCurrent.prepared() ||
            !updateCurrent.bindInt64(1, nextRevision) ||
            !updateCurrent.bindText(2, microscopeId.value()) ||
            !updateCurrent.bindInt64(3, equipmentDefinitionRevision) ||
            !updateCurrent.bindInt64(4, expectedCurrentRevision) ||
            updateCurrent.step() != SQLITE_DONE || sqlite3_changes(database_) != 1) {
            return core::Result<std::uint64_t, Error>::failure(
                makeError(ErrorCode::SqliteOperationFailed, database_));
        }
    } else {
        Statement insertCurrent(database_,
            "INSERT INTO configuration_profile_catalog_current ("
            "microscope_id, equipment_revision, revision) VALUES (?1, ?2, ?3)");
        if (!insertCurrent.prepared() ||
            !insertCurrent.bindText(1, microscopeId.value()) ||
            !insertCurrent.bindInt64(2, equipmentDefinitionRevision) ||
            !insertCurrent.bindInt64(3, nextRevision) ||
            insertCurrent.step() != SQLITE_DONE) {
            return core::Result<std::uint64_t, Error>::failure(
                makeError(ErrorCode::SqliteOperationFailed, database_));
        }
    }

    if (!transaction.commit()) {
        return core::Result<std::uint64_t, Error>::failure(
            makeError(ErrorCode::SqliteOperationFailed, database_));
    }
    return core::Result<std::uint64_t, Error>::success(nextRevision);
}

core::Result<std::optional<PersistedConfigurationProfileCatalog>, Error>
ConfigurationProfileCatalogSqliteStore::loadCurrentCatalog(
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentDefinitionRevision) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validRevision(equipmentDefinitionRevision)) {
        return core::Result<std::optional<PersistedConfigurationProfileCatalog>, Error>::failure(
            makeError(ErrorCode::InvalidRevision, database_));
    }
    Statement statement(database_,
        "SELECT revision FROM configuration_profile_catalog_current "
        "WHERE microscope_id = ?1 AND equipment_revision = ?2");
    if (!statement.prepared() ||
        !statement.bindText(1, microscopeId.value()) ||
        !statement.bindInt64(2, equipmentDefinitionRevision)) {
        return core::Result<std::optional<PersistedConfigurationProfileCatalog>, Error>::failure(
            makeError(ErrorCode::SqliteOperationFailed, database_));
    }
    const int step = statement.step();
    if (step == SQLITE_DONE) {
        return core::Result<std::optional<PersistedConfigurationProfileCatalog>, Error>::success(
            std::nullopt);
    }
    if (step != SQLITE_ROW || sqlite3_column_type(statement.get(), 0) != SQLITE_INTEGER) {
        return core::Result<std::optional<PersistedConfigurationProfileCatalog>, Error>::failure(
            makeError(ErrorCode::SqliteOperationFailed, database_));
    }
    const sqlite3_int64 revision = sqlite3_column_int64(statement.get(), 0);
    if (revision <= 0 || statement.step() != SQLITE_DONE) {
        return core::Result<std::optional<PersistedConfigurationProfileCatalog>, Error>::failure(
            makeError(ErrorCode::InvalidPersistedRecord, database_));
    }
    return loadRevisionUnlocked(microscopeId, equipmentDefinitionRevision,
        static_cast<std::uint64_t>(revision));
}

core::Result<std::optional<PersistedConfigurationProfileCatalog>, Error>
ConfigurationProfileCatalogSqliteStore::loadRevision(
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentDefinitionRevision,
    std::uint64_t revision) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!validRevision(equipmentDefinitionRevision) || !validRevision(revision)) {
        return core::Result<std::optional<PersistedConfigurationProfileCatalog>, Error>::failure(
            makeError(ErrorCode::InvalidRevision, database_));
    }
    return loadRevisionUnlocked(microscopeId, equipmentDefinitionRevision, revision);
}

core::Result<std::optional<PersistedConfigurationProfileCatalog>, Error>
ConfigurationProfileCatalogSqliteStore::loadRevisionUnlocked(
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentDefinitionRevision,
    std::uint64_t revision) {
    Statement statement(database_,
        "SELECT payload_format_version, payload FROM configuration_profile_catalog_revision "
        "WHERE microscope_id = ?1 AND equipment_revision = ?2 AND revision = ?3");
    if (!statement.prepared() ||
        !statement.bindText(1, microscopeId.value()) ||
        !statement.bindInt64(2, equipmentDefinitionRevision) ||
        !statement.bindInt64(3, revision)) {
        return core::Result<std::optional<PersistedConfigurationProfileCatalog>, Error>::failure(
            makeError(ErrorCode::SqliteOperationFailed, database_));
    }
    const int step = statement.step();
    if (step == SQLITE_DONE) {
        return core::Result<std::optional<PersistedConfigurationProfileCatalog>, Error>::success(
            std::nullopt);
    }
    if (step != SQLITE_ROW) {
        return core::Result<std::optional<PersistedConfigurationProfileCatalog>, Error>::failure(
            makeError(ErrorCode::SqliteOperationFailed, database_));
    }

    if (sqlite3_column_type(statement.get(), 0) != SQLITE_INTEGER ||
        sqlite3_column_type(statement.get(), 1) != SQLITE_BLOB) {
        return core::Result<std::optional<PersistedConfigurationProfileCatalog>, Error>::failure(
            makeError(ErrorCode::InvalidPersistedRecord, database_));
    }
    const sqlite3_int64 formatVersion = sqlite3_column_int64(statement.get(), 0);
    const int blobSize = sqlite3_column_bytes(statement.get(), 1);
    const auto* blob = static_cast<const std::uint8_t*>(
        sqlite3_column_blob(statement.get(), 1));
    if (formatVersion != static_cast<sqlite3_int64>(kPayloadFormatVersion) ||
        blob == nullptr || blobSize <= 0 ||
        static_cast<std::size_t>(blobSize) > kMaximumPayloadBytes) {
        return core::Result<std::optional<PersistedConfigurationProfileCatalog>, Error>::failure(
            makeError(ErrorCode::InvalidPersistedRecord, database_,
                "The stored catalog payload format or length is invalid."));
    }

    // SQLite owns the column buffer only while the statement remains positioned
    // on this row. Copy before stepping to verify there is exactly one row.
    std::vector<std::uint8_t> payload(
        blob, blob + static_cast<std::size_t>(blobSize));
    if (statement.step() != SQLITE_DONE) {
        return core::Result<std::optional<PersistedConfigurationProfileCatalog>, Error>::failure(
            makeError(ErrorCode::InvalidPersistedRecord, database_,
                "The catalog revision key returned more than one row."));
    }

    auto catalog = decodeCatalog(payload.data(), payload.size());
    if (!catalog) {
        return core::Result<std::optional<PersistedConfigurationProfileCatalog>, Error>::failure(
            makeError(ErrorCode::InvalidPersistedRecord, database_,
                "The stored catalog payload failed codec or Core domain validation."));
    }

    return core::Result<std::optional<PersistedConfigurationProfileCatalog>, Error>::success(
        PersistedConfigurationProfileCatalog{
            revision, equipmentDefinitionRevision, std::move(*catalog)});
}

} // namespace microscope::application::persistence
