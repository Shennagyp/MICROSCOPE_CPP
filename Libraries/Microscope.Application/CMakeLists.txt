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

add_library(microscope_application STATIC
    src/RuntimeHandle.cpp
    src/RuntimeWakeSignal.cpp)
add_library(Microscope::Application ALIAS microscope_application)

target_compile_features(microscope_application PUBLIC cxx_std_17)
target_include_directories(microscope_application
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

target_link_libraries(microscope_application
    PUBLIC Microscope::Core)

set_target_properties(microscope_application PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF)

if(BUILD_TESTING)
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

    add_executable(microscope_application_stage_position_feedback_reducer_tests
        tests/StagePositionFeedbackReducerTests.cpp)
    target_link_libraries(microscope_application_stage_position_feedback_reducer_tests
        PRIVATE Microscope::Application)
    set_target_properties(microscope_application_stage_position_feedback_reducer_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_application_stage_position_feedback_reducer_tests
        COMMAND microscope_application_stage_position_feedback_reducer_tests)

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
