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

                    Spacer()

                    Text(model.cameraStateText)
                        .foregroundColor(.secondary)
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

                    Spacer()

                    if model.isCameraOperationPending {
                        ProgressView()
                            .controlSize(.small)
                    }
                }
            }

            NativeFeatureSections(model: model)
        }
        .padding()
        .frame(minWidth: 620, minHeight: 560)
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
