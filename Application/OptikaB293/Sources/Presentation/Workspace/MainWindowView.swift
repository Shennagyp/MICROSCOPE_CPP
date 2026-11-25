import AppKit
import SwiftUI

struct MainWindowView: View {
    @ObservedObject var model: NativeCameraModel
    @ObservedObject var microscope: MicroscopeConfigurationModel
    @ObservedObject var resolutionCoordinator: MicroscopeResolutionCoordinator
    @ObservedObject var workspace: NativeWorkspaceModel
    @ObservedObject var gallery: NativeCaptureGalleryStore

    private let dockDividerWidth = OptikaLayout.workspaceDockResizeDividerWidth

    var body: some View {
        GeometryReader { geometry in
            let previewMinimumWidth = OptikaLayout.dualPreviewMinimumWidth
            let railWidth = NativeWorkspaceDockTabRail.width
            let maximumDockWidth = min(
                OptikaLayout.workspaceDockMaximumWidth,
                max(
                    OptikaLayout.workspaceDockMinimumWidth,
                    geometry.size.width - previewMinimumWidth - dockDividerWidth - railWidth
                )
            )
            let dockWidth = min(
                max(workspace.workspaceDockWidth, OptikaLayout.workspaceDockMinimumWidth),
                maximumDockWidth
            )

            HStack(spacing: 0) {
                NativeDualPreviewSurface(
                    frameFeed: model.frameFeed,
                    model: model,
                    microscope: microscope,
                    resolutionCoordinator: resolutionCoordinator,
                    workspace: workspace,
                    activeSlide: workspace.activeSlide,
                    measurementCalibration: microscope.measurementCalibration,
                    onCapture: { slide, image in
                        gallery.capture(image, for: slide)
                    }
                )
                .frame(
                    minWidth: previewMinimumWidth,
                    maxWidth: .infinity,
                    maxHeight: .infinity
                )
                .clipped()

                if workspace.isWorkspaceDockVisible {
                    NativeWorkspaceDockResizeDivider(
                        dockWidth: dockWidth,
                        maximumWidth: maximumDockWidth,
                        onResize: {
                            workspace.updateWorkspaceDockWidth(
                                $0,
                                maximum: maximumDockWidth
                            )
                        },
                        onResizeEnded: {
                            workspace.persistWorkspaceDockWidth()
                        }
                    )
                    .frame(width: dockDividerWidth)
                    .frame(maxHeight: .infinity)

                    NativeWorkspaceDock(
                        model: model,
                        workspace: workspace,
                        microscope: microscope,
                        resolutionCoordinator: resolutionCoordinator,
                        gallery: gallery,
                        onClose: { workspace.closeWorkspaceDock() }
                    )
                    .frame(width: dockWidth)
                    .frame(maxHeight: .infinity)
                }

                NativeWorkspaceDockTabRail(workspace: workspace)
                    .frame(width: railWidth)
                    .frame(maxHeight: .infinity)
            }
            .frame(width: geometry.size.width, height: geometry.size.height)
            .clipped()
        }
        .frame(
            minWidth: OptikaLayout.minimumWindowWidth,
            minHeight: OptikaLayout.minimumWindowHeight
        )
        .background(OptikaPalette.clear)
        .onAppear {
            if model.cameras.isEmpty {
                model.refreshCameras()
            }
        }
        .alert(isPresented: $model.isShowingError) {
            Alert(
                title: Text("Camera Error"),
                message: Text(model.errorMessage),
                dismissButton: .default(Text("OK"))
            )
        }
    }
}

