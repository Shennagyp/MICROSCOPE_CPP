cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(CameraQt LANGUAGES CXX)
    include(CTest)
endif()
if(NOT TARGET Camera::Common)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../Common"
                     "${CMAKE_CURRENT_BINARY_DIR}/camera_common")
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Core)

add_library(camera_qt_core STATIC
    src/FrameStream.cpp
    include/camera/qt/FrameStream.h
    include/camera/qt/FrameStreamMailbox.h)
add_library(Camera::Qt ALIAS camera_qt_core)
set_target_properties(camera_qt_core PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON)
target_compile_features(camera_qt_core PUBLIC cxx_std_17)
target_include_directories(camera_qt_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
target_link_libraries(camera_qt_core PUBLIC Camera::Common Qt6::Core)

if(BUILD_TESTING)
    add_executable(camera_qt_frame_stream_tests tests/FrameStreamTests.cpp)
    target_compile_features(camera_qt_frame_stream_tests PRIVATE cxx_std_17)
    target_link_libraries(camera_qt_frame_stream_tests PRIVATE Camera::Qt Qt6::Core)
    add_test(NAME camera_qt_frame_stream_tests COMMAND camera_qt_frame_stream_tests)

    add_executable(camera_qt_stream_handoff_tests tests/QtFrameStreamTests.cpp)
    target_compile_features(camera_qt_stream_handoff_tests PRIVATE cxx_std_17)
    target_link_libraries(camera_qt_stream_handoff_tests PRIVATE Camera::Qt Qt6::Core)
    add_test(NAME camera_qt_stream_handoff_tests COMMAND camera_qt_stream_handoff_tests)
endif()

include(GNUInstallDirs)
install(TARGETS camera_qt_core
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
