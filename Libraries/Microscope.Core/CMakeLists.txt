cmake_minimum_required(VERSION 3.21)

# Permit this library to be configured independently or included by the
# repository's root project.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(MicroscopeCore VERSION 0.9.0 LANGUAGES CXX)
    include(CTest)
endif()

add_library(microscope_core STATIC
    src/Identity.cpp)
add_library(Microscope::Core ALIAS microscope_core)

target_compile_features(microscope_core PUBLIC cxx_std_17)
target_include_directories(microscope_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

set_target_properties(microscope_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    VERSION "0.9.0"
    SOVERSION "0"
    CXX_EXTENSIONS OFF)

if(BUILD_TESTING)
    add_executable(microscope_core_contract_tests
        tests/CoreContractTests.cpp)
    target_link_libraries(microscope_core_contract_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_contract_tests PROPERTIES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_core_contract_tests
        COMMAND microscope_core_contract_tests)

    add_executable(microscope_core_feedback_tests
        tests/FeedbackContractTests.cpp)
    target_link_libraries(microscope_core_feedback_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_feedback_tests PROPERTIES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_core_feedback_tests
        COMMAND microscope_core_feedback_tests)
endif()
