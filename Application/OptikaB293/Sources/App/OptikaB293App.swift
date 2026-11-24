import AppKit
import SwiftUI

@main
@MainActor
struct OptikaB293App {
    // NSApplication.delegate is not the owner. Retain it for the complete
    // application lifetime rather than relying on a local variable's lifetime.
    private static var applicationDelegate: OptikaB293ApplicationDelegate?

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
    private var microscopeConfigurationWindowController: NSWindowController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        installMainMenu()
        showMainWindow()
    }

    func applicationShouldTerminateAfterLastWindowClosed(
        _ sender: NSApplication
    ) -> Bool {
        true
    }

    private var terminationInProgress = false

    func applicationShouldTerminate(
        _ sender: NSApplication
    ) -> NSApplication.TerminateReply {
        if terminationInProgress {
            return .terminateLater
        }

        if cameraModel.bridge.state == .closed {
            return .terminateNow
        }

        terminationInProgress = true
        cameraModel.bridge.close { [weak self] error, _, _ in
            MainActor.assumeIsolated {
                guard let self = self else {
                    NSApp.reply(toApplicationShouldTerminate: false)
                    return
                }

                self.terminationInProgress = false
                if let error = error {
                    // Do not silently quit while the camera session may remain
                    // active. Keep the app alive so the user can recover.
                    self.cameraModel.handleTerminationCloseFailure(error)
                    NSApp.reply(toApplicationShouldTerminate: false)
                    return
                }

                NSApp.reply(toApplicationShouldTerminate: true)
            }
        }
        return .terminateLater
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
        window.setContentSize(NSSize(width: 680, height: 700))
        window.minSize = NSSize(width: 480, height: 420)
        window.styleMask = [.titled, .closable, .miniaturizable, .resizable]
        window.center()

        let windowController = NSWindowController(window: window)
        cameraControlsWindowController = windowController
        windowController.showWindow(sender)
        NSApp.activate(ignoringOtherApps: true)
    }

    @objc private func openMicroscopeConfiguration(_ sender: Any?) {
        if let existingController = microscopeConfigurationWindowController {
            existingController.showWindow(sender)
            existingController.window?.makeKeyAndOrderFront(sender)
            NSApp.activate(ignoringOtherApps: true)
            return
        }

        let hostingController = NSHostingController(
            rootView: MicroscopeConfigurationWindow(model: cameraModel)
        )
        let window = NSWindow(contentViewController: hostingController)
        window.title = "Microscope Configuration"
        window.setContentSize(NSSize(width: 720, height: 760))
        window.minSize = NSSize(width: 560, height: 480)
        window.styleMask = [.titled, .closable, .miniaturizable, .resizable]
        window.center()

        let windowController = NSWindowController(window: window)
        microscopeConfigurationWindowController = windowController
        windowController.showWindow(sender)
        NSApp.activate(ignoringOtherApps: true)
    }

    private func showMainWindow() {
        let hostingController = NSHostingController(
            rootView: MainWindowView(
                model: cameraModel,
                workspace: composition.workspace,
                gallery: composition.gallery
            )
        )
        let window = NSWindow(contentViewController: hostingController)
        window.title = "OPTIKA B293 — Dual Slide"
        window.setContentSize(NSSize(width: OptikaLayout.defaultWindowWidth, height: OptikaLayout.defaultWindowHeight))
        window.minSize = NSSize(width: OptikaLayout.minimumWindowWidth, height: OptikaLayout.minimumWindowHeight)
        window.styleMask = [
            .titled,
            .closable,
            .miniaturizable,
            .resizable,
            .fullSizeContentView
        ]

        // Transparency belongs to the window/surface, not its controls.
        // fullSizeContentView lets the clear title-bar reveal the content
        // behind it while preserving native title text and traffic-light
        // controls. A non-opaque clear window lets the clear dock reveal the
        // desktop instead of being repainted with the default window color.
        window.titlebarAppearsTransparent = true
        window.isOpaque = false
        window.backgroundColor = .clear
        hostingController.view.wantsLayer = true
        hostingController.view.layer?.backgroundColor = OptikaPalette.nsClear.cgColor
        hostingController.view.layer?.isOpaque = false
        window.contentView?.wantsLayer = true
        window.contentView?.layer?.backgroundColor = OptikaPalette.nsClear.cgColor
        window.contentView?.layer?.isOpaque = false
        window.center()

        let windowController = NSWindowController(window: window)
        mainWindowController = windowController
        windowController.showWindow(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    private func installMainMenu() {
        let mainMenu = NSMenu()

        let appMenuItem = NSMenuItem()
        let appMenu = NSMenu(title: "OPTIKA B293")
        appMenu.addItem(NSMenuItem(
            title: "About OPTIKA B293",
            action: #selector(NSApplication.orderFrontStandardAboutPanel(_:)),
            keyEquivalent: ""
        ))
        appMenu.addItem(NSMenuItem.separator())
        appMenu.addItem(NSMenuItem(
            title: "Quit OPTIKA B293",
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

        let microscopeConfigurationItem = NSMenuItem(
            title: "Microscope Configuration…",
            action: #selector(openMicroscopeConfiguration(_:)),
            keyEquivalent: ""
        )
        microscopeConfigurationItem.target = self
        fileMenu.addItem(microscopeConfigurationItem)
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
