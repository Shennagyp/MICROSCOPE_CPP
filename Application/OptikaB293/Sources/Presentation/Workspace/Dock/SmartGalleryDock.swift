import AppKit
import SwiftUI

struct NativeSmartGalleryDock: View {
    @ObservedObject var gallery: NativeCaptureGalleryStore

    var body: some View {
        VStack(spacing: 0) {
            HStack {
                VStack(alignment: .leading, spacing: 2) {
                    Text("CAPTURES")
                        .font(.system(size: 13, weight: .bold))
                        .foregroundColor(OptikaPalette.secondaryText)
                    Text(gallery.captures.count == 80
                         ? "Latest 80 captures"
                         : "\(gallery.captures.count) captures")
                        .font(.system(size: 13, weight: .semibold))
                }

                Spacer()

                if gallery.isCapturing {
                    Text("● Saving…")
                        .font(.system(size: 13, weight: .medium))
                        .foregroundColor(OptikaPalette.secondaryText)
                }

            }
            .padding(.horizontal, 12)
            .padding(.vertical, 9)

            if let error = gallery.captureErrorMessage {
                HStack(alignment: .top, spacing: 6) {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .foregroundColor(OptikaPalette.warning)
                    Text(error)
                        .font(.system(size: 11))
                        .frame(maxWidth: .infinity, alignment: .leading)
                    Button {
                        gallery.dismissCaptureError()
                    } label: {
                        Image(systemName: "xmark")
                    }
                    .buttonStyle(BorderlessButtonStyle())
                }
                .padding(8)
                .background(OptikaPalette.warningFill)
                .padding(.horizontal, 8)
                .padding(.bottom, 8)
            }

            Divider()

            if gallery.captures.isEmpty {
                VStack(spacing: 10) {
                    Image(systemName: "photo.on.rectangle.angled")
                        .font(.system(size: 28, weight: .light))
                        .foregroundColor(OptikaPalette.secondaryText)
                    Text("No captures yet")
                        .font(.system(size: 13, weight: .semibold))
                    Text("Use the camera button on a slide's floating toolbar to save its current image.")
                        .font(.system(size: 12))
                        .foregroundColor(OptikaPalette.secondaryText)
                        .multilineTextAlignment(.center)
                        .frame(maxWidth: 220)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
                .padding(16)
            } else {
                ScrollView(.vertical, showsIndicators: true) {
                    VStack(spacing: 8) {
                        ForEach(gallery.captures) { item in
                            captureRow(item)
                        }
                    }
                    .padding(10)
                }
            }

            Divider()
            HStack {
                Image(systemName: "folder")
                    .foregroundColor(OptikaPalette.secondaryText)
                Button {
                    gallery.revealCaptureFolder()
                } label: {
                    HStack(spacing: 5) {
                        Image(systemName: "folder")
                        VStack(alignment: .leading, spacing: 1) {
                            Text("Pictures / AmScope Captures")
                                .font(.system(size: 11))
                            Text("Latest 80 shown; older PNGs stay on disk.")
                                .font(.system(size: 10))
                                .foregroundColor(OptikaPalette.secondaryText)
                        }
                    }
                }
                .buttonStyle(BorderlessButtonStyle())
                .help("Open the complete capture folder in Finder")
                Spacer()
            }
            .padding(.horizontal, 10)
            .padding(.vertical, 7)
        }
    }

    private func captureRow(_ item: NativeCaptureItem) -> some View {
        Button {
            NSWorkspace.shared.open(item.fileURL)
        } label: {
            HStack(spacing: 9) {
                Group {
                    if let image = NSImage(data: item.thumbnailData) {
                        Image(nsImage: image)
                            .resizable()
                            .aspectRatio(contentMode: .fit)
                    } else {
                        ZStack {
                            OptikaPalette.thumbnailShade
                            Image(systemName: "photo")
                                .foregroundColor(OptikaPalette.secondaryText)
                        }
                    }
                }
                .frame(width: 116, height: 84)
                .background(OptikaPalette.thumbnailSurface)
                .clipShape(RoundedRectangle(cornerRadius: 4))

                VStack(alignment: .leading, spacing: 5) {
                    Text(item.slide.title)
                        .font(.system(size: 13, weight: .semibold))
                    Text(item.dimensionsText)
                        .font(.system(size: 13, design: .monospaced))
                        .foregroundColor(OptikaPalette.secondaryText)
                    Text(item.capturedAt, style: .date)
                        .font(.system(size: 10))
                        .foregroundColor(OptikaPalette.secondaryText)
                    Text(item.capturedAt, style: .time)
                        .font(.system(size: 10))
                        .foregroundColor(OptikaPalette.secondaryText)
                }

                Spacer(minLength: 0)
                Image(systemName: "arrow.up.right")
                    .font(.system(size: 11))
                    .foregroundColor(OptikaPalette.secondaryText)
            }
            .padding(7)
            .background(
                RoundedRectangle(cornerRadius: 6)
                    .fill(OptikaPalette.windowSurface.opacity(OptikaPalette.floatingPanelAlpha))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 6)
                    .stroke(OptikaPalette.separator.opacity(OptikaPalette.dockBorderAlpha), lineWidth: 0.6)
            )
        }
        .buttonStyle(PlainButtonStyle())
        .help("Open capture in the default image viewer")
        .contextMenu {
            Button {
                gallery.reveal(item)
            } label: {
                HStack(spacing: 8) {
                    Image(systemName: "folder")
                    Text("Show in Finder")
                }
            }

            Button {
                gallery.delete(item)
            } label: {
                HStack(spacing: 8) {
                    Image(systemName: "trash")
                    Text("Delete Capture")
                }
            }
        }
    }
}

