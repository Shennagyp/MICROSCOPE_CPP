#include "microscope/application/persistence/ConfigurationProfileCatalogSqliteStore.h"

#include <sqlite3.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace microscope::core;
using namespace microscope::application::persistence;

int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

template <typename T, typename E>
T take(Result<T, E> result) {
    if (!result) throw std::runtime_error("configuration-profile fixture rejected");
    return std::move(result).value();
}

template <typename T>
T id(const char* value) {
    return take(T::create(value));
}

std::string uniqueDatabasePath() {
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
        ("microscope-configuration-profile-catalog-" +
            std::to_string(nonce) + ".sqlite")).string();
}

void removeDatabaseFiles(const std::string& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path + "-wal", ignored);
    std::filesystem::remove(path + "-shm", ignored);
}

ConfigurationProfile makeFullProfile(
    const char* profileId,
    ContrastMethod contrast,
    double aperture,
    double intensity,
    double exposure) {
    auto function = take(OpticalFunctionRequirement::create(
        OpticalElementFunction::PhasePlate,
        id<OpticalCompatibilityId>("phase-family-a"),
        2,
        OpticalPathRole::Imaging));
    auto apertureTarget = take(NonNegativeNumericalAperture::create(aperture));
    auto intensityTarget = take(IlluminationIntensitySetting::create(
        intensity, IlluminationIntensityUnit::Percent));
    auto exposureTime = take(PositiveMicroseconds::create(exposure));
    return take(ConfigurationProfile::create(
        id<ConfigurationProfileId>(profileId),
        contrast,
        {id<EquipmentComponentId>("objective-4x"),
         id<EquipmentComponentId>("objective-10x")},
        {id<EquipmentComponentId>("condenser-abbe")},
        {id<OpticalPathId>("transmitted-imaging"),
         id<OpticalPathId>("transmitted-illumination")},
        {id<EquipmentComponentId>("camera-main")},
        {id<EquipmentComponentId>("illumination-led")},
        {id<EquipmentComponentId>("phase-plate"),
         id<EquipmentComponentId>("phase-annulus")},
        {std::move(function)},
        std::move(apertureTarget),
        std::move(intensityTarget),
        ExposurePreset::create(std::move(exposureTime))));
}

ConfigurationProfile makeMinimalProfile(const char* profileId) {
    return take(ConfigurationProfile::create(
        id<ConfigurationProfileId>(profileId),
        ContrastMethod::Darkfield,
        {id<EquipmentComponentId>("objective-40x")},
        {id<EquipmentComponentId>("condenser-darkfield")},
        {id<OpticalPathId>("darkfield-imaging")},
        {id<EquipmentComponentId>("camera-main")},
        {id<EquipmentComponentId>("illumination-led")}));
}

ConfigurationProfileCatalog makeCatalog(double aperture = 1.25) {
    return take(ConfigurationProfileCatalog::create({
        makeFullProfile("profile-phase-contrast", ContrastMethod::PhaseContrast,
            aperture, 62.5, 8'000.0),
        makeMinimalProfile("profile-darkfield")
    }));
}

bool sqlExec(const std::string& path, const char* sql) {
    sqlite3* database = nullptr;
    if (sqlite3_open(path.c_str(), &database) != SQLITE_OK) {
        if (database) sqlite3_close_v2(database);
        return false;
    }
    char* message = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &message);
    sqlite3_free(message);
    sqlite3_close_v2(database);
    return result == SQLITE_OK;
}

void opensIndependentDatabaseAndRejectsInvalidArguments() {
    auto emptyPath = ConfigurationProfileCatalogSqliteStore::open("");
    check(!emptyPath &&
        emptyPath.error().code ==
            ConfigurationProfileCatalogStoreErrorCode::EmptyDatabasePath,
        "rejects an empty database path");

    const auto path = uniqueDatabasePath();
    auto opened = ConfigurationProfileCatalogSqliteStore::open(path);
    check(static_cast<bool>(opened),
        "creates its own catalog schema without requiring other persistence stores");

    if (opened) {
        auto emptyLoad = opened.value()->loadCurrentCatalog(
            id<MicroscopeId>("scope-open"), 1);
        check(emptyLoad && !emptyLoad.value(),
            "returns no catalog when the microscope/equipment scope has no current revision");
        opened.value().reset();
    }
    removeDatabaseFiles(path);
}

void roundTripsAllProfileFieldsAndPreservesCatalogHistory() {
    const auto path = uniqueDatabasePath();
    auto opened = ConfigurationProfileCatalogSqliteStore::open(path);
    if (!opened) throw std::runtime_error("could not open configuration-profile store");
    auto& store = *opened.value();
    const auto microscopeId = id<MicroscopeId>("scope-profile-store");
    const auto catalog = makeCatalog();

    const auto first = store.saveCurrentCatalog(microscopeId, 4, catalog, 0);
    check(first && first.value() == 1,
        "creates the first catalog revision for the exact equipment-definition revision");
    if (!first) {
        opened.value().reset();
        removeDatabaseFiles(path);
        return;
    }

    auto loaded = store.loadCurrentCatalog(microscopeId, 4);
    check(loaded && loaded.value().has_value(),
        "loads the current catalog revision");
    if (loaded && loaded.value()) {
        const auto& persisted = *loaded.value();
        check(persisted.revision == 1 &&
            persisted.equipmentDefinitionRevision == 4 &&
            persisted.catalog.profiles().size() == 2,
            "restores catalog identity, revision and profile count");

        const auto& phase = persisted.catalog.profiles()[0];
        check(phase.id().value() == "profile-phase-contrast" &&
            phase.contrastMethod() == ContrastMethod::PhaseContrast,
            "restores profile identity and contrast method");
        check(phase.compatibleObjectiveIds().size() == 2 &&
            phase.compatibleObjectiveIds()[1].value() == "objective-10x" &&
            phase.compatibleCondenserIds()[0].value() == "condenser-abbe",
            "restores compatible objective and condenser identities in their original order");
        check(phase.compatibleOpticalPathIds().size() == 2 &&
            phase.compatibleOpticalPathIds()[1].value() == "transmitted-illumination" &&
            phase.compatibleCameraIds()[0].value() == "camera-main" &&
            phase.compatibleIlluminationIds()[0].value() == "illumination-led",
            "restores optical-path, camera and illumination compatibility sets");
        check(phase.requiredPathComponentIds().size() == 2 &&
            phase.requiredPathComponentIds()[1].value() == "phase-annulus",
            "restores required optical-path component identities");
        check(phase.requiredOpticalFunctions().size() == 1 &&
            phase.requiredOpticalFunctions()[0].function() ==
                OpticalElementFunction::PhasePlate &&
            phase.requiredOpticalFunctions()[0].compatibilityId() &&
            phase.requiredOpticalFunctions()[0].compatibilityId()->value() ==
                "phase-family-a" &&
            phase.requiredOpticalFunctions()[0].minimumComponentCount() == 2 &&
            phase.requiredOpticalFunctions()[0].requiredPathRole() ==
                OpticalPathRole::Imaging,
            "restores optical-function requirements including compatibility, count and route role");
        check(phase.condenserApertureTarget() &&
            phase.condenserApertureTarget()->value() == 1.25,
            "restores optional condenser numerical-aperture target");
        check(phase.illuminationIntensityTarget() &&
            phase.illuminationIntensityTarget()->value() == 62.5 &&
            phase.illuminationIntensityTarget()->unit() ==
                IlluminationIntensityUnit::Percent,
            "restores optional illumination intensity and its declared unit");
        check(phase.exposurePreset() &&
            phase.exposurePreset()->exposureTimeMicroseconds() == 8'000.0,
            "restores optional exposure preset");

        const auto& darkfield = persisted.catalog.profiles()[1];
        check(darkfield.id().value() == "profile-darkfield" &&
            darkfield.contrastMethod() == ContrastMethod::Darkfield &&
            !darkfield.condenserApertureTarget() &&
            !darkfield.illuminationIntensityTarget() &&
            !darkfield.exposurePreset() &&
            darkfield.requiredOpticalFunctions().empty() &&
            darkfield.requiredPathComponentIds().empty(),
            "preserves absent optional settings and empty optional requirement collections");
    }

    const auto secondCatalog = makeCatalog(1.30);
    const auto second = store.saveCurrentCatalog(
        microscopeId, 4, secondCatalog, 1);
    check(second && second.value() == 2,
        "appends a new catalog revision using optimistic concurrency");

    auto historical = store.loadRevision(microscopeId, 4, 1);
    auto current = store.loadCurrentCatalog(microscopeId, 4);
    check(historical && historical.value() &&
        historical.value()->catalog.profiles()[0].condenserApertureTarget()->value() == 1.25,
        "retains the old catalog payload unchanged as a historical revision");
    check(current && current.value() && current.value()->revision == 2 &&
        current.value()->catalog.profiles()[0].condenserApertureTarget()->value() == 1.30,
        "loads the latest catalog revision independently of historical revisions");

    auto conflict = store.saveCurrentCatalog(microscopeId, 4, catalog, 1);
    check(!conflict &&
        conflict.error().code ==
            ConfigurationProfileCatalogStoreErrorCode::RevisionConflict,
        "rejects a stale catalog writer instead of overwriting a newer revision");

    auto otherMicroscope = store.loadCurrentCatalog(id<MicroscopeId>("scope-other"), 4);
    auto otherEquipment = store.loadCurrentCatalog(microscopeId, 5);
    check(otherMicroscope && !otherMicroscope.value() &&
        otherEquipment && !otherEquipment.value(),
        "isolates catalog revision streams by microscope and equipment-definition revision");

    auto zeroEquipment = store.loadCurrentCatalog(microscopeId, 0);
    auto zeroRevision = store.loadRevision(microscopeId, 4, 0);
    check(!zeroEquipment && zeroEquipment.error().code ==
            ConfigurationProfileCatalogStoreErrorCode::InvalidRevision &&
        !zeroRevision && zeroRevision.error().code ==
            ConfigurationProfileCatalogStoreErrorCode::InvalidRevision,
        "rejects invalid zero equipment and catalog revisions");

    opened.value().reset();
    removeDatabaseFiles(path);
}

void revisionPayloadsAreImmutableAndCorruptionIsRejected() {
    const auto path = uniqueDatabasePath();
    auto opened = ConfigurationProfileCatalogSqliteStore::open(path);
    if (!opened) throw std::runtime_error("could not open configuration-profile store");
    auto& store = *opened.value();

    auto saved = store.saveCurrentCatalog(
        id<MicroscopeId>("scope-immutable"), 1, makeCatalog(), 0);
    check(static_cast<bool>(saved),
        "writes a catalog revision before checking its immutability");

    const bool updateSucceeded = sqlExec(path,
        "UPDATE configuration_profile_catalog_revision SET payload = x'00' "
        "WHERE microscope_id = 'scope-immutable' AND equipment_revision = 1 AND revision = 1");
    const bool deleteSucceeded = sqlExec(path,
        "DELETE FROM configuration_profile_catalog_revision "
        "WHERE microscope_id = 'scope-immutable' AND equipment_revision = 1 AND revision = 1");
    check(!updateSucceeded && !deleteSucceeded,
        "SQLite triggers prevent mutation or deletion of a published catalog revision");

    const bool droppedTrigger = sqlExec(path,
        "DROP TRIGGER configuration_profile_catalog_revision_no_update");
    const bool corrupted = sqlExec(path,
        "UPDATE configuration_profile_catalog_revision SET payload = x'00' "
        "WHERE microscope_id = 'scope-immutable' AND equipment_revision = 1 AND revision = 1");
    check(droppedTrigger && corrupted,
        "test fixture can corrupt a catalog only after explicitly removing the immutability guard");

    auto invalid = store.loadRevision(
        id<MicroscopeId>("scope-immutable"), 1, 1);
    check(!invalid && invalid.error().code ==
        ConfigurationProfileCatalogStoreErrorCode::InvalidPersistedRecord,
        "rejects an unknown or malformed payload instead of constructing a partial catalog");

    opened.value().reset();
    removeDatabaseFiles(path);
}

} // namespace

int main() {
    try {
        opensIndependentDatabaseAndRejectsInvalidArguments();
        roundTripsAllProfileFieldsAndPreservesCatalogHistory();
        revisionPayloadsAreImmutableAndCorruptionIsRejected();
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return 1;
    }

    if (failures != 0) return 1;
    std::cout << "Configuration-profile catalog SQLite tests passed\n";
    return 0;
}
