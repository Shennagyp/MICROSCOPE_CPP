cmake_minimum_required(VERSION 3.21)

# Permit this library to be configured independently or included by the
# repository's root project.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(MicroscopeCore VERSION 0.9.0 LANGUAGES CXX)
    include(CTest)
endif()

find_package(Threads REQUIRED)

add_library(microscope_core STATIC
    src/Identity.cpp
    src/OpticalTopology.cpp
    src/LightPathCalibration.cpp
    src/OperationMode.cpp
    src/OperatingModeRecommender.cpp
    src/SampleModels.cpp
    src/SampleMicroscopyRequirementAssessor.cpp
    src/CameraSuitability.cpp
    src/LightPathCameraResolver.cpp
    src/LightPathConfigurationRequestFactory.cpp
    src/OpticalSpecifications.cpp
    src/IlluminationSpecifications.cpp
    src/StageSpecifications.cpp
    src/SlideGeometry.cpp
    src/DualSlideCarrierSpecification.cpp
    src/StageWorkingSurface2D.cpp
    src/StagePosition3D.cpp
    src/StageAxisReference.cpp
    src/StageMotionPort.cpp
    src/StageAxisPositionState.cpp
    src/StagePositionState.cpp
    src/DualSlideNavigation.cpp
    src/StageNavigationPlanner.cpp
    src/StageWorkingSurfaceNavigationPlanner.cpp
    src/Configuration.cpp
    src/ConfigurationTransition.cpp
    src/OperationStateMachine.cpp)
add_library(Microscope::Core ALIAS microscope_core)

target_link_libraries(microscope_core PUBLIC Threads::Threads)

target_compile_features(microscope_core PUBLIC cxx_std_17)
target_include_directories(microscope_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
        "$<INSTALL_INTERFACE:include>")

set_target_properties(microscope_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF)

if(BUILD_TESTING)
    add_executable(microscope_core_contract_tests
        tests/CoreContractTests.cpp)
    target_link_libraries(microscope_core_contract_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_contract_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_core_contract_tests
        COMMAND microscope_core_contract_tests)

    add_executable(microscope_core_feedback_tests
        tests/FeedbackContractTests.cpp)
    target_link_libraries(microscope_core_feedback_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_feedback_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_core_feedback_tests
        COMMAND microscope_core_feedback_tests)

    add_executable(microscope_core_equipment_tests
        tests/OpticalTopologyTests.cpp)
    target_link_libraries(microscope_core_equipment_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_equipment_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_core_equipment_tests
        COMMAND microscope_core_equipment_tests)

    add_executable(microscope_core_camera_suitability_tests
        tests/CameraSuitabilityTests.cpp)
    target_link_libraries(microscope_core_camera_suitability_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_camera_suitability_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_core_camera_suitability_tests
        COMMAND microscope_core_camera_suitability_tests)

    add_executable(microscope_core_sample_models_tests
        tests/SampleModelsTests.cpp)
    target_link_libraries(microscope_core_sample_models_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_sample_models_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_core_sample_models_tests
        COMMAND microscope_core_sample_models_tests)

    add_executable(microscope_core_operation_mode_tests
        tests/OperationModeTests.cpp)
    target_link_libraries(microscope_core_operation_mode_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_operation_mode_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_core_operation_mode_tests
        COMMAND microscope_core_operation_mode_tests)

    add_executable(microscope_core_light_path_calibration_tests
        tests/LightPathCalibrationTests.cpp)
    target_link_libraries(microscope_core_light_path_calibration_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_light_path_calibration_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_core_light_path_calibration_tests
        COMMAND microscope_core_light_path_calibration_tests)

    add_executable(microscope_core_optical_specification_tests
        tests/OpticalSpecificationsTests.cpp)
    target_link_libraries(microscope_core_optical_specification_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_optical_specification_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_core_optical_specification_tests
        COMMAND microscope_core_optical_specification_tests)

    add_executable(microscope_core_illumination_condenser_tests
        tests/IlluminationAndCondenserTests.cpp)
    target_link_libraries(microscope_core_illumination_condenser_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_illumination_condenser_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_core_illumination_condenser_tests
        COMMAND microscope_core_illumination_condenser_tests)

    add_executable(microscope_core_stage_specification_tests
        tests/StageSpecificationsTests.cpp)
    target_link_libraries(microscope_core_stage_specification_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_stage_specification_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_core_stage_specification_tests
        COMMAND microscope_core_stage_specification_tests)

    add_executable(microscope_core_configuration_tests
        tests/ConfigurationResolverTests.cpp)
    target_link_libraries(microscope_core_configuration_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_configuration_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_core_configuration_tests
        COMMAND microscope_core_configuration_tests)

    add_executable(microscope_core_operation_mode_recommender_tests
        tests/OperatingModeRecommenderTests.cpp)
    target_link_libraries(microscope_core_operation_mode_recommender_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_operation_mode_recommender_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_core_operation_mode_recommender_tests
        COMMAND microscope_core_operation_mode_recommender_tests)

    add_executable(microscope_core_operation_state_tests
        tests/OperationStateMachineTests.cpp)
    target_link_libraries(microscope_core_operation_state_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_operation_state_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_core_operation_state_tests
        COMMAND microscope_core_operation_state_tests)

    add_executable(microscope_core_stage_motion_tests
        tests/StageMotionPortTests.cpp)
    target_link_libraries(microscope_core_stage_motion_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_stage_motion_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(NAME microscope_core_stage_motion_tests
        COMMAND microscope_core_stage_motion_tests)

    add_executable(microscope_core_stage_position_state_tests
        tests/StageAxisPositionStateTests.cpp)
    target_link_libraries(microscope_core_stage_position_state_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_stage_position_state_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(NAME microscope_core_stage_position_state_tests
        COMMAND microscope_core_stage_position_state_tests)

    add_executable(microscope_core_stage_position_aggregate_tests
        tests/StagePositionStateTests.cpp)
    target_link_libraries(microscope_core_stage_position_aggregate_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_stage_position_aggregate_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(NAME microscope_core_stage_position_aggregate_tests
        COMMAND microscope_core_stage_position_aggregate_tests)

    add_executable(microscope_core_dual_slide_navigation_tests
        tests/DualSlideNavigationTests.cpp)
    target_link_libraries(microscope_core_dual_slide_navigation_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_dual_slide_navigation_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(NAME microscope_core_dual_slide_navigation_tests
        COMMAND microscope_core_dual_slide_navigation_tests)

    add_executable(microscope_core_stage_navigation_planner_tests
        tests/StageNavigationPlannerTests.cpp)
    target_link_libraries(microscope_core_stage_navigation_planner_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_stage_navigation_planner_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(NAME microscope_core_stage_navigation_planner_tests
        COMMAND microscope_core_stage_navigation_planner_tests)

    add_executable(microscope_core_working_surface_navigation_planner_tests
        tests/StageWorkingSurfaceNavigationPlannerTests.cpp)
    target_link_libraries(microscope_core_working_surface_navigation_planner_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_working_surface_navigation_planner_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(NAME microscope_core_working_surface_navigation_planner_tests
        COMMAND microscope_core_working_surface_navigation_planner_tests)

    add_executable(microscope_core_bounded_feedback_inbox_tests
        tests/BoundedFeedbackInboxTests.cpp)
    target_link_libraries(microscope_core_bounded_feedback_inbox_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_bounded_feedback_inbox_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(NAME microscope_core_bounded_feedback_inbox_tests
        COMMAND microscope_core_bounded_feedback_inbox_tests)

    add_executable(microscope_core_serialized_operation_runtime_tests
        tests/SerializedOperationRuntimeTests.cpp)
    target_link_libraries(microscope_core_serialized_operation_runtime_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_serialized_operation_runtime_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(NAME microscope_core_serialized_operation_runtime_tests
        COMMAND microscope_core_serialized_operation_runtime_tests)

    add_executable(microscope_core_optical_configuration_workflow_integration_tests
        tests/MicroscopeOpticalConfigurationWorkflowIntegrationTests.cpp)
    target_link_libraries(microscope_core_optical_configuration_workflow_integration_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_optical_configuration_workflow_integration_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(
        NAME microscope_core_optical_configuration_workflow_integration_tests
        COMMAND microscope_core_optical_configuration_workflow_integration_tests)

    add_executable(microscope_core_stage_working_surface_2d_tests
        tests/StageWorkingSurface2DTests.cpp)
    target_link_libraries(microscope_core_stage_working_surface_2d_tests
        PRIVATE Microscope::Core)
    set_target_properties(microscope_core_stage_working_surface_2d_tests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS OFF)
    add_test(NAME microscope_core_stage_working_surface_2d_tests
        COMMAND microscope_core_stage_working_surface_2d_tests)

endif()
