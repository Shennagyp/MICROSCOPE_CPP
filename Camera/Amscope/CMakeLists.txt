cmake_minimum_required(VERSION 3.21)

# Set the deployment target before enabling C++/Swift/Objective-C++ compilers.
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
        "Minimum macOS version for Amscope targets")
endif()

project(AmscopeCameraDriver VERSION 0.1.0 LANGUAGES CXX)

if(NOT APPLE)
    message(FATAL_ERROR
        "This initial Amscope target uses the bundled macOS vendor SDK. "
        "Windows and Linux backends are not part of this target.")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(CTest)

if(NOT TARGET Camera::Common)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../Common"
        "${CMAKE_CURRENT_BINARY_DIR}/camera_common")
endif()
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
    src/AmcamFeatureAccessValidation.h
    src/AmcamCameraProfiles.cpp
    src/AmcamEventQueue.h
    src/AmcamSessionPrimitives.h
    include/amscope/detail/FrameStallWatchdog.h
    include/amscope/AmcamCamera.h
    include/amscope/AmcamCameraProfiles.h
    include/amscope/CameraTypes.h
    ../Common/include/camera/CameraModels.h
    ../Common/include/camera/CameraFrames.h)
add_library(Amscope::Driver ALIAS amscope_driver)
set_target_properties(amscope_driver PROPERTIES EXPORT_NAME Driver)

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

# Optional Phase 3/4 integration targets. The core driver and common
# contracts remain Qt-free; Qt streaming and reusable camera UI remain supported
# Camera Module deliverables for consumers that choose the Qt stack.
option(AMSCOPE_BUILD_QT_ADAPTER "Build the optional AmScope Qt control/streaming adapter" OFF)
option(AMSCOPE_BUILD_PREVIEW_APP "Build the optional Qt Widgets preview composition app" OFF)
if(CAMERA_MODULE_PACKAGE_BUILD)
    set(_amscope_install_vendor_runtime_default OFF)
else()
    set(_amscope_install_vendor_runtime_default ON)
endif()
option(AMSCOPE_INSTALL_VENDOR_RUNTIME
    "Install the bundled AmScope vendor runtime; requires redistribution rights"
    ${_amscope_install_vendor_runtime_default})
# Standalone diagnostics. These tools intentionally use different acquisition
# paths and are never part of the production camera session or Qt preview.
option(AMSCOPE_BUILD_NATIVE_PROBE
    "Build the native AmcamCamera acquisition probe (no Qt)" OFF)
option(AMSCOPE_BUILD_SDK_WAIT_PROBE
    "Build the direct vendor WaitImageV3 reference probe (no Qt)" OFF)

if(AMSCOPE_BUILD_NATIVE_PROBE)
    add_executable(amscope_native_probe app/native_stream_probe.cpp)
    target_compile_features(amscope_native_probe PRIVATE cxx_std_17)
    target_link_libraries(amscope_native_probe PRIVATE Amscope::Driver)
    set_target_properties(amscope_native_probe PROPERTIES
        BUILD_RPATH "@loader_path"
        INSTALL_RPATH "@loader_path")
endif()

if(AMSCOPE_BUILD_SDK_WAIT_PROBE)
    add_executable(amscope_sdk_wait_probe app/vendor_wait_probe.cpp)
    target_compile_features(amscope_sdk_wait_probe PRIVATE cxx_std_17)
    target_include_directories(amscope_sdk_wait_probe PRIVATE
        "${AMSCOPE_VENDOR_INCLUDE_DIR}")
    target_link_libraries(amscope_sdk_wait_probe PRIVATE amscope_vendor)
    set_target_properties(amscope_sdk_wait_probe PROPERTIES
        BUILD_RPATH "@loader_path"
        INSTALL_RPATH "@loader_path")
    add_custom_command(TARGET amscope_sdk_wait_probe POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${AMSCOPE_VENDOR_LIBRARY}"
            "$<TARGET_FILE_DIR:amscope_sdk_wait_probe>/libamcam.dylib"
        VERBATIM)
endif()

if(BUILD_TESTING)
    add_executable(amscope_camera_types_tests
        tests/AmcamCameraTypesTests.cpp)
    target_compile_features(amscope_camera_types_tests PRIVATE cxx_std_17)
    target_include_directories(amscope_camera_types_tests PRIVATE include)
    target_link_libraries(amscope_camera_types_tests PRIVATE Camera::Common)
    add_test(NAME amscope_camera_types_tests COMMAND amscope_camera_types_tests)

    add_executable(amscope_acquisition_frame_gate_tests
        tests/AmcamAcquisitionFrameGateTests.cpp)
    target_compile_features(amscope_acquisition_frame_gate_tests PRIVATE cxx_std_17)
    target_include_directories(amscope_acquisition_frame_gate_tests PRIVATE include)
    add_test(NAME amscope_acquisition_frame_gate_tests
        COMMAND amscope_acquisition_frame_gate_tests)

    add_executable(amscope_profile_tests
        tests/AmcamCameraProfileTests.cpp
        src/AmcamCameraProfiles.cpp
        include/amscope/AmcamCameraProfiles.h)
    target_compile_features(amscope_profile_tests PRIVATE cxx_std_17)
    target_include_directories(amscope_profile_tests PRIVATE include)
    add_test(NAME amscope_profile_tests COMMAND amscope_profile_tests)

    add_executable(amscope_feature_access_validation_tests
        tests/AmcamFeatureAccessValidationTests.cpp
        src/AmcamFeatureAccessValidation.h)
    target_compile_features(amscope_feature_access_validation_tests PRIVATE cxx_std_17)
    target_include_directories(amscope_feature_access_validation_tests PRIVATE
        src
        "${AMSCOPE_VENDOR_INCLUDE_DIR}")
    target_link_libraries(amscope_feature_access_validation_tests PRIVATE Camera::Common)
    add_test(NAME amscope_feature_access_validation_tests
        COMMAND amscope_feature_access_validation_tests)

    add_executable(amscope_event_queue_tests
        tests/AmcamEventQueueTests.cpp
        src/AmcamEventQueue.h)
    target_compile_features(amscope_event_queue_tests PRIVATE cxx_std_17)
    target_include_directories(amscope_event_queue_tests PRIVATE src)
    add_test(NAME amscope_event_queue_tests COMMAND amscope_event_queue_tests)

    add_executable(amscope_session_primitives_tests
        tests/AmcamSessionPrimitivesTests.cpp
        src/AmcamSessionPrimitives.h)
    target_compile_features(amscope_session_primitives_tests PRIVATE cxx_std_17)
    target_include_directories(amscope_session_primitives_tests PRIVATE src)
    add_test(NAME amscope_session_primitives_tests
        COMMAND amscope_session_primitives_tests)

    add_executable(amscope_frame_stall_watchdog_tests
        tests/AmcamFrameStallWatchdogTests.cpp
        include/amscope/detail/FrameStallWatchdog.h)
    target_compile_features(amscope_frame_stall_watchdog_tests PRIVATE cxx_std_17)
    target_include_directories(amscope_frame_stall_watchdog_tests PRIVATE include)
    add_test(NAME amscope_frame_stall_watchdog_tests
        COMMAND amscope_frame_stall_watchdog_tests)

    # Compile and exercise the public non-Qt AmcamCamera API without
    # enumerating devices or requiring a physical camera.
    add_executable(amscope_public_api_tests
        tests/AmcamPublicApiTests.cpp)
    target_compile_features(amscope_public_api_tests PRIVATE cxx_std_17)
    target_link_libraries(amscope_public_api_tests PRIVATE Amscope::Driver)
    add_test(NAME amscope_public_api_tests COMMAND amscope_public_api_tests)
endif()

if(AMSCOPE_BUILD_QT_ADAPTER OR AMSCOPE_BUILD_PREVIEW_APP)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Core)
    if(NOT TARGET Camera::Qt)
        add_subdirectory(
            "${CMAKE_CURRENT_LIST_DIR}/../Qt"
            "${CMAKE_CURRENT_BINARY_DIR}/camera_qt")
    endif()
endif()

if(AMSCOPE_BUILD_QT_ADAPTER)
    add_library(amscope_qt
        src/QtAmcamCamera.cpp
        legacy/qt/include/amscope/QtAmcamCamera.h)
    add_library(Amscope::Qt ALIAS amscope_qt)
    set_target_properties(amscope_qt PROPERTIES
        AUTOMOC ON
        OUTPUT_NAME "camera_amscope_qt"
        EXPORT_NAME Qt)
    target_compile_features(amscope_qt PUBLIC cxx_std_17)
    target_include_directories(amscope_qt
        PUBLIC
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
            "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/legacy/qt/include>"
            "$<INSTALL_INTERFACE:include>")
    target_link_libraries(amscope_qt PUBLIC Amscope::Driver Camera::Qt Qt6::Core)
endif()

if(BUILD_TESTING AND AMSCOPE_BUILD_QT_ADAPTER)
    add_executable(amscope_qt_request_tests
        tests/QtAmcamCameraRequestTests.cpp)
    target_compile_features(amscope_qt_request_tests PRIVATE cxx_std_17)
    target_link_libraries(amscope_qt_request_tests PRIVATE Amscope::Qt Qt6::Core)
    add_test(NAME amscope_qt_request_tests COMMAND amscope_qt_request_tests)
endif()

if(AMSCOPE_BUILD_PREVIEW_APP)
    if(NOT AMSCOPE_BUILD_QT_ADAPTER)
        message(FATAL_ERROR "AMSCOPE_BUILD_PREVIEW_APP requires AMSCOPE_BUILD_QT_ADAPTER=ON")
    endif()

    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
    if(NOT TARGET Camera::QtWidgets)
        add_subdirectory(
            "${CMAKE_CURRENT_LIST_DIR}/../QtWidgets"
            "${CMAKE_CURRENT_BINARY_DIR}/camera_qt_widgets")
    endif()

    add_executable(amscope_preview app/main.cpp)
    target_compile_features(amscope_preview PRIVATE cxx_std_17)
    target_link_libraries(amscope_preview PRIVATE Amscope::Qt Camera::QtWidgets Qt6::Widgets)
    set_target_properties(amscope_preview PROPERTIES
        BUILD_RPATH "@loader_path")

    if(BUILD_TESTING)
        add_test(NAME amscope_preview_ui_composition_tests
            COMMAND amscope_preview --self-test-ui)
        set_tests_properties(amscope_preview_ui_composition_tests PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()

    # Physical-camera performance test. Intentionally not registered with
    # CTest because hosted CI has no camera attached.
    add_executable(amscope_hardware_rate_test app/hardware_rate_test.cpp)
    target_compile_features(amscope_hardware_rate_test PRIVATE cxx_std_17)
    target_link_libraries(amscope_hardware_rate_test
        PRIVATE Amscope::Driver Qt6::Widgets)
    set_target_properties(amscope_hardware_rate_test PROPERTIES
        BUILD_RPATH "@loader_path")
endif()

include(GNUInstallDirs)
install(TARGETS amscope_driver
    EXPORT AmscopeDriverTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(AMSCOPE_INSTALL_VENDOR_RUNTIME)
    install(FILES "${AMSCOPE_VENDOR_LIBRARY}"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}" RENAME "libamcam.dylib")
endif()
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

if(AMSCOPE_BUILD_QT_ADAPTER)
    install(DIRECTORY legacy/qt/include/
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(TARGETS amscope_qt
        EXPORT AmscopeQtTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if(AMSCOPE_BUILD_PREVIEW_APP)
    install(TARGETS amscope_preview
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
