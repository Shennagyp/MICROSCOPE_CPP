import AppKit
import SwiftUI

@main
@MainActor
struct AmscopeNativeApp {
    // NSApplication.delegate is not the owner. Retain it for the complete
    // application lifetime rather than relying on a local variable's lifetime.
    private static var applicationDelegate: AmscopeApplicationDelegate?

    static func main() {
        let application = NSApplication.shared
        let delegate = AmscopeApplicationDelegate()
        applicationDelegate = delegate
        application.delegate = delegate
        application.run()
    }
}

@MainActor
final class AmscopeApplicationDelegate: NSObject, NSApplicationDelegate {
    private let cameraModel = NativeCameraModel.shared
    private var mainWindowController: NSWindowController?
    private var cameraControlsWindowController: NSWindowController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        installMainMenu()
        showMainWindow()
    }

    func applicationShouldTerminateAfterLastWindowClosed(
        _ sender: NSApplication
    ) -> Bool {
        true
    }

    @objc private func openCameraControls(_ sender: Any?) {
        if let existingController = cameraControlsWindowController {
            existingController.showWindow(sender)
            existingController.window?.makeKeyAndOrderFront(sender)
            NSApp.activate(ignoringOtherApps: true)
            return
        }

        let hostingController = NSHostingController(
            rootView: CameraControlsWindow(model: cameraModel)
        )
        let window = NSWindow(contentViewController: hostingController)
        window.title = "Camera Controls"
        window.setContentSize(NSSize(width: 520, height: 560))
        window.minSize = NSSize(width: 480, height: 420)
        window.styleMask = [.titled, .closable, .miniaturizable, .resizable]

        let windowController = NSWindowController(window: window)
        cameraControlsWindowController = windowController
        windowController.showWindow(sender)
        NSApp.activate(ignoringOtherApps: true)
    }

    private func showMainWindow() {
        let hostingController = NSHostingController(
            rootView: MainWindowView(model: cameraModel)
        )
        let window = NSWindow(contentViewController: hostingController)
        window.title = "AmScope"
        window.setContentSize(NSSize(width: 1440, height: 900))
        window.minSize = NSSize(width: 960, height: 640)
        window.styleMask = [.titled, .closable, .miniaturizable, .resizable]
        window.center()

        let windowController = NSWindowController(window: window)
        mainWindowController = windowController
        windowController.showWindow(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    private func installMainMenu() {
        let mainMenu = NSMenu()

        let appMenuItem = NSMenuItem()
        let appMenu = NSMenu(title: "AmScope")
        appMenu.addItem(NSMenuItem(
            title: "About AmScope",
            action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)),
            keyEquivalent: ""
        ))
        appMenu.addItem(NSMenuItem.separator())
        appMenu.addItem(NSMenuItem(
            title: "Quit AmScope",
            action: #selector(NSApplication.terminate(_:)),
            keyEquivalent: "q"
        ))
        appMenuItem.submenu = appMenu
        mainMenu.addItem(appMenuItem)

        let fileMenuItem = NSMenuItem()
        let fileMenu = NSMenu(title: "File")

        let controlsItem = NSMenuItem(
            title: "Camera Controls…",
            action: #selector(openCameraControls(_:)),
            keyEquivalent: ""
        )
        controlsItem.target = self
        fileMenu.addItem(controlsItem)
        fileMenu.addItem(NSMenuItem.separator())
        fileMenu.addItem(NSMenuItem(
            title: "Close Window",
            action: #selector(NSWindow.performClose(_:)),
            keyEquivalent: "w"
        ))

        fileMenuItem.submenu = fileMenu
        mainMenu.addItem(fileMenuItem)
        NSApp.mainMenu = mainMenu
    }
}
