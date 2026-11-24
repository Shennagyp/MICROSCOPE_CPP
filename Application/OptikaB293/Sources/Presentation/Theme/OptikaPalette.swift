import AppKit
import SwiftUI

enum OptikaPalette {
    static let primaryText = Color.primary
    static let secondaryText = Color.secondary
    static let onAccentText = Color.white
    static let accent = Color(nsColor: NSColor.controlAccentColor)
    static let live = Color(nsColor: NSColor.systemRed)
    static let warning = Color(nsColor: NSColor.systemOrange)
    static let success = Color(nsColor: NSColor.systemGreen)
    static let measurement = Color(nsColor: NSColor.systemYellow)

    static let controlSurface = Color(nsColor: NSColor.controlBackgroundColor)
    static let windowSurface = Color(nsColor: NSColor.windowBackgroundColor)
    static let separator = Color(nsColor: NSColor.separatorColor)
    static let shadow = Color.black
    static let clear = Color.clear

    static let previewBackground = Color(nsColor: NSColor(calibratedWhite: 0.075, alpha: 1))
    static let measurementLabelBackground = Color(nsColor: NSColor(calibratedWhite: 0.05, alpha: 0.82))
    static let annotationLabelBackground = Color(nsColor: NSColor(calibratedWhite: 0.05, alpha: 0.84))

    static let liveSelectionFill = live.opacity(0.12)
    static let inactiveSelectionFill = primaryText.opacity(0.07)
    static let accentSelectionFill = accent.opacity(0.18)
    static let dockSelectedTabFill = onAccentText.opacity(0.82)
    static let warningFill = warning.opacity(0.10)
    static let thumbnailShade = shadow.opacity(0.08)
    static let thumbnailSurface = shadow.opacity(0.06)

    // Alpha 0.20 corresponds to the requested 80% transparency.
    static let floatingPanelAlpha = 0.20
    static let floatingPanelBorderAlpha = 0.85
    static let subtleBorderAlpha = 0.65
    static let dockBorderAlpha = 0.70
    static let shadowAlpha = 0.20

    static let nsClear = NSColor.clear
    static let nsPrimaryText = NSColor.labelColor
    static let nsSecondaryText = NSColor.secondaryLabelColor
    static let nsAccent = NSColor.controlAccentColor
    static let nsLive = NSColor.systemRed
    static let nsWarning = NSColor.systemOrange
    static let nsSuccess = NSColor.systemGreen
    static let nsMeasurement = NSColor.systemYellow
    static let nsSurface = NSColor.windowBackgroundColor
    static let nsControlSurface = NSColor.controlBackgroundColor
    static let nsSeparator = NSColor.separatorColor
    static let nsOverlayText = NSColor.white
    static let nsPreviewBackground = NSColor(calibratedWhite: 0.075, alpha: 1)
    static let nsMeasurementLabelBackground = NSColor(calibratedWhite: 0.05, alpha: 0.82)
    static let nsAnnotationLabelBackground = NSColor(calibratedWhite: 0.05, alpha: 0.84)
}
