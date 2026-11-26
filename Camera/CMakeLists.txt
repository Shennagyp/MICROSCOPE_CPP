cmake_minimum_required(VERSION 3.21)
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment target")
endif()
project(CameraModule VERSION 2.0.0 LANGUAGES C CXX)
include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(CAMERA_MODULE_BUILD_AMSCOPE "Build native AmScope provider (macOS/vendor SDK only)" ${APPLE})
option(CAMERA_MODULE_BUILD_GENTL "Build GenTL Consumer bridge library" ON)
option(CAMERA_MODULE_BUILD_GENAPI "Build optional GenApi 3.5.1 adapter" OFF)
option(CAMERA_MODULE_BUILD_QT "Build optional Qt Core frame-stream library" OFF)
option(CAMERA_MODULE_BUILD_QT_WIDGETS "Build optional reusable Qt Widgets library" OFF)
option(CAMERA_MODULE_BUILD_AMSCOPE_QT "Build optional AmScope Qt adapter" OFF)
option(CAMERA_MODULE_BUILD_PREVIEW_APP "Build AmScope preview app (not a package component)" OFF)

if(CAMERA_MODULE_BUILD_QT_WIDGETS OR CAMERA_MODULE_BUILD_AMSCOPE_QT OR CAMERA_MODULE_BUILD_PREVIEW_APP)
    set(CAMERA_MODULE_BUILD_QT ON)
endif()
if(CAMERA_MODULE_BUILD_PREVIEW_APP)
    set(CAMERA_MODULE_BUILD_QT_WIDGETS ON)
    set(CAMERA_MODULE_BUILD_AMSCOPE_QT ON)
endif()
if((CAMERA_MODULE_BUILD_AMSCOPE_QT OR CAMERA_MODULE_BUILD_PREVIEW_APP) AND NOT CAMERA_MODULE_BUILD_AMSCOPE)
    message(FATAL_ERROR "AmScope Qt targets require CAMERA_MODULE_BUILD_AMSCOPE=ON.")
endif()
if(CAMERA_MODULE_BUILD_GENAPI AND NOT CAMERA_MODULE_BUILD_GENTL)
    message(FATAL_ERROR "CAMERA_MODULE_BUILD_GENAPI requires CAMERA_MODULE_BUILD_GENTL=ON.")
endif()

set(CAMERA_MODULE_PACKAGE_BUILD TRUE)
add_subdirectory(Common)
if(CAMERA_MODULE_BUILD_GENTL)
    set(CAMERA_GENTL_ENABLE_GENAPI "${CAMERA_MODULE_BUILD_GENAPI}" CACHE BOOL
        "Build optional GenApi 3.5.1 integration" FORCE)
    add_subdirectory(GenTL)
endif()
if(CAMERA_MODULE_BUILD_QT)
    add_subdirectory(Qt)
endif()
if(CAMERA_MODULE_BUILD_QT_WIDGETS)
    add_subdirectory(QtWidgets)
endif()
if(CAMERA_MODULE_BUILD_AMSCOPE)
    set(AMSCOPE_BUILD_QT_ADAPTER "${CAMERA_MODULE_BUILD_AMSCOPE_QT}" CACHE BOOL
        "Build optional AmScope Qt adapter" FORCE)
    set(AMSCOPE_BUILD_PREVIEW_APP "${CAMERA_MODULE_BUILD_PREVIEW_APP}" CACHE BOOL
        "Build optional AmScope preview app" FORCE)
    add_subdirectory(Amscope)
endif()

install(EXPORT CameraCommonTargets FILE CameraCommonTargets.cmake NAMESPACE Camera::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CameraModule")
if(CAMERA_MODULE_BUILD_GENTL)
    install(EXPORT CameraGenTLTargets FILE CameraGenTLTargets.cmake NAMESPACE Camera::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CameraModule")
endif()
if(CAMERA_MODULE_BUILD_GENAPI)
    install(EXPORT CameraGenTLGenApiTargets FILE CameraGenTLGenApiTargets.cmake NAMESPACE Camera::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CameraModule")
endif()
if(CAMERA_MODULE_BUILD_QT)
    install(EXPORT CameraQtTargets FILE CameraQtTargets.cmake NAMESPACE Camera::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CameraModule")
endif()
if(CAMERA_MODULE_BUILD_QT_WIDGETS)
    install(EXPORT CameraQtWidgetsTargets FILE CameraQtWidgetsTargets.cmake NAMESPACE Camera::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CameraModule")
endif()
if(CAMERA_MODULE_BUILD_AMSCOPE)
    install(EXPORT AmscopeDriverTargets FILE AmscopeDriverTargets.cmake NAMESPACE Amscope::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CameraModule")
endif()
if(CAMERA_MODULE_BUILD_AMSCOPE_QT)
    install(EXPORT AmscopeQtTargets FILE AmscopeQtTargets.cmake NAMESPACE Amscope::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CameraModule")
endif()

set(CAMERA_MODULE_HAS_GENTL "${CAMERA_MODULE_BUILD_GENTL}")
set(CAMERA_MODULE_HAS_GENAPI "${CAMERA_MODULE_BUILD_GENAPI}")
set(CAMERA_MODULE_HAS_QT "${CAMERA_MODULE_BUILD_QT}")
set(CAMERA_MODULE_HAS_QT_WIDGETS "${CAMERA_MODULE_BUILD_QT_WIDGETS}")
set(CAMERA_MODULE_HAS_AMSCOPE "${CAMERA_MODULE_BUILD_AMSCOPE}")
set(CAMERA_MODULE_HAS_AMSCOPE_QT "${CAMERA_MODULE_BUILD_AMSCOPE_QT}")
configure_package_config_file(cmake/CameraModuleConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/CameraModuleConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CameraModule")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/CameraModuleConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/CameraModuleConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/CameraModuleConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/CameraModule")
