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
            let railOverlayWidth = NativeWorkspaceDockTabRail.width
            let isDockVisible = workspace.isWorkspaceDockVisible
            let maximumDockWidth = min(
                OptikaLayout.workspaceDockMaximumWidth,
                max(
                    OptikaLayout.workspaceDockMinimumWidth,
                    geometry.size.width - previewMinimumWidth - dockDividerWidth - railOverlayWidth
                )
            )
            let dockWidth = min(
                max(workspace.workspaceDockWidth, OptikaLayout.workspaceDockMinimumWidth),
                maximumDockWidth
            )
            // The rail is an overlay, not an HStack child. When the dock is
            // closed, the preview uses the full window width behind the rail.
            // When open, the dock and preview end at the rail's leading edge
            // so the rail never obscures dock content.
            let workspaceContentWidth = geometry.size.width - (isDockVisible ? railOverlayWidth : 0)

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

                if isDockVisible {
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
            }
            .frame(
                width: workspaceContentWidth,
                height: geometry.size.height,
                alignment: .leading
            )
            .frame(
                width: geometry.size.width,
                height: geometry.size.height,
                alignment: .leading
            )
            .overlay(alignment: .trailing) {
                NativeWorkspaceDockTabRail(workspace: workspace)
                    .frame(width: railOverlayWidth)
                    .frame(maxHeight: .infinity)
            }
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
