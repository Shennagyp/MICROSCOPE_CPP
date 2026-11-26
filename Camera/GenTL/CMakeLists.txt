cmake_minimum_required(VERSION 3.21)

project(CameraGenTL VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)

if(NOT TARGET Camera::Common)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../Common"
        "${CMAKE_CURRENT_BINARY_DIR}/camera_common")
endif()

add_library(camera_gentl_consumer_bridge STATIC
    src/ConsumerBridge.cpp
    src/GenTLProducerLibrary.cpp
    src/Producer.cpp
    src/GenTLFrameAdapter.cpp
    include/camera/gentl/ConsumerBridge.h
    include/camera/gentl/GenTLProducerLibrary.h
    include/camera/gentl/Producer.h
    include/camera/gentl/GenTLFrameAdapter.h
    include/camera/gentl/GenTL_v1_6.h)
add_library(Camera::GenTLConsumerBridge ALIAS camera_gentl_consumer_bridge)
set_target_properties(camera_gentl_consumer_bridge PROPERTIES EXPORT_NAME GenTLConsumerBridge)

target_compile_features(camera_gentl_consumer_bridge PUBLIC cxx_std_17)
target_include_directories(camera_gentl_consumer_bridge
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
target_link_libraries(camera_gentl_consumer_bridge
    PUBLIC Camera::Common
    PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(camera_gentl_consumer_bridge PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF)

install(TARGETS camera_gentl_consumer_bridge
    EXPORT CameraGenTLTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(BUILD_TESTING)
    add_library(gentl_consumer_bridge_fixture SHARED
        tests/MockConsumerBridge.c)
    target_include_directories(gentl_consumer_bridge_fixture PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/include")
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

    add_executable(camera_gentl_producer_tests
        tests/ProducerTests.cpp)
    target_compile_features(camera_gentl_producer_tests PRIVATE cxx_std_17)
    target_link_libraries(camera_gentl_producer_tests
        PRIVATE Camera::GenTLConsumerBridge)
    add_test(NAME camera_gentl_producer_tests
        COMMAND camera_gentl_producer_tests
            "$<TARGET_FILE:gentl_consumer_bridge_fixture>")

    add_executable(camera_gentl_cti_loader_tests
        tests/GenTLProducerLibraryTests.cpp)
    target_compile_features(camera_gentl_cti_loader_tests PRIVATE cxx_std_17)
    target_link_libraries(camera_gentl_cti_loader_tests
        PRIVATE Camera::GenTLConsumerBridge)
    add_test(NAME camera_gentl_cti_loader_tests
        COMMAND camera_gentl_cti_loader_tests
            "$<TARGET_FILE:gentl_consumer_bridge_fixture>")

    add_executable(camera_gentl_frame_adapter_tests
        tests/GenTLFrameAdapterTests.cpp)
    target_compile_features(camera_gentl_frame_adapter_tests PRIVATE cxx_std_17)
    target_link_libraries(camera_gentl_frame_adapter_tests
        PRIVATE Camera::GenTLConsumerBridge)
    add_test(NAME camera_gentl_frame_adapter_tests
        COMMAND camera_gentl_frame_adapter_tests)
endif()

option(CAMERA_GENTL_ENABLE_GENAPI
    "Build the optional GenApi 3.5.1 node-map integration" OFF)

if(CAMERA_GENTL_ENABLE_GENAPI)
    if(NOT TARGET Camera::Common)
        add_subdirectory(
            "${CMAKE_CURRENT_LIST_DIR}/../Common"
            "${CMAKE_CURRENT_BINARY_DIR}/camera_common")
    endif()

    set(GENAPI_ROOT "" CACHE PATH
        "Root of the EMVA GenApi 3.5.1 Mac64_Universal SDK")

    find_package(GenApi 3.5.1 EXACT CONFIG QUIET)
    if(CAMERA_MODULE_PACKAGE_BUILD AND NOT GenApi_FOUND)
        message(FATAL_ERROR
            "CameraModule package export with GenApi requires the official "
            "GenApi 3.5.1 CMake package. Set GenApi_DIR to its config directory.")
    endif()

    set(_camera_genapi_libraries "")
    set(_camera_genapi_include_dirs "")

    foreach(_candidate GenApi::GenApi GenApi GenICam::GenApi)
        if(TARGET ${_candidate})
            list(APPEND _camera_genapi_libraries ${_candidate})
            set(_camera_genapi_target_found TRUE)
            break()
        endif()
    endforeach()

    foreach(_candidate GenApi::GCBase GCBase::GCBase GenICam::GCBase GCBase)
        if(TARGET ${_candidate})
            list(APPEND _camera_genapi_libraries ${_candidate})
            set(_camera_gcbase_target_found TRUE)
            break()
        endif()
    endforeach()

    if(NOT _camera_genapi_target_found OR NOT _camera_gcbase_target_found)
        find_path(GENAPI_INCLUDE_DIR
            NAMES GenApi/GenApi.h
            HINTS "${GENAPI_ROOT}" "$ENV{GENICAM_ROOT}"
            PATH_SUFFIXES include library/CPP/include)

        if(NOT _camera_genapi_target_found)
            find_library(GENAPI_LIBRARY
                NAMES GenApi GenApi_v3_5 GenApi_3_5_1
                HINTS "${GENAPI_ROOT}" "$ENV{GENICAM_ROOT}"
                PATH_SUFFIXES
                    lib
                    lib/Mac64_Universal
                    lib/Mac64_Universal/Release
                    library/CPP/lib/Mac64_Universal
                    library/CPP/lib/Mac64_Universal/Release)
            if(GENAPI_LIBRARY)
                list(APPEND _camera_genapi_libraries "${GENAPI_LIBRARY}")
            endif()
        endif()

        if(NOT _camera_gcbase_target_found)
            find_library(GCBASE_LIBRARY
                NAMES GCBase GCBase_v3_5 GCBase_3_5_1
                HINTS "${GENAPI_ROOT}" "$ENV{GENICAM_ROOT}"
                PATH_SUFFIXES
                    lib
                    lib/Mac64_Universal
                    lib/Mac64_Universal/Release
                    library/CPP/lib/Mac64_Universal
                    library/CPP/lib/Mac64_Universal/Release)
            if(GCBASE_LIBRARY)
                list(APPEND _camera_genapi_libraries "${GCBASE_LIBRARY}")
            endif()
        endif()

        if(NOT GENAPI_INCLUDE_DIR OR
           (NOT _camera_genapi_target_found AND NOT GENAPI_LIBRARY) OR
           (NOT _camera_gcbase_target_found AND NOT GCBASE_LIBRARY))
            message(FATAL_ERROR
                "GenApi 3.5.1 was requested but its headers, GenApi library, "
                "and GCBase library were not found. Set GENAPI_ROOT or GenApi_DIR "
                "to the official Mac64_Universal SDK.")
        endif()

        if(GENAPI_INCLUDE_DIR)
            list(APPEND _camera_genapi_include_dirs "${GENAPI_INCLUDE_DIR}")
        endif()
    endif()

    add_library(camera_gentl_genapi STATIC
        src/GenApiNodeMap.cpp
        src/GenApiFeatureAccess.cpp
        src/GenApiCommandAccess.cpp
        src/GenApiDeviceAccess.cpp
        src/GenApiFeatureTree.h
        include/camera/gentl/GenApiDeviceAccess.h
        include/camera/gentl/GenApiNodeMap.h
        include/camera/gentl/GenApiFeatureAccess.h
        include/camera/gentl/GenApiCommandAccess.h)
    add_library(Camera::GenTLGenApi ALIAS camera_gentl_genapi)
    set_target_properties(camera_gentl_genapi PROPERTIES EXPORT_NAME GenTLGenApi)

    target_compile_features(camera_gentl_genapi PUBLIC cxx_std_17)
    target_include_directories(camera_gentl_genapi
        PUBLIC
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
            "$<INSTALL_INTERFACE:include>"
            "$<BUILD_INTERFACE:${_camera_genapi_include_dirs}>")
    target_link_libraries(camera_gentl_genapi
        PUBLIC Camera::GenTLConsumerBridge Camera::Common ${_camera_genapi_libraries})
    set_target_properties(camera_gentl_genapi PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)

    install(TARGETS camera_gentl_genapi
        EXPORT CameraGenTLGenApiTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

    if(BUILD_TESTING)
        add_executable(camera_gentl_genapi_tests
            tests/GenApiIntegrationTests.cpp)
        target_compile_features(camera_gentl_genapi_tests PRIVATE cxx_std_17)
        target_link_libraries(camera_gentl_genapi_tests
            PRIVATE Camera::GenTLGenApi)
        add_test(NAME camera_gentl_genapi_tests
            COMMAND camera_gentl_genapi_tests
                "$<TARGET_FILE:gentl_consumer_bridge_fixture>")
    endif()
endif()
