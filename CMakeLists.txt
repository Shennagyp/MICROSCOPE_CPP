cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment target")
endif()

project(OptikaB293Workstation VERSION 0.9.0 LANGUAGES CXX)

include(CTest)

option(OPTIKA_BUILD_NATIVE_APP "Build the OPTIKA B293 native macOS application" OFF)
option(OPTIKA_BUILD_AMSCOPE "Build the AmScope camera capability and its integrations" ON)
option(OPTIKA_BUILD_GENTL "Build the optional GenTL Consumer bridge adapter" ON)
option(OPTIKA_BUILD_GENTL_GENAPI "Build GenApi 3.5.1 integration (requires official SDK)" OFF)

if(OPTIKA_BUILD_NATIVE_APP AND NOT OPTIKA_BUILD_AMSCOPE)
    message(FATAL_ERROR
        "OPTIKA_BUILD_NATIVE_APP requires OPTIKA_BUILD_AMSCOPE=ON.")
endif()

if(OPTIKA_BUILD_NATIVE_APP)
    if(NOT APPLE)
        message(FATAL_ERROR "The OPTIKA B293 native application requires macOS.")
    endif()
    enable_language(Swift)
    enable_language(OBJCXX)
endif()

# The microscope platform libraries are backend-neutral. They are added
# independently from the existing AmScope camera capability.
add_subdirectory(Libraries/Microscope.Core)
add_subdirectory(Libraries/Microscope.Application)

if(OPTIKA_BUILD_GENTL)
    set(CAMERA_GENTL_ENABLE_GENAPI ${OPTIKA_BUILD_GENTL_GENAPI})
    add_subdirectory(Camera/GenTL)
endif()

if(OPTIKA_BUILD_AMSCOPE)
    add_subdirectory(Camera/Amscope)
endif()

if(OPTIKA_BUILD_NATIVE_APP)
    add_subdirectory(Application/OptikaB293)
endif()
