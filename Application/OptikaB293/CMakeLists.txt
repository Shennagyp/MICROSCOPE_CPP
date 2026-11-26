if(NOT APPLE)
    message(FATAL_ERROR "OPTIKA B293 is a native macOS application.")
endif()
if(NOT TARGET Amscope::Driver)
    message(FATAL_ERROR "The AmScope camera capability must be added before the application.")
endif()

find_library(OPTIKA_SWIFTUI_FRAMEWORK SwiftUI REQUIRED)
find_library(OPTIKA_APPKIT_FRAMEWORK AppKit REQUIRED)
find_library(OPTIKA_FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(OPTIKA_COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
find_library(OPTIKA_IMAGEIO_FRAMEWORK ImageIO REQUIRED)

set(OPTIKA_APP_SOURCES
    Sources/App/OptikaB293App.swift
    Sources/Composition/OptikaB293Composition.swift
    Sources/Features/Camera/NativeCameraModel.swift
    Sources/Features/Camera/NativeCameraFeatureModel.swift
    Sources/Features/Slides/SlideImageGeometry.swift
    Sources/Features/Capture/NativeCaptureGallery.swift
    Sources/Features/Optics/MicroscopeOpticalConfiguration.swift
    Sources/Features/Optics/MicroscopeConfigurationModel.swift
    Sources/Features/Optics/MicroscopeResolutionCoordinator.swift
    Sources/Features/Optics/ObjectiveExposureCoordinator.swift
    Sources/Features/Optics/MicroscopeConfigurationDraftStore.swift
    Sources/Features/Optics/NativeMicroscopeCalibration.swift
    Sources/Features/Optics/NativeAutomaticResolutionPolicy.swift
    Sources/Features/Workspace/NativeSlideID.swift
    Sources/Features/Workspace/NativeWorkspaceModel.swift
    Sources/Features/Workspace/NativeWorkspaceDockTab.swift
    Sources/Features/Workspace/NativeDockSystemModel.swift
    Sources/Presentation/Theme/OptikaPalette.swift
    Sources/Presentation/Theme/OptikaLayout.swift
    Sources/Presentation/Camera/CameraControlsWindow.swift
    Sources/Presentation/Camera/DockCameraFeatureControl.swift
    Sources/Presentation/Camera/NativeFeatureEditor.swift
    Sources/Presentation/Optics/MicroscopeConfigurationWindow.swift
    Sources/Presentation/Optics/ObjectiveExposureSettingsWindow.swift
    Sources/Presentation/Workspace/MainWindowView.swift
    Sources/Presentation/Workspace/NativeWorkspaceDockResizeDivider.swift
    Sources/Presentation/Workspace/Dock/NativeWorkspaceDock.swift
    Sources/Presentation/Workspace/Dock/CameraControlsDock.swift
    Sources/Presentation/Workspace/Dock/SmartGalleryDock.swift
    Sources/Features/Slides/SlideOverlayModels.swift
    Sources/Presentation/Preview/NativeSlideToolMode.swift
    Sources/Presentation/Preview/PreviewFrameRateMeter.swift
    Sources/Presentation/Preview/NativeMicroscopePaneControls.swift
    Sources/Presentation/Preview/NativeDualPreviewSurface.swift
    Sources/Presentation/Preview/NativeSlidePreviewPane.swift
    Sources/Presentation/Preview/NativeSlideImageView.swift
    Sources/Presentation/Preview/NativeSlideImageContainerView.swift
    Sources/Adapters/Camera/NativeCameraController.cpp
    Sources/Adapters/Camera/NativeCameraController.h
    Sources/Adapters/Camera/NativeCameraFrameFeed.swift
    Sources/Adapters/Camera/AmscopeNativeCameraBridge.mm
    Sources/Adapters/Camera/AmscopeNativeCameraBridge.h
    include/NativeMac-Bridging-Header.h
)

add_executable(optika_b293_dualslide_app MACOSX_BUNDLE ${OPTIKA_APP_SOURCES})
target_include_directories(optika_b293_dualslide_app PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Sources/Adapters/Camera")
target_compile_options(optika_b293_dualslide_app PRIVATE
    "$<$<COMPILE_LANGUAGE:Swift>:-import-objc-header>"
    "$<$<COMPILE_LANGUAGE:Swift>:${CMAKE_CURRENT_SOURCE_DIR}/include/NativeMac-Bridging-Header.h>"
    "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>"
    "$<$<COMPILE_LANGUAGE:Swift>:-swift-version>"
    "$<$<COMPILE_LANGUAGE:Swift>:5>"
)
target_link_libraries(optika_b293_dualslide_app PRIVATE
    # The native application consumes the same backend-neutral platform targets
    # used by other hosts. No hardware adapter is implied by linking the libraries.
    Microscope::Application
    Microscope::Core
    Amscope::Driver
    "${OPTIKA_SWIFTUI_FRAMEWORK}"
    "${OPTIKA_APPKIT_FRAMEWORK}"
    "${OPTIKA_FOUNDATION_FRAMEWORK}"
    "${OPTIKA_COREGRAPHICS_FRAMEWORK}"
    "${OPTIKA_IMAGEIO_FRAMEWORK}"
)
set_target_properties(optika_b293_dualslide_app PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.shennagyp.optika-b293.dualslide"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.9.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.9.0"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    OBJCXX_STANDARD 17
    OBJCXX_STANDARD_REQUIRED YES
)


if(BUILD_TESTING)
    add_executable(optika_dock_model_tests
        Sources/Features/Workspace/NativeDockSystemModel.swift
        Tests/NativeDockSystemModelTests.swift)
    target_compile_options(optika_dock_model_tests PRIVATE
        "$<$<COMPILE_LANGUAGE:Swift>:-swift-version>"
        "$<$<COMPILE_LANGUAGE:Swift>:5>")
    target_link_libraries(optika_dock_model_tests PRIVATE "${OPTIKA_FOUNDATION_FRAMEWORK}")
    add_test(NAME optika_dock_model_tests COMMAND optika_dock_model_tests)

    add_executable(optika_microscope_calibration_tests
        Sources/Features/Optics/NativeMicroscopeCalibration.swift
        Tests/NativeMicroscopeCalibrationTests.swift)
    target_compile_options(optika_microscope_calibration_tests PRIVATE
        "$<$<COMPILE_LANGUAGE:Swift>:-swift-version>"
        "$<$<COMPILE_LANGUAGE:Swift>:5>")
    target_link_libraries(optika_microscope_calibration_tests PRIVATE "${OPTIKA_FOUNDATION_FRAMEWORK}")
    add_test(NAME optika_microscope_calibration_tests COMMAND optika_microscope_calibration_tests)

    add_executable(optika_slide_image_geometry_tests
        Sources/Features/Slides/SlideImageGeometry.swift
        Sources/Features/Slides/SlideOverlayModels.swift
        Tests/SlideImageGeometryTests.swift)
    target_compile_options(optika_slide_image_geometry_tests PRIVATE
        "$<$<COMPILE_LANGUAGE:Swift>:-swift-version>"
        "$<$<COMPILE_LANGUAGE:Swift>:5>")
    target_link_libraries(optika_slide_image_geometry_tests PRIVATE
        "${OPTIKA_FOUNDATION_FRAMEWORK}"
        "${OPTIKA_COREGRAPHICS_FRAMEWORK}")
    add_test(NAME optika_slide_image_geometry_tests COMMAND optika_slide_image_geometry_tests)

    add_executable(optika_automatic_resolution_policy_tests
        Sources/Features/Optics/MicroscopeOpticalConfiguration.swift
        Sources/Features/Optics/NativeAutomaticResolutionPolicy.swift
        Tests/NativeAutomaticResolutionPolicyTests.swift)
    target_compile_options(optika_automatic_resolution_policy_tests PRIVATE
        "$<$<COMPILE_LANGUAGE:Swift>:-swift-version>"
        "$<$<COMPILE_LANGUAGE:Swift>:5>")
    target_link_libraries(optika_automatic_resolution_policy_tests PRIVATE
        "${OPTIKA_FOUNDATION_FRAMEWORK}")
    add_test(NAME optika_automatic_resolution_policy_tests COMMAND optika_automatic_resolution_policy_tests)

    add_executable(optika_preview_frame_rate_meter_tests
        Sources/Presentation/Preview/PreviewFrameRateMeter.swift
        Tests/PreviewFrameRateMeterTests.swift)
    target_compile_options(optika_preview_frame_rate_meter_tests PRIVATE
        "$<$<COMPILE_LANGUAGE:Swift>:-swift-version>"
        "$<$<COMPILE_LANGUAGE:Swift>:5>")
    target_link_libraries(optika_preview_frame_rate_meter_tests PRIVATE
        "${OPTIKA_FOUNDATION_FRAMEWORK}")
    add_test(NAME optika_preview_frame_rate_meter_tests
        COMMAND optika_preview_frame_rate_meter_tests)

    find_library(OPTIKA_COMBINE_FRAMEWORK Combine REQUIRED)
    add_executable(optika_workspace_model_tests
        Sources/Features/Workspace/NativeSlideID.swift
        Sources/Features/Workspace/NativeWorkspaceDockTab.swift
        Sources/Features/Workspace/NativeWorkspaceModel.swift
        Sources/Presentation/Theme/OptikaLayout.swift
        Tests/NativeWorkspaceModelTests.swift)
    target_compile_options(optika_workspace_model_tests PRIVATE
        "$<$<COMPILE_LANGUAGE:Swift>:-swift-version>"
        "$<$<COMPILE_LANGUAGE:Swift>:5>")
    target_link_libraries(optika_workspace_model_tests PRIVATE
        "${OPTIKA_FOUNDATION_FRAMEWORK}"
        "${OPTIKA_COMBINE_FRAMEWORK}"
        "${OPTIKA_COREGRAPHICS_FRAMEWORK}")
    add_test(NAME optika_workspace_model_tests
        COMMAND optika_workspace_model_tests)
endif()
