cmake_minimum_required(VERSION 3.21)

# Permit the Application library to be configured independently. In that
# mode it brings in its required Core target as a source dependency.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(MicroscopeApplication VERSION 0.9.0 LANGUAGES CXX)
    include(CTest)
endif()

if(NOT TARGET Microscope::Core)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../Microscope.Core"
        "${CMAKE_CURRENT_BINARY_DIR}/microscope_core")
endif()

if(NOT TARGET Camera::Common)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../Camera/Common"
        "${CMAKE_CURRENT_BINARY_DIR}/camera_common")
endif()

if(NOT TARGET Controller::Common)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../Controller/Common"
        "${CMAKE_CURRENT_BINARY_DIR}/controller_common")
endif()

find_package(SQLite3 REQUIRED)

add_library(microscope_application STATIC
    src/RuntimeHandle.cpp
    src/RuntimeWakeSignal.cpp
    src/CameraModelCatalogSqliteStore.cpp
    src/CameraComponentModelBindingSqliteStore.cpp
    src/CameraModelDiscoveryEnricher.cpp
    src/InstalledCameraModelEnricher.cpp
    src/MicroscopeConfigurationSqliteStore.cpp
    src/SampleSlideSqliteStore.cpp
    src/StageSlotAssignments.cpp
    src/StageSlotRegistration.cpp
    src/StageSlotAssignmentsSqliteStore.cpp
    src/ConfigurationProfileCatalogSqliteStore.cpp
    src/EquipmentDefinitionJsonLoader.cpp
    src/EquipmentDefinitionSqliteStore.cpp)
add_library(Microscope::Application ALIAS microscope_application)

target_compile_features(microscope_application PUBLIC cxx_std_17)
target_include_directories(microscope_application
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

target_link_libraries(microscope_application
    PUBLIC Microscope::Core Camera::Common Controller::Common
    PRIVATE SQLite::SQLite3)

set_target_properties(microscope_application PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF)

if(BUILD_TESTING)
    add_executable(microscope_application_camera_component_model_binding_store_tests
        tests/CameraComponentModelBindingSqliteStoreTests.cpp)
    target_link_libraries(microscope_application_camera_component_model_binding_store_tests
        PRIVATE Microscope::Application)
    set_target_properties(microscope_application_camera_component_model_binding_store_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_application_camera_component_model_binding_store_tests
        COMMAND microscope_application_camera_component_model_binding_store_tests)

    add_executable(microscope_application_camera_model_discovery_enricher_tests
        tests/CameraModelDiscoveryEnricherTests.cpp)
    target_link_libraries(microscope_application_camera_model_discovery_enricher_tests
        PRIVATE Microscope::Application)
    set_target_properties(microscope_application_camera_model_discovery_enricher_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_application_camera_model_discovery_enricher_tests
        COMMAND microscope_application_camera_model_discovery_enricher_tests)

    add_executable(microscope_application_microscope_configuration_store_tests
        tests/MicroscopeConfigurationSqliteStoreTests.cpp)
    target_link_libraries(microscope_application_microscope_configuration_store_tests
        PRIVATE Microscope::Application)
    set_target_properties(microscope_application_microscope_configuration_store_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_application_microscope_configuration_store_tests
        COMMAND microscope_application_microscope_configuration_store_tests)

    add_executable(microscope_application_sample_slide_store_tests
        tests/SampleSlideSqliteStoreTests.cpp)
    target_link_libraries(microscope_application_sample_slide_store_tests
        PRIVATE Microscope::Application SQLite::SQLite3)
    set_target_properties(microscope_application_sample_slide_store_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_application_sample_slide_store_tests
        COMMAND microscope_application_sample_slide_store_tests)

    add_executable(microscope_application_stage_slot_assignments_store_tests
        tests/StageSlotAssignmentsSqliteStoreTests.cpp)
    target_link_libraries(microscope_application_stage_slot_assignments_store_tests
        PRIVATE Microscope::Application SQLite::SQLite3)
    set_target_properties(microscope_application_stage_slot_assignments_store_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_application_stage_slot_assignments_store_tests
        COMMAND microscope_application_stage_slot_assignments_store_tests)

    add_executable(microscope_application_stage_slot_registration_tests
        tests/StageSlotRegistrationTests.cpp)
    target_link_libraries(microscope_application_stage_slot_registration_tests
        PRIVATE Microscope::Application)
    set_target_properties(microscope_application_stage_slot_registration_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_application_stage_slot_registration_tests
        COMMAND microscope_application_stage_slot_registration_tests)

    add_executable(microscope_application_stage_slot_assignments_tests
        tests/StageSlotAssignmentsTests.cpp)
    target_link_libraries(microscope_application_stage_slot_assignments_tests
        PRIVATE Microscope::Application)
    set_target_properties(microscope_application_stage_slot_assignments_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_application_stage_slot_assignments_tests
        COMMAND microscope_application_stage_slot_assignments_tests)


    add_executable(microscope_application_camera_model_catalog_store_tests
        tests/CameraModelCatalogSqliteStoreTests.cpp)
    target_link_libraries(microscope_application_camera_model_catalog_store_tests
        PRIVATE Microscope::Application)
    set_target_properties(microscope_application_camera_model_catalog_store_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_application_camera_model_catalog_store_tests
        COMMAND microscope_application_camera_model_catalog_store_tests)


    add_executable(microscope_application_configuration_profile_catalog_store_tests
        tests/ConfigurationProfileCatalogSqliteStoreTests.cpp)
    target_link_libraries(microscope_application_configuration_profile_catalog_store_tests
        PRIVATE Microscope::Application SQLite::SQLite3)
    set_target_properties(microscope_application_configuration_profile_catalog_store_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_application_configuration_profile_catalog_store_tests
        COMMAND microscope_application_configuration_profile_catalog_store_tests)

    add_executable(microscope_application_equipment_definition_json_loader_tests
        tests/EquipmentDefinitionJsonLoaderTests.cpp)
    target_link_libraries(microscope_application_equipment_definition_json_loader_tests
        PRIVATE Microscope::Application)
    set_target_properties(microscope_application_equipment_definition_json_loader_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_application_equipment_definition_json_loader_tests
        COMMAND microscope_application_equipment_definition_json_loader_tests)

    add_executable(microscope_application_equipment_definition_store_tests
        tests/EquipmentDefinitionSqliteStoreTests.cpp)
    target_link_libraries(microscope_application_equipment_definition_store_tests
        PRIVATE Microscope::Application SQLite::SQLite3)
    set_target_properties(microscope_application_equipment_definition_store_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_application_equipment_definition_store_tests
        COMMAND microscope_application_equipment_definition_store_tests)


    add_executable(microscope_application_contract_tests
        tests/ApplicationContractTests.cpp)
    target_link_libraries(microscope_application_contract_tests
        PRIVATE Microscope::Application)
    set_target_properties(microscope_application_contract_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_application_contract_tests
        COMMAND microscope_application_contract_tests)

    add_executable(microscope_application_runtime_wake_signal_tests
        tests/RuntimeWakeSignalTests.cpp)
    target_link_libraries(microscope_application_runtime_wake_signal_tests
        PRIVATE Microscope::Application)
    set_target_properties(microscope_application_runtime_wake_signal_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_application_runtime_wake_signal_tests
        COMMAND microscope_application_runtime_wake_signal_tests)

    add_executable(microscope_application_serialized_runtime_worker_tests
        tests/SerializedRuntimeWorkerTests.cpp)
    target_link_libraries(microscope_application_serialized_runtime_worker_tests
        PRIVATE Microscope::Application)
    set_target_properties(microscope_application_serialized_runtime_worker_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_application_serialized_runtime_worker_tests
        COMMAND microscope_application_serialized_runtime_worker_tests)


    add_executable(microscope_application_stage_motion_dispatcher_tests
        tests/StageMotionCommandDispatcherTests.cpp)
    target_link_libraries(microscope_application_stage_motion_dispatcher_tests
        PRIVATE Microscope::Application)
    set_target_properties(microscope_application_stage_motion_dispatcher_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_application_stage_motion_dispatcher_tests
        COMMAND microscope_application_stage_motion_dispatcher_tests)


endif()
