import AppKit
import SwiftUI

struct MainWindowView: View {
    @ObservedObject var model: NativeCameraModel

    var body: some View {
        VStack(spacing: 0) {
            NativePreviewSurface(bridge: model.bridge) { resolution, pixelFormat in
                model.updateFrameMetadata(
                    resolution: resolution,
                    pixelFormat: pixelFormat
                )
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            Divider()

            HStack(spacing: 22) {
                InformationBarField(title: "Camera", value: model.selectedCameraName)
                InformationBarField(title: "Resolution", value: model.resolutionText)
                InformationBarField(title: "Pixel Format", value: model.pixelFormatText)
                InformationBarField(title: "State", value: model.cameraStateText)

                Spacer(minLength: 8)
            }
            .font(.system(size: 12))
            .padding(.horizontal, 14)
            .padding(.vertical, 9)
            .background(Color(nsColor: NSColor.windowBackgroundColor))
        }
        .frame(minWidth: 960, minHeight: 640)
        .alert(isPresented: $model.isShowingError) {
            Alert(
                title: Text("Camera Error"),
                message: Text(model.errorMessage),
                dismissButton: .default(Text("OK"))
            )
        }
    }
}

private struct InformationBarField: View {
    let title: String
    let value: String

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(title)
                .font(.system(size: 10, weight: .medium))
                .foregroundColor(.secondary)

            Text(value)
                .font(.system(size: 12, weight: .medium, design: .monospaced))
                .foregroundColor(.primary)
        }
        .fixedSize(horizontal: true, vertical: false)
    }
}
