#include "microscope/application/persistence/EquipmentDefinitionSqliteStore.h"

#include <chrono>
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <string>

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

std::string document(const std::string& microscopeId, std::uint64_t revision) {
    return std::string("{") +
        "\"schemaVersion\":1," +
        "\"microscopeId\":\"" + microscopeId + "\"," +
        "\"revision\":" + std::to_string(revision) + "," +
        "\"components\":[{" +
            "\"id\":\"stage-main\"," +
            "\"kind\":\"mechanical-assembly\"," +
            "\"ports\":[]}" +
        "]," +
        "\"connections\":[]," +
        "\"opticalPaths\":[]}";
}

std::string temporaryDatabasePath() {
    const auto nonce = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    return (std::filesystem::temp_directory_path() /
        ("microscope-equipment-definition-store-" +
            std::to_string(nonce) + ".sqlite")).string();
}

void removeDatabase(const std::string& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path + "-wal", ignored);
    std::filesystem::remove(path + "-shm", ignored);
}

void verifiesBlobStorageAndDatabaseLevelImmutability(const std::string& path) {
    sqlite3* database = nullptr;
    const int opened = sqlite3_open_v2(
        path.c_str(), &database, SQLITE_OPEN_READWRITE, nullptr);
    check(opened == SQLITE_OK && database != nullptr,
        "opens the database independently to inspect the stored payload");
    if (opened != SQLITE_OK || database == nullptr) {
        if (database != nullptr) sqlite3_close_v2(database);
        return;
    }

    sqlite3_stmt* query = nullptr;
    const int prepared = sqlite3_prepare_v2(
        database,
        "SELECT typeof(payload) FROM equipment_definition_revision "
        "WHERE microscope_id = 'microscope-a' AND revision = 1",
        -1, &query, nullptr);
    check(prepared == SQLITE_OK, "prepares the equipment payload storage-type query");
    if (prepared == SQLITE_OK && sqlite3_step(query) == SQLITE_ROW) {
        const auto* type = sqlite3_column_text(query, 0);
        check(type != nullptr && std::string(
            reinterpret_cast<const char*>(type)) == "blob",
            "stores the exact JSON document using SQLite BLOB affinity");
    } else {
        check(false, "finds the first immutable equipment payload row");
    }
    if (query != nullptr) sqlite3_finalize(query);

    char* message = nullptr;
    const int update = sqlite3_exec(
        database,
        "UPDATE equipment_definition_revision SET payload = X'00' "
        "WHERE microscope_id = 'microscope-a' AND revision = 1",
        nullptr, nullptr, &message);
    check(update != SQLITE_OK,
        "database trigger prevents mutation of a published equipment revision");
    sqlite3_free(message);
    sqlite3_close_v2(database);
}

void verifiesAppendOnlyRevisionsAndRestartRoundTrip() {
    const auto path = temporaryDatabasePath();
    const auto firstDocument = document("microscope-a", 1);
    const auto secondDocument = document("microscope-a", 2);
    {
        auto opened = EquipmentDefinitionSqliteStore::open(path);
        check(static_cast<bool>(opened), "opens a new equipment-definition database");
        if (!opened) {
            removeDatabase(path);
            return;
        }

        auto& store = *opened.value();
        auto savedFirst = store.saveCurrentDefinition(firstDocument, 0);
        check(static_cast<bool>(savedFirst) && savedFirst.value() == 1,
            "stores the first equipment-definition revision");
        verifiesBlobStorageAndDatabaseLevelImmutability(path);

        auto secondConnection = EquipmentDefinitionSqliteStore::open(path);
        check(static_cast<bool>(secondConnection),
            "opens a second SQLite store connection for optimistic concurrency");
        if (secondConnection) {
            auto conflictingWriter =
                secondConnection.value()->saveCurrentDefinition(
                    document("microscope-a", 7), 0);
            check(!conflictingWriter &&
                conflictingWriter.error().code ==
                    EquipmentDefinitionStoreErrorCode::RevisionConflict,
                "a separate store connection cannot overwrite a current revision using a stale token");
        }

        auto current = store.loadCurrentDefinition(
            MicroscopeId::create("microscope-a").value());
        check(static_cast<bool>(current) && current.value().has_value(),
            "loads the current equipment definition");
        if (current && current.value()) {
            check(current.value()->revision == 1,
                "returns the current domain revision");
            check(current.value()->sourceDocument == firstDocument,
                "preserves the exact source JSON bytes");
            check(current.value()->equipment.microscopeId().value() == "microscope-a",
                "reconstructs the microscope identity through the JSON loader");
            check(current.value()->equipment.revision() == 1,
                "reconstructs the equipment revision through the JSON loader");
        }

        auto savedSecond = store.saveCurrentDefinition(secondDocument, 1);
        check(static_cast<bool>(savedSecond) && savedSecond.value() == 2,
            "appends a later equipment-definition revision");

        auto historical = store.loadRevision(
            MicroscopeId::create("microscope-a").value(), 1);
        check(static_cast<bool>(historical) && historical.value().has_value(),
            "loads an immutable historical equipment revision");
        if (historical && historical.value()) {
            check(historical.value()->sourceDocument == firstDocument,
                "historical revision retains its original document");
        }
    }

    {
        auto reopened = EquipmentDefinitionSqliteStore::open(path);
        check(static_cast<bool>(reopened),
            "reopens the same equipment-definition database");
        if (reopened) {
            auto current = reopened.value()->loadCurrentDefinition(
                MicroscopeId::create("microscope-a").value());
            check(static_cast<bool>(current) && current.value().has_value(),
                "loads current equipment after the store is reopened");
            if (current && current.value()) {
                check(current.value()->revision == 2 &&
                    current.value()->sourceDocument == secondDocument,
                    "restart round-trip returns the latest exact source revision");
            }
        }
    }

    removeDatabase(path);
}

void isolatesMicroscopesAndRejectsStaleWriters() {
    const auto path = temporaryDatabasePath();
    auto opened = EquipmentDefinitionSqliteStore::open(path);
    check(static_cast<bool>(opened), "opens the database for concurrency tests");
    if (!opened) {
        removeDatabase(path);
        return;
    }

    auto& store = *opened.value();
    auto first = store.saveCurrentDefinition(document("microscope-a", 5), 0);
    auto independent = store.saveCurrentDefinition(document("microscope-b", 8), 0);
    check(static_cast<bool>(first) && static_cast<bool>(independent),
        "stores definitions for independent microscope identities");

    auto stale = store.saveCurrentDefinition(document("microscope-a", 6), 0);
    check(!stale &&
        stale.error().code == EquipmentDefinitionStoreErrorCode::RevisionConflict,
        "rejects a writer whose expected current revision is stale");

    auto currentA = store.loadCurrentDefinition(MicroscopeId::create("microscope-a").value());
    auto currentB = store.loadCurrentDefinition(MicroscopeId::create("microscope-b").value());
    check(static_cast<bool>(currentA) && currentA.value().has_value() &&
        currentA.value()->revision == 5,
        "stale write leaves microscope A current revision unchanged");
    check(static_cast<bool>(currentB) && currentB.value().has_value() &&
        currentB.value()->revision == 8,
        "microscope B has an independent current revision");

    auto missing = store.loadRevision(
        MicroscopeId::create("microscope-a").value(), 99);
    check(static_cast<bool>(missing) && !missing.value().has_value(),
        "returns an empty result for an unknown historical revision");

    auto nonMonotonic = store.saveCurrentDefinition(document("microscope-a", 5), 5);
    check(!nonMonotonic &&
        nonMonotonic.error().code == EquipmentDefinitionStoreErrorCode::InvalidRevision,
        "rejects a non-increasing equipment revision");

    opened.value().reset();
    removeDatabase(path);
}

void rejectsInvalidDocumentsBeforePersistence() {
    const auto path = temporaryDatabasePath();
    auto opened = EquipmentDefinitionSqliteStore::open(path);
    check(static_cast<bool>(opened), "opens the database for validation tests");
    if (!opened) {
        removeDatabase(path);
        return;
    }

    auto empty = opened.value()->saveCurrentDefinition("", 0);
    check(!empty &&
        empty.error().code == EquipmentDefinitionStoreErrorCode::InvalidEquipmentDefinition,
        "rejects an empty JSON document before opening a write transaction");

    auto malformed = opened.value()->saveCurrentDefinition("{", 0);
    check(!malformed &&
        malformed.error().code == EquipmentDefinitionStoreErrorCode::InvalidEquipmentDefinition,
        "rejects malformed JSON before persistence");

    auto unsupported = document("microscope-a", 1);
    const auto schema = unsupported.find("\"schemaVersion\":1");
    unsupported.replace(schema, std::string("\"schemaVersion\":1").size(),
        "\"schemaVersion\":2");
    auto wrongSchema = opened.value()->saveCurrentDefinition(unsupported, 0);
    check(!wrongSchema &&
        wrongSchema.error().code == EquipmentDefinitionStoreErrorCode::InvalidEquipmentDefinition,
        "rejects unsupported JSON schema versions before persistence");

    auto current = opened.value()->loadCurrentDefinition(
        MicroscopeId::create("microscope-a").value());
    check(static_cast<bool>(current) && !current.value().has_value(),
        "failed validation leaves no current equipment definition");

    opened.value().reset();
    removeDatabase(path);
}

} // namespace

int main() {
    verifiesAppendOnlyRevisionsAndRestartRoundTrip();
    isolatesMicroscopesAndRejectsStaleWriters();
    rejectsInvalidDocumentsBeforePersistence();

    if (failures != 0) return 1;
    std::cout << "Equipment-definition SQLite store tests passed\n";
    return 0;
}
