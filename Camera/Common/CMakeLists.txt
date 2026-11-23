add_library(camera_common INTERFACE)
add_library(Camera::Common ALIAS camera_common)

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

    add_executable(camera_common_models_tests
        tests/CameraModelsTests.cpp)
    target_link_libraries(camera_common_models_tests PRIVATE Camera::Common)
    add_test(NAME camera_common_models_tests COMMAND camera_common_models_tests)

    add_executable(camera_common_frame_tests
        tests/CameraFramesTests.cpp)
    target_link_libraries(camera_common_frame_tests PRIVATE Camera::Common)
    add_test(NAME camera_common_frame_tests COMMAND camera_common_frame_tests)

    add_executable(camera_common_frame_rate_tests
        tests/FrameRateEstimatorTests.cpp)
    target_link_libraries(camera_common_frame_rate_tests PRIVATE Camera::Common)
    add_test(NAME camera_common_frame_rate_tests COMMAND camera_common_frame_rate_tests)

    add_executable(camera_common_operation_error_tests
        tests/CameraOperationErrorTests.cpp)
    target_link_libraries(camera_common_operation_error_tests PRIVATE Camera::Common)
    add_test(NAME camera_common_operation_error_tests COMMAND camera_common_operation_error_tests)

    add_executable(camera_common_frame_rate_measurement_tests
        tests/CameraFrameRateMeasurementTests.cpp)
    target_link_libraries(camera_common_frame_rate_measurement_tests PRIVATE Camera::Common)
    add_test(NAME camera_common_frame_rate_measurement_tests COMMAND camera_common_frame_rate_measurement_tests)
endif()
