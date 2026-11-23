cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(CameraQtWidgets LANGUAGES CXX)
    include(CTest)
endif()

if(NOT TARGET Camera::Common)
    add_subdirectory(
        "${CMAKE_CURRENT_LIST_DIR}/../Common"
        "${CMAKE_CURRENT_BINARY_DIR}/camera_common")
endif()

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

add_library(camera_qt_widgets STATIC
    src/FrameView.cpp
    include/camera/qtwidgets/FrameView.h)
add_library(Camera::QtWidgets ALIAS camera_qt_widgets)

target_compile_features(camera_qt_widgets PUBLIC cxx_std_17)
set_target_properties(camera_qt_widgets PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(camera_qt_widgets
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")
target_link_libraries(camera_qt_widgets PUBLIC Camera::Common Qt6::Widgets)

if(BUILD_TESTING)
    add_executable(camera_qt_widgets_frame_view_tests
        tests/FrameViewTests.cpp)
    target_compile_features(camera_qt_widgets_frame_view_tests PRIVATE cxx_std_17)
    target_link_libraries(camera_qt_widgets_frame_view_tests
        PRIVATE Camera::QtWidgets Qt6::Widgets)
    add_test(NAME camera_qt_widgets_frame_view_tests
        COMMAND camera_qt_widgets_frame_view_tests)
    set_tests_properties(camera_qt_widgets_frame_view_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

include(GNUInstallDirs)
install(TARGETS camera_qt_widgets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
