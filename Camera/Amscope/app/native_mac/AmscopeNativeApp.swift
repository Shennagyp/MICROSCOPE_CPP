import SwiftUI

@main
struct AmscopeNativeApp: App {
    @Environment(\.openWindow) private var openWindow

    var body: some Scene {
        WindowGroup("AmScope") {
            MainWindowView()
        }
        .defaultSize(width: 1440, height: 900)
        .commands {
            CommandGroup(after: .newItem) {
                Button("Camera Controls…") {
                    openWindow(id: "camera-controls")
                }
            }
        }

        Window("Camera Controls", id: "camera-controls") {
            CameraControlsWindow()
        }
        .defaultSize(width: 520, height: 560)
    }
}
