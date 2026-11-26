add_library(camera_common INTERFACE)
add_library(Camera::Common ALIAS camera_common)
set_target_properties(camera_common PROPERTIES EXPORT_NAME Common)
include(GNUInstallDirs)
install(TARGETS camera_common EXPORT CameraCommonTargets)
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

target_compile_features(camera_common INTERFACE cxx_std_17)
target_include_directories(camera_common
    INTERFACE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

if(BUILD_TESTING)
    add_executable(camera_common_feature_tests
        tests/CameraFeatureValidationTests.cpp)
    target_link_libraries(camera_common_feature_tests PRIVATE Camera::Common)
    add_test(NAME camera_common_feature_tests COMMAND camera_common_feature_tests)

    add_executable(camera_common_command_access_tests
        tests/CameraCommandAccessTests.cpp)
    target_link_libraries(camera_common_command_access_tests PRIVATE Camera::Common)
    add_test(NAME camera_common_command_access_tests
        COMMAND camera_common_command_access_tests)

    add_executable(camera_common_models_tests
        tests/CameraModelsTests.cpp)
    target_link_libraries(camera_common_models_tests PRIVATE Camera::Common)
    add_test(NAME camera_common_models_tests COMMAND camera_common_models_tests)

    add_executable(camera_common_frame_tests
        tests/CameraFramesTests.cpp)
    target_link_libraries(camera_common_frame_tests PRIVATE Camera::Common)
    add_test(NAME camera_common_frame_tests COMMAND camera_common_frame_tests)

    add_executable(camera_common_frame_mailbox_tests
        tests/FrameStreamMailboxTests.cpp)
    target_link_libraries(camera_common_frame_mailbox_tests PRIVATE Camera::Common)
    add_test(NAME camera_common_frame_mailbox_tests COMMAND camera_common_frame_mailbox_tests)

    add_executable(camera_common_frame_rate_tests
        tests/FrameRateEstimatorTests.cpp)
    target_link_libraries(camera_common_frame_rate_tests PRIVATE Camera::Common)
    add_test(NAME camera_common_frame_rate_tests COMMAND camera_common_frame_rate_tests)
endif()
