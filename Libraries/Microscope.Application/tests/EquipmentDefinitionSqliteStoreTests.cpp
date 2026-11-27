#include "microscope/application/persistence/EquipmentDefinitionSqliteStore.h"

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

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
    if (!result) throw std::runtime_error("equipment-definition store fixture rejected");
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
        ("microscope-equipment-definition-store-" +
            std::to_string(nonce) + ".sqlite")).string();
}

void removeDatabaseFiles(const std::string& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path + "-wal", ignored);
    std::filesystem::remove(path + "-shm", ignored);
}

std::string document(std::uint64_t revision, bool includeSecondComponent = false) {
    std::string result = R"JSON({
  "schemaVersion": 1,
  "microscopeId": "scope-equipment-store",
  "revision": REVISION,
  "components": [
    { "id": "mechanical-frame", "kind": "other" }
  ],
  "connections": [],
  "opticalPaths": []
})JSON";
    const auto token = result.find("REVISION");
    if (token == std::string::npos) throw std::runtime_error("missing revision fixture token");
    result.replace(token, 8, std::to_string(revision));
    if (includeSecondComponent) {
        const auto anchor = R"(    { "id": "mechanical-frame", "kind": "other" })";
        const auto position = result.find(anchor);
        if (position == std::string::npos) throw std::runtime_error("missing component fixture anchor");
        result.insert(position + anchor.size(),
            R"(,
    { "id": "optical-module", "kind": "optical-element",
      "ports": [
        { "id": "module-input", "direction": "input" },
        { "id": "module-output", "direction": "output" }
      ] })");
    }
    return result;
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

void opensIndependentDatabaseAndValidatesInput() {
    auto emptyPath = EquipmentDefinitionSqliteStore::open("");
    check(!emptyPath &&
        emptyPath.error().code == EquipmentDefinitionStoreErrorCode::EmptyDatabasePath,
        "rejects an empty database path");

    const auto path = uniqueDatabasePath();
    auto opened = EquipmentDefinitionSqliteStore::open(path);
    check(static_cast<bool>(opened),
        "creates an equipment-definition repository without requiring another store");

    if (opened) {
        auto empty = opened.value()->loadCurrentDefinition(
            id<MicroscopeId>("scope-no-definition"));
        check(empty && !empty.value(),
            "returns no equipment definition when the microscope has no revision");
        auto malformed = opened.value()->saveCurrentDefinition(
            id<MicroscopeId>("scope-equipment-store"), "{", 0);
        check(!malformed &&
            malformed.error().code == EquipmentDefinitionStoreErrorCode::InvalidDefinitionDocument &&
            malformed.error().jsonError.has_value(),
            "rejects invalid JSON and preserves the loader's structured parse diagnostic");
        opened.value().reset();
    }
    removeDatabaseFiles(path);
}

void savesExactValidatedDocumentsAndLoadsHistoricalRevisions() {
    const auto path = uniqueDatabasePath();
    auto opened = EquipmentDefinitionSqliteStore::open(path);
    if (!opened) throw std::runtime_error("could not open equipment-definition store");
    auto& store = *opened.value();

    const auto microscopeId = id<MicroscopeId>("scope-equipment-store");
    const std::string firstDocument = document(1);
    const auto first = store.saveCurrentDefinition(microscopeId, firstDocument, 0);
    check(first && first.value() == 1,
        "appends the first equipment definition revision");
    if (!first) {
        opened.value().reset();
        removeDatabaseFiles(path);
        return;
    }

    auto loadedFirst = store.loadCurrentDefinition(microscopeId);
    check(loadedFirst && loadedFirst.value(),
        "loads the current equipment definition");
    if (loadedFirst && loadedFirst.value()) {
        check(loadedFirst.value()->revision == 1 &&
            loadedFirst.value()->equipment.revision() == 1 &&
            loadedFirst.value()->equipment.microscopeId() == microscopeId,
            "reconstructs the canonical Core equipment definition with its stored identity and revision");
        check(loadedFirst.value()->sourceDocument == firstDocument,
            "preserves the exact original JSON source bytes rather than normalizing/re-serializing the model");
        check(loadedFirst.value()->equipment.components().size() == 1,
            "restores the physical component graph from the source document");
    }

    const std::string secondDocument = document(2, true);
    const auto second = store.saveCurrentDefinition(microscopeId, secondDocument, 1);
    check(second && second.value() == 2,
        "appends the next equipment definition revision");
    auto historical = store.loadRevision(microscopeId, 1);
    auto current = store.loadCurrentDefinition(microscopeId);
    check(historical && historical.value() &&
        historical.value()->sourceDocument == firstDocument &&
        historical.value()->equipment.components().size() == 1,
        "retains the exact historical source document and graph");
    check(current && current.value() && current.value()->revision == 2 &&
        current.value()->sourceDocument == secondDocument &&
        current.value()->equipment.components().size() == 2,
        "loads the latest definition without rewriting the historical version");

    auto conflict = store.saveCurrentDefinition(microscopeId, document(2), 1);
    check(!conflict &&
        conflict.error().code == EquipmentDefinitionStoreErrorCode::RevisionConflict,
        "rejects a stale writer that attempts to replace a newer equipment revision");

    auto mismatchedRevision = store.saveCurrentDefinition(
        microscopeId, document(9), 2);
    check(!mismatchedRevision &&
        mismatchedRevision.error().code == EquipmentDefinitionStoreErrorCode::RevisionMismatch,
        "requires the source document revision to be exactly expected current plus one");

    const auto otherMicroscope = id<MicroscopeId>("scope-other-equipment");
    auto mismatchedMicroscope = store.saveCurrentDefinition(
        otherMicroscope, document(1), 0);
    check(!mismatchedMicroscope &&
        mismatchedMicroscope.error().code == EquipmentDefinitionStoreErrorCode::InvalidDefinitionDocument,
        "rejects a source document whose MicroscopeId differs from the requested repository scope");

    auto absentRevision = store.loadRevision(microscopeId, 3);
    auto zeroRevision = store.loadRevision(microscopeId, 0);
    check(absentRevision && !absentRevision.value() &&
        !zeroRevision && zeroRevision.error().code ==
            EquipmentDefinitionStoreErrorCode::InvalidRevision,
        "distinguishes a missing historical revision from an invalid zero revision");

    opened.value().reset();
    removeDatabaseFiles(path);
}

void publishedEquipmentDefinitionDocumentsAreImmutable() {
    const auto path = uniqueDatabasePath();
    auto opened = EquipmentDefinitionSqliteStore::open(path);
    if (!opened) throw std::runtime_error("could not open equipment-definition store");

    const auto microscopeId = id<MicroscopeId>("scope-immutable-equipment");
    const auto saved = opened.value()->saveCurrentDefinition(
        microscopeId,
        R"({"schemaVersion":1,"microscopeId":"scope-immutable-equipment","revision":1,"components":[{"id":"frame","kind":"other"}],"connections":[],"opticalPaths":[]})",
        0);
    check(static_cast<bool>(saved),
        "writes a valid equipment definition before testing revision immutability");

    const bool updateSucceeded = sqlExec(path,
        "UPDATE microscope_equipment_definition_revision SET document_json = '{' "
        "WHERE microscope_id = 'scope-immutable-equipment' AND revision = 1");
    const bool deleteSucceeded = sqlExec(path,
        "DELETE FROM microscope_equipment_definition_revision "
        "WHERE microscope_id = 'scope-immutable-equipment' AND revision = 1");
    check(!updateSucceeded && !deleteSucceeded,
        "database triggers prevent update or deletion of published equipment-definition revisions");

    const bool droppedGuard = sqlExec(path,
        "DROP TRIGGER microscope_equipment_definition_revision_no_update");
    const bool corrupted = sqlExec(path,
        "UPDATE microscope_equipment_definition_revision SET document_json = '{' "
        "WHERE microscope_id = 'scope-immutable-equipment' AND revision = 1");
    check(droppedGuard && corrupted,
        "fixture can corrupt a document only after explicitly dropping the immutability guard");

    auto invalid = opened.value()->loadRevision(microscopeId, 1);
    check(!invalid &&
        invalid.error().code == EquipmentDefinitionStoreErrorCode::InvalidPersistedRecord &&
        invalid.error().jsonError.has_value(),
        "revalidates source JSON on load and reports corrupted persisted definitions");

    opened.value().reset();
    removeDatabaseFiles(path);
}

} // namespace

int main() {
    try {
        opensIndependentDatabaseAndValidatesInput();
        savesExactValidatedDocumentsAndLoadsHistoricalRevisions();
        publishedEquipmentDefinitionDocumentsAreImmutable();
    } catch (const std::exception& error) {
        std::cerr << "UNCAUGHT: " << error.what() << '\n';
        return 1;
    }

    if (failures != 0) return 1;
    std::cout << "Equipment-definition SQLite persistence tests passed\n";
    return 0;
}
