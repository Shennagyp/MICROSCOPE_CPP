#include "microscope/application/persistence/CameraModelCatalogSqliteStore.h"

#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace microscope::application::persistence {
namespace {

using StoreError = CameraModelCatalogStoreError;
using StoreErrorCode = CameraModelCatalogStoreErrorCode;

constexpr int kSchemaVersion = 1;
constexpr std::uint64_t kMaximumSqliteInteger =
    static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max());

using CameraProfileKey = std::tuple<std::string, std::string, std::string>;

[[nodiscard]] StoreError makeError(
    StoreErrorCode code,
    sqlite3* database,
    std::string detail = {}) {
    const int nativeCode = database == nullptr ? 0 : sqlite3_errcode(database);
    if (detail.empty() && database != nullptr) {
        detail = sqlite3_errmsg(database);
    }
    return StoreError{code, nativeCode, std::move(detail)};
}

[[nodiscard]] core::Status<StoreError> failureStatus(StoreError error) {
    return core::Status<StoreError>::failure(std::move(error));
}

class Statement final {
public:
    Statement(sqlite3* database, const char* sql) noexcept
        : database_(database) {
        result_ = sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr);
    }

    ~Statement() {
        if (statement_ != nullptr) {
            sqlite3_finalize(statement_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    [[nodiscard]] bool prepared() const noexcept {
        return result_ == SQLITE_OK && statement_ != nullptr;
    }

    [[nodiscard]] int resultCode() const noexcept {
        return result_;
    }

    [[nodiscard]] sqlite3_stmt* get() const noexcept {
        return statement_;
    }

    [[nodiscard]] sqlite3* database() const noexcept {
        return database_;
    }

private:
    sqlite3* database_ = nullptr;
    sqlite3_stmt* statement_ = nullptr;
    int result_ = SQLITE_ERROR;
};

class Transaction final {
public:
    explicit Transaction(sqlite3* database) noexcept
        : database_(database),
          active_(sqlite3_exec(database_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr)
                  == SQLITE_OK) {}

    ~Transaction() {
        if (active_) {
            sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }

    [[nodiscard]] bool active() const noexcept {
        return active_;
    }

    [[nodiscard]] bool commit() noexcept {
        if (!active_) {
            return false;
        }
        if (sqlite3_exec(database_, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
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
    const std::string& value) noexcept {
    return sqlite3_bind_text(
        statement, index, value.c_str(),
        static_cast<int>(value.size()), SQLITE_TRANSIENT) == SQLITE_OK;
}

[[nodiscard]] bool bindOptionalDouble(
    sqlite3_stmt* statement,
    int index,
    const std::optional<double>& value) noexcept {
    return value
        ? sqlite3_bind_double(statement, index, *value) == SQLITE_OK
        : sqlite3_bind_null(statement, index) == SQLITE_OK;
}

[[nodiscard]] bool bindOptionalInteger(
    sqlite3_stmt* statement,
    int index,
    const std::optional<std::uint32_t>& value) noexcept {
    return value
        ? sqlite3_bind_int64(statement, index, static_cast<sqlite3_int64>(*value))
              == SQLITE_OK
        : sqlite3_bind_null(statement, index) == SQLITE_OK;
}

[[nodiscard]] bool bindOptionalEnum(
    sqlite3_stmt* statement,
    int index,
    const auto& value) noexcept {
    return value
        ? sqlite3_bind_int(statement, index, static_cast<int>(*value)) == SQLITE_OK
        : sqlite3_bind_null(statement, index) == SQLITE_OK;
}

[[nodiscard]] std::optional<double> readOptionalDouble(
    sqlite3_stmt* statement,
    int column) noexcept {
    if (sqlite3_column_type(statement, column) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_double(statement, column);
}

[[nodiscard]] std::optional<std::uint32_t> readOptionalUInt32(
    sqlite3_stmt* statement,
    int column) noexcept {
    if (sqlite3_column_type(statement, column) == SQLITE_NULL) {
        return std::nullopt;
    }
    const auto value = sqlite3_column_int64(statement, column);
    if (value <= 0 ||
        value > static_cast<sqlite3_int64>(
                    std::numeric_limits<std::uint32_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

template <typename TEnum>
[[nodiscard]] std::optional<TEnum> readOptionalEnum(
    sqlite3_stmt* statement,
    int column) noexcept {
    if (sqlite3_column_type(statement, column) == SQLITE_NULL) {
        return std::nullopt;
    }
    return static_cast<TEnum>(sqlite3_column_int(statement, column));
}

[[nodiscard]] bool readCatalogRevision(
    sqlite3* database,
    std::uint64_t& revision) {
    Statement statement(
        database,
        "SELECT revision FROM camera_model_catalog_state WHERE id = 1");
    if (!statement.prepared()) {
        return false;
    }

    const int step = sqlite3_step(statement.get());
    if (step != SQLITE_ROW) {
        return false;
    }

    const sqlite3_int64 stored = sqlite3_column_int64(statement.get(), 0);
    if (stored < 0) {
        return false;
    }
    revision = static_cast<std::uint64_t>(stored);
    return true;
}

[[nodiscard]] bool hasValidSensorEnumValues(
    const camera::CameraSensorCharacteristics& sensor) noexcept {
    const auto knownTechnology = [](camera::CameraSensorTechnology value) {
        switch (value) {
        case camera::CameraSensorTechnology::CCD:
        case camera::CameraSensorTechnology::CMOS:
        case camera::CameraSensorTechnology::ScientificCMOS:
        case camera::CameraSensorTechnology::Unknown:
            return true;
        }
        return false;
    };
    const auto knownShutter = [](camera::CameraShutterType value) {
        switch (value) {
        case camera::CameraShutterType::Global:
        case camera::CameraShutterType::Rolling:
        case camera::CameraShutterType::FrameTransfer:
        case camera::CameraShutterType::GlobalReset:
        case camera::CameraShutterType::Unknown:
            return true;
        }
        return false;
    };
    const auto knownColorMode = [](camera::CameraColorMode value) {
        switch (value) {
        case camera::CameraColorMode::Monochrome:
        case camera::CameraColorMode::Color:
        case camera::CameraColorMode::Unknown:
            return true;
        }
        return false;
    };
    switch (sensor.provenance) {
    case camera::CameraSpecificationProvenance::ManufacturerDatasheet:
    case camera::CameraSpecificationProvenance::VendorModelDescriptor:
    case camera::CameraSpecificationProvenance::UserVerified:
    case camera::CameraSpecificationProvenance::Measured:
    case camera::CameraSpecificationProvenance::Unknown:
        break;
    default:
        return false;
    }
    return (!sensor.technology || knownTechnology(*sensor.technology)) &&
           (!sensor.shutterType || knownShutter(*sensor.shutterType)) &&
           (!sensor.colorMode || knownColorMode(*sensor.colorMode));
}

[[nodiscard]] bool bindCapabilityMasks(
    sqlite3_stmt* statement,
    int knownIndex,
    int supportedIndex,
    const camera::CameraCapabilities& capabilities) noexcept {
    static_assert(
        camera::CameraCapabilities::count <= 64,
        "Camera capability persistence mask supports at most 64 capability values");

    std::uint64_t knownMask = 0;
    std::uint64_t supportedMask = 0;
    for (std::size_t index = 0; index < camera::CameraCapabilities::count; ++index) {
        const auto capability = static_cast<camera::CameraCapability>(index);
        const auto status = capabilities.status(capability);
        if (status == camera::CapabilityStatus::Unknown) {
            continue;
        }
        const std::uint64_t bit = std::uint64_t{1} << index;
        knownMask |= bit;
        if (status == camera::CapabilityStatus::Supported) {
            supportedMask |= bit;
        }
    }

    return sqlite3_bind_int64(
               statement, knownIndex,
               static_cast<sqlite3_int64>(knownMask)) == SQLITE_OK &&
           sqlite3_bind_int64(
               statement, supportedIndex,
               static_cast<sqlite3_int64>(supportedMask)) == SQLITE_OK;
}

[[nodiscard]] bool readCapabilityMasks(
    sqlite3_stmt* statement,
    int knownIndex,
    int supportedIndex,
    camera::CameraCapabilities& capabilities) noexcept {
    static_assert(
        camera::CameraCapabilities::count <= 64,
        "Camera capability persistence mask supports at most 64 capability values");

    const auto knownMask = static_cast<std::uint64_t>(
        sqlite3_column_int64(statement, knownIndex));
    const auto supportedMask = static_cast<std::uint64_t>(
        sqlite3_column_int64(statement, supportedIndex));
    const std::uint64_t allowedMask =
        camera::CameraCapabilities::count == 64
            ? std::numeric_limits<std::uint64_t>::max()
            : ((std::uint64_t{1} << camera::CameraCapabilities::count) - 1);

    if ((knownMask & ~allowedMask) != 0 ||
        (supportedMask & ~allowedMask) != 0 ||
        (supportedMask & ~knownMask) != 0) {
        return false;
    }

    for (std::size_t index = 0; index < camera::CameraCapabilities::count; ++index) {
        const std::uint64_t bit = std::uint64_t{1} << index;
        if ((knownMask & bit) == 0) {
            continue;
        }
        capabilities.set(
            static_cast<camera::CameraCapability>(index),
            (supportedMask & bit) != 0);
    }
    return true;
}

} // namespace

CameraModelCatalogSqliteStore::CameraModelCatalogSqliteStore(
    sqlite3* database) noexcept
    : database_(database) {}

core::Result<
    std::unique_ptr<CameraModelCatalogSqliteStore>,
    CameraModelCatalogStoreError> CameraModelCatalogSqliteStore::open(
        const std::string& databasePath) {
    if (databasePath.empty()) {
        return core::Result<
            std::unique_ptr<CameraModelCatalogSqliteStore>,
            CameraModelCatalogStoreError>::failure(
                makeError(StoreErrorCode::EmptyDatabasePath, nullptr));
    }

    sqlite3* database = nullptr;
    const int openResult = sqlite3_open_v2(
        databasePath.c_str(),
        &database,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    if (openResult != SQLITE_OK) {
        auto error = makeError(
            StoreErrorCode::DatabaseOpenFailed,
            database,
            database == nullptr ? "SQLite could not allocate a database handle."
                                : sqlite3_errmsg(database));
        if (database != nullptr) {
            sqlite3_close_v2(database);
        }
        return core::Result<
            std::unique_ptr<CameraModelCatalogSqliteStore>,
            CameraModelCatalogStoreError>::failure(std::move(error));
    }

    sqlite3_busy_timeout(database, 5000);
    sqlite3_exec(database, "PRAGMA foreign_keys = ON", nullptr, nullptr, nullptr);

    auto store = std::unique_ptr<CameraModelCatalogSqliteStore>(
        new CameraModelCatalogSqliteStore(database));
    const auto initialized = store->initializeSchema();
    if (!initialized) {
        return core::Result<
            std::unique_ptr<CameraModelCatalogSqliteStore>,
            CameraModelCatalogStoreError>::failure(initialized.error());
    }

    return core::Result<
        std::unique_ptr<CameraModelCatalogSqliteStore>,
        CameraModelCatalogStoreError>::success(std::move(store));
}

CameraModelCatalogSqliteStore::~CameraModelCatalogSqliteStore() {
    if (database_ != nullptr) {
        sqlite3_close_v2(database_);
        database_ = nullptr;
    }
}

core::Status<CameraModelCatalogStoreError>
CameraModelCatalogSqliteStore::initializeSchema() {
    std::lock_guard<std::mutex> lock(mutex_);

    int schemaVersion = 0;
    {
        Statement statement(database_, "PRAGMA user_version");
        if (!statement.prepared() || sqlite3_step(statement.get()) != SQLITE_ROW) {
            return failureStatus(makeError(
                StoreErrorCode::SchemaMigrationFailed, database_));
        }
        schemaVersion = sqlite3_column_int(statement.get(), 0);
    }

    if (schemaVersion > kSchemaVersion) {
        return failureStatus(makeError(
            StoreErrorCode::UnsupportedSchemaVersion,
            database_,
            "Camera model catalog database schema is newer than this application."));
    }

    if (schemaVersion == kSchemaVersion) {
        return core::Status<CameraModelCatalogStoreError>::success();
    }

    Transaction transaction(database_);
    if (!transaction.active()) {
        return failureStatus(makeError(
            StoreErrorCode::SchemaMigrationFailed, database_));
    }

    constexpr const char* schemaSql = R"SQL(
        CREATE TABLE IF NOT EXISTS camera_model_catalog_state (
            id INTEGER PRIMARY KEY CHECK (id = 1),
            revision INTEGER NOT NULL CHECK (revision >= 0)
        );

        INSERT OR IGNORE INTO camera_model_catalog_state (id, revision)
        VALUES (1, 0);

        CREATE TABLE IF NOT EXISTS camera_model_profile (
            manufacturer TEXT NOT NULL,
            model TEXT NOT NULL,
            variant TEXT NOT NULL,
            profile_revision INTEGER NOT NULL CHECK (profile_revision > 0),
            source_reference TEXT NOT NULL,
            capability_known_mask INTEGER NOT NULL,
            capability_supported_mask INTEGER NOT NULL,
            sensor_technology INTEGER NULL,
            shutter_type INTEGER NULL,
            color_mode INTEGER NULL,
            active_width INTEGER NULL,
            active_height INTEGER NULL,
            pixel_pitch_x REAL NULL,
            pixel_pitch_y REAL NULL,
            read_noise REAL NULL,
            dark_current REAL NULL,
            full_well_capacity REAL NULL,
            maximum_frame_rate REAL NULL,
            sensor_provenance INTEGER NOT NULL,
            PRIMARY KEY (manufacturer, model, variant)
        );

        CREATE TABLE IF NOT EXISTS camera_model_qe_sample (
            manufacturer TEXT NOT NULL,
            model TEXT NOT NULL,
            variant TEXT NOT NULL,
            wavelength_nm REAL NOT NULL,
            quantum_efficiency REAL NOT NULL
                CHECK (quantum_efficiency >= 0.0 AND quantum_efficiency <= 1.0),
            PRIMARY KEY (manufacturer, model, variant, wavelength_nm),
            FOREIGN KEY (manufacturer, model, variant)
                REFERENCES camera_model_profile (manufacturer, model, variant)
                ON DELETE CASCADE
        );

        CREATE INDEX IF NOT EXISTS idx_camera_model_catalog_model
        ON camera_model_profile (model, manufacturer);
        PRAGMA user_version = 1;
    )SQL";

    char* errorMessage = nullptr;
    const int executeResult = sqlite3_exec(
        database_, schemaSql, nullptr, nullptr, &errorMessage);
    if (executeResult != SQLITE_OK) {
        std::string detail = errorMessage == nullptr
            ? sqlite3_errmsg(database_)
            : errorMessage;
        sqlite3_free(errorMessage);
        return failureStatus(makeError(
            StoreErrorCode::SchemaMigrationFailed, database_, std::move(detail)));
    }

    if (!transaction.commit()) {
        return failureStatus(makeError(
            StoreErrorCode::SchemaMigrationFailed, database_));
    }

    return core::Status<CameraModelCatalogStoreError>::success();
}

core::Result<
    std::optional<camera::CameraModelCatalog>,
    CameraModelCatalogStoreError> CameraModelCatalogSqliteStore::load() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::uint64_t catalogRevision = 0;
    if (!readCatalogRevision(database_, catalogRevision)) {
        return core::Result<
            std::optional<camera::CameraModelCatalog>,
            CameraModelCatalogStoreError>::failure(
                makeError(StoreErrorCode::SqliteOperationFailed, database_));
    }

    if (catalogRevision == 0) {
        return core::Result<
            std::optional<camera::CameraModelCatalog>,
            CameraModelCatalogStoreError>::success(std::nullopt);
    }

    std::map<CameraProfileKey, std::vector<camera::CameraQuantumEfficiencySample>>
        quantumEfficiencyByProfile;

    {
        Statement statement(
            database_,
            "SELECT manufacturer, model, variant, wavelength_nm, quantum_efficiency "
            "FROM camera_model_qe_sample "
            "ORDER BY manufacturer, model, variant, wavelength_nm");
        if (!statement.prepared()) {
            return core::Result<
                std::optional<camera::CameraModelCatalog>,
                CameraModelCatalogStoreError>::failure(
                    makeError(StoreErrorCode::SqliteOperationFailed, database_));
        }

        for (;;) {
            const int step = sqlite3_step(statement.get());
            if (step == SQLITE_DONE) {
                break;
            }
            if (step != SQLITE_ROW) {
                return core::Result<
                    std::optional<camera::CameraModelCatalog>,
                    CameraModelCatalogStoreError>::failure(
                        makeError(StoreErrorCode::SqliteOperationFailed, database_));
            }

            const auto* manufacturerText =
                sqlite3_column_text(statement.get(), 0);
            const auto* modelText = sqlite3_column_text(statement.get(), 1);
            const auto* variantText = sqlite3_column_text(statement.get(), 2);
            if (manufacturerText == nullptr || modelText == nullptr ||
                variantText == nullptr) {
                return core::Result<
                    std::optional<camera::CameraModelCatalog>,
                    CameraModelCatalogStoreError>::failure(
                        makeError(StoreErrorCode::InvalidPersistedProfile, database_));
            }

            const CameraProfileKey key{
                reinterpret_cast<const char*>(manufacturerText),
                reinterpret_cast<const char*>(modelText),
                reinterpret_cast<const char*>(variantText)};
            const double wavelength = sqlite3_column_double(statement.get(), 3);
            const double qe = sqlite3_column_double(statement.get(), 4);
            quantumEfficiencyByProfile[key].push_back({wavelength, qe});
        }
    }

    std::vector<camera::CameraModelProfile> profiles;
    {
        Statement statement(
            database_,
            "SELECT manufacturer, model, variant, profile_revision, source_reference, "
            "capability_known_mask, capability_supported_mask, sensor_technology, "
            "shutter_type, color_mode, active_width, active_height, pixel_pitch_x, "
            "pixel_pitch_y, read_noise, dark_current, full_well_capacity, "
            "maximum_frame_rate, sensor_provenance "
            "FROM camera_model_profile ORDER BY manufacturer, model, variant");
        if (!statement.prepared()) {
            return core::Result<
                std::optional<camera::CameraModelCatalog>,
                CameraModelCatalogStoreError>::failure(
                    makeError(StoreErrorCode::SqliteOperationFailed, database_));
        }

        for (;;) {
            const int step = sqlite3_step(statement.get());
            if (step == SQLITE_DONE) {
                break;
            }
            if (step != SQLITE_ROW) {
                return core::Result<
                    std::optional<camera::CameraModelCatalog>,
                    CameraModelCatalogStoreError>::failure(
                        makeError(StoreErrorCode::SqliteOperationFailed, database_));
            }

            const auto* manufacturerText =
                sqlite3_column_text(statement.get(), 0);
            const auto* modelText = sqlite3_column_text(statement.get(), 1);
            const auto* variantText = sqlite3_column_text(statement.get(), 2);
            const auto* sourceText = sqlite3_column_text(statement.get(), 4);
            if (manufacturerText == nullptr || modelText == nullptr ||
                variantText == nullptr || sourceText == nullptr) {
                return core::Result<
                    std::optional<camera::CameraModelCatalog>,
                    CameraModelCatalogStoreError>::failure(
                        makeError(StoreErrorCode::InvalidPersistedProfile, database_));
            }

            const auto manufacturer =
                std::string(reinterpret_cast<const char*>(manufacturerText));
            const auto model =
                std::string(reinterpret_cast<const char*>(modelText));
            const auto variant =
                std::string(reinterpret_cast<const char*>(variantText));
            const auto sourceReference =
                std::string(reinterpret_cast<const char*>(sourceText));

            const sqlite3_int64 profileRevision =
                sqlite3_column_int64(statement.get(), 3);
            if (profileRevision <= 0) {
                return core::Result<
                    std::optional<camera::CameraModelCatalog>,
                    CameraModelCatalogStoreError>::failure(
                        makeError(StoreErrorCode::InvalidPersistedProfile, database_));
            }

            camera::CameraCapabilities capabilities;
            if (!readCapabilityMasks(statement.get(), 5, 6, capabilities)) {
                return core::Result<
                    std::optional<camera::CameraModelCatalog>,
                    CameraModelCatalogStoreError>::failure(
                        makeError(StoreErrorCode::InvalidPersistedProfile, database_));
            }

            camera::CameraSensorCharacteristics sensor;
            sensor.technology = readOptionalEnum<camera::CameraSensorTechnology>(
                statement.get(), 7);
            sensor.shutterType = readOptionalEnum<camera::CameraShutterType>(
                statement.get(), 8);
            sensor.colorMode = readOptionalEnum<camera::CameraColorMode>(
                statement.get(), 9);

            const auto width = readOptionalUInt32(statement.get(), 10);
            const auto height = readOptionalUInt32(statement.get(), 11);
            if (width.has_value() != height.has_value()) {
                return core::Result<
                    std::optional<camera::CameraModelCatalog>,
                    CameraModelCatalogStoreError>::failure(
                        makeError(StoreErrorCode::InvalidPersistedProfile, database_));
            }
            if (width && height) {
                sensor.activeSensorDimensions = camera::ImageResolution{*width, *height};
            }

            sensor.pixelPitchXMicrometers = readOptionalDouble(statement.get(), 12);
            sensor.pixelPitchYMicrometers = readOptionalDouble(statement.get(), 13);
            sensor.readNoiseElectronsRms = readOptionalDouble(statement.get(), 14);
            sensor.darkCurrentElectronsPerPixelPerSecond =
                readOptionalDouble(statement.get(), 15);
            sensor.fullWellCapacityElectrons = readOptionalDouble(statement.get(), 16);
            sensor.maximumFrameRateFramesPerSecond =
                readOptionalDouble(statement.get(), 17);
            sensor.provenance = static_cast<camera::CameraSpecificationProvenance>(
                sqlite3_column_int(statement.get(), 18));

            const CameraProfileKey key{manufacturer, model, variant};
            const auto qe = quantumEfficiencyByProfile.find(key);
            if (qe != quantumEfficiencyByProfile.end()) {
                sensor.quantumEfficiency = qe->second;
            }

            if (!hasValidSensorEnumValues(sensor)) {
                return core::Result<
                    std::optional<camera::CameraModelCatalog>,
                    CameraModelCatalogStoreError>::failure(
                        makeError(StoreErrorCode::InvalidPersistedProfile, database_));
            }

            camera::CameraModelKey modelKey{
                manufacturer,
                model,
                variant.empty()
                    ? std::optional<std::string>{}
                    : std::optional<std::string>{variant}};
            auto profile = camera::CameraModelProfile::create(
                std::move(modelKey),
                capabilities,
                std::move(sensor),
                static_cast<std::uint64_t>(profileRevision),
                sourceReference);
            if (!profile) {
                return core::Result<
                    std::optional<camera::CameraModelCatalog>,
                    CameraModelCatalogStoreError>::failure(
                        makeError(StoreErrorCode::InvalidPersistedProfile, database_));
            }
            profiles.push_back(std::move(*profile));
        }
    }

    auto catalog = camera::CameraModelCatalog::create(
        std::move(profiles), catalogRevision);
    if (!catalog) {
        return core::Result<
            std::optional<camera::CameraModelCatalog>,
            CameraModelCatalogStoreError>::failure(
                makeError(StoreErrorCode::InvalidPersistedProfile, database_));
    }

    return core::Result<
        std::optional<camera::CameraModelCatalog>,
        CameraModelCatalogStoreError>::success(
            std::optional<camera::CameraModelCatalog>{std::move(*catalog)});
}

core::Status<CameraModelCatalogStoreError> CameraModelCatalogSqliteStore::save(
    const camera::CameraModelCatalog& catalog,
    std::uint64_t expectedCurrentRevision) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (expectedCurrentRevision >= kMaximumSqliteInteger ||
        catalog.revision() != expectedCurrentRevision + 1 ||
        catalog.revision() > kMaximumSqliteInteger) {
        return failureStatus(makeError(
            StoreErrorCode::InvalidCatalogRevision,
            database_,
            "The new catalog revision must be exactly one greater than the expected current revision."));
    }

    Transaction transaction(database_);
    if (!transaction.active()) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    std::uint64_t currentRevision = 0;
    if (!readCatalogRevision(database_, currentRevision)) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }
    if (currentRevision != expectedCurrentRevision) {
        return failureStatus(makeError(
            StoreErrorCode::RevisionConflict,
            database_,
            "The camera model catalog changed since it was loaded."));
    }

    if (sqlite3_exec(
            database_,
            "DELETE FROM camera_model_qe_sample; "
            "DELETE FROM camera_model_profile;",
            nullptr,
            nullptr,
            nullptr) != SQLITE_OK) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    constexpr const char* insertProfileSql = R"SQL(
        INSERT INTO camera_model_profile (
            manufacturer, model, variant, profile_revision, source_reference,
            capability_known_mask, capability_supported_mask, sensor_technology,
            shutter_type, color_mode, active_width, active_height, pixel_pitch_x,
            pixel_pitch_y, read_noise, dark_current, full_well_capacity,
            maximum_frame_rate, sensor_provenance
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )SQL";
    constexpr const char* insertQeSql = R"SQL(
        INSERT INTO camera_model_qe_sample (
            manufacturer, model, variant, wavelength_nm, quantum_efficiency
        ) VALUES (?, ?, ?, ?, ?)
    )SQL";

    Statement insertProfile(database_, insertProfileSql);
    Statement insertQe(database_, insertQeSql);
    if (!insertProfile.prepared() || !insertQe.prepared()) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    for (const auto& profile : catalog.profiles()) {
        const auto& key = profile.key();
        const auto& sensor = profile.sensorCharacteristics();
        if (!hasValidSensorEnumValues(sensor)) {
            return failureStatus(makeError(
                StoreErrorCode::InvalidPersistedProfile,
                database_,
                "Camera profile contains an unknown sensor enum value."));
        }

        const auto bindAndCheck = [&](bool success) {
            return success;
        };

        if (!bindAndCheck(bindText(insertProfile.get(), 1, key.manufacturer)) ||
            !bindAndCheck(bindText(insertProfile.get(), 2, key.model)) ||
            !bindAndCheck(bindText(insertProfile.get(), 3, key.variant.value_or(""))) ||
            sqlite3_bind_int64(
                insertProfile.get(), 4,
                static_cast<sqlite3_int64>(profile.profileRevision())) != SQLITE_OK ||
            !bindAndCheck(bindText(insertProfile.get(), 5, profile.sourceReference())) ||
            !bindCapabilityMasks(insertProfile.get(), 6, 7, profile.capabilities()) ||
            !bindOptionalEnum(insertProfile.get(), 8, sensor.technology) ||
            !bindOptionalEnum(insertProfile.get(), 9, sensor.shutterType) ||
            !bindOptionalEnum(insertProfile.get(), 10, sensor.colorMode) ||
            !bindOptionalInteger(
                insertProfile.get(), 11,
                sensor.activeSensorDimensions
                    ? std::optional<std::uint32_t>{
                          sensor.activeSensorDimensions->width}
                    : std::nullopt) ||
            !bindOptionalInteger(
                insertProfile.get(), 12,
                sensor.activeSensorDimensions
                    ? std::optional<std::uint32_t>{
                          sensor.activeSensorDimensions->height}
                    : std::nullopt) ||
            !bindOptionalDouble(insertProfile.get(), 13, sensor.pixelPitchXMicrometers) ||
            !bindOptionalDouble(insertProfile.get(), 14, sensor.pixelPitchYMicrometers) ||
            !bindOptionalDouble(insertProfile.get(), 15, sensor.readNoiseElectronsRms) ||
            !bindOptionalDouble(
                insertProfile.get(), 16,
                sensor.darkCurrentElectronsPerPixelPerSecond) ||
            !bindOptionalDouble(insertProfile.get(), 17, sensor.fullWellCapacityElectrons) ||
            !bindOptionalDouble(
                insertProfile.get(), 18,
                sensor.maximumFrameRateFramesPerSecond) ||
            sqlite3_bind_int(
                insertProfile.get(), 19,
                static_cast<int>(sensor.provenance)) != SQLITE_OK) {
            return failureStatus(makeError(
                StoreErrorCode::SqliteOperationFailed, database_));
        }

        if (sqlite3_step(insertProfile.get()) != SQLITE_DONE) {
            return failureStatus(makeError(
                StoreErrorCode::SqliteOperationFailed, database_));
        }
        sqlite3_reset(insertProfile.get());
        sqlite3_clear_bindings(insertProfile.get());

        for (const auto& sample : sensor.quantumEfficiency) {
            if (!bindText(insertQe.get(), 1, key.manufacturer) ||
                !bindText(insertQe.get(), 2, key.model) ||
                !bindText(insertQe.get(), 3, key.variant.value_or("")) ||
                sqlite3_bind_double(
                    insertQe.get(), 4, sample.wavelengthNanometers) != SQLITE_OK ||
                sqlite3_bind_double(
                    insertQe.get(), 5, sample.quantumEfficiency) != SQLITE_OK) {
                return failureStatus(makeError(
                    StoreErrorCode::SqliteOperationFailed, database_));
            }

            if (sqlite3_step(insertQe.get()) != SQLITE_DONE) {
                return failureStatus(makeError(
                    StoreErrorCode::SqliteOperationFailed, database_));
            }
            sqlite3_reset(insertQe.get());
            sqlite3_clear_bindings(insertQe.get());
        }
    }

    Statement updateRevision(
        database_,
        "UPDATE camera_model_catalog_state SET revision = ? "
        "WHERE id = 1 AND revision = ?");
    if (!updateRevision.prepared() ||
        sqlite3_bind_int64(
            updateRevision.get(), 1,
            static_cast<sqlite3_int64>(catalog.revision())) != SQLITE_OK ||
        sqlite3_bind_int64(
            updateRevision.get(), 2,
            static_cast<sqlite3_int64>(expectedCurrentRevision)) != SQLITE_OK ||
        sqlite3_step(updateRevision.get()) != SQLITE_DONE ||
        sqlite3_changes(database_) != 1) {
        return failureStatus(makeError(
            StoreErrorCode::RevisionConflict, database_));
    }

    if (!transaction.commit()) {
        return failureStatus(makeError(
            StoreErrorCode::SqliteOperationFailed, database_));
    }

    return core::Status<CameraModelCatalogStoreError>::success();
}

} // namespace microscope::application::persistence
