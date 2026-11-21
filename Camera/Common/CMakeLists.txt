add_library(camera_common INTERFACE)
add_library(Camera::Common ALIAS camera_common)

target_compile_features(camera_common INTERFACE cxx_std_17)
target_include_directories(camera_common
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
