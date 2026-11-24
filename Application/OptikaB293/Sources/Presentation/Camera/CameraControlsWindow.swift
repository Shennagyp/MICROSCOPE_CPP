import SwiftUI

struct CameraControlsWindow: View {
    @ObservedObject var model: NativeCameraModel

    var body: some View {
        ScrollView(.vertical) {
            VStack(alignment: .leading, spacing: 14) {
                GroupBox(label: Text("Camera")) {
                    VStack(alignment: .leading, spacing: 10) {
                        Picker("Device", selection: $model.selectedDeviceID) {
                            Text("Select a camera").tag("")
                            ForEach(model.cameras, id: \.deviceID) { camera in
                                Text(camera.displayName).tag(camera.deviceID)
                            }
                        }
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .disabled(model.cameraState != .closed ||
                                  model.isCameraOperationPending ||
                                  model.isEnumeratingCameras)

                        HStack {
                            Button("Refresh") {
                                model.refreshCameras()
                            }
                            .disabled(model.isEnumeratingCameras ||
                                      model.isCameraOperationPending ||
                                      model.cameraState != .closed)

                            Spacer()

                            if model.isEnumeratingCameras {
                                Text("Searching…")
                                    .font(.caption)
                                    .foregroundColor(OptikaPalette.secondaryText)
                            }
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
                                .foregroundColor(OptikaPalette.secondaryText)
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }

                GroupBox(label: Text("Acquisition")) {
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
                            Text("Working…")
                                .font(.caption)
                                .foregroundColor(OptikaPalette.secondaryText)
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }

                NativeFeatureSections(model: model, features: model.features)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(16)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
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
