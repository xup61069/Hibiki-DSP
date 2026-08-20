#pragma once

// SPDX-License-Identifier: GPL-3.0-only

#include <string>
#include <vector>

namespace hibiki {

struct Vst3SdkClassInfoV1 {
    std::string class_id;
    std::string name;
    std::string vendor;
    std::string category;
    std::string version;
    bool audio_module{false};
};

// Optional control-plane bridge to the official open-source VST3 SDK. The
// SDK itself is never vendored into this repository; callers provide a local
// pinned checkout and receive catalog data only (no RT/plugin execution).
class Vst3SdkModuleCatalogV1 final {
public:
    [[nodiscard]] bool scan(const std::string& module_path,
                            std::vector<Vst3SdkClassInfoV1>& classes,
                            std::string& error) const;
};

}  // namespace hibiki
