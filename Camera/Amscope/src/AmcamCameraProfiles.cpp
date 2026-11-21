#include "amscope/AmcamCameraProfiles.h"

#include <cctype>
#include <utility>

namespace amscope {
namespace {

std::string normalizedModelCode(std::string_view value) {
    auto first = value.begin();
    auto last = value.end();

    while (first != last &&
           std::isspace(static_cast<unsigned char>(*first)) != 0) {
        ++first;
    }
    while (first != last &&
           std::isspace(static_cast<unsigned char>(*(last - 1))) != 0) {
        --last;
    }
    return std::string(first, last);
}

} // namespace

AmcamCameraProfileRegistry::AmcamCameraProfileRegistry(
    std::vector<AmcamCameraModelProfile> profiles) {
    profiles_.reserve(profiles.size());
    // Match Swift's "newest profile wins" duplicate-key behavior.
    for (auto& profile : profiles) {
        const auto key = normalizedModelCode(profile.modelCode);
        profiles_.insert_or_assign(key, std::move(profile));
    }
}

bool AmcamCameraProfileRegistry::allows(
    std::string_view modelCode,
    AmcamCameraFeature feature) const {
    const auto found = profiles_.find(normalizedModelCode(modelCode));
    if (found == profiles_.end()) {
        return true;
    }

    const auto& profile = found->second;
    if (profile.deniedFeatures.find(feature) != profile.deniedFeatures.end()) {
        return false;
    }
    return profile.allowedFeatures.find(feature) != profile.allowedFeatures.end();
}

} // namespace amscope
