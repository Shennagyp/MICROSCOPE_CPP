import SwiftUI

struct CameraControlsWindow: View {
    @ObservedObject var model: NativeCameraModel

    var body: some View {
        Form {
            Section(header: Text("Camera")) {
                HStack {
                    Picker("Device", selection: $model.selectedDeviceID) {
                        Text("Select a camera").tag("")
                        ForEach(model.cameras, id: \.deviceID) { camera in
                            Text(camera.displayName).tag(camera.deviceID)
                        }
                    }
                    .disabled(model.cameraState != .closed ||
                              model.isCameraOperationPending ||
                              model.isEnumeratingCameras)

                    Button("Refresh") {
                        model.refreshCameras()
                    }
                    .disabled(model.isEnumeratingCameras ||
                              model.isCameraOperationPending ||
                              model.cameraState != .closed)
                }

                HStack {
                    Button("Open") {
                        model.openSelectedCamera()
                    }
                    .disabled(!model.canOpen)

                    Button("Close") {
                        model.closeCamera()
                    }
                    .disabled(!model.canClose)
                }
            }

            Section(header: Text("Acquisition")) {
                HStack {
                    Button("Start") {
                        model.startAcquisition()
                    }
                    .disabled(!model.canStart)

                    Button("Stop") {
                        model.stopAcquisition()
                    }
                    .disabled(!model.canStop)
                }

                HStack {
                    Text("State")
                    Spacer()
                    Text(model.cameraStateText)
                        .foregroundColor(.secondary)
                }
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
        .onAppear {
            if model.cameras.isEmpty {
                model.refreshCameras()
            }
        }
    }
}
