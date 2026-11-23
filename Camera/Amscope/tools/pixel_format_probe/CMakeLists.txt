cmake_minimum_required(VERSION 3.21)
project(AmscopeSdkPixelFormatProbe VERSION 1.0.0 LANGUAGES CXX)

if(NOT APPLE)
    message(FATAL_ERROR "The bundled Amcam SDK probe currently targets macOS.")
endif()

get_filename_component(
    _default_sdk_root
    "\${CMAKE_CURRENT_LIST_DIR}/../../../VendorSDK/AmscopeVendorSDK"
    ABSOLUTE)
set(AMSCOPE_VENDOR_SDK_ROOT "\${_default_sdk_root}" CACHE PATH
    "Path to the bundled Amscope vendor SDK")
set(AMSCOPE_VENDOR_MAC_ARCH "auto" CACHE STRING
    "Vendor macOS library directory: auto, x64+arm64, or x64+x86")
set_property(CACHE AMSCOPE_VENDOR_MAC_ARCH PROPERTY STRINGS
    auto x64+arm64 x64+x86)

if(AMSCOPE_VENDOR_MAC_ARCH STREQUAL "auto")
    if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
        set(_vendor_arch "x64+arm64")
    elseif(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
        set(_vendor_arch "x64+x86")
    elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(_vendor_arch "x64+arm64")
    else()
        set(_vendor_arch "x64+x86")
    endif()
else()
    set(_vendor_arch "\${AMSCOPE_VENDOR_MAC_ARCH}")
endif()

if(NOT _vendor_arch STREQUAL "x64+arm64" AND
   NOT _vendor_arch STREQUAL "x64+x86")
    message(FATAL_ERROR "Unsupported vendor macOS architecture: \${_vendor_arch}")
endif()

set(_vendor_include_dir "\${AMSCOPE_VENDOR_SDK_ROOT}/inc")
set(_vendor_library
    "\${AMSCOPE_VENDOR_SDK_ROOT}/mac/\${_vendor_arch}/libamcam.dylib")

if(NOT EXISTS "\${_vendor_include_dir}/amcam.h")
    message(FATAL_ERROR "Amcam SDK header not found: \${_vendor_include_dir}/amcam.h")
endif()
if(NOT EXISTS "\${_vendor_library}")
    message(FATAL_ERROR "Amcam SDK library not found: \${_vendor_library}")
endif()

add_executable(amscope_sdk_pixel_format_probe main.cpp)
target_compile_features(amscope_sdk_pixel_format_probe PRIVATE cxx_std_17)
target_include_directories(amscope_sdk_pixel_format_probe PRIVATE
    "\${_vendor_include_dir}")
target_link_libraries(amscope_sdk_pixel_format_probe PRIVATE
    "\${_vendor_library}")
set_target_properties(amscope_sdk_pixel_format_probe PROPERTIES
    BUILD_RPATH "@loader_path"
    INSTALL_RPATH "@loader_path")

add_custom_command(TARGET amscope_sdk_pixel_format_probe POST_BUILD
    COMMAND "\${CMAKE_COMMAND}" -E copy_if_different
        "\${_vendor_library}"
        "$<TARGET_FILE_DIR:amscope_sdk_pixel_format_probe>/libamcam.dylib"
    VERBATIM)
