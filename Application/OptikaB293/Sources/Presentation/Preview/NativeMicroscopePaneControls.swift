import SwiftUI

struct NativeMicroscopePaneControls: View {
    @ObservedObject var model: NativeCameraModel
    @ObservedObject var features: NativeCameraFeatureModel
    @ObservedObject var workspace: NativeWorkspaceModel
    @ObservedObject var microscope: MicroscopeConfigurationModel
    @ObservedObject var resolutionCoordinator: MicroscopeResolutionCoordinator
    let slide: NativeSlideID

    private var resolutionDescriptor: AmscopeCameraFeatureDescriptor? {
        features.previewResolutionDescriptor
    }

    private var isLiveTarget: Bool {
        workspace.activeSlide == slide
    }

    var body: some View {
        HStack(alignment: .center, spacing: 4) {
            Picker(
                "Objective",
                selection: Binding(
                    get: { microscope.configuration.activeObjectiveID },
                    set: { microscope.selectObjective($0) }
                )
            ) {
                ForEach(microscope.configuration.objectives) { objective in
                    Text(String(format: "%.0f×", objective.magnification))
                        .tag(objective.id)
                }
            }
            .labelsHidden()
            .pickerStyle(PopUpButtonPickerStyle())
            .font(.system(size: 9))
            .frame(width: 56)
            .disabled(model.isCameraOperationPending)
            .help("Select the active microscope objective")

            if let descriptor = resolutionDescriptor {
                Picker(
                    "Resolution",
                    selection: Binding(
                        get: { features.selectedPreviewResolutionSymbol ?? "" },
                        set: { resolutionCoordinator.selectManualPreviewResolution(to: $0) }
                    )
                ) {
                    if features.selectedPreviewResolutionSymbol == nil {
                        Text("Select…").tag("")
                    }
                    ForEach(descriptor.enumerationValues, id: \.self) { symbol in
                        Text(resolutionOptionTitle(symbol)).tag(symbol)
                    }
                }
                .labelsHidden()
                .pickerStyle(PopUpButtonPickerStyle())
                .font(.system(size: 9))
                .frame(width: 84)
                .disabled(!model.canChangePreviewResolution)
                .help("Choose a resolution. Manual selection disables automatic resolution.")
            } else {
                let resolutionStatus = !model.canAccessFeatures
                    ? "—"
                    : (features.isFeatureSnapshotLoading ? "Loading…" : "Unavailable")
                Text(resolutionStatus)
                    .font(.system(size: 9))
                    .foregroundColor(OptikaPalette.secondaryText)
                    .frame(width: 84, height: 20, alignment: .leading)
            }

            Toggle(
                "AUTO",
                isOn: Binding(
                    get: { resolutionCoordinator.isAutomaticResolutionEnabled },
                    set: { resolutionCoordinator.setAutomaticResolutionEnabled($0) }
                )
            )
            .font(.system(size: 8, weight: .semibold))
            .toggleStyle(SwitchToggleStyle())
            .disabled(resolutionDescriptor == nil ||
                      !model.canAccessFeatures ||
                      model.isCameraOperationPending ||
                      features.isFeatureWritePending ||
                      features.isFeatureSnapshotLoading)
            .help(resolutionCoordinator.automaticResolutionHelp)

            Button {
                workspace.selectActiveSlide(slide)
            } label: {
                HStack(spacing: 3) {
                    Image(systemName: "dot.radiowaves.left.and.right")
                        .font(.system(size: 9, weight: .semibold))
                    Text(isLiveTarget ? "LIVE" : "SET LIVE")
                        .font(.system(size: 8, weight: .bold))
                }
                .foregroundColor(isLiveTarget ? OptikaPalette.live : OptikaPalette.primaryText)
                .padding(.horizontal, 5)
                .frame(height: 21)
                .background(
                    Capsule().fill(
                        isLiveTarget
                            ? OptikaPalette.liveSelectionFill
                            : OptikaPalette.inactiveSelectionFill
                    )
                )
            }
            .buttonStyle(.plain)
            .disabled(isLiveTarget)
            .help(
                isLiveTarget
                    ? "\(slide.title) is receiving the live camera feed."
                    : "Route the live camera feed to \(slide.title)."
            )
            .accessibilityLabel(
                isLiveTarget
                    ? "\(slide.title) is the live feed destination"
                    : "Route the live camera feed to \(slide.title)"
            )
        }
        .padding(.horizontal, 4)
        .padding(.vertical, 2)
        .fixedSize(horizontal: true, vertical: true)
        .accessibilityElement(children: .contain)
        .accessibilityLabel("\(slide.title) microscope and live-feed controls")
    }

    private func resolutionOptionTitle(_ symbol: String) -> String {
        guard let separator = symbol.firstIndex(of: ":") else {
            return symbol
        }
        return String(symbol[symbol.index(after: separator)...])
            .replacingOccurrences(of: "x", with: " × ")
    }
}

/// One AppKit consumer owns the bridge's single latest-frame mailbox and
/// routes frames to the selected slide pane. Each pane retains only its own
/// most recently presented CGImage; SwiftUI never observes individual frames.
