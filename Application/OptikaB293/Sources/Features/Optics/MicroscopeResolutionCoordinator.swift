import Combine
import Foundation

@MainActor
final class MicroscopeResolutionCoordinator: ObservableObject {
    @Published private(set) var isAutomaticResolutionEnabled = false {
        didSet {
            UserDefaults.standard.set(
                isAutomaticResolutionEnabled,
                forKey: Self.automaticResolutionDefaultsKey
            )
        }
    }

    private weak var camera: NativeCameraModel?
    private weak var microscope: MicroscopeConfigurationModel?
    private static let automaticResolutionDefaultsKey =
        "AmscopeNative.AutomaticResolutionEnabled.v1"

    init(camera: NativeCameraModel, microscope: MicroscopeConfigurationModel) {
        self.camera = camera
        self.microscope = microscope
        isAutomaticResolutionEnabled = UserDefaults.standard.bool(
            forKey: Self.automaticResolutionDefaultsKey
        )
        camera.features.onPreviewResolutionReadbackChanged = { [weak self] in
            self?.reconcileAutomaticResolution()
        }
        microscope.onConfigurationChanged = { [weak self] in
            self?.reconcileAutomaticResolution()
        }
    }

    var automaticResolutionHelp: String {
        "Chooses the lowest camera-reported mode that meets the Rayleigh sampling target for the active objective and configured H&E chromatin wavelength band. If no mode meets the target, the maximum reported mode is selected."
    }

    var automaticResolutionSummary: String {
        guard let camera, let descriptor = camera.features.previewResolutionDescriptor else {
            return "Camera modes unavailable"
        }
        guard let microscope,
              let choice = NativeAutomaticResolutionPolicy.choose(
                modeSymbols: descriptor.enumerationValues,
                configuration: microscope.configuration
              ) else {
            return "Check microscope calibration"
        }
        let lower = String(format: "%.2f", choice.minimumBandSamplingFactor)
        let upper = String(format: "%.2f", choice.maximumBandSamplingFactor)
        let band = String(
            format: "%.0f–%.0f nm",
            microscope.configuration.chromatinBandLowerNanometers,
            microscope.configuration.chromatinBandUpperNanometers
        )
        return "\(choice.width) × \(choice.height) · RF \(lower)–\(upper) · \(band)" +
            (choice.meetsTarget ? "" : " · target not met")
    }

    func setAutomaticResolutionEnabled(_ enabled: Bool) {
        guard enabled != isAutomaticResolutionEnabled else { return }
        isAutomaticResolutionEnabled = enabled
        if enabled { reconcileAutomaticResolution() }
    }

    func selectManualPreviewResolution(to symbol: String) {
        guard !symbol.isEmpty, let camera else { return }
        setAutomaticResolutionEnabled(false)
        camera.changePreviewResolution(to: symbol)
    }

    private func reconcileAutomaticResolution() {
        guard isAutomaticResolutionEnabled,
              let camera,
              let microscope,
              camera.canChangePreviewResolution,
              let descriptor = camera.features.previewResolutionDescriptor,
              let current = camera.features.selectedPreviewResolutionSymbol,
              let choice = NativeAutomaticResolutionPolicy.choose(
                modeSymbols: descriptor.enumerationValues,
                configuration: microscope.configuration
              ),
              current != choice.symbol else {
            return
        }
        camera.changePreviewResolution(to: choice.symbol)
    }
}
