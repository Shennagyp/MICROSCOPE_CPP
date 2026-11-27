import AppKit
import SwiftUI

struct NativeWorkspaceDock: View {
    @ObservedObject var model: NativeCameraModel
    @ObservedObject var workspace: NativeWorkspaceModel
    @ObservedObject var microscope: MicroscopeConfigurationModel
    @ObservedObject var resolutionCoordinator: MicroscopeResolutionCoordinator
    @ObservedObject var gallery: NativeCaptureGalleryStore
    let onClose: () -> Void

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 10) {
                Image(systemName: workspace.selectedWorkspaceDockTab.symbolName)
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundColor(OptikaPalette.secondaryText)

                Text(workspace.selectedWorkspaceDockTab.title)
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundColor(OptikaPalette.primaryText)
                    .lineLimit(1)

                Spacer(minLength: 8)

                Button(action: onClose) {
                    Image(systemName: "xmark")
                        .font(.system(size: 11, weight: .semibold))
                        .frame(width: 28, height: 28)
                        .contentShape(Rectangle())
                }
                .buttonStyle(PlainButtonStyle())
                .foregroundColor(OptikaPalette.secondaryText)
                .help("Hide dock")
                .accessibilityLabel("Hide \(workspace.selectedWorkspaceDockTab.title) dock")
            }
            .padding(.horizontal, 12)
            .frame(height: 44)
            .background(OptikaPalette.controlSurface)

            Divider()

            Group {
                switch workspace.selectedWorkspaceDockTab {
                case .cameraControls:
                    NativeCameraControlsDock(model: model, features: model.features, workspace: workspace, microscope: microscope, resolutionCoordinator: resolutionCoordinator)
                case .smartGallery:
                    NativeSmartGalleryDock(gallery: gallery)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .background(OptikaPalette.controlSurface)
        .clipShape(RoundedRectangle(cornerRadius: 8))
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .stroke(
                    OptikaPalette.separator.opacity(OptikaPalette.floatingPanelBorderAlpha),
                    lineWidth: 0.8
                )
        )
    }
}

/// Floating launch controls for the workspace docks.
///
/// The buttons are independent surfaces. There is deliberately no
/// full-height rail background, separator, or enclosing bar.
struct NativeWorkspaceDockTabRail: View {
    @ObservedObject var workspace: NativeWorkspaceModel

    static let width: CGFloat = OptikaLayout.dockRailWidth
    private let tabHeight: CGFloat = OptikaLayout.dockTabHeight

    var body: some View {
        VStack(spacing: 10) {
            dockTabButton(.cameraControls)
            dockTabButton(.smartGallery)
        }
        .padding(.vertical, 8)
        .frame(width: Self.width)
        .fixedSize(horizontal: false, vertical: true)
    }

    private func dockTabButton(_ tab: NativeWorkspaceDockTab) -> some View {
        let isSelected = workspace.selectedWorkspaceDockTab == tab
        let isOpen = workspace.isWorkspaceDockVisible && isSelected
        let title = tab.title
        let actionTitle = isOpen ? "Hide \(title)" : "Show \(title)"

        return Button {
            workspace.activateWorkspaceDockTab(tab)
        } label: {
            VStack(spacing: 6) {
                Image(systemName: tab.symbolName)
                    .font(.system(size: 13, weight: .semibold))
                    .frame(width: 16, height: 16)

                Text(title)
                    .font(.system(size: 9, weight: .semibold))
                    .lineLimit(1)
                    .minimumScaleFactor(0.8)
                    .frame(width: 88, height: 13)
                    .rotationEffect(.degrees(90))
                    .frame(width: 13, height: 88)
            }
            .frame(width: Self.width - 8, height: tabHeight)
            .contentShape(RoundedRectangle(cornerRadius: 9))
            .background(
                RoundedRectangle(cornerRadius: 9)
                    .fill(
                        isOpen
                            ? OptikaPalette.accentSelectionFill
                            : (isSelected
                                ? OptikaPalette.inactiveSelectionFill
                                : OptikaPalette.controlSurface)
                    )
            )
            .overlay(
                RoundedRectangle(cornerRadius: 9)
                    .stroke(
                        isOpen
                            ? OptikaPalette.accent.opacity(0.7)
                            : OptikaPalette.separator.opacity(0.8),
                        lineWidth: 0.8
                    )
            )
            .shadow(
                color: Color.black.opacity(0.24),
                radius: 8,
                x: -3,
                y: 3
            )
        }
        .buttonStyle(PlainButtonStyle())
        .frame(width: Self.width - 8, height: tabHeight)
        .contentShape(Rectangle())
        .foregroundColor(isSelected ? OptikaPalette.accent : OptikaPalette.primaryText)
        .help(actionTitle)
        .accessibilityLabel(actionTitle)
        .accessibilityHint(
            isOpen
                ? "Hides the dock and gives the preview area more width."
                : (workspace.isWorkspaceDockVisible
                    ? "Switches the open dock to \(title)."
                    : "Opens the \(title) dock.")
        )
    }
}
