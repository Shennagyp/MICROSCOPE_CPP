cmake_minimum_required(VERSION 3.21)

project(CameraGenTL VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

add_library(camera_gentl_consumer_bridge STATIC
    src/ConsumerBridge.cpp
    include/camera/gentl/ConsumerBridge.h)
add_library(Camera::GenTLConsumerBridge ALIAS camera_gentl_consumer_bridge)

target_compile_features(camera_gentl_consumer_bridge PUBLIC cxx_std_17)
target_include_directories(camera_gentl_consumer_bridge
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
target_link_libraries(camera_gentl_consumer_bridge PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(camera_gentl_consumer_bridge PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF)

if(BUILD_TESTING)
    add_library(gentl_consumer_bridge_fixture SHARED
        tests/MockConsumerBridge.c)
    set_target_properties(gentl_consumer_bridge_fixture PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS OFF
        PREFIX "lib"
        OUTPUT_NAME "gentl_consumer_bridge_fixture")

    add_executable(camera_gentl_consumer_bridge_tests
        tests/ConsumerBridgeTests.cpp)
    target_compile_features(camera_gentl_consumer_bridge_tests PRIVATE cxx_std_17)
    target_link_libraries(camera_gentl_consumer_bridge_tests
        PRIVATE Camera::GenTLConsumerBridge)
    add_test(NAME camera_gentl_consumer_bridge_tests
        COMMAND camera_gentl_consumer_bridge_tests
            "$<TARGET_FILE:gentl_consumer_bridge_fixture>")
endif()
