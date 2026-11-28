cmake_minimum_required(VERSION 3.21)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(ControllerCommon VERSION 0.9.0 LANGUAGES CXX)
    include(CTest)
endif()

if(NOT TARGET Microscope::Core)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../../Libraries/Microscope.Core"
        "${CMAKE_CURRENT_BINARY_DIR}/microscope_core")
endif()

add_library(controller_common STATIC src/Stage.cpp src/StageMotionCommandTranslator.cpp)
add_library(Controller::Common ALIAS controller_common)
target_compile_features(controller_common PUBLIC cxx_std_17)
target_include_directories(controller_common PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
    "$<INSTALL_INTERFACE:include>")
target_link_libraries(controller_common PUBLIC Microscope::Core)
set_target_properties(controller_common PROPERTIES
    POSITION_INDEPENDENT_CODE ON CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS OFF)
if(BUILD_TESTING)
    add_executable(controller_common_stage_tests tests/StageTests.cpp)
    target_link_libraries(controller_common_stage_tests PRIVATE Controller::Common)
    set_target_properties(controller_common_stage_tests PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS OFF)
    add_test(NAME controller_common_stage_tests COMMAND controller_common_stage_tests)

    add_executable(controller_common_stage_motion_translation_tests
        tests/StageMotionCommandTranslatorTests.cpp)
    target_link_libraries(controller_common_stage_motion_translation_tests
        PRIVATE Controller::Common)
    set_target_properties(controller_common_stage_motion_translation_tests PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED YES CXX_EXTENSIONS OFF)
    add_test(NAME controller_common_stage_motion_translation_tests
        COMMAND controller_common_stage_motion_translation_tests)
endif()
