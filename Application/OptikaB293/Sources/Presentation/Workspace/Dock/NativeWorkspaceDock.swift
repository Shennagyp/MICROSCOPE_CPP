import AppKit
import SwiftUI

struct NativeWorkspaceDock: View {
    @ObservedObject var model: NativeCameraModel
    @ObservedObject var workspace: NativeWorkspaceModel
    @ObservedObject var gallery: NativeCaptureGalleryStore
    let onClose: () -> Void

    var body: some View {
        VStack(spacing: 0) {
            HStack(spacing: 8) {
                // Keep the close hit target on the leading edge. The permanent
                // title rail sits immediately outside the floating panel.
                Button(action: onClose) {
                    Image(systemName: "xmark")
                        .font(.system(size: 10, weight: .semibold))
                        .frame(width: 24, height: 24)
                        .contentShape(Rectangle())
                }
                .buttonStyle(PlainButtonStyle())
                .foregroundColor(OptikaPalette.secondaryText)
                .help("Close dock")
                .accessibilityLabel("Close \(workspace.selectedWorkspaceDockTab.title) dock")

                Image(systemName: model.selectedWorkspaceDockTab.symbolName)
                    .font(.system(size: 12, weight: .semibold))
                    .foregroundColor(OptikaPalette.secondaryText)

                Text(model.selectedWorkspaceDockTab.title.uppercased())
                    .font(.system(size: 12, weight: .semibold))
                    .foregroundColor(OptikaPalette.primaryText)

                Spacer(minLength: 8)
            }
            .padding(.horizontal, 10)
            .frame(height: 36)
            .background(OptikaPalette.controlSurface.opacity(0.20))

            Divider()

            Group {
                switch model.selectedWorkspaceDockTab {
                case .cameraControls:
                    NativeCameraControlsDock(model: model)
                case .smartGallery:
                    NativeSmartGalleryDock(gallery: gallery)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .background(OptikaPalette.controlSurface.opacity(0.20))
        .clipShape(RoundedRectangle(cornerRadius: 8))
        .overlay(
            RoundedRectangle(cornerRadius: 8)
                .stroke(
                    OptikaPalette.separator.opacity(0.85),
                    lineWidth: 0.8
                )
        )
    }
}

struct NativeWorkspaceDockTabRail: View {
    @ObservedObject var workspace: NativeWorkspaceModel

    static let width: CGFloat = 26
    private let tabHeight: CGFloat = 104

    var body: some View {
        VStack(spacing: 0) {
            Spacer(minLength: 0)

            VStack(spacing: 0) {
                dockTabButton(.cameraControls)
                dockTabButton(.smartGallery)
            }

            Spacer(minLength: 0)
        }
        .frame(width: Self.width)
        .frame(maxHeight: .infinity)
    }

    private func dockTabButton(_ tab: NativeWorkspaceDockTab) -> some View {
        let isSelected = model.selectedWorkspaceDockTab == tab
        let isOpen = workspace.isWorkspaceDockVisible && isSelected
        let accent = OptikaPalette.accent

        return Button {
            workspace.toggleWorkspaceDock(tab)
        } label: {
            VStack(spacing: 4) {
                Image(systemName: tab.symbolName)
                    .font(.system(size: 10, weight: .semibold))
                    .frame(width: 11, height: 11)

                Text(tab.title)
                    .font(.system(size: 9, weight: .semibold))
                    .lineLimit(1)
                    .minimumScaleFactor(0.8)
                    .frame(width: 82, height: 12)
                    .rotationEffect(.degrees(90))
                    .frame(width: 12, height: 82)
            }
            .frame(width: Self.width, height: tabHeight)
            .contentShape(Rectangle())
            .background(
                Rectangle().fill(
                    isOpen
                        ? accent
                        : OptikaPalette.windowSurface
                )
            )
            .overlay(
                Rectangle().stroke(
                    isOpen
                        ? accent
                        : OptikaPalette.separator,
                    lineWidth: isOpen ? 1 : 0.7
                )
            )
            .overlay(alignment: .leading) {
                Rectangle()
                    .fill(isOpen ? OptikaPalette.dockSelectedTabFill : .clear)
                    .frame(width: 1.5)
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(PlainButtonStyle())
        .frame(width: Self.width, height: tabHeight)
        .contentShape(Rectangle())
        .foregroundColor(isOpen ? .white : OptikaPalette.primaryText)
        .help(isOpen ? "Close \(tab.title) dock" : "Open \(tab.title) dock")
        .accessibilityLabel(
            isOpen ? "Close \(tab.title) dock" : "Open \(tab.title) dock"
        )
        .accessibilityHint(
            isOpen
                ? "Hides the floating dock without resizing the viewer."
                : "Shows the \(tab.title) dock over the viewer."
        )
    }
}
