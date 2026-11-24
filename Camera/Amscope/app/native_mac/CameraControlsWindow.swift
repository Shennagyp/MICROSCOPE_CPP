import SwiftUI

struct CameraControlsWindow: View {
    var body: some View {
        Form {
            Section("Camera") {
                Label("No camera connected", systemImage: "video.slash")
                    .foregroundStyle(.secondary)
            }

            Section("Acquisition") {
                Text("Acquisition controls will be connected to the native C++ controller.")
                    .foregroundStyle(.secondary)
            }

            Section("Imaging") {
                Text("Feature controls will be populated from the camera feature model.")
                    .foregroundStyle(.secondary)
            }

            Section("Hardware") {
                Text("Supported camera accessories will appear here.")
                    .foregroundStyle(.secondary)
            }
        }
        .padding()
        .frame(minWidth: 480, minHeight: 420)
    }
}
