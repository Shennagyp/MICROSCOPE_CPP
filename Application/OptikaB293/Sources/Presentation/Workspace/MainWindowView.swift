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
            // The panel is a true overlay: it is not an HStack child and
            // therefore never takes width away from or resizes the preview.
            .overlay(alignment: .trailing) {
                if workspace.isWorkspaceDockVisible {
                    HStack(spacing: 0) {
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
                        .shadow(
                            color: Color.black.opacity(0.28),
                            radius: 16,
                            x: -5,
                            y: 0
                        )
                    }
                    // Keep the floating panel immediately left of the
                    // floating launcher rail, without reserving rail space.
                    .padding(.trailing, railWidth)
                    .padding(.vertical, 12)
                    .zIndex(1)
                }
            }
            .overlay(alignment: .trailing) {
                NativeWorkspaceDockTabRail(workspace: workspace)
                    .frame(width: railWidth)
                    .frame(maxHeight: .infinity)
                    .zIndex(2)
            }
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
