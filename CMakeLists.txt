cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment target")
endif()

project(OptikaB293Workstation VERSION 0.9.0 LANGUAGES CXX)

include(CTest)

option(OPTIKA_BUILD_NATIVE_APP "Build the OPTIKA B293 native macOS application" OFF)

if(OPTIKA_BUILD_NATIVE_APP)
    if(NOT APPLE)
        message(FATAL_ERROR "The OPTIKA B293 native application requires macOS.")
    endif()
    enable_language(Swift)
    enable_language(OBJCXX)
endif()

add_subdirectory(Camera/Amscope)

if(OPTIKA_BUILD_NATIVE_APP)
    add_subdirectory(Application/OptikaB293)
endif()
