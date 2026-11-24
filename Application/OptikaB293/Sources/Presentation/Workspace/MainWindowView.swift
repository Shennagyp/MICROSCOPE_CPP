import AppKit
import SwiftUI

struct MainWindowView: View {
    @ObservedObject var model: NativeCameraModel
    @ObservedObject var workspace: NativeWorkspaceModel
    @ObservedObject var gallery: NativeCaptureGalleryStore

    private let dockDividerWidth = OptikaLayout.workspaceDockResizeDividerWidth
    private let maximumDockWidth = OptikaLayout.workspaceDockMaximumWidth

    var body: some View {
        GeometryReader { geometry in
            let dockWidth = min(workspace.workspaceDockWidth, OptikaLayout.workspaceDockMaximumWidth)

            NativeDualPreviewSurface(
                bridge: model.bridge,
                model: model,
                workspace: workspace,
                activeSlide: workspace.activeSlide,
                measurementCalibration: model.measurementCalibration,
                onCapture: { slide, image in
                    gallery.capture(image, for: slide)
                }
            )
            .frame(width: geometry.size.width, height: geometry.size.height)
            .clipped()
            .overlay(alignment: .trailing) {
                if workspace.isWorkspaceDockVisible {
                    HStack(spacing: 0) {
                        NativeWorkspaceDockResizeDivider(
                            dockWidth: dockWidth,
                            maximumWidth: maximumDockWidth,
                            onResize: {
                                workspace.setWorkspaceDockWidth($0, maximum: OptikaLayout.workspaceDockMaximumWidth)
                            }
                        )
                        .frame(width: dockDividerWidth)
                        .frame(maxHeight: .infinity)

                        NativeWorkspaceDock(
                            model: model,
                            workspace: workspace,
                            gallery: gallery,
                            onClose: { workspace.closeWorkspaceDock() }
                        )
                        .frame(width: dockWidth)
                        .frame(maxHeight: .infinity)
                    }
                    .frame(maxHeight: .infinity)
                    // The panel floats above the preview and stops before the
                    // permanent title rail. It never participates in preview layout.
                    .padding(.trailing, NativeWorkspaceDockTabRail.width)
                    .shadow(
                        color: OptikaPalette.shadow.opacity(OptikaPalette.shadowAlpha),
                        radius: 14,
                        x: -6,
                        y: 0
                    )
                    .zIndex(50)
                }
            }
            .overlay(alignment: .trailing) {
                NativeWorkspaceDockTabRail(workspace: workspace)
                    .frame(width: NativeWorkspaceDockTabRail.width)
                    .frame(maxHeight: .infinity)
                    .contentShape(Rectangle())
                    .allowsHitTesting(true)
                    .zIndex(100)
            }
        }
        .frame(minWidth: OptikaLayout.minimumWindowWidth, minHeight: OptikaLayout.minimumWindowHeight)
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

