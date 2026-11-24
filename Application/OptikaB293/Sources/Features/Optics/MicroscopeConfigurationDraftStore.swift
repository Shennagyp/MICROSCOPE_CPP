import Combine
import Foundation

@MainActor
final class MicroscopeConfigurationDraftStore: ObservableObject {
    @Published var configuration: MicroscopeOpticalConfiguration
    @Published private(set) var hasAppliedChanges = false

    init(configuration: MicroscopeOpticalConfiguration) {
        self.configuration = configuration
    }

    func update(_ mutation: (inout MicroscopeOpticalConfiguration) -> Void) {
        var updated = configuration
        mutation(&updated)
        configuration = updated
        hasAppliedChanges = false
    }

    func restoreDefaults() {
        configuration = .optikaB293
        hasAppliedChanges = false
    }

    func markApplied(_ configuration: MicroscopeOpticalConfiguration) {
        self.configuration = configuration
        hasAppliedChanges = true
    }
}
