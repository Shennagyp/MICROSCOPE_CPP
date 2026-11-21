cmake_minimum_required(VERSION 3.21)
project(AmscopeCameraDriver VERSION 0.1.0 LANGUAGES CXX)

if(NOT APPLE)
    message(FATAL_ERROR
        "This initial Amscope target uses the bundled macOS vendor SDK. "
        "Windows and Linux backends are not part of this target.")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(CTest)

add_subdirectory(
    "${CMAKE_CURRENT_LIST_DIR}/../Common"
    "${CMAKE_CURRENT_BINARY_DIR}/camera_common")
set(AMSCOPE_VENDOR_SDK_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../VendorSDK/AmscopeVendorSDK"
    CACHE PATH "Path to the bundled Amscope vendor SDK")
set(AMSCOPE_VENDOR_MAC_ARCH "auto" CACHE STRING
    "Vendor macOS library directory: auto, x64+arm64, or x64+x86")
set_property(CACHE AMSCOPE_VENDOR_MAC_ARCH PROPERTY STRINGS auto x64+arm64 x64+x86)

if(AMSCOPE_VENDOR_MAC_ARCH STREQUAL "auto")
    if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
        set(_amscope_vendor_arch "x64+arm64")
    elseif(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
        set(_amscope_vendor_arch "x64+x86")
    elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(_amscope_vendor_arch "x64+arm64")
    else()
        set(_amscope_vendor_arch "x64+x86")
    endif()
else()
    set(_amscope_vendor_arch "${AMSCOPE_VENDOR_MAC_ARCH}")
endif()

if(NOT _amscope_vendor_arch STREQUAL "x64+arm64" AND
   NOT _amscope_vendor_arch STREQUAL "x64+x86")
    message(FATAL_ERROR "Unsupported vendor macOS architecture: ${_amscope_vendor_arch}")
endif()

set(AMSCOPE_VENDOR_INCLUDE_DIR "${AMSCOPE_VENDOR_SDK_ROOT}/inc")
set(AMSCOPE_VENDOR_LIBRARY
    "${AMSCOPE_VENDOR_SDK_ROOT}/mac/${_amscope_vendor_arch}/libamcam.dylib")

if(NOT EXISTS "${AMSCOPE_VENDOR_INCLUDE_DIR}/amcam.h")
    message(FATAL_ERROR "Vendor header not found: ${AMSCOPE_VENDOR_INCLUDE_DIR}/amcam.h")
endif()
if(NOT EXISTS "${AMSCOPE_VENDOR_LIBRARY}")
    message(FATAL_ERROR "Vendor library not found: ${AMSCOPE_VENDOR_LIBRARY}")
endif()

add_library(amscope_vendor SHARED IMPORTED GLOBAL)
set_target_properties(amscope_vendor PROPERTIES IMPORTED_LOCATION "${AMSCOPE_VENDOR_LIBRARY}")

add_library(amscope_driver SHARED
    src/AmcamCamera.cpp
    src/AmcamCameraProfiles.cpp
    include/amscope/AmcamCamera.h
    include/amscope/AmcamCameraProfiles.h
    include/amscope/CameraTypes.h)
add_library(Amscope::Driver ALIAS amscope_driver)

target_compile_features(amscope_driver PUBLIC cxx_std_17)
target_include_directories(amscope_driver
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${AMSCOPE_VENDOR_INCLUDE_DIR}")
target_link_libraries(amscope_driver PUBLIC Camera::Common PRIVATE amscope_vendor)

set_target_properties(amscope_driver PROPERTIES
    VERSION "${PROJECT_VERSION}"
    SOVERSION "0"
    OUTPUT_NAME "camera_amscope_driver"
    BUILD_RPATH "@loader_path"
    INSTALL_RPATH "@loader_path")

add_custom_command(TARGET amscope_driver POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${AMSCOPE_VENDOR_LIBRARY}"
        "$<TARGET_FILE_DIR:amscope_driver>/libamcam.dylib"
    VERBATIM)

option(AMSCOPE_BUILD_QT_ADAPTER "Build the Qt 6 Core adapter" ON)
option(AMSCOPE_BUILD_PREVIEW_APP "Build the Qt Widgets preview application" ON)

if(BUILD_TESTING)
    add_executable(amscope_profile_tests
        tests/AmcamCameraProfileTests.cpp
        src/AmcamCameraProfiles.cpp
        include/amscope/AmcamCameraProfiles.h)
    target_compile_features(amscope_profile_tests PRIVATE cxx_std_17)
    target_include_directories(amscope_profile_tests PRIVATE include)
    add_test(NAME amscope_profile_tests COMMAND amscope_profile_tests)
endif()

if(AMSCOPE_BUILD_QT_ADAPTER OR AMSCOPE_BUILD_PREVIEW_APP)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Core)
endif()

if(AMSCOPE_BUILD_QT_ADAPTER)
    add_library(amscope_qt
        src/QtAmcamCamera.cpp
        include/amscope/QtAmcamCamera.h)
    add_library(Amscope::Qt ALIAS amscope_qt)
    set_target_properties(amscope_qt PROPERTIES
        AUTOMOC ON
        OUTPUT_NAME "camera_amscope_qt")
    target_compile_features(amscope_qt PUBLIC cxx_std_17)
    target_include_directories(amscope_qt
        PUBLIC
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
            "$<INSTALL_INTERFACE:include>")
    target_link_libraries(amscope_qt PUBLIC Amscope::Driver Qt6::Core)
endif()

if(AMSCOPE_BUILD_PREVIEW_APP)
    if(NOT AMSCOPE_BUILD_QT_ADAPTER)
        message(FATAL_ERROR "AMSCOPE_BUILD_PREVIEW_APP requires AMSCOPE_BUILD_QT_ADAPTER=ON")
    endif()

    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

    add_executable(amscope_preview app/main.cpp)
    target_compile_features(amscope_preview PRIVATE cxx_std_17)
    target_link_libraries(amscope_preview PRIVATE Amscope::Qt Qt6::Widgets)
    set_target_properties(amscope_preview PROPERTIES
        BUILD_RPATH "@loader_path")
endif()

include(GNUInstallDirs)
install(TARGETS amscope_driver LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES "${AMSCOPE_VENDOR_LIBRARY}"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}" RENAME "libamcam.dylib")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../Common/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(AMSCOPE_BUILD_QT_ADAPTER)
    install(TARGETS amscope_qt
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()

if(AMSCOPE_BUILD_PREVIEW_APP)
    install(TARGETS amscope_preview
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
