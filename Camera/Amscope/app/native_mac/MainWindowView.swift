import AppKit
import SwiftUI

struct MainWindowView: View {
    @ObservedObject var model: NativeCameraModel

    private let minimumPreviewWidth: CGFloat = 500
    private let dockDividerWidth: CGFloat = 6

    var body: some View {
        GeometryReader { geometry in
            let maximumDockWidth = max(
                280,
                geometry.size.width -
                    minimumPreviewWidth -
                    dockDividerWidth
            )
            let dockWidth = min(model.workspaceDockWidth, maximumDockWidth)

            HStack(spacing: 0) {
                NativeDualPreviewSurface(
                    bridge: model.bridge,
                    activeSlide: model.activeSlide,
                    measurementCalibration: model.measurementCalibration,
                    onActiveSlideChanged: model.selectActiveSlide,
                    onCapture: { slide, image in
                        model.captureGallery.capture(image, for: slide)
                    }
                )
                .frame(
                    minWidth: minimumPreviewWidth,
                    maxWidth: .infinity,
                    maxHeight: .infinity
                )
                .clipped()
                .layoutPriority(1)

                if model.isWorkspaceDockVisible {
                    NativeWorkspaceDockResizeDivider(
                        dockWidth: dockWidth,
                        maximumWidth: maximumDockWidth,
                        onResize: {
                            model.setWorkspaceDockWidth($0, maximum: maximumDockWidth)
                        }
                    )
                    .frame(width: dockDividerWidth)
                    .frame(maxHeight: .infinity)

                    NativeWorkspaceDock(
                        model: model,
                        onClose: { model.closeWorkspaceDock() }
                    )
                    .frame(width: dockWidth)
                    .frame(maxHeight: .infinity)
                    .layoutPriority(0)
                }

            }
            .frame(width: geometry.size.width, height: geometry.size.height)
            .overlay(alignment: .trailing) {
                // The tool rail floats above the app content. The dock caption
                // keeps its close control on the opposite side to avoid overlap.
                NativeWorkspaceDockTabRail(model: model)
                    .frame(width: NativeWorkspaceDockTabRail.width)
                    .frame(maxHeight: .infinity)
                    .contentShape(Rectangle())
                    .allowsHitTesting(true)
                    .zIndex(100)
            }
        }
        .frame(minWidth: 960, minHeight: 640)
        .background(Color.clear)
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

struct NativeWorkspaceDockResizeDivider: NSViewRepresentable {
    let dockWidth: CGFloat
    let maximumWidth: CGFloat
    let onResize: (CGFloat) -> Void

    func makeNSView(context: Context) -> NativeWorkspaceDockResizeDividerView {
        NativeWorkspaceDockResizeDividerView(
            dockWidth: dockWidth,
            maximumWidth: maximumWidth,
            onResize: onResize
        )
    }

    func updateNSView(
        _ nsView: NativeWorkspaceDockResizeDividerView,
        context: Context
    ) {
        nsView.update(
            dockWidth: dockWidth,
            maximumWidth: maximumWidth,
            onResize: onResize
        )
    }
}

@MainActor
final class NativeWorkspaceDockResizeDividerView: NSView {
    private var dockWidth: CGFloat
    private var maximumWidth: CGFloat
    private var onResize: ((CGFloat) -> Void)?

    private var dragStartWindowX: CGFloat?
    private var dragStartWidth: CGFloat?
    private var isHovering = false
    private var isDragging = false
    private var resizeTrackingArea: NSTrackingArea?

    init(
        dockWidth: CGFloat,
        maximumWidth: CGFloat,
        onResize: @escaping (CGFloat) -> Void
    ) {
        self.dockWidth = dockWidth
        self.maximumWidth = maximumWidth
        self.onResize = onResize
        super.init(frame: .zero)
        wantsLayer = true
        layer?.backgroundColor = NSColor.clear.cgColor
    }

    required init?(coder: NSCoder) {
        fatalError("NativeWorkspaceDockResizeDividerView does not support NSCoder initialization")
    }

    override var mouseDownCanMoveWindow: Bool { false }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()

        if let resizeTrackingArea = resizeTrackingArea {
            removeTrackingArea(resizeTrackingArea)
        }

        let area = NSTrackingArea(
            rect: .zero,
            options: [.mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect],
            owner: self,
            userInfo: nil
        )
        addTrackingArea(area)
        resizeTrackingArea = area
    }

    override func resetCursorRects() {
        addCursorRect(bounds, cursor: NSCursor.resizeLeftRight)
    }

    override func mouseEntered(with event: NSEvent) {
        isHovering = true
        needsDisplay = true
    }

    override func mouseExited(with event: NSEvent) {
        isHovering = false
        needsDisplay = true
    }

    override func mouseDown(with event: NSEvent) {
        dragStartWindowX = event.locationInWindow.x
        dragStartWidth = dockWidth
        isDragging = true
        needsDisplay = true
    }

    override func mouseDragged(with event: NSEvent) {
        guard let dragStartWindowX = dragStartWindowX,
              let dragStartWidth = dragStartWidth else {
            return
        }

        let horizontalDelta = event.locationInWindow.x - dragStartWindowX
        let upperBound = max(280, maximumWidth)
        let nextWidth = min(max(dragStartWidth - horizontalDelta, 280), upperBound)
        onResize?(nextWidth)
    }

    override func mouseUp(with event: NSEvent) {
        mouseDragged(with: event)
        dragStartWindowX = nil
        dragStartWidth = nil
        isDragging = false
        needsDisplay = true
    }

    override func draw(_ dirtyRect: NSRect) {
        super.draw(dirtyRect)

        let emphasized = isHovering || isDragging
        let lineWidth: CGFloat = emphasized ? 2 : 1
        let lineRect = CGRect(
            x: (bounds.width - lineWidth) / 2,
            y: 5,
            width: lineWidth,
            height: max(0, bounds.height - 10)
        )

        (emphasized ? NSColor.controlAccentColor : NSColor.separatorColor).setFill()
        NSBezierPath(
            roundedRect: lineRect,
            xRadius: lineWidth / 2,
            yRadius: lineWidth / 2
        ).fill()
    }

    func update(
        dockWidth: CGFloat,
        maximumWidth: CGFloat,
        onResize: @escaping (CGFloat) -> Void
    ) {
        self.dockWidth = dockWidth
        self.maximumWidth = maximumWidth
        self.onResize = onResize
    }
}

struct DockCameraFeatureControl: View {
    @ObservedObject var model: NativeCameraModel
    let descriptor: AmscopeCameraFeatureDescriptor

    private var editingDisabled: Bool {
        !model.canAccessFeatures ||
        !descriptor.isAvailable ||
        !descriptor.isWritable ||
        model.isFeatureWritePending ||
        model.isFeatureSnapshotLoading ||
        model.isCameraOperationPending
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            // Feature names vary by camera and may be much longer than a
            // compact two-column row can display. Let the label wrap fully
            // and keep the editor on its own line.
            Text(descriptor.displayName)
                .font(.system(size: 12, weight: .medium))
                .foregroundColor(.primary)
                .fixedSize(horizontal: false, vertical: true)
                .frame(maxWidth: .infinity, alignment: .leading)

            editor
                .frame(maxWidth: .infinity, alignment: .leading)
        }
        .frame(maxWidth: .infinity, minHeight: 42, alignment: .leading)
        .padding(.horizontal, 8)
        .padding(.vertical, 8)
        .background(
            RoundedRectangle(cornerRadius: 6)
                .fill(Color(nsColor: NSColor.controlBackgroundColor))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 6)
                .stroke(Color(nsColor: NSColor.separatorColor).opacity(0.65), lineWidth: 0.5)
        )
        .help(featureHelpText)
        .accessibilityElement(children: .contain)
        .accessibilityLabel(descriptor.displayName)
    }

    private var featureHelpText: String {
        var parts = [descriptor.displayName, featureGroupTitle(descriptor.group.rawValue)]
        if let unit = descriptor.unit, !unit.isEmpty { parts.append("Unit: \(unit)") }
        if descriptor.hasNumericRange {
            if descriptor.integerRange {
                parts.append("Range: \(descriptor.minimumInteger)–\(descriptor.maximumInteger)")
                if descriptor.hasIntegerIncrement {
                    parts.append("Step: \(descriptor.integerIncrement)")
                }
            } else {
                parts.append("Range: \(descriptor.minimumFloatingPoint)–\(descriptor.maximumFloatingPoint)")
                if descriptor.hasFloatingPointIncrement {
                    parts.append("Step: \(descriptor.floatingPointIncrement)")
                }
            }
        }
        if let reason = descriptor.disabledReason, !reason.isEmpty { parts.append(reason) }
        if let error = model.featureError(for: descriptor) { parts.append(error) }
        return parts.joined(separator: " · ")
    }

    @ViewBuilder
    private var editor: some View {
        if !descriptor.isWritable {
            Text(model.displayValue(for: descriptor))
                .font(.system(size: 12, design: .monospaced))
                .foregroundColor(.secondary)
                .lineLimit(1)
                .multilineTextAlignment(.leading)
        } else {
            switch descriptor.kind {
            case .boolean:
                Toggle(
                    "",
                    isOn: Binding(
                        get: { model.booleanValue(for: descriptor) },
                        set: { model.writeBoolean($0, for: descriptor) }
                    )
                )
                .labelsHidden()
                .toggleStyle(SwitchToggleStyle())
                .disabled(editingDisabled)

            case .integer:
                if !descriptor.discreteIntegerValues.isEmpty {
                    Picker(
                        "",
                        selection: Binding(
                            get: { model.integerValue(for: descriptor) },
                            set: { model.writeInteger($0, for: descriptor) }
                        )
                    ) {
                        ForEach(descriptor.discreteIntegerValues.map(\.int64Value), id: \.self) { value in
                            Text(String(value)).tag(value)
                        }
                    }
                    .labelsHidden()
                    .pickerStyle(PopUpButtonPickerStyle())
                    .disabled(editingDisabled)
                } else {
                    textEditor
                }

            case .enumeration:
                if descriptor.enumerationValues.isEmpty {
                    Text("No options")
                        .font(.system(size: 12))
                        .foregroundColor(.secondary)
                } else {
                    Picker(
                        "",
                        selection: Binding(
                            get: { model.enumerationValue(for: descriptor) },
                            set: { model.writeEnumeration($0, for: descriptor) }
                        )
                    ) {
                        ForEach(descriptor.enumerationValues, id: \.self) { symbol in
                            Text(symbol).tag(symbol)
                        }
                    }
                    .labelsHidden()
                    .pickerStyle(MenuPickerStyle())
                    .disabled(editingDisabled)
                }

            case .floatingPoint, .string, .integerVector:
                textEditor

            @unknown default:
                Text(model.displayValue(for: descriptor))
                    .font(.system(size: 11))
                    .foregroundColor(.secondary)
            }
        }
    }

    private var textEditor: some View {
        HStack(spacing: 3) {
            TextField(
                "Value",
                text: Binding(
                    get: { model.draftValue(for: descriptor) },
                    set: { model.setDraftValue($0, for: descriptor) }
                )
            )
            .textFieldStyle(RoundedBorderTextFieldStyle())
            .font(.system(size: 11, design: .monospaced))
            .frame(width: 88)
            .disabled(editingDisabled)

            Button("Set") {
                model.commitDraft(for: descriptor)
            }
            .disabled(editingDisabled)
        }
    }
}

func featureGroupTitle(_ rawValue: Int) -> String {
    switch rawValue {
    case 0: return "General"
    case 1: return "Exposure"
    case 2: return "Image Format"
    case 3: return "Image Processing"
    case 4: return "Color"
    case 5: return "Trigger"
    case 6: return "Sensor"
    case 7: return "Transport"
    case 8: return "Hardware"
    default: return "Other"
    }
}
