import SwiftUI

struct CameraControlsWindow: View {
    var body: some View {
        Form {
            Section(header: Text("Camera")) {
                HStack {
                    Image(systemName: "video.slash")
                    Text("No camera connected")
                }
                .foregroundColor(.secondary)
            }

            Section(header: Text("Acquisition")) {
                Text("Acquisition controls will be connected to the native C++ controller.")
                    .foregroundColor(.secondary)
            }

            Section(header: Text("Imaging")) {
                Text("Feature controls will be populated from the camera feature model.")
                    .foregroundColor(.secondary)
            }

            Section(header: Text("Hardware")) {
                Text("Supported camera accessories will appear here.")
                    .foregroundColor(.secondary)
            }
        }
        .padding()
        .frame(minWidth: 480, minHeight: 420)
    }
}
